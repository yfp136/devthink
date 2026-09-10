// Kernel 实现：引擎集成 + 总线路由 + 回调注入
#include "engines/kernel.h"

#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "platform/media_backend.h"

#include "core/error_codes.h"
#include "core/msg_bus.h"
#include "core/op_dict.h"
#include "core/util.h"
#include "engines/engine_registry.h"

namespace sm {

using json = nlohmann::json;

namespace {

// 兼容「数组参数」与「单值参数」两种写法：§5.3 规格中 media_ids / item_ids
// 为数组，早期 QML / 单测使用单值 media_id / item_id，两者都必须继续可用。
std::vector<std::string> string_list_param(const json& p,
                                           const char* array_key,
                                           const char* single_key) {
  std::vector<std::string> out;
  if (p.contains(array_key) && p[array_key].is_array()) {
    for (const auto& v : p[array_key])
      if (v.is_string()) out.push_back(v.get<std::string>());
  }
  if (out.empty()) {
    std::string one = p.value(single_key, std::string());
    if (!one.empty()) out.push_back(one);
  }
  return out;
}

// §5.3 media.update_tags：style_tags 为数组；M1 侧接口为逗号分隔全量替换
// （§10.1.5），此处统一转换，字符串写法仍兼容。
std::string style_tags_param(const json& p) {
  if (!p.contains("style_tags")) return std::string();
  const json& t = p["style_tags"];
  if (t.is_array()) {
    std::string joined;
    for (const auto& v : t) {
      if (!v.is_string()) continue;
      if (!joined.empty()) joined.push_back(',');
      joined += v.get<std::string>();
    }
    return joined;
  }
  if (t.is_string()) return t.get<std::string>();
  return std::string();
}

}  // namespace

Kernel::Kernel()
    : hb_(std::make_unique<HeartbeatMonitor>()),
      timeline_(std::make_unique<timeline::TimelineEngine>(bus_)),
      media_(std::make_unique<media::MediaEngine>()),
      scene_(std::make_unique<scene::SceneStore>()),
      playlist_(std::make_unique<playlist::PlaylistExecutor>()),
      media_lib_(std::make_unique<media_lib::MediaLibrary>()) {}

Kernel::~Kernel() {
  stop();
  // 先摘除全部总线 sink（含默认 sink），再让引擎成员逐个析构。
  // 理由：成员析构仍会产生事件——如 ~PlaylistExecutor → stop() →
  // evt.playlist.ended 经回调 bus_.post 广播；若外部宿主（UI 桥、测试探针）
  // 已先于本 Kernel 析构，其捕获的 [this] 即为悬垂对象。切断回调后
  // 析构期事件仅落环形记录，不再向外部派发。
  bus_.clear_sinks();
}

void Kernel::init() {
  // 1. 注入引擎间回调
  wire_callbacks();

  // 2. 注册总线 sink
  register_bus_sinks();

  // 3. 引擎注册表心跳初始化
  for (const auto& desc : engine_registry())
    hb_->note_heartbeat(desc.id, 0);
}

void Kernel::set_engine_metrics(std::function<double()> load_pct,
                                std::function<std::size_t()> mem_mb) {
  load_pct_fn_ = std::move(load_pct);
  mem_mb_fn_ = std::move(mem_mb);
}

void Kernel::pump_heartbeat(std::int64_t now_ms) {
  // 1. 记心跳并推进失联判定（§5.2：2s 一次、连续 3 次丢失判失联）。
  //    Phase 1 引擎与内核同进程，注册表即「应当在线」的集合。
  for (const auto& desc : engine_registry())
    hb_->note_heartbeat(desc.id, now_ms);
  hb_->tick(now_ms);

  // 2. 采样（未注入采样源则按 0，见 set_engine_metrics 注释）
  const double load_pct = load_pct_fn_ ? load_pct_fn_() : 0.0;
  const std::size_t mem_mb = mem_mb_fn_ ? mem_mb_fn_() : 0;

  // 3. diff 在线态 → 仅边沿翻转发 up/down；在线引擎逐 tick 发 heartbeat
  for (const auto& kv : hb_->snapshot()) {
    const std::string& id = kv.first;
    const bool online_now = kv.second.online;
    const bool online_prev = engine_online_[id];  // 缺省 false（视作未知）
    if (online_now != online_prev) {
      engine_online_[id] = online_now;
      bus_.post(make_event("engine.core",
                           online_now ? "evt.engine.up" : "evt.engine.down",
                           {{"engine_id", id}}));
    }
    if (online_now) {
      bus_.post(make_event("engine.core", "evt.engine.heartbeat",
                           {{"engine_id", id},
                            {"load_pct", load_pct},
                            {"mem_mb", mem_mb}}));
    }
  }
}

void Kernel::log_event(const std::string& level, const std::string& msg) {
  bus_.post(make_event("engine.core", "evt.log",
                       {{"level", level}, {"msg", msg}}));
}

void Kernel::wire_callbacks() {
  // ---- TimelineEngine → MediaEngine / SceneStore / MsgBus ----
  timeline_->set_media_preload_cb(
      [this](const std::string& item_id, const std::string& media_id,
             const json& meta) -> bool {
        media_->preload(item_id, media_id, meta);
        return true;
      });
  timeline_->set_media_play_cb(
      [this](const std::string& item_id, const std::string& media_id,
             const json& meta) {
        media_->play(item_id, media_id, meta);
      });
  timeline_->set_media_stop_cb(
      [this](const std::string& item_id) { media_->stop(item_id); });
  timeline_->set_scene_recall_cb(
      [this](const std::string& scene_id, int fade_ms,
             const std::string& recall_mode) {
        scene_->recall(scene_id, fade_ms, recall_mode);
      });
  timeline_->set_command_cb(
      [this](const std::string& op, const json& params) {
        bus_.post(make_cmd("engine.timeline", "engine.*", op, params));
      });
  timeline_->set_item_event_cb(
      [this](const std::string& evt, const timeline::TimelineItem& item,
             const std::string& reason) {
        json p = {
          {"item_id", item.item_id},
          {"track_index", item.track_index},
          {"start_ms", item.start_ms},
          {"ref_uuid", item.ref_uuid},
          {"reason", reason}
        };
        bus_.post(make_event("engine.timeline", evt, p));
      });

  // ---- PlaylistExecutor → MediaEngine / SceneStore / MsgBus ----
  playlist_->set_play_media_cb(
      [this](const std::string& media_id, const json& meta) {
        media_->play("pl_" + media_id, media_id, meta);
      });
  playlist_->set_recall_scene_cb(
      [this](const std::string& scene_id, int fade_ms) {
        scene_->recall(scene_id, fade_ms);
      });
  playlist_->set_send_command_cb(
      [this](const std::string& op, const json& params) {
        bus_.post(make_cmd("engine.playlist", "engine.*", op, params));
      });
  playlist_->set_is_media_done_cb(
      [this]() -> bool {
        return media_->state() != media::PlayState::playing;
      });
  playlist_->set_stop_media_cb(
      [this]() { media_->stop(""); });
  playlist_->set_event_cb(
      [this](const std::string& evt, const json& params) {
        bus_.post(make_event("engine.playlist", evt, params));
      });

  // ---- SceneStore → MediaEngine（召回时应用 state_json）----
  // §10.3.2 召回时序：先停当前媒体输出（若在播放），再按 state_json 应用新状态；
  // 状态「全部应用完成后」才发 evt.scene.recalled（见下方 recalled 回调）。
  scene_->set_apply_state_cb(
      [this](const std::string& state_json, int fade_ms,
             const std::string& recall_mode) {
        (void)recall_mode;
        json j = json::object();
        try {
          j = json::parse(state_json);
        } catch (...) {
          std::printf("[scene] 召回：state_json 解析失败，忽略应用\n");
          return;
        }
        // 1) 先停当前媒体输出（§10.3.2 / [1166]）
        auto st = media_->state();
        if (st == media::PlayState::playing || st == media::PlayState::paused)
          media_->stop("");

        // 2) 恢复媒体源：仅当 state_json 指向已入库素材时真正起播
        if (!j.contains("media") || !j["media"].is_object()) return;
        const json& mj = j["media"];
        if (!mj.contains("source") || !mj["source"].is_object()) return;
        const json& src = mj["source"];
        std::string type = src.value("type", "none");
        std::string media_id = src.value("media_id", "");
        if (type != "media" || media_id.empty()) return;

        std::string path = media_lib_->stored_path(media_id);
        if (path.empty()) {
          std::printf("[scene] 召回：素材不存在或未入库 media_id=%s\n",
                      media_id.c_str());
          return;
        }
        double gain_db = 0.0;
        if (j.contains("master") && j["master"].is_object())
          gain_db = j["master"].value("gain_db", 0.0);
        json meta = {{"media_path", path},
                     {"gain_db", gain_db},
                     {"fade_in_ms", fade_ms}};
        media_->play("scene_recall", media_id, meta);
      });

  // §10.3.2 / §5.4：召回完成后广播 evt.scene.recalled
  // {scene_id, fade_ms, applied_at_ms}。由 M3 统一发出，保证总线召回、
  // F1-F4 快捷键（scene_go）、节目单触发三条路径事件载荷完全一致。
  scene_->set_recalled_cb([this](const std::string& scene_id, int fade_ms,
                                 int64_t applied_at_ms) {
    bus_.post(make_event("engine.scene", "evt.scene.recalled",
                         {{"scene_id", scene_id},
                          {"fade_ms", fade_ms},
                          {"applied_at_ms", applied_at_ms}}));
  });

  // ---- MediaLibrary 回调：接入平台适配层 ----
  media_lib_->set_copy_cb([](const std::string& src, const std::string& dst) {
    // 文件复制：跨平台实现
    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) { std::fclose(in); return false; }
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
      std::fwrite(buf, 1, n, out);
    std::fclose(in);
    std::fclose(out);
    return true;
  });
  media_lib_->set_probe_cb([](const std::string& path) -> json {
    return platform::probe_media(path);
  });
  media_lib_->set_thumb_cb([](const std::string& path, int64_t dur) -> std::string {
    return platform::generate_thumbnail(path, dur);
  });
  // §10.1.1 导入第一步：存在性校验（文件缺失 → err 2001）
  media_lib_->set_stat_cb([](const std::string& path) -> bool {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
  });
  // §5.4：导入进度/完成事件上总线（evt.media.import_progress / import_done）
  media_lib_->set_event_cb(
      [this](const std::string& evt, const json& payload) {
        bus_.post(make_event("engine.media", evt, payload));
      });

  // ---- MediaEngine 回调：接入平台适配层 ----
  media_->set_open_cb([](const std::string& path, int64_t trim) -> bool {
    return platform::open_media_file(path, trim);
  });
  media_->set_start_cb([](double gain_db, int64_t fade_in_ms) {
    platform::start_media_playback(gain_db, fade_in_ms);
  });
  media_->set_stop_cb([](int64_t fade_out_ms) {
    platform::stop_media_playback(fade_out_ms);
  });
  media_->set_pos_cb([]() -> int64_t {
    return platform::get_media_pos_ms();
  });
  media_->set_duration_cb([](const std::string& path) -> int64_t {
    return platform::get_media_duration_ms(path);
  });
  media_->set_close_cb([]() {
    platform::close_media_file();
  });
  // §5.4：媒体错误事件上总线（M2 error 态统一走 evt.error，携 §5.5 错误码）
  media_->set_event_cb([this](const std::string& evt, const json& payload) {
    bus_.post(make_event("engine.media", evt, payload));
  });
  // §10.2.4：播放自然结束 → 记录日志；条目推进仍由 M5 按 duration 驱动
  // （调度器未开放「外部通知条目结束」接口，此处不做总线事件以免与
  //  evt.timeline.item_ended 重复）。
  media_->set_ended_cb([](const std::string& item_id,
                          const std::string& reason) {
    std::printf("[media] 播放结束 item=%s reason=%s\n", item_id.c_str(),
                reason.c_str());
  });

  // §10.2.4 pause / resume / seek：平台适配层当前未提供对应原语
  // （见 platform/media_backend.h：仅 open/start/stop/pos/duration/close），
  // 故此处注入「已受理、待平台里程碑落地」的空实现。M2 内部状态机仍然
  // 完整迁移（playing ⇄ paused、seek 成功返回 true），不会因缺少平台
  // 原语而卡死在中间态。
  media_->set_pause_cb([]() {});
  media_->set_resume_cb([]() {});
  media_->set_seek_cb([](int64_t pos_ms) { (void)pos_ms; });
}

std::vector<std::string> Kernel::collect_referencing_ids(
    const std::string& ref_uuid) const {
  std::vector<std::string> out;
  if (ref_uuid.empty()) return out;

  std::set<std::string> seen;
  // 时间线条目：ref_uuid 命中即视为引用（§7.4 条目引用）
  for (const auto& item : timeline_->scheduler().all_items()) {
    if (item && item->ref_uuid == ref_uuid && seen.insert(item->item_id).second)
      out.push_back(item->item_id);
  }
  // 节目单条目：ref_uuid 命中即视为引用
  for (const auto& pi : playlist_->items()) {
    if (pi.ref_uuid != ref_uuid) continue;
    std::string tag = "playlist/" + pi.item_id;
    if (seen.insert(tag).second) out.push_back(tag);
  }
  return out;
}

void Kernel::register_bus_sinks() {
  // TimelineEngine 注册为 engine.timeline
  timeline_->register_bus();

  // MediaLibrary 响应 media.* 指令（engine.media 命名空间）
  bus_.register_sink("engine.media", [this](const Envelope& env) {
    if (env.type != "cmd") return;
    const std::string& op = env.op;
    const json& p = env.params;

    if (op == "media.import") {
      // 导入文件
      std::vector<std::string> paths;
      if (p.contains("paths") && p["paths"].is_array())
        for (const auto& path : p["paths"])
          paths.push_back(path.get<std::string>());
      bool auto_tag = p.value("auto_tag", false);
      auto result = media_lib_->import_files(paths, auto_tag);
      bus_.post(make_reply(env, ec::OK, result));
    } else if (op == "media.query") {
      auto result = media_lib_->query(p);
      bus_.post(make_reply(env, ec::OK, result));
    } else if (op == "media.preview") {
      std::string path = p.value("path", p.value("media_id", ""));
      media_->preview(path);
      bus_.post(make_reply(env, ec::OK, {{"state", "previewing"}}));
    } else if (op == "media.remove") {
      // §5.3 media.remove{media_ids:[]}：软删进回收站；单个 media_id 兼容
      auto ids = string_list_param(p, "media_ids", "media_id");
      bool ok = !ids.empty();
      for (const auto& id : ids)
        if (!media_lib_->remove(id)) ok = false;
      bus_.post(make_reply(env, ok ? ec::OK : ec::FILE_MISSING, {}));
    } else if (op == "media.restore") {
      // §5.3 media.restore{media_ids:[]}：从回收站恢复
      auto ids = string_list_param(p, "media_ids", "media_id");
      bool ok = !ids.empty();
      for (const auto& id : ids)
        if (!media_lib_->restore(id)) ok = false;
      bus_.post(make_reply(env, ok ? ec::OK : ec::FILE_MISSING, {}));
    } else if (op == "media.purge") {
      // §10.1.4：物理删除。引用计数 >0 → 5003（message 含被引用明细）；
      // packaged=1 → 5003；素材不存在 → 2001。
      auto ids = string_list_param(p, "media_ids", "media_id");
      if (ids.empty()) {
        bus_.post(make_reply(env, ec::BAD_PARAM, {}));
      } else {
        int fail_code = ec::OK;
        std::string fail_msg;
        for (const auto& id : ids) {
          auto refs = collect_referencing_ids(id);
          auto r = media_lib_->purge_ex(id, refs);
          if (!r.ok) {
            fail_code = r.error_code != 0 ? r.error_code : ec::FILE_MISSING;
            fail_msg = r.message;
            break;
          }
        }
        if (fail_code == ec::OK)
          bus_.post(make_reply(env, ec::OK, {}));
        else
          bus_.post(make_reply(env, fail_code, {{"message", fail_msg}}));
      }
    } else if (op == "media.update_tags") {
      std::string id = p.value("media_id", "");
      std::string tags = style_tags_param(p);
      bool ok = media_lib_->update_tags(id, tags);
      bus_.post(make_reply(env, ok ? ec::OK : ec::FILE_MISSING, {}));
    } else if (op == "sys.set_volume") {
      // §5.3 sys.set_volume{ch, gain_db}：ch ∈ {master,music,mic}，
      // gain_db ∈ [-60, 6]。目标地址按字典为 engine.media。
      std::string ch = p.value("ch", "");
      if (!p.contains("gain_db") || !p["gain_db"].is_number()) {
        bus_.post(make_reply(env, ec::BAD_PARAM,
                             {{"message", "gain_db 必须为数值"}}));
      } else if (ch != "master" && ch != "music" && ch != "mic") {
        bus_.post(make_reply(env, ec::BAD_PARAM,
                             {{"message", "ch 必须为 master/music/mic"}}));
      } else {
        double gain_db = p["gain_db"].get<double>();
        if (gain_db < -60.0 || gain_db > 6.0) {
          bus_.post(make_reply(env, ec::BAD_PARAM,
                               {{"message", "gain_db 超出 [-60, 6] dB"}}));
        } else {
          channel_gain_db_[ch] = gain_db;
          bus_.post(make_reply(env, ec::OK, {{"ch", ch}, {"gain_db", gain_db}}));
        }
      }
    } else {
      bus_.post(make_reply(env, ec::UNKNOWN_OP, {}));
    }
  });

  // SceneStore 响应 scene.* 指令（engine.scene 命名空间）
  bus_.register_sink("engine.scene", [this](const Envelope& env) {
    if (env.type != "cmd") return;
    const std::string& op = env.op;
    const json& p = env.params;

    if (op == "scene.save") {
      std::string name = p.value("scene_name", "");
      std::string folder = p.value("folder", "");
      int fade = p.value("fade_ms", 0);
      std::string mode = p.value("recall_mode", "fade");
      std::string id = scene_->save(name, folder, fade, mode);
      if (id.empty())
        bus_.post(make_reply(env, ec::BAD_PARAM, {}));
      else {
        bus_.post(make_reply(env, ec::OK, {{"scene_id", id}}));
        bus_.post(make_event("engine.scene", "evt.scene.saved",
                             {{"scene_id", id}}));
      }
    } else if (op == "scene.recall") {
      std::string id = p.value("scene_id", "");
      int fade = p.value("fade_ms", -1);
      std::string mode = p.value("recall_mode", "");
      bool ok = scene_->recall(id, fade, mode);
      // 成功时 evt.scene.recalled 由 M3 召回完成回调统一发出（§10.3.2），
      // 此处只回 rsp，避免同一动作发两条召回事件。
      bus_.post(make_reply(env, ok ? ec::OK : ec::SCENE_NOT_FOUND, {}));
    } else if (op == "scene.delete") {
      // §10.3.3 / [1171]：场景不存在 → 5001；被 timeline_item.ref_uuid 或
      // playlist_item.ref_uuid 引用 → 5003（提示先解除引用）。
      std::string id = p.value("scene_id", "");
      if (id.empty()) {
        bus_.post(make_reply(env, ec::BAD_PARAM, {}));
      } else if (!scene_->find(id)) {
        bus_.post(make_reply(env, ec::SCENE_NOT_FOUND, {}));
      } else {
        auto refs = collect_referencing_ids(id);
        if (scene_->remove(id, refs)) {
          bus_.post(make_reply(env, ec::OK, {}));
        } else {
          bus_.post(make_reply(
              env, ec::REFERENCED_OBJECT_MISSING,
              {{"message", "场景仍被引用，请先解除引用"},
               {"referencing_ids", refs}}));
        }
      }
    } else if (op == "scene.list") {
      std::string folder = p.value("folder", "");
      auto result = scene_->list(folder);
      bus_.post(make_reply(env, ec::OK, result));
    } else {
      bus_.post(make_reply(env, ec::UNKNOWN_OP, {}));
    }
  });

  // PlaylistExecutor 响应 playlist.* 指令（engine.playlist 命名空间）
  bus_.register_sink("engine.playlist", [this](const Envelope& env) {
    if (env.type != "cmd") return;
    const std::string& op = env.op;
    const json& p = env.params;

    if (op == "playlist.load") {
      std::vector<playlist::PlaylistItem> items;
      if (p.contains("items") && p["items"].is_array()) {
        for (const auto& ji : p["items"]) {
          playlist::PlaylistItem item;
          item.item_id = ji.value("item_id", sm::uuid_hex32());
          item.sort_index = ji.value("sort_index", 0);
          item.type = playlist::item_type_from_string(
              ji.value("type", "media"));
          item.ref_uuid = ji.value("ref_uuid", "");
          std::string trig = ji.value("trigger", "auto");
          if (trig == "go") item.trigger = playlist::TriggerMode::go;
          else if (trig == "delay") item.trigger = playlist::TriggerMode::delay;
          else if (trig == "timecode")
            item.trigger = playlist::TriggerMode::timecode;
          else item.trigger = playlist::TriggerMode::auto_;
          item.delay_ms = ji.value("delay_ms", 0);
          if (ji.contains("meta")) item.meta = ji["meta"];
          items.push_back(item);
        }
      }
      // §5.4 evt.playlist.playing 载荷含 playlist_id；M4 不持节目单库，
      // 标识由宿主在装载时从指令参数注入（缺省空串表示匿名节目单）。
      playlist_->set_playlist_id(p.value("playlist_id", std::string()));
      playlist_->load(items);
      bus_.post(make_reply(env, ec::OK, {{"item_count", items.size()}}));
    } else if (op == "playlist.start") {
      playlist_->start();
      bus_.post(make_reply(env, ec::OK, {{"state", "running"}}));
    } else if (op == "playlist.go") {
      playlist_->go();
      bus_.post(make_reply(env, ec::OK, {{"state",
          playlist::exec_state_name(playlist_->state())}}));
    } else if (op == "playlist.next") {
      playlist_->next();
      bus_.post(make_reply(env, ec::OK, {}));
    } else if (op == "playlist.stop") {
      playlist_->stop();
      bus_.post(make_reply(env, ec::OK, {{"state", "ended"}}));
    } else if (op == "playlist.pause") {
      playlist_->pause();
      bus_.post(make_reply(env, ec::OK, {{"state", "paused"}}));
    } else if (op == "playlist.resume") {
      playlist_->resume();
      bus_.post(make_reply(env, ec::OK, {{"state", "running"}}));
    } else if (op == "playlist.state") {
      bus_.post(make_reply(env, ec::OK, {
        {"state", playlist::exec_state_name(playlist_->state())},
        {"current_index", playlist_->current_index()},
        {"item_count", playlist_->item_count()}
      }));
    } else {
      bus_.post(make_reply(env, ec::UNKNOWN_OP, {}));
    }
  });

  // 内核控制面（§5.3 sys.* / remote.hello）：sys.ping、sys.get_state、
  // sys.shutdown、remote.hello。sys.set_volume 按字典 dst 由 engine.media 承接。
  bus_.register_sink("engine.core", [this](const Envelope& env) {
    if (env.type != "cmd") return;
    const std::string& op = env.op;

    if (op == "sys.ping") {
      bus_.post(make_reply(env, ec::OK,
                           {{"pong", true},
                            {"monotonic_ms", now_monotonic_ms()}}));
    } else if (op == "sys.get_state") {
      bus_.post(make_reply(env, ec::OK, {}));
      // §5.4：evt.state.snapshot 携带完整状态快照
      json snap = json::object();
      try {
        snap = json::parse(get_status_json());
      } catch (...) {
        snap = json::object();
      }
      bus_.post(make_event("engine.core", "evt.state.snapshot", snap));
    } else if (op == "sys.shutdown") {
      // 进程退出由宿主主循环承接（main_headless），此处只确认受理
      bus_.post(make_reply(env, ec::OK, {}));
      std::printf("[kernel] 收到 sys.shutdown，交由宿主退出\n");
    } else if (op == "remote.hello") {
      // §9.5 [1014] 握手应答携带协议版本与能力表；
      // 控制面白名单判定在遥控入口（main_headless TCP 适配器调用
      // sm::is_remote_control_op，越界回 1002），此处只负责应答。
      json caps = json::array();
      for (const auto& e : cmd_ops()) caps.push_back(e.op);
      bus_.post(make_reply(env, ec::OK,
                           {{"v", kEnvelopeVersion},
                            {"tcp_port", 9000},
                            {"capabilities", caps}}));
    } else {
      bus_.post(make_reply(env, ec::UNKNOWN_OP, {}));
    }
  });

  // 总线默认 sink：打印未路由消息
  bus_.set_default_sink([](const Envelope& e) {
    if (e.type == "cmd")
      std::printf("[bus] unrouted cmd %s -> %s\n",
                   e.op.c_str(), e.dst.c_str());
  });
}

void Kernel::start() {
  timeline_->start();
  std::printf("[kernel] TimelineEngine 调度线程已启动\n");
}

void Kernel::stop() {
  timeline_->stop();
  std::printf("[kernel] 所有引擎已停止\n");
}

// ---- 状态查询 ----
std::string Kernel::get_status_json() const {
  json j;
  j["mode"] = "headless";
  j["play_state"] = timeline::play_state_name(timeline_->scheduler().play_state());
  j["pos_ms"] = timeline_->scheduler().pos_ms();
  j["total_ms"] = timeline_->scheduler().total_duration_ms();
  j["media_state"] = media::play_state_name(media_->state());
  j["playlist_state"] = playlist::exec_state_name(playlist_->state());
  j["playlist_index"] = playlist_->current_index();
  j["scene_count"] = scene_->count();
  j["media_count"] = media_lib_->count();

  json engines = json::array();
  for (const auto& desc : engine_registry())
    engines.push_back({{"id", desc.id}, {"module", desc.module}});
  j["engines"] = engines;
  return j.dump();
}

std::string Kernel::get_playlist_json() const {
  json j;
  j["state"] = playlist::exec_state_name(playlist_->state());
  j["current_index"] = playlist_->current_index();
  json items = json::array();
  for (const auto& item : playlist_->items()) {
    items.push_back({
      {"item_id", item.item_id},
      {"sort_index", item.sort_index},
      {"type", playlist::item_type_name(item.type)},
      {"ref_uuid", item.ref_uuid},
      {"trigger", playlist::trigger_mode_name(item.trigger)},
      {"delay_ms", item.delay_ms}
    });
  }
  j["items"] = items;
  return j.dump();
}

// ---- 快捷操作 ----
bool Kernel::scene_go(const std::string& scene_id, int fade_ms) {
  return scene_->recall(scene_id, fade_ms);
}

bool Kernel::transport_play(const std::string& media_id) {
  if (media_id.empty()) {
    timeline_->scheduler().play(0);
  } else {
    json meta = {{"gain_db", 0.0}, {"fade_in_ms", 0}};
    media_->play("manual", media_id, meta);
  }
  return true;
}

bool Kernel::transport_stop() {
  timeline_->scheduler().stop();
  media_->stop("");
  return true;
}

bool Kernel::transport_pause() {
  timeline_->scheduler().pause();
  return true;
}

bool Kernel::transport_resume() {
  timeline_->scheduler().resume();
  return true;
}

bool Kernel::playlist_go() {
  playlist_->go();
  return true;
}

bool Kernel::playlist_start() {
  playlist_->start();
  return true;
}

bool Kernel::playlist_stop() {
  playlist_->stop();
  return true;
}

bool Kernel::playlist_next() {
  playlist_->next();
  return true;
}

}  // namespace sm

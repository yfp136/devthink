// Kernel 实现：引擎集成 + 总线路由 + 回调注入
#include "engines/kernel.h"

#include <chrono>
#include <cstdio>

#include "core/error_codes.h"
#include "core/msg_bus.h"
#include "core/util.h"
#include "engines/engine_registry.h"

namespace sm {

using json = nlohmann::json;

Kernel::Kernel()
    : hb_(std::make_unique<HeartbeatMonitor>()),
      timeline_(std::make_unique<timeline::TimelineEngine>(bus_)),
      media_(std::make_unique<media::MediaEngine>()),
      scene_(std::make_unique<scene::SceneStore>()),
      playlist_(std::make_unique<playlist::PlaylistExecutor>()),
      media_lib_(std::make_unique<media_lib::MediaLibrary>()) {}

Kernel::~Kernel() { stop(); }

void Kernel::init() {
  // 1. 注入引擎间回调
  wire_callbacks();

  // 2. 注册总线 sink
  register_bus_sinks();

  // 3. 引擎注册表心跳初始化
  for (const auto& desc : engine_registry())
    hb_->note_heartbeat(desc.id, 0);
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
  scene_->set_apply_state_cb(
      [this](const std::string& state_json, int fade_ms,
             const std::string& recall_mode) {
        // P1：状态应用到 MediaEngine（fade 由 M3 管理，M2 执行音量变化）
        // 解析 state_json 获取 gain_db
        try {
          auto j = json::parse(state_json);
          if (j.contains("master")) {
            double gain_db = j["master"].value("gain_db", 0.0);
            // 简化：直接调整 MediaEngine 的增益
            // 实际需要通过总线发 master.* 指令
            (void)gain_db;  // P1 暂存
          }
        } catch (...) {}
        bus_.post(make_event("engine.scene", "evt.scene.recalled",
                             {{"fade_ms", fade_ms}, {"recall_mode", recall_mode}}));
      });

  // ---- MediaLibrary stub 回调 ----
  media_lib_->set_copy_cb([](const std::string&, const std::string&) {
    return true;  // P1 stub：不实际复制文件
  });
  media_lib_->set_probe_cb([](const std::string&) -> json {
    return json::object();  // P1 stub：无实际探测
  });
  media_lib_->set_thumb_cb([](const std::string&, int64_t) -> std::string {
    return "";  // P1 stub：无缩略图
  });
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
      std::string id = p.value("media_id", "");
      bool ok = media_lib_->remove(id);
      bus_.post(make_reply(env, ok ? ec::OK : ec::FILE_MISSING, {}));
    } else if (op == "media.restore") {
      std::string id = p.value("media_id", "");
      bool ok = media_lib_->restore(id);
      bus_.post(make_reply(env, ok ? ec::OK : ec::FILE_MISSING, {}));
    } else if (op == "media.update_tags") {
      std::string id = p.value("media_id", "");
      std::string tags = p.value("style_tags", "");
      bool ok = media_lib_->update_tags(id, tags);
      bus_.post(make_reply(env, ok ? ec::OK : ec::FILE_MISSING, {}));
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
      if (ok) {
        bus_.post(make_reply(env, ec::OK, {}));
        bus_.post(make_event("engine.scene", "evt.scene.recalled",
                             {{"scene_id", id}}));
      } else {
        bus_.post(make_reply(env, ec::SCENE_NOT_FOUND, {}));
      }
    } else if (op == "scene.delete") {
      std::string id = p.value("scene_id", "");
      bool ok = scene_->remove(id);
      bus_.post(make_reply(env, ok ? ec::OK : ec::SCENE_NOT_FOUND, {}));
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

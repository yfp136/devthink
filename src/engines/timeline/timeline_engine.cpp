// TimelineEngine 实现：总线指令处理 + 调度线程
#include "engines/timeline/timeline_engine.h"

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/error_codes.h"
#include "core/util.h"
#include "nlohmann/json.hpp"

namespace sm {
namespace timeline {

using json = nlohmann::json;

namespace {
// 单调时钟毫秒（播放位置推算基准；实际部署由 M2 音频时钟驱动）
int64_t mono_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

TimelineEngine::TimelineEngine(MsgBus& bus)
    : bus_(bus), scheduler_(std::make_unique<TimelineScheduler>()) {}

TimelineEngine::~TimelineEngine() { stop(); }

void TimelineEngine::register_bus() {
  bus_.register_sink("engine.timeline", [this](const Envelope& env) {
    if (env.type == "cmd") handle_command(env);
  });
}

Envelope TimelineEngine::make_rsp(const Envelope& req, int code,
                                  const json& params) {
  return sm::make_reply(req, code, params);
}

void TimelineEngine::report_bpm(double bpm, double confidence) {
  // §5.4：无效节拍直接丢弃（检测器冷启动阶段会给出 0 或负值）
  if (!(bpm > 0.0)) return;
  if (confidence < 0.0) confidence = 0.0;
  if (confidence > 1.0) confidence = 1.0;
  // 数值未变不重发：MIDI 时钟 24ppq 会把同一 BPM 反复上报，去重后
  // evt.transport.bpm 才是「节拍变化」语义，遥控面不会被动刷屏。
  if (last_bpm_.load() == bpm &&
      last_bpm_confidence_.load() == confidence) {
    return;
  }
  last_bpm_.store(bpm);
  last_bpm_confidence_.store(confidence);
  bus_.post(sm::make_event("engine.timeline", "evt.transport.bpm",
                           {{"bpm", bpm}, {"confidence", confidence}}));
}

void TimelineEngine::handle_command(const Envelope& env) {
  const std::string& op = env.op;
  const json& p = env.params;

  // ---- transport.*（播放控制）----
  if (op == "transport.play") {
    // §5.3：params = {item_id?}（从该条目起点起播）；兼容历史 from_ms 写法
    int64_t from_ms = p.value("from_ms", int64_t(0));
    std::string item_id = p.value("item_id", std::string());
    if (!item_id.empty()) {
      ItemPtr it = scheduler_->find_item(item_id);
      if (!it) {
        bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));  // 3001
        return;
      }
      from_ms = it->start_ms;
    }
    scheduler_->play(from_ms);
    is_playing_ = true;
    play_base_ms_ = from_ms;
    play_start_ms_ = mono_now_ms();
    bus_.post(make_rsp(env, ec::OK, {{"state", "playing"}, {"pos_ms", from_ms}}));
    bus_.post(sm::make_event("engine.timeline", "evt.transport.state",
                             {{"state", "playing"}, {"pos_ms", from_ms}}));
    return;
  }
  if (op == "transport.pause") {
    scheduler_->pause();
    is_playing_ = false;
    bus_.post(make_rsp(env, ec::OK, {{"state", "paused"}}));
    bus_.post(sm::make_event("engine.timeline", "evt.transport.state",
                             {{"state", "paused"},
                              {"pos_ms", scheduler_->pos_ms()}}));
    return;
  }
  if (op == "transport.resume") {
    scheduler_->resume();
    is_playing_ = true;
    play_start_ms_ = mono_now_ms();
    play_base_ms_ = scheduler_->pos_ms();
    bus_.post(make_rsp(env, ec::OK, {{"state", "playing"}}));
    bus_.post(sm::make_event("engine.timeline", "evt.transport.state",
                             {{"state", "playing"},
                              {"pos_ms", scheduler_->pos_ms()}}));
    return;
  }
  if (op == "transport.stop") {
    scheduler_->stop();
    is_playing_ = false;
    bus_.post(make_rsp(env, ec::OK, {{"state", "stopped"}}));
    bus_.post(sm::make_event("engine.timeline", "evt.transport.state",
                             {{"state", "stopped"}, {"pos_ms", 0}}));
    return;
  }
  if (op == "transport.seek") {
    // §5.3：参数名为 to_ms；桌面 UI（TimelineDock.qml）沿用 pos_ms，两者都接受
    int64_t pos = p.contains("to_ms") ? p.value("to_ms", int64_t(0))
                                      : p.value("pos_ms", int64_t(0));
    int code = scheduler_->seek(pos);
    if (code != ec::OK) {
      bus_.post(make_rsp(env, code, {{"pos_ms", pos}}));  // 3003 播放头越界
      return;
    }
    play_base_ms_ = pos;
    play_start_ms_ = mono_now_ms();
    bus_.post(make_rsp(env, ec::OK, {{"pos_ms", pos}}));
    bus_.post(sm::make_event(
        "engine.timeline", "evt.transport.state",
        {{"state", play_state_name(scheduler_->play_state())}, {"pos_ms", pos}}));
    return;
  }
  if (op == "transport.goto_show") {
    // §5.3：{item_id} 跳转到指定条目起点并起播（演出跳转）
    std::string item_id = p.value("item_id", std::string());
    ItemPtr it = scheduler_->find_item(item_id);
    if (!it) {
      bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));  // 3001 条目/轨道不存在
      return;
    }
    int code = scheduler_->seek(it->start_ms);
    if (code != ec::OK) {
      bus_.post(make_rsp(env, code, {}));  // 3003
      return;
    }
    scheduler_->play(it->start_ms);
    is_playing_ = true;
    play_base_ms_ = it->start_ms;
    play_start_ms_ = mono_now_ms();
    bus_.post(make_rsp(env, ec::OK,
                       {{"item_id", item_id}, {"pos_ms", it->start_ms}}));
    bus_.post(sm::make_event("engine.timeline", "evt.transport.state",
                             {{"state", "playing"}, {"pos_ms", it->start_ms}}));
    return;
  }
  if (op == "transport.state") {
    json j;
    j["state"] = play_state_name(scheduler_->play_state());
    j["pos_ms"] = scheduler_->pos_ms();
    j["total_ms"] = scheduler_->total_duration_ms();
    json items = json::array();
    for (const auto& it : scheduler_->active_items()) {
      items.push_back({
        {"item_id", it->item_id},
        {"track_index", it->track_index},
        {"start_ms", it->start_ms},
        {"duration_ms", it->duration_ms},
        {"ref_type", ref_type_to_string(it->ref_type)},
        {"ref_uuid", it->ref_uuid},
        {"state", item_state_name(it->state)},
      });
    }
    j["active_items"] = items;
    bus_.post(make_rsp(env, ec::OK, j));
    return;
  }

  // ---- timeline.*（编辑/配置）----
  if (op == "timeline.track_configure") {
    // §5.3 / §7.4：{tracks:[{index,type,name,enabled}]} 下发轨道配置
    if (!p.contains("tracks") || !p["tracks"].is_array() || p["tracks"].empty()) {
      bus_.post(make_rsp(env, ec::BAD_PARAM, {}));  // 1002
      return;
    }
    std::vector<TrackConfig> cfgs;
    for (const auto& jt : p["tracks"]) {
      if (!jt.is_object()) {
        bus_.post(make_rsp(env, ec::BAD_PARAM, {}));
        return;
      }
      TrackConfig cfg;
      cfg.index = jt.value("index", -1);
      if (cfg.index < 0) {
        bus_.post(make_rsp(env, ec::BAD_PARAM, {}));
        return;
      }
      cfg.type = track_type_from_string(jt.value("type", std::string("audio")));
      cfg.name = jt.value("name", std::string());
      cfg.enabled = jt.value("enabled", true);
      cfgs.push_back(cfg);
    }
    int code = scheduler_->configure_tracks(cfgs);
    if (code != ec::OK) {
      bus_.post(make_rsp(env, code, {}));
      return;
    }
    bus_.post(make_rsp(env, ec::OK,
                       {{"track_count", int(scheduler_->track_count())}}));
    return;
  }
  if (op == "timeline.item_insert") {
    TimelineItem item;
    item.item_id = p.value("item_id", sm::uuid_hex32());
    item.track_index = p.value("track_index", 0);
    item.start_ms = p.value("start_ms", 0);
    item.duration_ms = p.value("duration_ms", 0);
    item.ref_type = ref_type_from_string(p.value("ref_type", "media"));
    item.ref_uuid = p.value("ref_uuid", "");
    item.loop = p.value("loop", 0);
    if (p.contains("meta")) item.meta = p["meta"];

    // §7.4 编辑校验：轨道越界 3001 / 同轨同起点冲突 3002
    int code = scheduler_->insert_item(item);
    if (code == ec::OK) {
      bus_.post(make_rsp(env, ec::OK, {{"item_id", item.item_id}}));
    } else {
      bus_.post(make_rsp(env, code, {}));
    }
    return;
  }
  if (op == "timeline.item_remove") {
    // §5.3：{item_ids:[]}；兼容历史单条 item_id 写法
    std::vector<std::string> ids;
    if (p.contains("item_ids") && p["item_ids"].is_array()) {
      for (const auto& j : p["item_ids"])
        if (j.is_string()) ids.push_back(j.get<std::string>());
    } else if (p.contains("item_id")) {
      ids.push_back(p.value("item_id", std::string()));
    }
    if (ids.empty()) {
      bus_.post(make_rsp(env, ec::BAD_PARAM, {}));  // 1002
      return;
    }
    int removed = 0;
    for (const auto& id : ids)
      if (scheduler_->remove_item(id)) ++removed;
    if (removed == 0) {
      bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));  // 3001 条目不存在
      return;
    }
    bus_.post(make_rsp(env, ec::OK, {{"removed", removed}}));
    return;
  }
  if (op == "timeline.item_update") {
    // §5.3：{item_id, patch:{}}；兼容历史扁平字段写法
    std::string id = p.value("item_id", std::string());
    ItemPtr item = scheduler_->find_item(id);
    if (!item) {
      bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));  // 3001
      return;
    }
    json patch = (p.contains("patch") && p["patch"].is_object())
                     ? p["patch"]
                     : json::object();
    auto pick = [&](const char* key, int64_t cur) -> int64_t {
      if (patch.contains(key)) return patch[key].get<int64_t>();
      if (p.contains(key)) return p[key].get<int64_t>();
      return cur;
    };
    int64_t new_start = pick("start_ms", item->start_ms);

    // §7.4：移动后同轨同起点冲突 → 3002（未修改任何状态）
    const Track& tr = scheduler_->track(item->track_index);
    for (const auto& other : tr.items) {
      if (other->item_id != id && other->start_ms == new_start) {
        bus_.post(make_rsp(env, ec::ITEM_CONFLICT, {}));
        return;
      }
    }

    bool ok = scheduler_->update_item(id, [&](TimelineItem& it) {
      it.start_ms = new_start;
      it.duration_ms = pick("duration_ms", it.duration_ms);
      if (patch.contains("loop")) it.loop = patch["loop"].get<int>();
      else if (p.contains("loop")) it.loop = p["loop"].get<int>();
      if (patch.contains("meta")) it.meta = patch["meta"];
      else if (p.contains("meta")) it.meta = p["meta"];
    });
    if (ok) {
      bus_.post(make_rsp(env, ec::OK, {{"item_id", id}}));
    } else {
      bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));
    }
    return;
  }
  if (op == "timeline.load") {
    // §5.3：{project_id, revision?}；Phase 1 由工程层把条目数组一并下发（items）。
    // §10.6[1263]：载入前复位状态机并广播 stopped，任一环节失败即整体拒绝，
    // 禁止半载入（先校验后应用）。
    if (!p.contains("items") && !p.contains("project_id")) {
      bus_.post(make_rsp(env, ec::BAD_PARAM, {}));  // 1002
      return;
    }
    std::vector<TimelineItem> staged;
    if (p.contains("items")) {
      if (!p["items"].is_array()) {
        bus_.post(make_rsp(env, ec::BAD_PARAM, {}));
        return;
      }
      const int track_total = int(scheduler_->track_count());
      std::set<std::string> seen_ids;
      std::map<std::pair<int, int64_t>, bool> seen_slots;
      for (const auto& ji : p["items"]) {
        if (!ji.is_object()) {
          bus_.post(make_rsp(env, ec::BAD_PARAM, {}));
          return;
        }
        TimelineItem item;
        item.item_id = ji.value("item_id", sm::uuid_hex32());
        item.track_index = ji.value("track_index", 0);
        item.start_ms = ji.value("start_ms", 0);
        item.duration_ms = ji.value("duration_ms", 0);
        item.ref_type = ref_type_from_string(ji.value("ref_type", "media"));
        item.ref_uuid = ji.value("ref_uuid", "");
        item.loop = ji.value("loop", 0);
        if (ji.contains("meta")) item.meta = ji["meta"];

        if (item.item_id.empty() || !seen_ids.insert(item.item_id).second) {
          bus_.post(make_rsp(env, ec::BAD_PARAM, {}));  // item_id 缺失/重复
          return;
        }
        if (item.track_index < 0 || item.track_index >= track_total) {
          bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));  // 3001 轨道越界
          return;
        }
        if (!seen_slots.emplace(std::make_pair(item.track_index, item.start_ms),
                                true)
                 .second) {
          bus_.post(make_rsp(env, ec::ITEM_CONFLICT, {}));  // 3002 同轨同起点
          return;
        }
        staged.push_back(std::move(item));
      }
    }

    // 应用阶段（校验已全部通过，此后不再失败）
    scheduler_->stop();
    scheduler_->clear_items();
    for (const auto& it : staged) scheduler_->insert_item(it);
    bus_.post(make_rsp(env, ec::OK,
                       {{"total_ms", scheduler_->total_duration_ms()},
                        {"item_count", int(staged.size())}}));
    bus_.post(sm::make_event("engine.timeline", "evt.transport.state",
                             {{"state", "stopped"}, {"pos_ms", 0}}));
    return;
  }
  if (op == "timeline.tracks") {
    json arr = json::array();
    for (int i = 0; i < int(scheduler_->track_count()); ++i) {
      arr.push_back({
        {"index", i},
        {"type", track_type_name(scheduler_->track_type(i))},
        {"name", scheduler_->track(i).name},
      });
    }
    bus_.post(make_rsp(env, ec::OK, {{"tracks", arr}}));
    return;
  }

  // 未知指令
  bus_.post(make_rsp(env, ec::UNKNOWN_OP, {}));
}

void TimelineEngine::start() {
  if (running_.load()) return;
  running_ = true;
  tick_thread_ = std::thread([this] { tick_thread(); });
}

void TimelineEngine::stop() {
  if (!running_.load()) return;
  running_ = false;
  if (tick_thread_.joinable()) tick_thread_.join();
}

void TimelineEngine::tick_thread() {
  // 2ms 周期 tick（§7.3）
  using namespace std::chrono;
  auto next = steady_clock::now();
  // §5.4：evt.transport.clock 广播节流（100ms 一次，避免 2ms 周期刷爆总线）
  constexpr int64_t kClockIntervalMs = 100;
  int64_t last_clock_ms = 0;

  while (running_.load()) {
    next += milliseconds(2);

    int64_t pos_ms = scheduler_->pos_ms();
    const int64_t now_ms = mono_now_ms();
    if (is_playing_.load()) {
      // 用单调时钟驱动播放位置（实际部署由音频时钟驱动更精确）
      pos_ms = play_base_ms_ + (now_ms - play_start_ms_);
    }

    scheduler_->tick(pos_ms);

    if (now_ms - last_clock_ms >= kClockIntervalMs) {
      last_clock_ms = now_ms;
      bus_.post(sm::make_event(
          "engine.timeline", "evt.transport.clock",
          {{"pos_ms", pos_ms},
           {"total_ms", scheduler_->total_duration_ms()},
           {"state", play_state_name(scheduler_->play_state())}}));
    }

    std::this_thread::sleep_until(next);
  }
}

}  // namespace timeline
}  // namespace sm

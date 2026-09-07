// TimelineEngine 实现：总线指令处理 + 调度线程
#include "engines/timeline/timeline_engine.h"

#include <chrono>

#include "core/error_codes.h"
#include "core/util.h"
#include "nlohmann/json.hpp"

namespace sm {
namespace timeline {

using json = nlohmann::json;

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

void TimelineEngine::handle_command(const Envelope& env) {
  const std::string& op = env.op;
  const json& p = env.params;

  // ---- transport.*（播放控制）----
  if (op == "transport.play") {
    int64_t from_ms = p.value("from_ms", 0);
    scheduler_->play(from_ms);
    is_playing_ = true;
    play_base_ms_ = from_ms;
    play_start_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    bus_.post(make_rsp(env, ec::OK, {{"state", "playing"}}));
    return;
  }
  if (op == "transport.pause") {
    scheduler_->pause();
    is_playing_ = false;
    bus_.post(make_rsp(env, ec::OK, {{"state", "paused"}}));
    return;
  }
  if (op == "transport.resume") {
    scheduler_->resume();
    is_playing_ = true;
    play_start_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    play_base_ms_ = scheduler_->pos_ms();
    bus_.post(make_rsp(env, ec::OK, {{"state", "playing"}}));
    return;
  }
  if (op == "transport.stop") {
    scheduler_->stop();
    is_playing_ = false;
    bus_.post(make_rsp(env, ec::OK, {{"state", "stopped"}}));
    return;
  }
  if (op == "transport.seek") {
    int64_t pos = p.value("pos_ms", 0);
    scheduler_->seek(pos);
    play_base_ms_ = pos;
    play_start_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    bus_.post(make_rsp(env, ec::OK, {{"pos_ms", pos}}));
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

    int code = scheduler_->insert_item(item);
    if (code == ec::OK) {
      bus_.post(make_rsp(env, ec::OK, {{"item_id", item.item_id}}));
      // 广播变更事件
      bus_.post(sm::make_event(
          "engine.timeline", "evt.timeline.item_added",
          {{"item_id", item.item_id}, {"track_index", item.track_index}}));
    } else {
      bus_.post(make_rsp(env, code, {}));
    }
    return;
  }
  if (op == "timeline.item_remove") {
    std::string id = p.value("item_id", "");
    if (scheduler_->remove_item(id)) {
      bus_.post(make_rsp(env, ec::OK, {}));
      bus_.post(sm::make_event("engine.timeline", "evt.timeline.item_removed",
                               {{"item_id", id}}));
    } else {
      bus_.post(make_rsp(env, ec::TRACK_MISSING, {}));
    }
    return;
  }
  if (op == "timeline.item_update") {
    std::string id = p.value("item_id", "");
    bool ok = scheduler_->update_item(id, [&](TimelineItem& it) {
      if (p.contains("start_ms")) it.start_ms = p["start_ms"];
      if (p.contains("duration_ms")) it.duration_ms = p["duration_ms"];
      if (p.contains("loop")) it.loop = p["loop"];
      if (p.contains("meta")) it.meta = p["meta"];
    });
    if (ok) {
      bus_.post(make_rsp(env, ec::OK, {}));
      bus_.post(sm::make_event("engine.timeline", "evt.timeline.item_updated",
                               {{"item_id", id}}));
    } else {
      bus_.post(make_rsp(env, ec::ITEM_CONFLICT, {}));
    }
    return;
  }
  if (op == "timeline.load") {
    // 简化：从 params 直接加载条目数组（完整实现从 DB 加载）
    scheduler_->stop();
    if (p.contains("items") && p["items"].is_array()) {
      for (const auto& ji : p["items"]) {
        TimelineItem item;
        item.item_id = ji.value("item_id", sm::uuid_hex32());
        item.track_index = ji.value("track_index", 0);
        item.start_ms = ji.value("start_ms", 0);
        item.duration_ms = ji.value("duration_ms", 0);
        item.ref_type = ref_type_from_string(ji.value("ref_type", "media"));
        item.ref_uuid = ji.value("ref_uuid", "");
        item.loop = ji.value("loop", 0);
        if (ji.contains("meta")) item.meta = ji["meta"];
        scheduler_->insert_item(item);
      }
    }
    bus_.post(make_rsp(env, ec::OK, {{"total_ms", scheduler_->total_duration_ms()}}));
    bus_.post(sm::make_event("engine.timeline", "evt.timeline.loaded",
                             {{"total_ms", scheduler_->total_duration_ms()}}));
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

  while (running_.load()) {
    next += milliseconds(2);

    int64_t pos_ms = scheduler_->pos_ms();
    if (is_playing_.load()) {
      // 用单调时钟驱动播放位置（实际部署由音频时钟驱动更精确）
      auto now = duration_cast<milliseconds>(
                     steady_clock::now().time_since_epoch())
                     .count();
      pos_ms = play_base_ms_ + (now - play_start_ms_);
    }

    scheduler_->tick(pos_ms);

    std::this_thread::sleep_until(next);
  }
}

}  // namespace timeline
}  // namespace sm

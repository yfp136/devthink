// PlaylistExecutor 实现（§10.5）
#include "engines/playlist/playlist_executor.h"

#include <algorithm>
#include <cstdio>

namespace sm {
namespace playlist {

using json = nlohmann::json;

// ---- 枚举名 ----
const char* item_type_name(ItemType t) {
  switch (t) {
    case ItemType::media:            return "media";
    case ItemType::scene:           return "scene";
    case ItemType::delay:            return "delay";
    case ItemType::command:          return "command";
    case ItemType::timeline_segment: return "timeline_segment";
  }
  return "unknown";
}

ItemType item_type_from_string(const std::string& s) {
  if (s == "media") return ItemType::media;
  if (s == "scene") return ItemType::scene;
  if (s == "delay") return ItemType::delay;
  if (s == "command") return ItemType::command;
  if (s == "timeline_segment") return ItemType::timeline_segment;
  return ItemType::media;
}

const char* trigger_mode_name(TriggerMode t) {
  switch (t) {
    case TriggerMode::auto_:     return "auto";
    case TriggerMode::go:        return "go";
    case TriggerMode::delay:     return "delay";
    case TriggerMode::timecode:  return "timecode";
  }
  return "unknown";
}

const char* loop_mode_name(LoopMode m) {
  switch (m) {
    case LoopMode::none:    return "none";
    case LoopMode::all:      return "all";
    case LoopMode::current:  return "current";
  }
  return "unknown";
}

const char* exec_state_name(ExecState s) {
  switch (s) {
    case ExecState::idle:        return "idle";
    case ExecState::loaded:      return "loaded";
    case ExecState::running:     return "running";
    case ExecState::waiting_go:  return "waiting_go";
    case ExecState::paused:      return "paused";
    case ExecState::ended:       return "ended";
  }
  return "unknown";
}

// ---- PlaylistExecutor ----
PlaylistExecutor::PlaylistExecutor() = default;
PlaylistExecutor::~PlaylistExecutor() { stop(); }

void PlaylistExecutor::load(const std::vector<PlaylistItem>& items) {
  items_ = items;
  // 按 sort_index 排序
  std::sort(items_.begin(), items_.end(),
            [](const PlaylistItem& a, const PlaylistItem& b) {
              return a.sort_index < b.sort_index;
            });
  current_index_ = -1;
  state_ = ExecState::loaded;
  emit("evt.playlist.loaded", {{"item_count", items_.size()}});
}

void PlaylistExecutor::start() {
  if (state_.load() != ExecState::loaded) return;
  if (items_.empty()) {
    emit("evt.error", {{"code", 5002}, {"message", "playlist empty"}});
    return;
  }
  state_ = ExecState::running;
  current_index_ = 0;
  // §10.5.3：节目单演出与时间线播放互斥
  // （由调用方负责先发 transport.stop）
  execute_current(0);
}

void PlaylistExecutor::pause() {
  if (state_.load() == ExecState::running ||
      state_.load() == ExecState::waiting_go) {
    if (stop_media_) stop_media_();
    state_ = ExecState::paused;
  }
}

void PlaylistExecutor::resume() {
  if (state_.load() == ExecState::paused) {
    state_ = ExecState::running;
    // 重新执行当前条目
    if (current_index_ >= 0 && current_index_ < int(items_.size()))
      execute_current(0);
  }
}

void PlaylistExecutor::stop() {
  if (stop_media_) stop_media_();
  state_ = ExecState::ended;
  emit("evt.playlist.ended", {{"reason", "manual_stop"}});
}

void PlaylistExecutor::go() {
  if (state_.load() == ExecState::waiting_go) {
    state_ = ExecState::running;
    advance(0, "go");
  }
}

void PlaylistExecutor::next() {
  if (state_.load() == ExecState::running ||
      state_.load() == ExecState::waiting_go) {
    if (stop_media_) stop_media_();
    state_ = ExecState::running;
    advance(0, "next");
  }
}

void PlaylistExecutor::execute_current(int64_t now_ms) {
  if (current_index_ < 0 || current_index_ >= int(items_.size())) {
    state_ = ExecState::ended;
    emit("evt.playlist.ended", {{"reason", "end"}});
    return;
  }

  const auto& item = items_[current_index_];

  // timecode 在 P1 禁止
  if (item.trigger == TriggerMode::timecode) {
    emit("evt.error", {{"code", 5003}, {"message", "timecode not supported in P1"}});
    advance(now_ms, "skipped_timecode");
    return;
  }

  // timeline_segment P1 跳过
  if (item.type == ItemType::timeline_segment) {
    emit("evt.error", {{"code", 5003}, {"message", "timeline_segment not supported in P1"}});
    advance(now_ms, "skipped_segment");
    return;
  }

  // 按类型执行
  switch (item.type) {
    case ItemType::media:
      if (play_media_)
        play_media_(item.ref_uuid, item.meta);
      break;
    case ItemType::scene: {
      int fade = item.meta.value("fade_ms", 0);
      if (recall_scene_)
        recall_scene_(item.ref_uuid, fade);
      // scene 召回是瞬时的（M3 内部处理淡变），直接推进
      // 但如果 trigger=auto，需要等 evt.scene.recalled
      // P1 简化：recall 后立即可推进
      break;
    }
    case ItemType::delay: {
      // delay 条目：等待 delay_ms 后推进
      waiting_for_delay_ = true;
      delay_start_ms_ = now_ms;
      delay_target_ms_ = item.delay_ms > 0 ? item.delay_ms
                                            : item.meta.value("delay_ms", 0);
      break;
    }
    case ItemType::command: {
      std::string op = item.ref_uuid;
      json params = item.meta.value("params", json::object());
      if (send_command_ && !op.empty())
        send_command_(op, params);
      break;
    }
    default:
      break;
  }

  emit("evt.playlist.item_started", {
    {"index", current_index_},
    {"item_id", item.item_id},
    {"type", item_type_name(item.type)}
  });
}

bool PlaylistExecutor::is_current_done(int64_t now_ms) {
  if (current_index_ < 0 || current_index_ >= int(items_.size())) return true;
  const auto& item = items_[current_index_];

  switch (item.type) {
    case ItemType::media:
      // 由 is_media_done_ 回调判断
      if (is_media_done_) return is_media_done_();
      return false;
    case ItemType::scene:
      // scene 召回瞬时完成（P1 简化）
      return true;
    case ItemType::delay:
      if (waiting_for_delay_)
        return (now_ms - delay_start_ms_) >= delay_target_ms_;
      return true;
    case ItemType::command:
      // 瞬时完成
      return true;
    default:
      return true;
  }
}

void PlaylistExecutor::advance(int64_t now_ms, const std::string& reason) {
  emit("evt.playlist.item_ended", {
    {"index", current_index_},
    {"reason", reason}
  });

  // loop_mode=current：当前项结束后循环自身
  if (loop_mode_ == LoopMode::current) {
    execute_current(now_ms);
    return;
  }

  ++current_index_;

  // 越界 → 检查循环
  if (current_index_ >= int(items_.size())) {
    if (loop_mode_ == LoopMode::all) {
      current_index_ = 0;
      execute_current(now_ms);
    } else {
      state_ = ExecState::ended;
      emit("evt.playlist.ended", {{"reason", "end"}});
    }
    return;
  }

  // 检查下一项的 trigger_mode
  const auto& next_item = items_[current_index_];
  execute_current(now_ms);

  // trigger=go：内容执行完后进入 waiting_go
  // 但这里刚执行完 execute_current，需要等内容结束才进 waiting_go
  // （由 tick 在 is_current_done 后检查 trigger_mode）
}

void PlaylistExecutor::tick(int64_t now_ms) {
  if (state_.load() != ExecState::running) return;
  if (current_index_ < 0 || current_index_ >= int(items_.size())) return;

  const auto& item = items_[current_index_];

  // 检查当前条目是否结束
  if (!is_current_done(now_ms)) return;

  // 条目结束，按 trigger_mode 决定推进
  switch (item.trigger) {
    case TriggerMode::auto_:
      // 自然推进
      advance(now_ms, "auto");
      break;
    case TriggerMode::go:
      // 停在 waiting_go，等手动 go
      state_ = ExecState::waiting_go;
      emit("evt.playlist.waiting_go", {{"index", current_index_}});
      break;
    case TriggerMode::delay:
      // 内容结束后额外等待 delay_ms
      if (!waiting_for_delay_) {
        waiting_for_delay_ = true;
        delay_start_ms_ = now_ms;
        delay_target_ms_ = item.delay_ms;
      } else if ((now_ms - delay_start_ms_) >= delay_target_ms_) {
        waiting_for_delay_ = false;
        advance(now_ms, "delay_complete");
      }
      break;
    case TriggerMode::timecode:
      // P1 不支持，直接跳过
      advance(now_ms, "skipped_timecode");
      break;
  }
}

void PlaylistExecutor::emit(const std::string& name, const json& params) {
  if (event_cb_) event_cb_(name, params);
}

}  // namespace playlist
}  // namespace sm

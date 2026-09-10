// TimelineScheduler 实现（§7 + §10.6）
// 纯逻辑调度器：按 pos_ms 推进六轨条目状态机，2ms 粒度扫描。
#include "engines/timeline/timeline_scheduler.h"

#include <algorithm>
#include <cstdio>

#include "core/error_codes.h"

namespace sm {
namespace timeline {

// ---- 枚举名 ----
const char* track_type_name(TrackType t) {
  switch (t) {
    case TrackType::audio:   return "audio";
    case TrackType::video:   return "video";
    case TrackType::scene:   return "scene";
    case TrackType::command: return "command";
    case TrackType::vj:      return "vj";
    case TrackType::light:   return "light";
    case TrackType::pixel:   return "pixel";
    case TrackType::device:  return "device";
  }
  return "unknown";
}

TrackType track_type_from_string(const std::string& s, TrackType fallback) {
  if (s == "audio") return TrackType::audio;
  if (s == "video") return TrackType::video;
  if (s == "scene") return TrackType::scene;
  if (s == "command") return TrackType::command;
  if (s == "vj") return TrackType::vj;
  if (s == "light") return TrackType::light;
  if (s == "pixel") return TrackType::pixel;
  if (s == "device") return TrackType::device;
  return fallback;
}

const char* item_state_name(ItemState s) {
  switch (s) {
    case ItemState::scheduled: return "scheduled";
    case ItemState::preloaded: return "preloaded";
    case ItemState::active:    return "active";
    case ItemState::done:      return "done";
    case ItemState::cancelled: return "cancelled";
    case ItemState::aborted:   return "aborted";
  }
  return "unknown";
}

const char* play_state_name(PlayState s) {
  switch (s) {
    case PlayState::stopped: return "stopped";
    case PlayState::playing: return "playing";
    case PlayState::paused:  return "paused";
  }
  return "unknown";
}

RefType ref_type_from_string(const std::string& s) {
  if (s == "media") return RefType::media;
  if (s == "scene") return RefType::scene;
  if (s == "command") return RefType::command;
  if (s == "clip") return RefType::clip;
  if (s == "cue") return RefType::cue;
  if (s == "pixel_program") return RefType::pixel_program;
  if (s == "device_action") return RefType::device_action;
  return RefType::unknown;
}

std::string ref_type_to_string(RefType t) {
  switch (t) {
    case RefType::media: return "media";
    case RefType::scene: return "scene";
    case RefType::command: return "command";
    case RefType::clip: return "clip";
    case RefType::cue: return "cue";
    case RefType::pixel_program: return "pixel_program";
    case RefType::device_action: return "device_action";
    default: return "unknown";
  }
}

// ---- TimelineScheduler ----
TimelineScheduler::TimelineScheduler() {
  // 默认配置：4 条启用轨（P1）+ 4 条空壳轨（P2-4）
  configure_tracks({
    {TrackType::audio, "主音频轨"},
    {TrackType::video, "主视频轨"},
    {TrackType::scene, "场景指令轨"},
    {TrackType::command, "指令轨"},
    {TrackType::vj, "VJ 特效轨（Phase 2）"},
    {TrackType::light, "灯光轨（Phase 3）"},
    {TrackType::pixel, "像素灯带轨（Phase 3）"},
    {TrackType::device, "硬件中控轨（Phase 3）"},
  });
}

void TimelineScheduler::configure_tracks(
    const std::vector<std::pair<TrackType, std::string>>& tracks) {
  tracks_.clear();
  tracks_.reserve(tracks.size());
  for (size_t i = 0; i < tracks.size(); ++i) {
    Track t;
    t.index = int(i);
    t.type = tracks[i].first;
    t.name = tracks[i].second;
    tracks_.push_back(std::move(t));
  }
  recalc_total_duration();
}

// §7.4 / §5.3：timeline.track_configure 下发轨道配置（含 enabled）。
// 就地更新已有轨道；索引超出当前轨道数时按需扩展（P1 为固定 8 轨）。
int TimelineScheduler::configure_tracks(const std::vector<TrackConfig>& tracks) {
  // 配置整体替换：清空重建，保持 items 为空（工程载入时先 track_configure 再 item_insert）
  std::vector<Track> rebuilt;
  int max_index = -1;
  for (const auto& cfg : tracks) {
    if (cfg.index < 0) return ec::BAD_PARAM;  // 1002
    if (cfg.index > max_index) max_index = cfg.index;
  }
  rebuilt.resize(size_t(max_index + 1));
  for (size_t i = 0; i < rebuilt.size(); ++i) {
    rebuilt[i].index = int(i);
    rebuilt[i].type = TrackType::audio;
    rebuilt[i].name = "轨 " + std::to_string(i);
    rebuilt[i].enabled = false;
  }
  for (const auto& cfg : tracks) {
    Track& t = rebuilt[size_t(cfg.index)];
    t.index = cfg.index;
    t.type = cfg.type;
    t.name = cfg.name;
    t.enabled = cfg.enabled;
  }

  // 迁移既有条目（按 track_index 归位；越界条目从索引一并剔除，避免悬空）
  for (auto it = item_index_.begin(); it != item_index_.end();) {
    const ItemPtr& item = it->second;
    if (item->track_index < 0 || item->track_index >= int(rebuilt.size())) {
      it = item_index_.erase(it);
      continue;
    }
    rebuilt[size_t(item->track_index)].items.push_back(item);
    ++it;
  }
  for (auto& t : rebuilt) {
    std::sort(t.items.begin(), t.items.end(),
              [](const ItemPtr& a, const ItemPtr& b) {
                return a->start_ms < b->start_ms;
              });
  }
  tracks_ = std::move(rebuilt);
  recalc_total_duration();
  return ec::OK;
}

int64_t TimelineScheduler::calc_end_ms(const TimelineItem& item) const {
  if (item.loop <= 0) return item.start_ms + item.duration_ms;
  return item.start_ms + item.duration_ms * (item.loop + 1);
}

void TimelineScheduler::recalc_total_duration() {
  int64_t max_end = 0;
  for (const auto& tr : tracks_) {
    for (const auto& it : tr.items) {
      int64_t e = calc_end_ms(*it);
      if (e > max_end) max_end = e;
    }
  }
  total_duration_ms_ = max_end;
}

ItemPtr TimelineScheduler::find_item(const std::string& item_id) const {
  auto it = item_index_.find(item_id);
  if (it == item_index_.end()) return nullptr;
  return it->second;
}

std::vector<ItemPtr> TimelineScheduler::all_items() const {
  std::vector<ItemPtr> out;
  out.reserve(item_index_.size());
  for (const auto& [id, item] : item_index_) out.push_back(item);
  return out;
}

void TimelineScheduler::clear_items() {
  for (auto& tr : tracks_) tr.items.clear();
  item_index_.clear();
  total_duration_ms_ = 0;
  pos_ms_ = 0;
  state_ = PlayState::stopped;
}

int TimelineScheduler::insert_item(const TimelineItem& item) {
  if (item.track_index < 0 || item.track_index >= int(tracks_.size()))
    return ec::TRACK_MISSING;  // 3001

  auto& tr = tracks_[item.track_index];

  // §7.4 编辑一致性校验：同轨同起点冲突 → 3002（ITEM_CONFLICT）
  for (const auto& exist : tr.items) {
    if (exist->start_ms == item.start_ms) return ec::ITEM_CONFLICT;  // 3002
  }

  auto new_item = std::make_shared<TimelineItem>(item);
  new_item->end_ms = calc_end_ms(item);
  new_item->preload_at_ms =
      std::max(int64_t(0), item.start_ms - kPreloadAheadMs);

  // 按 start_ms 插入到正确位置
  auto it = std::lower_bound(
      tr.items.begin(), tr.items.end(), item.start_ms,
      [](const ItemPtr& a, int64_t ms) { return a->start_ms < ms; });
  tr.items.insert(it, new_item);
  item_index_[item.item_id] = new_item;

  recalc_total_duration();
  return ec::OK;
}

bool TimelineScheduler::remove_item(const std::string& item_id) {
  auto it = item_index_.find(item_id);
  if (it == item_index_.end()) return false;
  ItemPtr item = it->second;
  item_index_.erase(it);

  auto& tr = tracks_[item->track_index];
  auto vec_it =
      std::find(tr.items.begin(), tr.items.end(), item);
  if (vec_it != tr.items.end()) {
    // 播放中条目：标记 aborted，交给 tick 自然处理（不强行中断）
    if (item->state == ItemState::active || item->state == ItemState::preloaded)
      item->state = ItemState::aborted;
    tr.items.erase(vec_it);
  }

  recalc_total_duration();
  return true;
}

bool TimelineScheduler::update_item(
    const std::string& item_id,
    const std::function<void(TimelineItem&)>& updater) {
  auto it = item_index_.find(item_id);
  if (it == item_index_.end()) return false;
  updater(*it->second);
  // 重新计算派生字段
  it->second->end_ms = calc_end_ms(*it->second);
  it->second->preload_at_ms =
      std::max(int64_t(0), it->second->start_ms - kPreloadAheadMs);
  recalc_total_duration();
  return true;
}

std::vector<ItemPtr> TimelineScheduler::active_items() const {
  std::vector<ItemPtr> result;
  for (const auto& tr : tracks_) {
    for (const auto& it : tr.items)
      if (it->state == ItemState::active) result.push_back(it);
  }
  return result;
}

void TimelineScheduler::reset_all_items() {
  for (const auto& tr : tracks_) {
    for (const auto& it : tr.items) {
      it->state = ItemState::scheduled;
      it->active_at_ms = 0;
    }
  }
}

// ---- 播放控制 ----
void TimelineScheduler::play(int64_t start_from_ms) {
  reset_all_items();
  pos_ms_ = start_from_ms;
  state_ = PlayState::playing;
  // 场景轨不重放已越过条目（防闪变，§7.3 Seek 语义）
  for (auto& tr : tracks_) {
    if (tr.type == TrackType::scene) {
      for (auto& it : tr.items) {
        if (it->start_ms <= start_from_ms)
          it->state = ItemState::done;
      }
    }
  }
}

void TimelineScheduler::pause() {
  if (state_.load() == PlayState::playing) state_ = PlayState::paused;
}

void TimelineScheduler::resume() {
  if (state_.load() == PlayState::paused) state_ = PlayState::playing;
}

void TimelineScheduler::stop() {
  // 停止所有 active 条目
  for (const auto& tr : tracks_) {
    for (const auto& it : tr.items) {
      if (it->state == ItemState::active || it->state == ItemState::preloaded) {
        const bool was_active = (it->state == ItemState::active);
        // 媒体条目回调停止
        if ((tr.type == TrackType::audio || tr.type == TrackType::video) &&
            media_stop_) {
          media_stop_(it->item_id);
        }
        it->state = ItemState::scheduled;
        // §5.4：已起播的条目终止时以 evt.timeline.item_ended（reason=stop）广播
        if (was_active) emit_item_event("evt.timeline.item_ended", *it, "stop");
      }
    }
  }
  state_ = PlayState::stopped;
  pos_ms_ = 0;
}

int TimelineScheduler::seek(int64_t pos_ms) {
  // §5.5：播放头越界 → 3003（PLAYHEAD_OUT_OF_RANGE）
  if (pos_ms < 0) return ec::PLAYHEAD_OUT_OF_RANGE;
  if (total_duration_ms_ > 0 && pos_ms > total_duration_ms_)
    return ec::PLAYHEAD_OUT_OF_RANGE;

  // 停止所有 active 条目
  for (const auto& tr : tracks_) {
    for (const auto& it : tr.items) {
      if (it->state == ItemState::active || it->state == ItemState::preloaded) {
        if ((tr.type == TrackType::audio || tr.type == TrackType::video) &&
            media_stop_) {
          media_stop_(it->item_id);
        }
        it->state = ItemState::scheduled;
      }
    }
  }
  // 场景轨不重放已越过条目（防闪变，§7.3）
  for (auto& tr : tracks_) {
    if (tr.type == TrackType::scene) {
      for (auto& it : tr.items) {
        if (it->start_ms <= pos_ms)
          it->state = ItemState::done;  // 标记为已执行，不再触发
      }
    }
  }
  pos_ms_ = pos_ms;
  return ec::OK;
}

// ---- 核心 tick ----
void TimelineScheduler::tick(int64_t pos_ms) {
  if (state_.load() != PlayState::playing) return;
  pos_ms_ = pos_ms;

  for (auto& tr : tracks_) {
    // §7.4：被禁用的轨道不参与调度
    if (!tr.enabled) continue;
    for (auto& item : tr.items) {
      // scheduled → preloaded（预载阶段）
      if (item->state == ItemState::scheduled) {
        if (pos_ms >= item->preload_at_ms && pos_ms < item->start_ms) {
          if (tr.type == TrackType::audio || tr.type == TrackType::video) {
            if (media_preload_) {
              if (media_preload_(item->item_id, item->ref_uuid, item->meta))
                item->state = ItemState::preloaded;
            }
          } else {
            item->state = ItemState::preloaded;
          }
        }
      }
      // preloaded → active（到达 start_ms）
      if (item->state == ItemState::preloaded && pos_ms >= item->start_ms) {
        activate_item(item, pos_ms);
      }
      // scheduled 直接到点（跳过预载，如 seek 后）
      if (item->state == ItemState::scheduled && pos_ms >= item->start_ms) {
        activate_item(item, pos_ms);
      }

      // active 条目：检查是否结束
      if (item->state == ItemState::active) {
        int64_t end = calc_end_ms(*item);
        if (pos_ms >= end) {
          finish_item(item, "done", pos_ms);
        }
      }

      // aborted 条目：立即收尾
      if (item->state == ItemState::aborted) {
        if (tr.type == TrackType::audio || tr.type == TrackType::video) {
          if (media_stop_) media_stop_(item->item_id);
        }
        emit_item_event("evt.timeline.item_ended", *item, "aborted");
        item->state = ItemState::done;  // 转为终态
      }
    }
  }

  // 检查是否全部结束（所有条目都 done 且 pos_ms >= total_duration）
  if (pos_ms >= total_duration_ms_ && total_duration_ms_ > 0) {
    bool all_done = true;
    for (const auto& tr : tracks_) {
      if (!tr.enabled) continue;  // 禁用轨不计入结束判定
      for (const auto& it : tr.items) {
        if (it->state != ItemState::done && it->state != ItemState::cancelled) {
          all_done = false;
          break;
        }
      }
      if (!all_done) break;
    }
    if (all_done) {
      state_ = PlayState::stopped;
    }
  }
}

void TimelineScheduler::activate_item(const ItemPtr& item, int64_t now_ms) {
  item->state = ItemState::active;
  item->active_at_ms = now_ms;

  TrackType type = tracks_[item->track_index].type;
  switch (type) {
    case TrackType::audio:
    case TrackType::video:
      if (media_play_)
        media_play_(item->item_id, item->ref_uuid, item->meta);
      break;
    case TrackType::scene: {
      int fade_ms = item->meta.value("fade_ms", 500);
      std::string mode = item->meta.value("recall_mode", std::string("cut"));
      if (scene_recall_)
        scene_recall_(item->ref_uuid, fade_ms, mode);
      // scene 条目是瞬时触发，激活后立即标记 done（但 fade 过程由 M3 自行管理）
      item->state = ItemState::done;
      emit_item_event("evt.timeline.item_ended", *item, "done");
      return;  // 已转 done，不重复发 started
    }
    case TrackType::command: {
      // meta.command_op / meta.params 或 ref_uuid 作为 op
      std::string op = item->ref_uuid;
      if (item->meta.contains("op"))
        op = item->meta["op"].get<std::string>();
      nlohmann::json params = nlohmann::json::object();
      if (item->meta.contains("params"))
        params = item->meta["params"];
      if (command_ && !op.empty())
        command_(op, params);
      // command 条目瞬时触发
      item->state = ItemState::done;
      emit_item_event("evt.timeline.item_ended", *item, "done");
      return;
    }
    default:
      // vj/light/pixel/device：Phase 2-4 点亮，P1 静默跳过
      item->state = ItemState::done;
      emit_item_event("evt.timeline.item_ended", *item, "skipped_phase");
      return;
  }

  emit_item_event("evt.timeline.item_started", *item);
}

void TimelineScheduler::finish_item(const ItemPtr& item,
                                    const std::string& reason, int64_t /*now_ms*/) {
  item->state = ItemState::done;

  TrackType type = tracks_[item->track_index].type;
  if (type == TrackType::audio || type == TrackType::video) {
    if (media_stop_) media_stop_(item->item_id);
  }

  emit_item_event("evt.timeline.item_ended", *item, reason);
}

void TimelineScheduler::emit_item_event(const char* evt,
                                        const TimelineItem& item,
                                        const std::string& reason) {
  if (item_event_) {
    // 回调由上层封装为总线事件广播
    item_event_(evt, item, reason);
  }
}

}  // namespace timeline
}  // namespace sm

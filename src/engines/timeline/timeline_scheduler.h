// M5 TimelineEngine 时间线调度器（规格 §7 + §10.6）
// 职责：六轨全局调度，2ms 周期扫描，条目状态机管理，跨轨对齐 ≤ ±10ms。
// 调度器为纯逻辑内核，不依赖平台渲染/音频；播放控制通过回调（M2 媒体引擎）
// 与消息总线（scene/command 轨）下发。平台接入层把回调替换为实际引擎调用。
//
// 轨道类型（§7.1）：audio / video / scene / command（P1 启用）；
//                 vj / light / pixel / device（P2-4 点亮，P1 空壳占位）
//
// 条目状态机（§7.3）：scheduled -> preloaded -> active -> done
//                    异常路径：cancelled / aborted
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace timeline {

// ---- 轨道类型 ----
enum class TrackType {
  audio = 0,
  video = 1,
  scene = 2,
  command = 3,
  vj = 4,
  light = 5,
  pixel = 6,
  device = 7,
};

const char* track_type_name(TrackType t);

// ---- 条目状态 ----
enum class ItemState {
  scheduled,   // 待执行
  preloaded,   // 已预载（提前 20ms）
  active,      // 播放中
  done,        // 正常结束
  cancelled,   // 被取消（未进入 active 前删除）
  aborted,     // 播放中被打断
};

const char* item_state_name(ItemState s);

// ---- 条目引用类型 ----
enum class RefType {
  media,     // 音频/视频/图片素材
  scene,     // 场景快照
  command,   // 总线指令
  clip,      // 剪辑虚拟素材（Phase 2）
  cue,       // 灯光 Cue（Phase 3）
  pixel_program,  // 像素节目（Phase 3）
  device_action,  // 中控动作（Phase 3）
  unknown,
};

RefType ref_type_from_string(const std::string& s);
std::string ref_type_to_string(RefType t);

// ---- 条目（§7.2，与 timeline_item 表对应）----
struct TimelineItem {
  std::string item_id;
  int track_index = 0;       // 轨道索引（0-7 对应 TrackType）
  int64_t start_ms = 0;      // 轨道内绝对起点（毫秒）
  int64_t duration_ms = 0;   // 持续时间（毫秒）
  RefType ref_type = RefType::media;
  std::string ref_uuid;      // 素材ID/场景ID/指令op
  int loop = 0;              // 循环次数，0=不循环
  nlohmann::json meta = nlohmann::json::object();  // 按 ref_type 语义
  ItemState state = ItemState::scheduled;

  // 运行时字段（不入库）
  int64_t preload_at_ms = 0;  // 预调度时间点 = start_ms - 20ms
  int64_t active_at_ms = 0;   // 实际激活时间（调度器记录）
  int64_t end_ms = 0;         // 结束时间（计算值，考虑 loop 展开）
};

using ItemPtr = std::shared_ptr<TimelineItem>;

// ---- 轨道 ----
struct Track {
  int index = 0;
  TrackType type = TrackType::audio;
  std::string name;
  std::vector<ItemPtr> items;  // 按 start_ms 排序
};

// ---- 播放状态 ----
enum class PlayState { stopped, playing, paused };

const char* play_state_name(PlayState s);

// ---- 调度器回调（平台/引擎接入层实现）----
// 媒体预载：返回 true 表示接受预载（M2 开始打开文件）
using MediaPreloadFn =
    std::function<bool(const std::string& item_id, const std::string& media_id,
                       const nlohmann::json& meta)>;
// 媒体开始播放
using MediaPlayFn =
    std::function<void(const std::string& item_id, const std::string& media_id,
                       const nlohmann::json& meta)>;
// 媒体停止（打断）
using MediaStopFn = std::function<void(const std::string& item_id)>;
// 场景召回
using SceneRecallFn =
    std::function<void(const std::string& scene_id, int fade_ms,
                       const std::string& recall_mode)>;
// 总线指令发送（command 轨）
using CommandFn = std::function<void(const std::string& op,
                                     const nlohmann::json& params)>;
// 条目事件回调（item_started / item_ended，供总线事件广播）
using ItemEventFn =
    std::function<void(const std::string& evt_name, const TimelineItem& item,
                       const std::string& reason)>;

// ---- TimelineScheduler（§7.3 调度器核心）----
class TimelineScheduler {
 public:
  TimelineScheduler();

  // ---- 配置回调 ----
  void set_media_preload_cb(MediaPreloadFn cb) { media_preload_ = std::move(cb); }
  void set_media_play_cb(MediaPlayFn cb) { media_play_ = std::move(cb); }
  void set_media_stop_cb(MediaStopFn cb) { media_stop_ = std::move(cb); }
  void set_scene_recall_cb(SceneRecallFn cb) { scene_recall_ = std::move(cb); }
  void set_command_cb(CommandFn cb) { command_ = std::move(cb); }
  void set_item_event_cb(ItemEventFn cb) { item_event_ = std::move(cb); }

  // ---- 轨道配置 ----
  // 从工程载入轨道配置（track_index → type + name）；P1 默认 4 启用轨
  void configure_tracks(const std::vector<std::pair<TrackType, std::string>>& tracks);

  // ---- 条目管理（编辑 API）----
  // 插入条目；返回错误码（0=成功，3001=轨道不存在，3002=同轨同起点冲突可选）
  int insert_item(const TimelineItem& item);
  // 删除条目；正在播放的条目自然结束（aborted 语义）
  bool remove_item(const std::string& item_id);
  // 更新条目元信息（播放中条目允许改 meta，不影响当前播放到下一次起播生效）
  bool update_item(const std::string& item_id,
                   const std::function<void(TimelineItem&)>& updater);
  // 查询条目
  ItemPtr find_item(const std::string& item_id) const;

  // ---- 播放控制 ----
  void play(int64_t start_from_ms = 0);
  void pause();
  void resume();
  void stop();
  void seek(int64_t pos_ms);

  PlayState play_state() const { return state_.load(); }
  int64_t pos_ms() const { return pos_ms_.load(); }
  int64_t total_duration_ms() const { return total_duration_ms_; }

  // ---- 调度 tick（由宿主时钟线程调用，§7.3：2ms 周期）----
  // 传入当前 pos_ms（由音频时钟或单调时钟换算）
  void tick(int64_t pos_ms);

  // ---- 查询 ----
  size_t track_count() const { return tracks_.size(); }
  const Track& track(int idx) const { return tracks_.at(idx); }
  TrackType track_type(int idx) const { return tracks_.at(idx).type; }

  // 当前活跃条目列表（所有 active 状态的条目）
  std::vector<ItemPtr> active_items() const;

  // 预调度提前量（默认 20ms，§7.3）
  static constexpr int64_t kPreloadAheadMs = 20;

 private:
  std::vector<Track> tracks_;
  std::map<std::string, ItemPtr> item_index_;  // item_id -> 条目（所有轨道）
  int64_t total_duration_ms_ = 0;

  std::atomic<PlayState> state_{PlayState::stopped};
  std::atomic<int64_t> pos_ms_{0};

  // 回调
  MediaPreloadFn media_preload_;
  MediaPlayFn media_play_;
  MediaStopFn media_stop_;
  SceneRecallFn scene_recall_;
  CommandFn command_;
  ItemEventFn item_event_;

  // 计算条目结束时间（考虑 loop 展开）
  int64_t calc_end_ms(const TimelineItem& item) const;

  // 重新计算总时长
  void recalc_total_duration();

  // 触发条目事件（包装 item_event_ 回调）
  void emit_item_event(const char* evt, const TimelineItem& item,
                       const std::string& reason = "");

  // 激活一个条目（进入 active 状态）
  void activate_item(const ItemPtr& item, int64_t now_ms);

  // 结束一个条目（进入 done 状态）
  void finish_item(const ItemPtr& item, const std::string& reason,
                   int64_t now_ms);

  // 重置所有条目为 scheduled（seek/stop 时）
  void reset_all_items();
};

}  // namespace timeline
}  // namespace sm

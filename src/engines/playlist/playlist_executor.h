// M4 播放列表执行器（§10.5）
// 职责：把 playlist_item 按 sort_index 翻译为对 M2/M3 的动作，
// 按 trigger_mode 决定何时推进到下一项。实现"一键演出"。
//
// 状态机（§10.5.2）：
//   idle → loaded → running → (waiting_go | paused) → ended
//
// trigger_mode：
//   auto   — 上一项自然结束即推进
//   go     — 内容结束后停等待 playlist.go 手动推进
//   delay  — 内容结束后额外等 delay_ms
//   timecode — P1 禁止（跳过 + warn）
//
// loop_mode：none / all / current
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace playlist {

// ---- 播放列表条目类型 ----
enum class ItemType { media, scene, delay, command, timeline_segment };
const char* item_type_name(ItemType t);
ItemType item_type_from_string(const std::string& s);

// ---- 推进模式 ----
enum class TriggerMode { auto_, go, delay, timecode };
const char* trigger_mode_name(TriggerMode t);

// ---- 循环模式 ----
enum class LoopMode { none, all, current };
const char* loop_mode_name(LoopMode t);

// ---- 执行状态 ----
enum class ExecState { idle, loaded, running, waiting_go, paused, ended };
const char* exec_state_name(ExecState s);

// ---- 节目单条目 ----
struct PlaylistItem {
  std::string item_id;
  int sort_index = 0;
  ItemType type = ItemType::media;
  std::string ref_uuid;      // media_id / scene_id / command op
  TriggerMode trigger = TriggerMode::auto_;
  int64_t delay_ms = 0;      // trigger=delay 时的额外等待
  nlohmann::json meta = nlohmann::json::object();  // fade_ms / gain_db / params / note
  std::string note;          // 备注（command 条目可放 params JSON 文本）
};

// ---- 执行回调 ----
// 播放媒体
using PlayMediaFn = std::function<void(const std::string& media_id,
                                        const nlohmann::json& meta)>;
// 召回场景
using RecallSceneFn = std::function<void(const std::string& scene_id,
                                          int fade_ms)>;
// 发送总线指令
using SendCommandFn = std::function<void(const std::string& op,
                                          const nlohmann::json& params)>;
// 媒体播放结束回调（执行器调用注册判断条目是否结束）
using IsMediaDoneFn = std::function<bool()>;
// 停止当前媒体
using StopMediaFn = std::function<void()>;
// 事件通知
using EventFn = std::function<void(const std::string& evt_name,
                                    const nlohmann::json& params)>;

class PlaylistExecutor {
 public:
  PlaylistExecutor();
  ~PlaylistExecutor();

  // ---- 回调注入 ----
  void set_play_media_cb(PlayMediaFn cb) { play_media_ = std::move(cb); }
  void set_recall_scene_cb(RecallSceneFn cb) { recall_scene_ = std::move(cb); }
  void set_send_command_cb(SendCommandFn cb) { send_command_ = std::move(cb); }
  void set_is_media_done_cb(IsMediaDoneFn cb) { is_media_done_ = std::move(cb); }
  void set_stop_media_cb(StopMediaFn cb) { stop_media_ = std::move(cb); }
  void set_event_cb(EventFn cb) { event_cb_ = std::move(cb); }

  // ---- 节目单载入 ----
  // 从 items 数组生成执行序列（按 sort_index 排序）
  void load(const std::vector<PlaylistItem>& items);

  // ---- 播放控制 ----
  void start();
  void pause();
  void resume();
  void stop();

  // GO：推进到下一项（waiting_go → running 下一项）
  void go();

  // next：立即终止当前条目并跳到下一项
  void next();

  // ---- tick（宿主周期调用，100ms 间隔 §10.5.3）----
  void tick(int64_t now_ms);

  // ---- 状态查询 ----
  ExecState state() const { return state_.load(); }
  int current_index() const { return current_index_; }
  size_t item_count() const { return items_.size(); }
  const std::vector<PlaylistItem>& items() const { return items_; }

  // 设置循环模式
  void set_loop_mode(LoopMode mode) { loop_mode_ = mode; }
  LoopMode loop_mode() const { return loop_mode_; }

 private:
  std::vector<PlaylistItem> items_;
  std::atomic<ExecState> state_{ExecState::idle};
  int current_index_ = -1;
  LoopMode loop_mode_ = LoopMode::none;

  // delay 条目等待
  int64_t delay_start_ms_ = 0;
  int64_t delay_target_ms_ = 0;
  bool waiting_for_delay_ = false;

  // 回调
  PlayMediaFn play_media_;
  RecallSceneFn recall_scene_;
  SendCommandFn send_command_;
  IsMediaDoneFn is_media_done_;
  StopMediaFn stop_media_;
  EventFn event_cb_;

  // 执行当前条目
  void execute_current(int64_t now_ms);
  // 推进到下一项
  void advance(int64_t now_ms, const std::string& reason);
  // 检查当前条目是否结束
  bool is_current_done(int64_t now_ms);
  // 发事件（注意：不可命名为 emit —— Qt 的 <QtGlobal>/qobjectdefs.h 把 emit
  // 定义为空宏，凡先包含 Qt 头再包含本头（如 src/app/qt/kernel_host.cpp）的 TU
  // 都会把该声明展开成 `void (const std::string&, ...);`，MSVC 报 C2059
  // syntax error: 'const'。故统一命名为 emit_event。）
  void emit_event(const std::string& name, const nlohmann::json& params);
};

}  // namespace playlist
}  // namespace sm

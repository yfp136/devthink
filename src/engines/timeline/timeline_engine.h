// TimelineEngine 引擎实现（M5，engine.timeline）
// 作为消息总线的 engine.timeline 地址处理者，接收 timeline.* / transport.* 指令
// 并调度 TimelineScheduler。播放控制线程由宿主启动（2ms tick 周期）。
//
// 本文件是 Phase 1 的纯逻辑实现，不依赖平台音频/视频；实际媒体播放通过
// 回调接口委托给 M2 MediaEngine。
#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "core/msg_bus.h"
#include "engines/timeline/timeline_scheduler.h"

namespace sm {
namespace timeline {

class TimelineEngine {
 public:
  TimelineEngine(MsgBus& bus);
  ~TimelineEngine();

  // 启动调度线程（独立线程，2ms tick 周期，§7.3）
  void start();
  // 停止调度线程
  void stop();

  TimelineScheduler& scheduler() { return *scheduler_; }
  const TimelineScheduler& scheduler() const { return *scheduler_; }

  // 注册为总线 sink（dst = engine.timeline）
  void register_bus();

  // 媒体回调设置（由 M2 MediaEngine 注入）
  void set_media_preload_cb(MediaPreloadFn cb) { scheduler_->set_media_preload_cb(std::move(cb)); }
  void set_media_play_cb(MediaPlayFn cb) { scheduler_->set_media_play_cb(std::move(cb)); }
  void set_media_stop_cb(MediaStopFn cb) { scheduler_->set_media_stop_cb(std::move(cb)); }
  void set_scene_recall_cb(SceneRecallFn cb) { scheduler_->set_scene_recall_cb(std::move(cb)); }
  void set_command_cb(CommandFn cb) { scheduler_->set_command_cb(std::move(cb)); }

 private:
  void handle_command(const Envelope& env);
  void tick_thread();
  Envelope make_rsp(const Envelope& req, int code,
                    const nlohmann::json& params = nlohmann::json::object());

  MsgBus& bus_;
  std::unique_ptr<TimelineScheduler> scheduler_;

  std::thread tick_thread_;
  std::atomic<bool> running_{false};

  // 播放时钟：用单调时钟模拟（实际部署由 M2 音频时钟驱动）
  std::atomic<int64_t> play_start_ms_{0};
  std::atomic<int64_t> play_base_ms_{0};
  std::atomic<bool> is_playing_{false};
};

}  // namespace timeline
}  // namespace sm

// ShowMaster Kernel 集成模块
// 把 M1-M5 真实引擎实例化并互相关联，接入消息总线。
// TimelineEngine ← MediaEngine / SceneStore / PlaylistExecutor / MediaLibrary
// 所有引擎通过 MsgBus 通信；Kernel 负责生命周期和回调注入。
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/msg_bus.h"
#include "engines/media/media_engine.h"
#include "engines/media_lib/media_library.h"
#include "engines/playlist/playlist_executor.h"
#include "engines/scene/scene_store.h"
#include "engines/timeline/timeline_engine.h"
#include "nlohmann/json.hpp"

namespace sm {

class HeartbeatMonitor;

class Kernel {
 public:
  Kernel();
  ~Kernel();

  // 初始化：创建所有引擎、注入回调、注册总线 sink
  void init();

  // 启动：启动调度线程
  void start();

  // 停止：停止所有线程
  void stop();

  // ---- 引擎访问 ----
  MsgBus& bus() { return bus_; }
  timeline::TimelineEngine& timeline() { return *timeline_; }
  media::MediaEngine& media() { return *media_; }
  scene::SceneStore& scene() { return *scene_; }
  playlist::PlaylistExecutor& playlist() { return *playlist_; }
  media_lib::MediaLibrary& media_lib() { return *media_lib_; }
  HeartbeatMonitor& heartbeat() { return *hb_; }

  // ---- 状态查询（供 WebGateway 调用）----
  std::string get_status_json() const;
  std::string get_playlist_json() const;

  // ---- §5.4 引擎心跳事件 ----------------------------------------------
  // HeartbeatMonitor 只做在线判定、自己不发事件。宿主在每次心跳 tick（2s）
  // 调用本方法：先按注册表记心跳并推进判定，再 diff 上次在线态，生成
  //   evt.engine.up / evt.engine.down  —— 仅在线态发生边沿变化时发
  //   evt.engine.heartbeat {engine_id, load_pct, mem_mb} —— 每个在线引擎每次发
  // 两个宿主 tick 点（headless 主循环 / Qt KernelWorker）共用此入口，
  // 避免 up/down 判定逻辑重复实现。
  void pump_heartbeat(std::int64_t now_ms);

  // evt.engine.heartbeat 的采样源注入。load_pct 为 0..100 百分比，
  // mem_mb 为进程驻留集（headless 用 remote::Watchdog::get_rss_mb）。
  // 未注入时两者按 0 上报：Phase 1 无逐引擎 CPU 采样，字段保留以稳定事件契约。
  void set_engine_metrics(std::function<double()> load_pct,
                          std::function<std::size_t()> mem_mb);

  // ---- §5.4 evt.log {level, msg} --------------------------------------
  // 统一日志事件入口（广播）。level 取 debug / info / warn / error；
  // 用于把宿主侧诊断信息并入总线，使遥控面 [1016] 能同源观测。
  void log_event(const std::string& level, const std::string& msg);

  // ---- 快捷操作（供 WebGateway 直接调用）----
  // 场景切换
  bool scene_go(const std::string& scene_id, int fade_ms = -1);
  // 播放控制
  bool transport_play(const std::string& media_id = "");
  bool transport_stop();
  bool transport_pause();
  bool transport_resume();
  // 节目单 GO
  bool playlist_go();
  bool playlist_start();
  bool playlist_stop();
  bool playlist_next();

 private:
  MsgBus bus_;
  std::unique_ptr<HeartbeatMonitor> hb_;
  std::unique_ptr<timeline::TimelineEngine> timeline_;
  std::unique_ptr<media::MediaEngine> media_;
  std::unique_ptr<scene::SceneStore> scene_;
  std::unique_ptr<playlist::PlaylistExecutor> playlist_;
  std::unique_ptr<media_lib::MediaLibrary> media_lib_;

  // §5.3 sys.set_volume 记录的通道增益（dB）：master / music / mic。
  // 平台里程碑落地 WASAPI 音量原语后由 M2 消费该值。
  std::map<std::string, double> channel_gain_db_;

  // evt.engine.heartbeat 采样源（见 set_engine_metrics）；空 = 按 0 上报。
  std::function<double()> load_pct_fn_;
  std::function<std::size_t()> mem_mb_fn_;

  // 上次心跳判定出的在线态：evt.engine.up/down 只在边沿翻转时发。
  std::map<std::string, bool> engine_online_;

  // 注册总线 sink（各引擎响应各自命名空间的指令）
  void register_bus_sinks();

  // 注入引擎间回调
  void wire_callbacks();

  // 收集「引用了某对象」的全部条目 id —— 供引用保护判定（§10.1.4 / §10.3，
  // 被引用时删除须回 5003）。同时覆盖时间线条目与节目单条目。
  std::vector<std::string> collect_referencing_ids(
      const std::string& ref_uuid) const;
};

}  // namespace sm

// ShowMaster Kernel 集成模块
// 把 M1-M5 真实引擎实例化并互相关联，接入消息总线。
// TimelineEngine ← MediaEngine / SceneStore / PlaylistExecutor / MediaLibrary
// 所有引擎通过 MsgBus 通信；Kernel 负责生命周期和回调注入。
#pragma once

#include <memory>
#include <string>

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

  // 注册总线 sink（各引擎响应各自命名空间的指令）
  void register_bus_sinks();

  // 注入引擎间回调
  void wire_callbacks();
};

}  // namespace sm

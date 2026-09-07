// WebGateway：REST API → 消息总线桥接 + WebSocket 事件/预览推送
// 把浏览器的 HTTP 请求翻译为内核信封，push 进 RemoteQueue（解耦实时引擎）。
// REST 端点：
//   POST /api/login           登录 → {token, role}
//   POST /api/logout          登出
//   POST /api/cmd             通用指令 {op, params} → 入队
//   GET  /api/status          引擎健康/播放状态
//   GET  /api/engines         引擎注册表
//   GET  /api/playlist        节目单
//   POST /api/scene/go        场景切换快捷 {scene_id}
//   POST /api/transport/play  播放快捷 {media_id}
//   POST /api/transport/stop  停止
//   POST /api/password        改密码
//   GET  /                    内嵌 Web UI
// WebSocket：
//   /ws/events                实时事件推送（引擎心跳/错误/播放状态变更）
//   /ws/preview               PGM 画面定帧推送（JPEG）
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "web/auth.h"
#include "web/http_server.h"
#include "web/remote_queue.h"

namespace sm {

class HeartbeatMonitor;

namespace web {

class WebGateway {
 public:
  WebGateway(Auth& auth, RemoteQueue& queue, HeartbeatMonitor& hb);

  // 注册全部路由到 HttpServer
  void register_routes(HttpServer& server);

  // 引擎事件回调：当 MsgBus 有事件时调用此方法，
  // 通过 ws_broadcast("/ws/events") 推给所有 WebSocket 客户端
  void on_engine_event(const std::string& event_json);

  // PGM 预览帧回调：引擎渲染一帧后调此方法推送 JPEG（base64）
  // fps 由引擎决定（建议 5-15fps），不影响本地渲染精度
  void on_pgm_frame(const std::string& jpeg_b64);

  // 设置引擎状态查询回调（用于 GET /api/status）
  void set_status_provider(std::function<std::string()> cb) {
    status_provider_ = std::move(cb);
  }

  // 设置节目单查询回调
  void set_playlist_provider(std::function<std::string()> cb) {
    playlist_provider_ = std::move(cb);
  }

 private:
  Auth& auth_;
  RemoteQueue& queue_;
  HeartbeatMonitor& hb_;
  HttpServer* server_ = nullptr;
  std::function<std::string()> status_provider_;
  std::function<std::string()> playlist_provider_;

  // 鉴权辅助：从请求中取 token 并校验
  bool check_auth(const HttpRequest& req, WebSession& session);

  // JSON 响应辅助
  static HttpResponse json_ok(const std::string& body);
  static HttpResponse json_err(int status, const std::string& msg);
};

}  // namespace web
}  // namespace sm

// HTTP/WebSocket Server（Web 远控核心）
// 提供 REST API（GET/POST 路由）+ WebSocket 实时通道。
// 线程模型：主线程 accept 循环；每个连接一个工作线程。
// WebSocket：RFC 6455 基本帧（text/binary/close/ping/pong）。
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "web/socket.h"

namespace sm {
namespace web {

struct HttpRequest {
  std::string method;        // GET / POST
  std::string path;          // 含 query string
  std::string path_only;     // 去掉 query
  std::string query;         // ?后面的部分（不含?）
  std::map<std::string, std::string> headers;  // 小写 key
  std::string body;
  std::string peer_ip;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
  // 额外响应头（如 Set-Cookie / WWW-Authenticate）
  std::vector<std::pair<std::string, std::string>> extra_headers;
  // CORS：自动加 Access-Control-Allow-Origin: *
  bool cors = true;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
 public:
  // 注册路由（精确匹配 path_only）
  void route(const std::string& method, const std::string& path, HttpHandler h);
  void get(const std::string& path, HttpHandler h);
  void post(const std::string& path, HttpHandler h);

  // WebSocket：指定路径升级后，on_msg 在收到 WS text 消息时回调。
  // 服务端可通过 ws_broadcast() 向该路径所有连接推送消息。
  void ws(const std::string& path,
          std::function<void(const std::string& msg)> on_msg);

  // 向某路径的所有 WebSocket 连接广播文本消息（线程安全）。
  void ws_broadcast(const std::string& path, const std::string& msg);

  // 启动监听（阻塞）。stop() 从另一线程调用可使其退出。
  bool start(int port);

  // 停止服务器。
  void stop();

  bool is_running() const { return running_.load(); }
  int port() const { return port_; }

 private:
  void accept_loop();
  void handle_connection(socket_t client, const std::string& peer_ip);
  bool parse_http_request(socket_t s, HttpRequest& req);
  HttpResponse dispatch(const HttpRequest& req);
  static std::string status_text(int code);
  static void send_response(socket_t s, const HttpResponse& resp);
  bool handle_ws_handshake(socket_t s, const HttpRequest& req,
                           const std::string& path);
  void ws_session(socket_t s, const std::string& path,
                  std::function<void(const std::string&)> on_msg);

  // WebSocket 发送帧（server→client，不掩码）
  static bool ws_send_frame(socket_t s, uint8_t opcode, const char* data,
                            size_t len);
  // WebSocket 读取一帧
  static bool ws_recv_frame(socket_t s, uint8_t& opcode, std::string& payload);

  struct Route {
    std::string method;
    std::string path;
    HttpHandler handler;
  };
  std::vector<Route> routes_;

  struct WsRoute {
    std::string path;
    std::function<void(const std::string&)> on_msg;
  };
  std::vector<WsRoute> ws_routes_;

  struct WsConn {
    socket_t sock;
    std::string path;
  };
  std::mutex ws_mutex_;
  std::vector<WsConn> ws_conns_;

  socket_t listen_sock_ = kInvalidSocket;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  int port_ = 0;
};

}  // namespace web
}  // namespace sm

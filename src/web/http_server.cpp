#include "web/http_server.h"
#include "web/ws_crypto.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace sm {
namespace web {

// ---- 公共路由注册 ----
void HttpServer::route(const std::string& method, const std::string& path,
                       HttpHandler h) {
  routes_.push_back({method, path, std::move(h)});
}
void HttpServer::get(const std::string& path, HttpHandler h) { route("GET", path, std::move(h)); }
void HttpServer::post(const std::string& path, HttpHandler h) { route("POST", path, std::move(h)); }

void HttpServer::ws(const std::string& path,
                    std::function<void(const std::string&)> on_msg) {
  ws_routes_.push_back({path, std::move(on_msg)});
}

void HttpServer::ws_broadcast(const std::string& path, const std::string& msg) {
  std::lock_guard<std::mutex> lk(ws_mutex_);
  for (auto& c : ws_conns_) {
    if (c.path == path)
      ws_send_frame(c.sock, 0x01, msg.data(), msg.size());
  }
}

// ---- 启动/停止 ----
bool HttpServer::start(int port) {
  port_ = port;
  listen_sock_ = listen_on(port);
  if (listen_sock_ == kInvalidSocket) return false;
  running_ = true;
  accept_thread_ = std::thread([this] { accept_loop(); });
  return true;
}

void HttpServer::stop() {
  running_ = false;
  if (listen_sock_ != kInvalidSocket) {
    close_socket(listen_sock_);
    listen_sock_ = kInvalidSocket;
  }
  if (accept_thread_.joinable()) accept_thread_.join();
}

void HttpServer::accept_loop() {
  while (running_.load()) {
    std::string peer;
    socket_t client = accept_client(listen_sock_, &peer);
    if (client == kInvalidSocket) {
      if (!running_.load()) break;
      continue;
    }
    // detach：每个连接独立线程处理
    std::thread([this, client, peer] {
      handle_connection(client, peer);
    }).detach();
  }
}

// ---- HTTP 请求解析 ----
namespace {

std::string to_lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::string url_decode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int hi = std::tolower(s[i+1]);
      int lo = std::tolower(s[i+2]);
      auto hex = [](char c) { return c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10); };
      out += char(hex(hi) * 16 + hex(lo));
      i += 2;
    } else if (s[i] == '+') {
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}

std::map<std::string, std::string> parse_query(const std::string& q) {
  std::map<std::string, std::string> out;
  std::string pair;
  std::istringstream ss(q);
  while (std::getline(ss, pair, '&')) {
    auto pos = pair.find('=');
    if (pos != std::string::npos)
      out[url_decode(pair.substr(0, pos))] = url_decode(pair.substr(pos + 1));
    else
      out[url_decode(pair)] = "";
  }
  return out;
}

}  // namespace

bool HttpServer::parse_http_request(socket_t s, HttpRequest& req) {
  // 读取直到 \r\n\r\n（请求头结束）
  std::string raw;
  char buf[4096];
  while (true) {
    size_t end = raw.find("\r\n\r\n");
    if (end != std::string::npos) break;
    int n = recv_some(s, buf, sizeof(buf));
    if (n <= 0) return false;
    raw.append(buf, n);
    if (raw.size() > 65536) return false;  // 头过大保护
  }

  size_t header_end = raw.find("\r\n\r\n");
  std::string head = raw.substr(0, header_end);
  std::string extra = raw.substr(header_end + 4);

  std::istringstream hs(head);
  std::string line;
  if (!std::getline(hs, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();

  // 请求行：METHOD PATH HTTP/1.1
  std::istringstream rl(line);
  rl >> req.method >> req.path;
  if (req.method.empty() || req.path.empty()) return false;

  // 拆 path / query
  auto qpos = req.path.find('?');
  if (qpos != std::string::npos) {
    req.path_only = req.path.substr(0, qpos);
    req.query = req.path.substr(qpos + 1);
  } else {
    req.path_only = req.path;
  }

  // 解析 header
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    std::string key = to_lower(line.substr(0, pos));
    std::string val = line.substr(pos + 1);
    while (!val.empty() && val[0] == ' ') val.erase(0, 1);
    req.headers[key] = val;
  }

  // 读取 body
  auto it = req.headers.find("content-length");
  if (it != req.headers.end()) {
    size_t clen = std::stoul(it->second);
    req.body = extra;
    while (req.body.size() < clen) {
      int n = recv_some(s, buf, int(clen - req.body.size()));
      if (n <= 0) return false;
      req.body.append(buf, n);
    }
    if (req.body.size() > clen) req.body.resize(clen);
  }
  return true;
}

// ---- 路由分发 ----
HttpResponse HttpServer::dispatch(const HttpRequest& req) {
  // CORS preflight
  if (req.method == "OPTIONS") {
    HttpResponse r;
    r.body = "";
    return r;
  }

  // 检查 WebSocket 升级请求
  auto ws_it = req.headers.find("upgrade");
  if (ws_it != req.headers.end() && to_lower(ws_it->second) == "websocket")
    return {};  // 由 handle_ws_handshake 处理

  // 精确路由匹配
  for (const auto& r : routes_) {
    if (r.method == req.method && r.path == req.path_only)
      return r.handler(req);
  }

  // 前缀匹配（用于静态文件），但 /api/ 路径不参与前缀回退
  if (req.path_only.compare(0, 5, "/api/") != 0) {
    for (const auto& r : routes_) {
      if (r.method == req.method &&
          r.path.back() == '/' &&
          req.path_only.compare(0, r.path.size(), r.path) == 0)
        return r.handler(req);
    }
  }

  HttpResponse r;
  r.status = 404;
  r.body = R"({"error":"not_found"})";
  return r;
}

// ---- HTTP 响应发送 ----
std::string HttpServer::status_text(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default:  return "OK";
  }
}

void HttpServer::send_response(socket_t s, const HttpResponse& resp) {
  std::ostringstream out;
  out << "HTTP/1.1 " << resp.status << " " << status_text(resp.status) << "\r\n";
  out << "Content-Type: " << resp.content_type << "\r\n";
  out << "Content-Length: " << resp.body.size() << "\r\n";
  if (resp.cors) {
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    out << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
  }
  for (const auto& h : resp.extra_headers)
    out << h.first << ": " << h.second << "\r\n";
  out << "\r\n" << resp.body;
  std::string sstr = out.str();
  send_all(s, sstr.data(), int(sstr.size()));
}

// ---- 连接处理主流程 ----
void HttpServer::handle_connection(socket_t client, const std::string& peer_ip) {
  HttpRequest req;
  req.peer_ip = peer_ip;
  if (!parse_http_request(client, req)) {
    close_socket(client);
    return;
  }

  // WebSocket 升级请求？
  auto ws_it = req.headers.find("upgrade");
  if (ws_it != req.headers.end() && to_lower(ws_it->second) == "websocket") {
    handle_ws_handshake(client, req, req.path_only);
    return;  // ws_handshake 会接管该 socket
  }

  // 普通 HTTP
  HttpResponse resp = dispatch(req);
  send_response(client, resp);
  close_socket(client);
}

// ---- WebSocket 握手 ----
bool HttpServer::handle_ws_handshake(socket_t s, const HttpRequest& req,
                                     const std::string& path) {
  // 查找匹配的 WS 路由
  std::function<void(const std::string&)> on_msg;
  bool found = false;
  for (const auto& wr : ws_routes_) {
    if (wr.path == path) {
      on_msg = wr.on_msg;
      found = true;
      break;
    }
  }
  if (!found) {
    HttpResponse r;
    r.status = 404;
    r.body = R"({"error":"ws_not_found"})";
    send_response(s, r);
    close_socket(s);
    return false;
  }

  auto key_it = req.headers.find("sec-websocket-key");
  if (key_it == req.headers.end()) {
    close_socket(s);
    return false;
  }

  std::string accept = ws_accept_key(key_it->second);

  std::ostringstream out;
  out << "HTTP/1.1 101 Switching Protocols\r\n";
  out << "Upgrade: websocket\r\n";
  out << "Connection: Upgrade\r\n";
  out << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
  std::string hs = out.str();
  if (send_all(s, hs.data(), int(hs.size())) != int(hs.size())) {
    close_socket(s);
    return false;
  }

  // 注册连接
  {
    std::lock_guard<std::mutex> lk(ws_mutex_);
    ws_conns_.push_back({s, path});
  }

  // 进入 WS 会话循环
  ws_session(s, path, on_msg);
  return true;
}

// ---- WebSocket 帧收发 ----
bool HttpServer::ws_send_frame(socket_t s, uint8_t opcode, const char* data,
                                size_t len) {
  uint8_t hdr[10];
  hdr[0] = 0x80 | opcode;  // FIN=1 + opcode
  int hdr_len;
  if (len < 126) {
    hdr[1] = uint8_t(len);
    hdr_len = 2;
  } else if (len < 65536) {
    hdr[1] = 126;
    hdr[2] = uint8_t(len >> 8);
    hdr[3] = uint8_t(len);
    hdr_len = 4;
  } else {
    hdr[1] = 127;
    uint64_t l = len;
    for (int i = 0; i < 8; ++i)
      hdr[2 + i] = uint8_t(l >> (56 - i * 8));
    hdr_len = 10;
  }
  if (send_all(s, reinterpret_cast<char*>(hdr), hdr_len) != hdr_len) return false;
  if (len > 0 && send_all(s, data, int(len)) != int(len)) return false;
  return true;
}

bool HttpServer::ws_recv_frame(socket_t s, uint8_t& opcode, std::string& payload) {
  uint8_t hdr[2];
  int n = recv_some(s, reinterpret_cast<char*>(hdr), 2);
  if (n < 2) return false;

  bool fin = hdr[0] & 0x80;
  opcode = hdr[0] & 0x0F;
  bool masked = hdr[1] & 0x80;
  uint64_t len = hdr[1] & 0x7F;

  if (len == 126) {
    uint8_t ext[2];
    if (recv_some(s, reinterpret_cast<char*>(ext), 2) < 2) return false;
    len = (uint64_t(ext[0]) << 8) | ext[1];
  } else if (len == 127) {
    uint8_t ext[8];
    if (recv_some(s, reinterpret_cast<char*>(ext), 8) < 8) return false;
    len = 0;
    for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
  }

  uint8_t mask[4] = {0};
  if (masked) {
    if (recv_some(s, reinterpret_cast<char*>(mask), 4) < 4) return false;
  }

  if (len > 16 * 1024 * 1024) return false;  // 16MB 保护

  payload.resize(len);
  if (len > 0) {
    size_t got = 0;
    char buf[8192];
    while (got < len) {
      int chunk = int(std::min(len - got, uint64_t(sizeof(buf))));
      int r = recv_some(s, buf, chunk);
      if (r <= 0) return false;
      if (masked)
        for (int i = 0; i < r; ++i)
          buf[i] ^= mask[(got + i) % 4];
      payload.replace(got, r, buf, r);
      got += r;
    }
  }
  return true;
}

void HttpServer::ws_session(socket_t s, const std::string& path,
                            std::function<void(const std::string&)> on_msg) {
  uint8_t opcode;
  std::string payload;
  while (running_.load()) {
    if (!ws_recv_frame(s, opcode, payload)) break;
    if (opcode == 0x8) {  // close
      break;
    } else if (opcode == 0x9) {  // ping → pong
      ws_send_frame(s, 0xA, payload.data(), payload.size());
    } else if (opcode == 0x1) {  // text
      if (on_msg) on_msg(payload);
    }
    // binary (0x2) 暂忽略（PGM 预览下行不需要客户端发二进制）
  }

  // 从连接列表移除
  {
    std::lock_guard<std::mutex> lk(ws_mutex_);
    ws_conns_.erase(
        std::remove_if(ws_conns_.begin(), ws_conns_.end(),
                       [&](const WsConn& c) { return c.sock == s; }),
        ws_conns_.end());
  }
  close_socket(s);
}

}  // namespace web
}  // namespace sm

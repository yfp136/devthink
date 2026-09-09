// TCP 协议适配器实现
#include "protocols/tcp_adapter.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sm {
namespace proto {

TcpAdapter::TcpAdapter() = default;

TcpAdapter::~TcpAdapter() { stop(); }

bool TcpAdapter::start() {
  listen_sock_ = sm::web::listen_on(listen_port_);
  if (listen_sock_ == sm::web::kInvalidSocket) {
    std::fprintf(stderr, "[tcp] 无法绑定端口 %d\n", listen_port_);
    return false;
  }
  running_ = true;
  accept_thread_ = std::thread(&TcpAdapter::accept_loop, this);
  std::printf("[tcp] 监听 0.0.0.0:%d（等待外部设备连接）\n", listen_port_);
  return true;
}

void TcpAdapter::stop() {
  running_ = false;
  if (listen_sock_ != sm::web::kInvalidSocket) {
    sm::web::close_socket(listen_sock_);
    listen_sock_ = sm::web::kInvalidSocket;
  }
  if (accept_thread_.joinable())
    accept_thread_.join();

  // 关闭所有客户端连接
  {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (auto& [id, c] : clients_) {
      sm::web::close_socket(c.sock);
      if (c.thread.joinable())
        c.thread.detach();
    }
    clients_.clear();
  }
}

void TcpAdapter::accept_loop() {
  while (running_) {
    std::string peer_ip;
    sm::web::socket_t client = sm::web::accept_client(listen_sock_, &peer_ip);
    if (client == sm::web::kInvalidSocket) {
      if (running_)
        std::fprintf(stderr, "[tcp] accept 失败\n");
      break;
    }

    int id = next_client_id_++;
    {
      std::lock_guard<std::mutex> lk(clients_mutex_);
      clients_[id] = {client, peer_ip,
                       std::thread(&TcpAdapter::client_loop, this,
                                   client, id, peer_ip)};
    }
    std::printf("[tcp] 客户端 #%d 连接: %s\n", id, peer_ip.c_str());
  }
}

void TcpAdapter::client_loop(sm::web::socket_t sock, int client_id,
                              std::string peer_ip) {
  char buf[4096];
  std::string accumulated;

  while (running_) {
    int n = sm::web::recv_some(sock, buf, sizeof(buf));
    if (n <= 0) break;  // 连接关闭或错误

    accumulated.append(buf, n);

    // 按换行分隔解析消息
    size_t pos;
    while ((pos = accumulated.find('\n')) != std::string::npos) {
      std::string line = accumulated.substr(0, pos);
      accumulated.erase(0, pos + 1);

      // 去除 \r
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      if (line.empty()) continue;

      // 构造 ProtocolMessage
      ProtocolMessage msg;
      msg.type = ProtocolType::TCP;
      msg.source = peer_ip + ":" + std::to_string(client_id);
      msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();

      // 尝试解析为 JSON：address=cmd, payload=完整JSON
      // 非 JSON：address=line, payload=line
      if (line.front() == '{') {
        // JSON 指令：期望 {"op":"go","params":{...}}
        msg.address = "json";
        msg.payload = line;
      } else {
        // 文本指令：第一个 token 为 address
        size_t sp = line.find(' ');
        if (sp != std::string::npos) {
          msg.address = line.substr(0, sp);
          msg.payload = line.substr(sp + 1);
        } else {
          msg.address = line;
          msg.payload = "";
        }
      }

      if (callback_)
        callback_(msg);
    }
  }

  // 清理
  {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    clients_.erase(client_id);
  }
  sm::web::close_socket(sock);
  std::printf("[tcp] 客户端 #%d 断开\n", client_id);
}

bool TcpAdapter::send(const std::string& address,
                      const std::string& payload) {
  // address = "-1" 或 "all" → 广播
  // address = 数字 → 指定 client_id
  std::string msg = payload;
  if (msg.empty() || msg.back() != '\n')
    msg += '\n';

  if (address == "-1" || address == "all") {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (auto& [id, c] : clients_)
      sm::web::send_all(c.sock, msg.data(), int(msg.size()));
    return true;
  }

  // 指定 client_id
  try {
    int id = std::stoi(address);
    std::lock_guard<std::mutex> lk(clients_mutex_);
    auto it = clients_.find(id);
    if (it != clients_.end())
      return sm::web::send_all(it->second.sock, msg.data(),
                                int(msg.size())) > 0;
  } catch (...) {}
  return false;
}

bool TcpAdapter::send_to(const std::string& ip, int port,
                          const std::string& message) {
  // 主动连接目标 IP:port 并发送
#ifdef _WIN32
  sm::web::socket_t sock = static_cast<sm::web::socket_t>(
      socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
#else
  int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
  if (sock == sm::web::kInvalidSocket) return false;

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

  if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    sm::web::close_socket(sock);
    return false;
  }

  std::string msg = message;
  if (msg.empty() || msg.back() != '\n')
    msg += '\n';

  int sent = sm::web::send_all(sock, msg.data(), int(msg.size()));
  sm::web::close_socket(sock);
  return sent > 0;
}

int TcpAdapter::connection_count() {
  std::lock_guard<std::mutex> lk(clients_mutex_);
  return static_cast<int>(clients_.size());
}

} // namespace proto
} // namespace sm

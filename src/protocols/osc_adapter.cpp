// OSC 协议适配器实现
#include "protocols/osc_adapter.h"

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

OscAdapter::OscAdapter() = default;
OscAdapter::~OscAdapter() { stop(); }

bool OscAdapter::start() {
  // 创建 UDP socket
#ifdef _WIN32
  sock_ = static_cast<sm::web::socket_t>(
      socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
#else
  sock_ = socket(AF_INET, SOCK_DGRAM, 0);
#endif
  if (sock_ == sm::web::kInvalidSocket) {
    std::fprintf(stderr, "[osc] 无法创建 UDP socket\n");
    return false;
  }

  // 设置 SO_REUSEADDR
  sm::web::set_reuse_addr(sock_);

  // 绑定
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(listen_port_));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::fprintf(stderr, "[osc] 无法绑定 UDP 端口 %d\n", listen_port_);
    sm::web::close_socket(sock_);
    sock_ = sm::web::kInvalidSocket;
    return false;
  }

  running_ = true;
  recv_thread_ = std::thread(&OscAdapter::recv_loop, this);
  std::printf("[osc] 监听 UDP 0.0.0.0:%d\n", listen_port_);
  return true;
}

void OscAdapter::stop() {
  running_ = false;
  if (sock_ != sm::web::kInvalidSocket) {
    sm::web::close_socket(sock_);
    sock_ = sm::web::kInvalidSocket;
  }
  if (recv_thread_.joinable())
    recv_thread_.join();
}

void OscAdapter::recv_loop() {
  uint8_t buf[65536];

  while (running_) {
    sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);

#ifdef _WIN32
    int n = recvfrom(static_cast<SOCKET>(sock_),
                     reinterpret_cast<char*>(buf), sizeof(buf), 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLen);
#else
    ssize_t n = recvfrom(sock_, buf, sizeof(buf), 0,
                          reinterpret_cast<sockaddr*>(&from), &fromLen);
#endif

    if (n <= 0) {
      if (running_)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 解析 OSC 包
    std::string address;
    std::vector<OscArg> args;

    if (parse_packet(buf, static_cast<int>(n), address, args)) {
      ProtocolMessage pm;
      pm.type = ProtocolType::OSC;
      char ip[32];
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
      pm.source = std::string(ip) + ":" + std::to_string(ntohs(from.sin_port));
      pm.address = address;
      pm.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();

      // 将参数序列化为 JSON payload
      std::ostringstream ss;
      ss << "[";
      for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) ss << ",";
        switch (args[i].type) {
          case 'i': ss << args[i].i; break;
          case 'f': ss << args[i].f; break;
          case 's': ss << "\"" << args[i].s << "\""; break;
          case 'T': ss << "true"; break;
          case 'F': ss << "false"; break;
          case 'N': ss << "null"; break;
          default: ss << "0"; break;
        }
      }
      ss << "]";
      pm.payload = ss.str();

      if (callback_)
        callback_(pm);
    }
  }
}

bool OscAdapter::parse_packet(const uint8_t* data, int len,
                                std::string& address,
                                std::vector<OscArg>& args) {
  if (len < 4) return false;

  // 检查是否是 OSC Bundle（以 #bundle 开头）
  if (memcmp(data, "#bundle\0", 8) == 0) {
    // 简化处理：不递归解析 bundle，只解析第一个消息
    // Bundle 格式: #bundle\0 + 8字节时间戳 + 4字节大小 + 数据
    if (len < 20) return false;
    int elem_size = ntohl(*reinterpret_cast<const uint32_t*>(data + 16));
    if (16 + 4 + elem_size > len) return false;
    return parse_packet(data + 20, elem_size, address, args);
  }

  // 1. 解析地址模式
  int addr_end = 0;
  while (addr_end < len && data[addr_end] != '\0') ++addr_end;
  if (addr_end >= len) return false;
  address = std::string(reinterpret_cast<const char*>(data), addr_end);

  // 跳过 null + padding 到 4 字节对齐
  int pos = align4(addr_end + 1);
  if (pos >= len) return true;  // 无参数

  // 2. 解析类型标签
  if (data[pos] != ',') return false;
  pos++;
  int tag_end = pos;
  while (tag_end < len && data[tag_end] != '\0') ++tag_end;
  if (tag_end >= len) return false;

  std::string tags(reinterpret_cast<const char*>(data + pos),
                    tag_end - pos);
  pos = align4(tag_end + 1);

  // 3. 解析参数
  for (char t : tags) {
    OscArg arg;
    arg.type = t;

    switch (t) {
      case 'i': {
        if (pos + 4 > len) return false;
        uint32_t v = ntohl(*reinterpret_cast<const uint32_t*>(data + pos));
        arg.i = *reinterpret_cast<const int32_t*>(&v);
        pos += 4;
        break;
      }
      case 'f': {
        if (pos + 4 > len) return false;
        uint32_t v = ntohl(*reinterpret_cast<const uint32_t*>(data + pos));
        arg.f = *reinterpret_cast<const float*>(&v);
        pos += 4;
        break;
      }
      case 's': {
        int str_end = pos;
        while (str_end < len && data[str_end] != '\0') ++str_end;
        arg.s = std::string(reinterpret_cast<const char*>(data + pos),
                             str_end - pos);
        pos = align4(str_end + 1);
        break;
      }
      case 'b': {
        if (pos + 4 > len) return false;
        uint32_t sz = ntohl(*reinterpret_cast<const uint32_t*>(data + pos));
        pos += 4;
        if (pos + static_cast<int>(sz) > len) return false;
        arg.b.assign(data + pos, data + pos + sz);
        pos = align4(pos + sz);
        break;
      }
      case 'T': case 'F': case 'N':
        // 无数据，不消耗 pos
        break;
      default:
        break;
    }
    args.push_back(std::move(arg));
  }

  return true;
}

std::vector<uint8_t> OscAdapter::encode_message(const std::string& address,
                                                  const std::vector<OscArg>& args) {
  std::vector<uint8_t> out;

  // 1. 地址 + null + padding
  out.insert(out.end(), address.begin(), address.end());
  out.push_back('\0');
  while (out.size() % 4 != 0) out.push_back('\0');

  // 2. 类型标签
  out.push_back(',');
  for (const auto& a : args) out.push_back(a.type);
  out.push_back('\0');
  while (out.size() % 4 != 0) out.push_back('\0');

  // 3. 参数
  for (const auto& a : args) {
    switch (a.type) {
      case 'i': {
        uint32_t v = htonl(static_cast<uint32_t>(a.i));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + 4);
        break;
      }
      case 'f': {
        uint32_t v;
        std::memcpy(&v, &a.f, 4);
        v = htonl(v);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + 4);
        break;
      }
      case 's': {
        out.insert(out.end(), a.s.begin(), a.s.end());
        out.push_back('\0');
        while (out.size() % 4 != 0) out.push_back('\0');
        break;
      }
      case 'b': {
        uint32_t sz = htonl(static_cast<uint32_t>(a.b.size()));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&sz);
        out.insert(out.end(), p, p + 4);
        out.insert(out.end(), a.b.begin(), a.b.end());
        while (out.size() % 4 != 0) out.push_back('\0');
        break;
      }
      case 'T': case 'F': case 'N':
        // 无数据
        break;
    }
  }

  return out;
}

bool OscAdapter::send(const std::string& address,
                      const std::string& payload) {
  // payload 是 JSON 数组，简化处理：发送无参数消息
  (void)payload;
  std::vector<OscArg> args;
  return send_message(address, args);
}

bool OscAdapter::send_message(const std::string& address,
                               const std::vector<OscArg>& args,
                               const std::string& target_ip,
                               int target_port) {
  std::string ip = target_ip.empty() ? target_ip_ : target_ip;
  int port = target_port == 0 ? target_port_ : target_port;

  std::vector<uint8_t> pkt = encode_message(address, args);
  if (pkt.empty()) return false;

  // 创建 UDP socket
#ifdef _WIN32
  sm::web::socket_t s = static_cast<sm::web::socket_t>(
      socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
#else
  sm::web::socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
#endif
  if (s == sm::web::kInvalidSocket) return false;

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

#ifdef _WIN32
  int sent = sendto(static_cast<SOCKET>(s),
                     reinterpret_cast<const char*>(pkt.data()),
                     static_cast<int>(pkt.size()), 0,
                     reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#else
  ssize_t sent = sendto(s, pkt.data(), pkt.size(), 0,
                         reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#endif

  sm::web::close_socket(s);
  return sent > 0;
}

} // namespace proto
} // namespace sm

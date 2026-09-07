// 跨平台 TCP Socket 抽象（Web Server 底层传输）
// macOS/Linux 用 POSIX socket；Windows 用 WinSock2。
// 提供：创建/绑定/监听/接受/收发/关闭 + 跨平台初始化。
#pragma once

#include <cstdint>
#include <string>

namespace sm {
namespace web {

#ifdef _WIN32
using socket_t = uintptr_t;   // SOCKET
static constexpr socket_t kInvalidSocket = static_cast<socket_t>(~0ULL);
#else
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

// 全局初始化（Windows 调 WSAStartup，其他平台空操作）；进程启动时调用一次。
// 返回 true 表示成功。
bool net_init();

// 全局清理（Windows 调 WSACleanup）。
void net_shutdown();

// 创建一个 IPv4 TCP listening socket，绑定 0.0.0.0:port 并开始监听。
// backlog=16。失败返回 kInvalidSocket。
socket_t listen_on(int port, int backlog = 16);

// 接受连接，返回 client socket（失败 kInvalidSocket）。
// 可选输出对端 IP 字符串。
socket_t accept_client(socket_t srv, std::string* out_peer_ip = nullptr);

// 发送全部字节（循环 send 直到全部发出或出错）；返回实际发出字节数。
int send_all(socket_t s, const char* data, int len);

// 接收最多 len 字节到 buf；返回实际收到的字节数，0=对端关闭，<0=错误。
int recv_some(socket_t s, char* buf, int len);

// 关闭 socket。
void close_socket(socket_t s);

// 设置 SO_REUSEADDR（避免 TIME_WAIT 导致 bind 失败）。
void set_reuse_addr(socket_t s);

}  // namespace web
}  // namespace sm

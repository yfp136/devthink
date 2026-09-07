#include "web/socket.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <cstring>
#endif

namespace sm {
namespace web {

bool net_init() {
#ifdef _WIN32
  WSADATA wsa;
  int r = WSAStartup(MAKEWORD(2, 2), &wsa);
  return r == 0;
#else
  return true;
#endif
}

void net_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}

void set_reuse_addr(socket_t s) {
  int opt = 1;
  setsockopt(static_cast<socket_t>(s), SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&opt), sizeof(opt));
}

socket_t listen_on(int port, int backlog) {
  socket_t srv = static_cast<socket_t>(
      socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (srv == kInvalidSocket) return kInvalidSocket;
  set_reuse_addr(srv);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<unsigned short>(port));

  if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close_socket(srv);
    return kInvalidSocket;
  }
  if (listen(srv, backlog) != 0) {
    close_socket(srv);
    return kInvalidSocket;
  }
  return srv;
}

socket_t accept_client(socket_t srv, std::string* out_peer_ip) {
  sockaddr_in client;
  socklen_t len = sizeof(client);
  socket_t client_sock = static_cast<socket_t>(
      accept(srv, reinterpret_cast<sockaddr*>(&client), &len));
  if (client_sock == kInvalidSocket) return kInvalidSocket;
  if (out_peer_ip) {
    char ip[64] = {0};
    inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
    *out_peer_ip = ip;
  }
  return client_sock;
}

int send_all(socket_t s, const char* data, int len) {
  int sent = 0;
  while (sent < len) {
    int n = static_cast<int>(
#ifdef _WIN32
        send(static_cast<SOCKET>(s), data + sent, len - sent, 0));
#else
        send(static_cast<int>(s), data + sent, len - sent, 0));
#endif
    if (n <= 0) return sent;
    sent += n;
  }
  return sent;
}

int recv_some(socket_t s, char* buf, int len) {
#ifdef _WIN32
  return static_cast<int>(recv(static_cast<SOCKET>(s), buf, len, 0));
#else
  return static_cast<int>(recv(static_cast<int>(s), buf, len, 0));
#endif
}

void close_socket(socket_t s) {
  if (s == kInvalidSocket) return;
#ifdef _WIN32
  closesocket(static_cast<SOCKET>(s));
#else
  close(static_cast<int>(s));
#endif
}

}  // namespace web
}  // namespace sm

// TCP 协议适配器
// 需求：TCP 对接外部设备（灯光台、中控主机、第三方系统）
// 功能：
//   1. 监听 TCP 端口，接收外部设备连接
//   2. 解析换行分隔的文本/JSON 指令
//   3. 转发到消息总线
//   4. 支持向指定 IP:port 发送控制指令
#pragma once

#include "protocols/protocol_adapter.h"
#include "web/socket.h"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace sm {
namespace proto {

class TcpAdapter : public ProtocolAdapter {
public:
  TcpAdapter();
  ~TcpAdapter() override;

  // 配置
  void set_listen_port(int port) { listen_port_ = port; }

  // 启动监听
  bool start() override;

  // 停止
  void stop() override;

  bool is_running() const override { return running_; }

  // 发送到指定 client_id（-1 = 广播所有连接）
  bool send(const std::string& address,
            const std::string& payload) override;

  // 发送到指定 IP:port（主动连接并发送）
  bool send_to(const std::string& ip, int port,
               const std::string& message);

  // 获取当前连接数
  int connection_count();

private:
  void accept_loop();
  void client_loop(sm::web::socket_t sock, int client_id,
                   std::string peer_ip);

  int listen_port_ = 9000;
  sm::web::socket_t listen_sock_ = sm::web::kInvalidSocket;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;

  std::mutex clients_mutex_;
  struct Client {
    sm::web::socket_t sock;
    std::string peer_ip;
    std::thread thread;
  };
  std::map<int, Client> clients_;
  int next_client_id_ = 1;
};

} // namespace proto
} // namespace sm

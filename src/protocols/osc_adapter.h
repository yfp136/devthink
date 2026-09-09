// OSC (Open Sound Control) 协议适配器
// 需求：OSC 对接（灯光控制台、VJ 软件、音频工作站等）
// 功能：
//   1. 监听 UDP 端口，接收 OSC 1.0 消息
//   2. 解析 OSC 地址模式 + 类型标签 + 参数
//   3. 路由到 ShowMaster 指令
//   4. 支持向指定 IP:port 发送 OSC 消息
//
// OSC 1.0 包格式：
//   /address/pattern\0 + ,typeTag\0 + binary args (4字节对齐)
//   类型: i(int32) f(float32) s(string) b(blob) T(True) F(False)
#pragma once

#include "protocols/protocol_adapter.h"
#include "web/socket.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace sm {
namespace proto {

// OSC 参数
struct OscArg {
  char type = 0;     // 'i', 'f', 's', 'b', 'T', 'F', 'N'
  int32_t i = 0;
  float f = 0.0f;
  std::string s;
  std::vector<uint8_t> b;
};

class OscAdapter : public ProtocolAdapter {
public:
  OscAdapter();
  ~OscAdapter() override;

  // 配置
  void set_listen_port(int port) { listen_port_ = port; }
  void set_target(const std::string& ip, int port) {
    target_ip_ = ip;
    target_port_ = port;
  }

  // 启动 UDP 监听
  bool start() override;
  void stop() override;
  bool is_running() const override { return running_; }

  // 发送 OSC 消息
  // address: OSC 地址（如 /showmaster/go）
  // payload: JSON 格式参数（解析为 OSC 参数）
  bool send(const std::string& address,
            const std::string& payload) override;

  // 发送原始 OSC 消息到指定目标
  bool send_message(const std::string& address,
                    const std::vector<OscArg>& args,
                    const std::string& target_ip = "",
                    int target_port = 0);

  // OSC 编解码（public 供测试与外部使用）
  bool parse_packet(const uint8_t* data, int len,
                    std::string& address,
                    std::vector<OscArg>& args);
  std::vector<uint8_t> encode_message(const std::string& address,
                                       const std::vector<OscArg>& args);

private:
  void recv_loop();

  // 辅助：4 字节对齐
  static int align4(int n) { return (n + 3) & ~3; }

  int listen_port_ = 8000;
  std::string target_ip_ = "127.0.0.1";
  int target_port_ = 9000;

  sm::web::socket_t sock_ = sm::web::kInvalidSocket;
  std::atomic<bool> running_{false};
  std::thread recv_thread_;
};

} // namespace proto
} // namespace sm

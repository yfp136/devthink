// 硬件协议适配器统一接口
// 需求：TCP/MIDI/OSC 对接，支持外部设备控制 ShowMaster，
// 也支持 ShowMaster 向外部设备发送指令。
#pragma once

#include <functional>
#include <string>

namespace sm {
namespace proto {

// 协议类型
enum class ProtocolType {
  TCP,
  MIDI,
  OSC
};

// 接收到的协议消息（统一中间表示）
struct ProtocolMessage {
  ProtocolType type;
  std::string source;       // 来源标识（IP:port / MIDI port / OSC address）
  std::string address;      // 地址（TCP command / MIDI CC# / OSC path）
  std::string payload;      // 负载（JSON / MIDI data / OSC args）
  int64_t timestamp_ms = 0; // 接收时间戳
};

// 协议消息回调
using ProtocolCallback = std::function<void(const ProtocolMessage&)>;

// 协议适配器基类
class ProtocolAdapter {
public:
  virtual ~ProtocolAdapter() = default;

  // 启动监听 / 连接
  virtual bool start() = 0;

  // 停止
  virtual void stop() = 0;

  // 是否运行中
  virtual bool is_running() const = 0;

  // 发送消息到外部设备
  virtual bool send(const std::string& address,
                    const std::string& payload) = 0;

  // 注册接收回调
  void set_callback(ProtocolCallback cb) { callback_ = std::move(cb); }

protected:
  ProtocolCallback callback_;
};

} // namespace proto
} // namespace sm

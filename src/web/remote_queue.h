// 远程指令独立队列（规格 §9.5：远程指令不影响本地演出毫秒级精度）
// 设计：网络/WebSocket 线程把指令 push 进队列；本地调度线程在自己的
// tick 周期内 drain 队列并投递到 MsgBus。两者完全解耦，网络卡顿/延迟
// 不影响引擎的实时渲染/播放线程。
//
// 队列内部为无锁环形缓冲（单生产者-单消费者模式足够；多生产者加 spinlock）。
// 丢弃策略：满时丢弃最旧指令并记录（演出安全 > 指令完整）。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace sm {
namespace web {

struct RemoteCommand {
  std::string id;          // 信封 id
  std::string op;          // 指令名（§5.3 字典）
  std::string params_json; // 参数 JSON 文本
  std::string src;         // 来源标记（remote.web:<peer>）
  int64_t enqueue_ms;      // 入队时间
};

class RemoteQueue {
 public:
  explicit RemoteQueue(size_t capacity = 256);

  // 入队（网络线程调用）；满时丢弃最旧并返回 false。
  bool push(RemoteCommand cmd);

  // 排空队列（本地调度线程周期调用）；返回取出的指令数。
  // 对每条指令调用 handler。
  size_t drain(std::function<void(const RemoteCommand&)> handler);

  // 当前队列长度（近似值）
  size_t size() const;

  // 累计入队/丢弃计数（自检/监控用）
  uint64_t total_pushed() const { return total_pushed_.load(); }
  uint64_t total_dropped() const { return total_dropped_.load(); }

 private:
  std::mutex mu_;
  std::vector<RemoteCommand> ring_;
  size_t head_ = 0;   // 下一个写入位置
  size_t tail_ = 0;   // 下一个读取位置
  size_t count_ = 0;
  std::atomic<uint64_t> total_pushed_{0};
  std::atomic<uint64_t> total_dropped_{0};
};

}  // namespace web
}  // namespace sm

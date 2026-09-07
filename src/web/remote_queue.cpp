#include "web/remote_queue.h"

namespace sm {
namespace web {

RemoteQueue::RemoteQueue(size_t capacity)
    : ring_(capacity > 0 ? capacity : 256) {}

bool RemoteQueue::push(RemoteCommand cmd) {
  std::lock_guard<std::mutex> lk(mu_);
  if (count_ >= ring_.size()) {
    // 满：丢弃最旧（tail 前进），优先保证新指令被处理
    tail_ = (tail_ + 1) % ring_.size();
    --count_;
    total_dropped_.fetch_add(1);
  }
  ring_[head_] = std::move(cmd);
  head_ = (head_ + 1) % ring_.size();
  ++count_;
  total_pushed_.fetch_add(1);
  return true;
}

size_t RemoteQueue::drain(std::function<void(const RemoteCommand&)> handler) {
  std::vector<RemoteCommand> batch;
  {
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = count_;
    batch.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      batch.push_back(ring_[tail_]);
      tail_ = (tail_ + 1) % ring_.size();
    }
    count_ = 0;
  }
  for (const auto& cmd : batch)
    handler(cmd);
  return batch.size();
}

size_t RemoteQueue::size() const {
  std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(mu_));
  return count_;
}

}  // namespace web
}  // namespace sm

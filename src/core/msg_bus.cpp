#include "core/msg_bus.h"

namespace sm {

// ---- MsgBus ----
void MsgBus::register_sink(const std::string& dst, Sink sink) {
  std::lock_guard<std::mutex> lk(mu_);
  sinks_[dst] = std::move(sink);
}

bool MsgBus::unregister_sink(const std::string& dst) {
  std::lock_guard<std::mutex> lk(mu_);
  return sinks_.erase(dst) > 0;
}

void MsgBus::set_default_sink(Sink sink) {
  std::lock_guard<std::mutex> lk(mu_);
  default_ = std::move(sink);
}

void MsgBus::clear_sinks() {
  std::lock_guard<std::mutex> lk(mu_);
  sinks_.clear();
  default_ = nullptr;
}

void MsgBus::post(const Envelope& e) {
  // 1) 锁内：记录 + 解析目标（快照需要调用的 sink 副本）
  Sink fallback;            // 广播时=default_；精确未命中时=default_
  std::vector<Sink> targets;
  {
    std::lock_guard<std::mutex> lk(mu_);
    ring_.push_back(e);
    ++posted_;

    if (e.dst == "*") {
      targets.reserve(sinks_.size());
      for (const auto& kv : sinks_)
        if (kv.second) targets.push_back(kv.second);
      fallback = default_;
    } else {
      auto it = sinks_.find(e.dst);
      if (it != sinks_.end() && it->second)
        targets.push_back(it->second);
      else
        fallback = default_;
    }
  }

  // 2) 锁外调用：sink 内部可能再次 post（引擎回 rsp），不能持有互斥量
  for (const auto& s : targets) s(e);
  if (fallback) fallback(e);
}

std::vector<Envelope> MsgBus::recent(std::size_t n) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<Envelope> out;
  const std::size_t start = ring_.size() > n ? ring_.size() - n : 0;
  for (std::size_t i = start; i < ring_.size(); ++i) out.push_back(ring_[i]);
  return out;
}

// ---- HeartbeatMonitor ----
void HeartbeatMonitor::note_heartbeat(const std::string& engine_id, std::int64_t now_ms) {
  auto& h = health_[engine_id];
  if (!h.online) {
    // 首次上线 / 重新上线
    h.engine_id = engine_id;
    h.online = true;
    h.missed = 0;
  } else {
    // 心跳间隙不超过一个周期则清零丢失计数，否则按实际间隔折算
    const std::int64_t gap = now_ms - h.last_seen_ms;
    h.missed = (gap <= kIntervalMs) ? 0 : h.missed + 1;
    if (h.missed >= kMaxLost) h.online = false;
  }
  h.last_seen_ms = now_ms;
}

void HeartbeatMonitor::tick(std::int64_t now_ms) {
  for (auto& kv : health_) {
    auto& h = kv.second;
    // 距上次心跳 ≥ kIntervalMs*kMaxLost（即连续 3 个心跳周期无上报）判失联
    if (now_ms - h.last_seen_ms >= kIntervalMs * kMaxLost) {
      h.online = false;
      h.missed = kMaxLost;
    }
  }
}

bool HeartbeatMonitor::online(const std::string& engine_id) const {
  auto it = health_.find(engine_id);
  return it != health_.end() && it->second.online;
}

std::vector<std::string> HeartbeatMonitor::offline_engines() const {
  std::vector<std::string> out;
  for (const auto& kv : health_) {
    if (!kv.second.online) out.push_back(kv.first);
  }
  return out;
}

}  // namespace sm

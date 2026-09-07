#include "core/msg_bus.h"

namespace sm {

// ---- MsgBus ----
void MsgBus::register_sink(const std::string& dst, Sink sink) {
  sinks_[dst] = std::move(sink);
}

void MsgBus::set_default_sink(Sink sink) { default_ = std::move(sink); }

void MsgBus::post(const Envelope& e) {
  ring_.push_back(e);
  ++posted_;

  if (e.dst == "*") {
    for (const auto& kv : sinks_) {
      if (kv.second) kv.second(e);
    }
    if (default_) default_(e);
    return;
  }
  auto it = sinks_.find(e.dst);
  if (it != sinks_.end() && it->second) {
    it->second(e);
  } else if (default_) {
    default_(e);
  }
}

std::vector<Envelope> MsgBus::recent(std::size_t n) const {
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

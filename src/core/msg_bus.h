// 消息总线与心跳监控（规格 §5.2）
// 里程碑说明：本文件为纯逻辑内核版总线——同步路由 + 环形记录 + 心跳判定，
// 线程模型（独立总线线程、异步投递）在平台里程碑接入时于总线线程外层实现，
// 本层的路由/秩序/记录语义保持不变。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/envelope.h"

namespace sm {

// ---- 总线 --------------------------------------------------------------
class MsgBus {
 public:
  using Sink = std::function<void(const Envelope&)>;

  // 注册目标地址（engine.xxx / engine.ui）的处理 sink；重复注册覆盖
  void register_sink(const std::string& dst, Sink sink);

  // 未注册 dst 时投递到默认 sink（如 core 派发器）
  void set_default_sink(Sink sink);

  // 投递信封：先写入环形记录，再路由。
  // dst == "*" 广播给全部已注册 sink；精确匹配未命中时走默认 sink。
  void post(const Envelope& e);

  // 已投递总量（含广播，用于自检）
  std::size_t posted_count() const { return posted_; }

  // 最近 n 条记录（用于回放/断言，§5.2 环形内存缓冲语义）
  std::vector<Envelope> recent(std::size_t n) const;

  void clear() { ring_.clear(); sinks_.clear(); default_ = nullptr; posted_ = 0; }

 private:
  std::map<std::string, Sink> sinks_;
  Sink default_;
  std::deque<Envelope> ring_;
  std::size_t posted_ = 0;
};

// ---- 引擎心跳监控（§5.2：每 2s 心跳，连续 3 次丢失判失联）--------------
struct EngineHealth {
  std::string engine_id;
  std::int64_t last_seen_ms = 0;
  int missed = 0;        // 连续丢失次数
  bool online = false;   // 当前是否判定在线
};

class HeartbeatMonitor {
 public:
  static constexpr std::int64_t kIntervalMs = 2000;   // 心跳周期 2s
  static constexpr int kMaxLost = 3;                  // 连续 3 次丢失 = 失联

  // 记录一次心跳；按给定“当前时间”推进（测试可注入时间轴）
  void note_heartbeat(const std::string& engine_id, std::int64_t now_ms);

  // 依据心跳间隔推进判定（now_ms 距 last_seen 超过 kIntervalMs*kMaxLost 判离线）
  void tick(std::int64_t now_ms);

  bool online(const std::string& engine_id) const;

  // 当前失联引擎列表
  std::vector<std::string> offline_engines() const;

  const std::map<std::string, EngineHealth>& snapshot() const { return health_; }

  void clear() { health_.clear(); }

 private:
  std::map<std::string, EngineHealth> health_;
};

}  // namespace sm

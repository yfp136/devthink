// 消息总线与心跳监控（规格 §5.2）
// 里程碑说明：本文件为纯逻辑内核版总线——同步路由 + 环形记录 + 心跳判定，
// 线程模型（独立总线线程、异步投递）在平台里程碑接入时于总线线程外层实现，
// 本层的路由/秩序/记录语义保持不变。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/envelope.h"

namespace sm {

// ---- 总线 --------------------------------------------------------------
// 线程模型：本层路由/秩序/记录语义与 §5.2 一致，且允许「业务线程发指令」与
// 「引擎自带线程广播事件」（如 TimelineEngine 2ms tick 线程的 evt.transport.clock）
// 并发投递。内部用互斥量保护 sink 表与环形缓冲；sink 回调在锁外调用，
// 以支持 sink 内部再次 post（如引擎回 rsp）。
class MsgBus {
 public:
  using Sink = std::function<void(const Envelope&)>;

  // 注册目标地址（engine.xxx / engine.ui）的处理 sink；重复注册覆盖
  void register_sink(const std::string& dst, Sink sink);

  // 注销目标地址的 sink；返回是否确有注销（重复注销返回 false）。
  // 宿主对象（UI 桥、测试探针）析构前应显式摘除，避免总线后续向已析构对象派发。
  bool unregister_sink(const std::string& dst);

  // 未注册 dst 时投递到默认 sink（如 core 派发器）
  void set_default_sink(Sink sink);

  // 清空全部 sink（含默认 sink），但保留环形记录。
  // Kernel 析构时调用：切断外部回调后，引擎成员析构期间产生的事件
  // （如 ~PlaylistExecutor → evt.playlist.ended 广播）不再派发给外部对象。
  void clear_sinks();

  // 投递信封：先写入环形记录，再路由。
  // dst == "*" 广播给全部已注册 sink；精确匹配未命中时走默认 sink。
  void post(const Envelope& e);

  // 已投递总量（含广播，用于自检）
  std::size_t posted_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return posted_;
  }

  // 最近 n 条记录（用于回放/断言，§5.2 环形内存缓冲语义）
  std::vector<Envelope> recent(std::size_t n) const;

  void clear() {
    std::lock_guard<std::mutex> lk(mu_);
    ring_.clear();
    sinks_.clear();
    default_ = nullptr;
    posted_ = 0;
  }

 private:
  mutable std::mutex mu_;
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

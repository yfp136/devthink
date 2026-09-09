// 7x24 稳定性看门狗
// 需求：支持7x24小时长期稳定运行、抗干扰、后台常驻、异常自动重启
// 功能：心跳自检 / 死锁检测 / 内存监控 / 线程状态监控
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace sm {
namespace remote {

// 看门狗配置
struct WatchdogConfig {
  // 心跳超时（毫秒）：超过此时间未收到心跳 → 判定卡死
  int heartbeat_timeout_ms = 10000;
  // 检查间隔（毫秒）
  int check_interval_ms = 3000;
  // 最大 RSS 内存阈值（MB），超过触发告警
  size_t max_rss_mb = 4096;
  // 线程数上限（异常检测）
  size_t max_thread_count = 256;
  // 崩溃后自动重启的最大次数（0 = 不限制）
  int max_restart_count = 0;
  // 重启延迟（毫秒）
  int restart_delay_ms = 5000;
};

// 看门狗状态（内部用 atomic）
struct WatchdogStatusInternal {
  std::atomic<int64_t> last_heartbeat_ms{0};
  std::atomic<bool>     healthy{true};
  std::atomic<bool>     deadlock_detected{false};
  std::atomic<size_t>   current_rss_mb{0};
  std::atomic<size_t>   current_thread_count{0};
  std::atomic<int>      restart_count{0};
};

// 看门狗状态快照（可复制，用于外部查询）
struct WatchdogStatus {
  int64_t last_heartbeat_ms = 0;
  bool     healthy = true;
  bool     deadlock_detected = false;
  size_t   current_rss_mb = 0;
  size_t   current_thread_count = 0;
  int      restart_count = 0;
};

// 告警类型
enum class AlertType {
  HeartbeatTimeout,
  MemoryExceeded,
  ThreadCountExceeded,
  DeadlockDetected,
  ServiceRestarted
};

// 告警回调
using AlertCallback = std::function<void(AlertType, const std::string& msg)>;

class Watchdog {
public:
  Watchdog();
  ~Watchdog();

  // 启动看门狗监控线程
  void start(const WatchdogConfig& cfg = {});

  // 停止监控
  void stop();

  // 业务循环调用：喂狗（重置心跳计时器）
  void feed();

  // 注册告警回调
  void set_alert_callback(AlertCallback cb);

  // 获取当前状态
  WatchdogStatus status() const;

  // 触发主动重启
  void request_restart(const std::string& reason);

  // 设置重启函数（由 main_headless 提供：重启内核引擎）
  void set_restart_fn(std::function<void()> fn);

private:
  void monitor_loop();

  WatchdogConfig cfg_;
  WatchdogStatusInternal status_;
  std::atomic<bool> running_{false};
  std::atomic<bool> restart_requested_{false};
  std::string restart_reason_;
  std::mutex cb_mutex_;
  AlertCallback alert_cb_;
  std::function<void()> restart_fn_;
  std::thread monitor_thread_;

  // 平台相关：获取进程 RSS 和线程数
  size_t get_rss_mb();
  size_t get_thread_count();
};

} // namespace remote
} // namespace sm

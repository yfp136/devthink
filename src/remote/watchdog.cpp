// 7x24 稳定性看门狗实现
#include "remote/watchdog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#include <fstream>
#include <unistd.h>
#endif

namespace sm {
namespace remote {

Watchdog::Watchdog() = default;
Watchdog::~Watchdog() { stop(); }

void Watchdog::start(const WatchdogConfig& cfg) {
  cfg_ = cfg;
  running_ = true;

  // 初始化心跳
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  status_.last_heartbeat_ms = now;

  monitor_thread_ = std::thread(&Watchdog::monitor_loop, this);
}

void Watchdog::stop() {
  running_ = false;
  if (monitor_thread_.joinable())
    monitor_thread_.join();
}

void Watchdog::feed() {
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  status_.last_heartbeat_ms = now;
  status_.deadlock_detected = false;
}

void Watchdog::set_alert_callback(AlertCallback cb) {
  std::lock_guard<std::mutex> lk(cb_mutex_);
  alert_cb_ = std::move(cb);
}

void Watchdog::set_restart_fn(std::function<void()> fn) {
  restart_fn_ = std::move(fn);
}

WatchdogStatus Watchdog::status() const {
  WatchdogStatus snap;
  snap.last_heartbeat_ms = status_.last_heartbeat_ms.load();
  snap.healthy = status_.healthy.load();
  snap.deadlock_detected = status_.deadlock_detected.load();
  snap.current_rss_mb = status_.current_rss_mb.load();
  snap.current_thread_count = status_.current_thread_count.load();
  snap.restart_count = status_.restart_count.load();
  return snap;
}

void Watchdog::request_restart(const std::string& reason) {
  restart_requested_ = true;
  restart_reason_ = reason;
}

void Watchdog::monitor_loop() {
  while (running_.load()) {
    // 1. 心跳检查
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = status_.last_heartbeat_ms.load();
    auto elapsed = now - last;

    if (elapsed > cfg_.heartbeat_timeout_ms) {
      status_.healthy = false;
      status_.deadlock_detected = true;

      std::string msg = "心跳超时 " + std::to_string(elapsed) +
                        "ms（阈值 " +
                        std::to_string(cfg_.heartbeat_timeout_ms) + "ms）";
      {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (alert_cb_) alert_cb_(AlertType::HeartbeatTimeout, msg);
      }
      std::fprintf(stderr, "[watchdog] %s\n", msg.c_str());

      // 触发重启
      if (restart_fn_) {
        status_.restart_count++;
        std::fprintf(stderr, "[watchdog] 触发引擎重启 (#%d)\n",
                     status_.restart_count.load());
        {
          std::lock_guard<std::mutex> lk(cb_mutex_);
          if (alert_cb_)
            alert_cb_(AlertType::ServiceRestarted,
                      "心跳超时触发自动重启");
        }
        restart_fn_();
        feed();  // 重启后重置心跳
        status_.healthy = true;
      }
    }

    // 2. 内存监控
    size_t rss = get_rss_mb();
    status_.current_rss_mb = rss;
    if (cfg_.max_rss_mb > 0 && rss > cfg_.max_rss_mb) {
      std::string msg = "RSS=" + std::to_string(rss) + "MB 超过阈值 " +
                        std::to_string(cfg_.max_rss_mb) + "MB";
      {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (alert_cb_) alert_cb_(AlertType::MemoryExceeded, msg);
      }
      std::fprintf(stderr, "[watchdog] %s\n", msg.c_str());
    }

    // 3. 线程数监控
    size_t threads = get_thread_count();
    status_.current_thread_count = threads;
    if (cfg_.max_thread_count > 0 && threads > cfg_.max_thread_count) {
      std::string msg = "线程数=" + std::to_string(threads) +
                        " 超过阈值 " +
                        std::to_string(cfg_.max_thread_count);
      {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (alert_cb_) alert_cb_(AlertType::ThreadCountExceeded, msg);
      }
      std::fprintf(stderr, "[watchdog] %s\n", msg.c_str());
    }

    // 4. 外部重启请求
    if (restart_requested_.load()) {
      {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        if (alert_cb_)
          alert_cb_(AlertType::ServiceRestarted,
                    "外部请求重启: " + restart_reason_);
      }
      if (restart_fn_) {
        std::fprintf(stderr, "[watchdog] 外部请求重启: %s\n",
                     restart_reason_.c_str());
        restart_fn_();
        feed();
      }
      restart_requested_ = false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(cfg_.check_interval_ms));
  }
}

// ---- 平台相关：获取 RSS ----

size_t Watchdog::get_rss_mb() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    return pmc.WorkingSetSize / (1024 * 1024);
  return 0;
#else
  // macOS/Linux: 读取 /proc/self/status 或 getrusage
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
#ifdef __APPLE__
    return ru.ru_maxrss / (1024 * 1024);  // macOS: bytes
#else
    return ru.ru_maxrss / 1024;  // Linux: KB
#endif
  }
  return 0;
#endif
}

// ---- 平台相关：获取线程数 ----

size_t Watchdog::get_thread_count() {
#if defined(_WIN32)
  // Windows: 统计伪句柄
  DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  size_t count = 0;
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID == pid) count++;
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  return count;
#else
  // macOS/Linux: 读取 /proc/self/status
  std::ifstream f("/proc/self/status");
  if (!f) return 0;
  std::string line;
  while (std::getline(f, line)) {
    if (line.compare(0, 8, "Threads:") == 0) {
      size_t n = 0;
      std::sscanf(line.c_str(), "Threads: %zu", &n);
      return n;
    }
  }
  return 0;
#endif
}

} // namespace remote
} // namespace sm

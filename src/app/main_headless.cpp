// ShowMaster Headless 服务器模式入口
// 工控机/服务器无界面常驻运行：启动 Kernel(M1-M5引擎) + Web Server。
//
// 用法：
//   sm_headless [port]              - 独立运行模式（直接启动）
//   sm_headless --port 8080         - 指定端口独立运行
//   sm_headless --install           - 注册为 Windows Service（开机自启）
//   sm_headless --uninstall         - 卸载 Windows Service
//   sm_headless --service           - 以服务模式运行（由 SCM 调用）
//   sm_headless --start             - 启动已注册的服务
//   sm_headless --stop              - 停止已注册的服务
//   sm_headless --status            - 查询服务状态
//
// 浏览器访问 http://<服务器IP>:<port> 即可远程管控。
// 7x24 稳定：看门狗心跳监控 + SCM 崩溃自动重启 + 死锁检测。
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "engines/kernel.h"
#include "core/msg_bus.h"
#include "core/op_dict.h"
#include "engines/engine_registry.h"
#include "protocols/osc_adapter.h"
#include "protocols/tcp_adapter.h"
#include "remote/service_manager.h"
#include "remote/watchdog.h"
#include "web/auth.h"
#include "web/http_server.h"
#include "web/remote_queue.h"
#include "web/web_gateway.h"
#include "platform/media_backend.h"

static std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

// ---- 辅助：解析命令行参数 ----
static int parse_port(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
      return std::atoi(argv[++i]);
  }
  // 兼容旧用法：第一个参数为端口号
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (!arg.empty() && arg[0] != '-' && std::atoi(arg.c_str()) > 0)
      return std::atoi(arg.c_str());
  }
  return 8080;
}

static bool has_flag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return true;
  return false;
}

// ---- 获取自身可执行文件路径 ----
static std::string get_self_path() {
#if defined(_WIN32)
  char buf[1024];
  DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
  return std::string(buf, len);
#else
  char buf[1024];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
  if (len <= 0) return std::string("sm_headless");
  return std::string(buf, len);
#endif
}

// ---- 核心业务运行函数（被服务模式和独立模式共用）----
static int run_headless(int argc, char** argv) {
  int port = parse_port(argc, argv);

  std::printf("=== ShowMaster Headless 服务器模式 ===\n");
  std::printf("端口: %d\n", port);

  if (!sm::web::net_init()) {
    std::fprintf(stderr, "[FATAL] 网络初始化失败\n");
    return 1;
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // ---- 看门狗初始化（7x24 稳定性）----
  sm::remote::WatchdogConfig wdcfg;
  wdcfg.heartbeat_timeout_ms = 10000;
  wdcfg.check_interval_ms = 3000;
  wdcfg.max_rss_mb = 4096;
  wdcfg.max_thread_count = 256;

  sm::remote::Watchdog watchdog;

  // ---- Kernel 初始化（M1-M5 全部引擎）----
  std::unique_ptr<sm::Kernel> kernel = std::make_unique<sm::Kernel>();

  // 看门狗重启函数：销毁并重建 Kernel（恢复到干净状态）
  watchdog.set_restart_fn([&]() {
    std::fprintf(stderr, "[watchdog] 重建 Kernel...\n");
    kernel->stop();
    kernel.reset();
    kernel = std::make_unique<sm::Kernel>();
    kernel->init();
    kernel->start();
    std::fprintf(stderr, "[watchdog] Kernel 重建完成\n");
  });

  // 告警回调：记录到日志
  watchdog.set_alert_callback([](sm::remote::AlertType type,
                                  const std::string& msg) {
    const char* type_str = "";
    switch (type) {
      case sm::remote::AlertType::HeartbeatTimeout:     type_str = "HEARTBEAT_TIMEOUT"; break;
      case sm::remote::AlertType::MemoryExceeded:      type_str = "MEM_EXCEEDED"; break;
      case sm::remote::AlertType::ThreadCountExceeded:  type_str = "THREAD_EXCEEDED"; break;
      case sm::remote::AlertType::DeadlockDetected:     type_str = "DEADLOCK"; break;
      case sm::remote::AlertType::ServiceRestarted:     type_str = "RESTARTED"; break;
    }
    std::fprintf(stderr, "[watchdog][%s] %s\n", type_str, msg.c_str());
  });

  // 向 SCM 报告 RUNNING
  sm::remote::report_service_state(sm::remote::ServiceState::Running);

  // 启动看门狗
  watchdog.start(wdcfg);

  // 初始化 Kernel
  kernel->init();
  kernel->start();

  std::printf("[kernel] M1-M5 引擎全部在线\n");
  std::printf("[kernel]   M1 素材库 / M2 媒体引擎 / M3 场景快照\n");
  std::printf("[kernel]   M4 播放列表 / M5 时间线调度器\n");

  // ---- Web 远控层 ----
  sm::web::Auth auth;
  auth.init_defaults();

  sm::web::RemoteQueue queue(256);

  sm::web::WebGateway gateway(auth, queue, kernel->heartbeat());
  sm::web::HttpServer server;

  // 状态提供者：从 Kernel 获取实时状态
  gateway.set_status_provider([&]() -> std::string {
    return kernel->get_status_json();
  });
  gateway.set_playlist_provider([&]() -> std::string {
    return kernel->get_playlist_json();
  });
  gateway.set_scene_list_provider([&]() -> std::string {
    return kernel->scene().list().dump();
  });

  // 快捷操作回调（直接执行，低延迟，不入队）
  gateway.set_scene_go_fn([&](const std::string& id, int fade) -> bool {
    return kernel->scene_go(id, fade);
  });
  gateway.set_transport_play_fn([&](const std::string& media_id) -> bool {
    return kernel->transport_play(media_id);
  });
  gateway.set_transport_stop_fn([&]() -> bool {
    return kernel->transport_stop();
  });
  gateway.set_transport_pause_fn([&]() -> bool {
    return kernel->transport_pause();
  });
  gateway.set_transport_resume_fn([&]() -> bool {
    return kernel->transport_resume();
  });
  gateway.set_playlist_go_fn([&]() -> bool {
    return kernel->playlist_go();
  });
  gateway.set_playlist_start_fn([&]() -> bool {
    return kernel->playlist_start();
  });
  gateway.set_playlist_stop_fn([&]() -> bool {
    return kernel->playlist_stop();
  });
  gateway.set_playlist_next_fn([&]() -> bool {
    return kernel->playlist_next();
  });

  gateway.register_routes(server);

  // 注册总线事件 → WebSocket 广播
  kernel->bus().register_sink("*", [&](const sm::Envelope& e) {
    std::string evt = sm::envelope_to_json(e);
    gateway.on_engine_event(evt);
  });

  // ---- 硬件协议适配器（TCP / OSC）----
  // 接收外部设备指令 → 投递到消息总线
  sm::proto::TcpAdapter tcp_adapter;
  tcp_adapter.set_listen_port(port + 1000);  // TCP 端口 = Web端口 + 1000

  tcp_adapter.set_callback([&](const sm::proto::ProtocolMessage& msg) {
    // 将 TCP 指令转换为总线命令
    nlohmann::json params = nlohmann::json::object();
    if (!msg.payload.empty() && msg.payload.front() == '{')
      params = nlohmann::json::parse(msg.payload, nullptr, false);

    std::string dst = "engine.media";
    const char* ns = sm::op_namespace(msg.address);
    if (ns) {
      std::string n = ns;
      if (n == "media" || n == "transport") dst = "engine.media";
      else if (n == "timeline") dst = "engine.timeline";
      else if (n == "scene") dst = "engine.scene";
      else if (n == "playlist") dst = "engine.playlist";
    }

    sm::Envelope env = sm::make_cmd("proto.tcp", dst, msg.address, params);
    kernel->bus().post(env);
  });

  sm::proto::OscAdapter osc_adapter;
  osc_adapter.set_listen_port(port + 2000);  // OSC 端口 = Web端口 + 2000

  osc_adapter.set_callback([&](const sm::proto::ProtocolMessage& msg) {
    // OSC 地址映射到总线操作
    // /showmaster/go → playlist.go
    // /showmaster/scene/<id> → scene.recall
    // /showmaster/play/<id> → media.play
    // /showmaster/stop → media.stop
    std::string op;
    nlohmann::json params = nlohmann::json::object();
    std::string dst = "engine.media";

    if (msg.address.find("/go") != std::string::npos) {
      op = "playlist.go";
      dst = "engine.playlist";
    } else if (msg.address.find("/scene/") != std::string::npos) {
      op = "scene.recall";
      dst = "engine.scene";
      // 从地址提取场景 ID
      size_t pos = msg.address.find("/scene/");
      if (pos != std::string::npos) {
        std::string id = msg.address.substr(pos + 7);
        params["scene_id"] = id;
      }
    } else if (msg.address.find("/play/") != std::string::npos) {
      op = "media.play";
      size_t pos = msg.address.find("/play/");
      if (pos != std::string::npos) {
        std::string id = msg.address.substr(pos + 6);
        params["media_id"] = id;
      }
    } else if (msg.address.find("/stop") != std::string::npos) {
      op = "media.stop";
    } else {
      // 未识别的地址：使用原始地址作为 op
      op = msg.address;
    }

    if (!msg.payload.empty() && msg.payload.front() == '[')
      params["osc_args"] = nlohmann::json::parse(msg.payload, nullptr, false);

    sm::Envelope env = sm::make_cmd("proto.osc", dst, op, params);
    kernel->bus().post(env);
  });

  // 启动协议适配器
  if (tcp_adapter.start())
    std::printf("[tcp] 硬件对接 TCP 端口: %d\n", port + 1000);

  if (osc_adapter.start())
    std::printf("[osc] 硬件对接 OSC 端口: %d\n", port + 2000);

  // 启动 HTTP Server
  if (!server.start(port)) {
    std::fprintf(stderr, "[FATAL] 无法绑定端口 %d\n", port);
    sm::remote::report_service_state(sm::remote::ServiceState::Stopped);
    return 1;
  }
  std::printf("[web] HTTP Server 监听 0.0.0.0:%d\n", port);
  std::printf("[web] 浏览器访问: http://0.0.0.0:%d\n", port);
  std::printf("[web] 默认账号: admin / admin123\n");

  // 探测音频输出端点（Windows：WASAPI 默认渲染设备；决定媒体引擎
  // 播放时启用音频渲染还是丢弃音频。失败不影响服务运行，仅无声。）
  if (sm::platform::init_audio_output())
    std::printf("[audio] WASAPI 默认渲染端点可用\n");
  else
    std::printf("[audio] 音频端点不可用 — 媒体播放将以纯视频模式运行\n");
  std::printf("[watchdog] 7x24 稳定性看门狗已启动\n");
  std::printf("[watchdog]   心跳超时: %dms\n", wdcfg.heartbeat_timeout_ms);
  std::printf("[watchdog]   内存阈值: %zuMB\n", wdcfg.max_rss_mb);
  std::printf("[watchdog]   线程上限: %zu\n", wdcfg.max_thread_count);
  std::printf("\n端口分配:\n");
  std::printf("  Web 远控: %d (浏览器)\n", port);
  std::printf("  TCP 对接: %d (外部设备)\n", port + 1000);
  std::printf("  OSC 对接: %d (UDP, 灯光台/VJ)\n", port + 2000);
  std::printf("\n按 Ctrl+C 退出\n\n");

  // ---- 主循环：drain 远程队列 → 投递总线 + 心跳 tick + PGM 预览 + 喂狗 ----
  auto last_tick = std::time(nullptr);

  // P1-3 PGM 实时预览驱动：播放/暂停期间以 ~10Hz 抓取最新解码帧，
  // 经 gateway.on_pgm_frame → /ws/preview 推给浏览器。空 JPEG（无媒体/无
  // 解码后端）不推；静止画面与上一帧相同不重复推，避免无谓带宽与 CPU。
  std::string last_pgm_jpeg;
  int preview_div = 0;

  while (g_running.load()) {
    // 排空远程指令队列，投递到消息总线
    queue.drain([&](const sm::web::RemoteCommand& cmd) {
      nlohmann::json params = nlohmann::json::parse(cmd.params_json,
                                                     nullptr, false);
      if (params.is_null()) params = nlohmann::json::object();

      // 根据 op 命名空间决定 dst
      std::string dst = "engine.media";
      const char* ns = sm::op_namespace(cmd.op);
      if (ns) {
        std::string n = ns;
        if (n == "media" || n == "transport") dst = "engine.media";
        else if (n == "timeline") dst = "engine.timeline";
        else if (n == "scene") dst = "engine.scene";
        else if (n == "playlist") dst = "engine.playlist";
        else if (n == "sys") dst = "engine.ui";
      }

      sm::Envelope env = sm::make_cmd(cmd.src, dst, cmd.op, params);
      kernel->bus().post(env);
    });

    // 心跳 tick（每 2 秒）
    auto now = std::time(nullptr);
    if (now - last_tick >= 2) {
      kernel->heartbeat().tick(now * 1000);
      for (const auto& desc : sm::engine_registry())
        kernel->heartbeat().note_heartbeat(desc.id, now * 1000);
      last_tick = now;
    }

    // PGM 实时预览（P1-3）：主循环 50ms/次，每 2 拍 ≈10Hz 抓帧
    if (++preview_div >= 2) {
      preview_div = 0;
      const sm::media::PlayState ps = kernel->media().state();
      if (ps == sm::media::PlayState::playing ||
          ps == sm::media::PlayState::paused) {
        std::string jpeg = sm::platform::capture_pgm_frame_jpeg();
        if (!jpeg.empty() && jpeg != last_pgm_jpeg) {
          gateway.on_pgm_frame(jpeg);
          last_pgm_jpeg = std::move(jpeg);
        }
      } else if (!last_pgm_jpeg.empty()) {
        // 离开播放/暂停态即复位，避免跨素材复用旧帧去重
        last_pgm_jpeg.clear();
      }
    }

    // 喂狗
    watchdog.feed();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // ---- 清理 ----
  std::printf("\n[shutdown] 停止引擎...\n");
  sm::remote::report_service_state(sm::remote::ServiceState::StopPending);
  osc_adapter.stop();
  tcp_adapter.stop();
  watchdog.stop();
  kernel->stop();
  server.stop();
  sm::web::net_shutdown();
  sm::platform::shutdown_audio_output();  // 兜底回收渲染线程与事件
  sm::remote::report_service_state(sm::remote::ServiceState::Stopped);
  std::printf("[shutdown] 完成\n");
  return 0;
}

// ---- 服务管理命令 ----
static const char* kServiceName = "ShowMasterSvc";
static const char* kServiceDisplay = "ShowMaster 全域智能舞美管控平台";
static const char* kServiceDesc =
    "ShowMaster 全域智能舞美管控平台 - 7x24小时无界面常驻服务，"
    "支持Web远程管控、节目单播放、GO触发、场景切换等。";

static int cmd_install(int argc, char** argv) {
  int port = parse_port(argc, argv);

  sm::remote::ServiceConfig cfg;
  cfg.name = kServiceName;
  cfg.display_name = kServiceDisplay;
  cfg.description = kServiceDesc;
  cfg.binary_path = get_self_path();
  cfg.port = port;
  cfg.auto_start = true;

  if (sm::remote::install_service(cfg)) {
    std::printf("\n[service] 安装成功！\n");
    std::printf("[service] 启动服务: net start %s\n", kServiceName);
    std::printf("[service] 停止服务: net stop %s\n", kServiceName);
    std::printf("[service] 服务管理: sc.exe query %s\n", kServiceName);
    std::printf("[service] 自动启动: 已配置（延迟自动启动，等待网络就绪后启动）\n");
    std::printf("[service] 崩溃恢复: 5s/10s/30s 自动重启\n");
    return 0;
  }
  return 1;
}

static int cmd_uninstall() {
  if (sm::remote::uninstall_service(kServiceName)) {
    std::printf("[service] 卸载成功\n");
    return 0;
  }
  return 1;
}

static int cmd_start() {
  if (sm::remote::start_service(kServiceName)) {
    std::printf("[service] 服务已启动\n");
    return 0;
  }
  std::fprintf(stderr, "[service] 启动失败\n");
  return 1;
}

static int cmd_stop() {
  if (sm::remote::stop_service(kServiceName)) {
    std::printf("[service] 服务已停止\n");
    return 0;
  }
  std::fprintf(stderr, "[service] 停止失败\n");
  return 1;
}

static int cmd_status() {
  bool installed = sm::remote::is_service_installed(kServiceName);
  bool running = sm::remote::is_service_running(kServiceName);
  std::printf("[service] %s\n", kServiceName);
  std::printf("[service]   安装状态: %s\n", installed ? "已安装" : "未安装");
  std::printf("[service]   运行状态: %s\n", running ? "运行中" : "已停止");
  return 0;
}

static void print_usage() {
  std::printf("ShowMaster Headless 服务器模式\n\n");
  std::printf("用法:\n");
  std::printf("  sm_headless [port]               独立运行（直接启动）\n");
  std::printf("  sm_headless --port <N>            指定端口独立运行\n");
  std::printf("  sm_headless --install [--port N]  注册为 Windows Service\n");
  std::printf("  sm_headless --uninstall           卸载 Windows Service\n");
  std::printf("  sm_headless --start               启动已注册的服务\n");
  std::printf("  sm_headless --stop                停止已注册的服务\n");
  std::printf("  sm_headless --status              查询服务状态\n");
  std::printf("  sm_headless --service             以服务模式运行（由 SCM 调用）\n");
  std::printf("\n默认端口: 8080\n");
}

// ---- main ----
int main(int argc, char** argv) {
  // 无参数或端口号：独立运行
  if (argc == 1 || (argc == 2 && std::atoi(argv[1]) > 0)) {
    return run_headless(argc, argv);
  }

  // --help
  if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
    print_usage();
    return 0;
  }

  // 服务管理命令（优先于 --port 检查，因为 --install 可附带 --port）
  if (has_flag(argc, argv, "--install")) {
    return cmd_install(argc, argv);
  }

  // --uninstall
  if (has_flag(argc, argv, "--uninstall")) {
    return cmd_uninstall();
  }

  // --start
  if (has_flag(argc, argv, "--start")) {
    return cmd_start();
  }

  // --stop
  if (has_flag(argc, argv, "--stop")) {
    return cmd_stop();
  }

  // --status
  if (has_flag(argc, argv, "--status")) {
    return cmd_status();
  }

  // --service：SCM 调用的服务模式
  if (has_flag(argc, argv, "--service")) {
    int port = parse_port(argc, argv);
    sm::remote::ServiceConfig cfg;
    cfg.name = kServiceName;
    cfg.display_name = kServiceDisplay;
    cfg.port = port;

    // 向 SCM 报告 START_PENDING
    sm::remote::report_service_state(
        sm::remote::ServiceState::StartPending);

    // 以服务模式运行
    return sm::remote::run_as_service(cfg, run_headless, argc, argv);
  }

  // --port：独立运行（放在服务命令之后，因为 --install 可附带 --port）
  if (has_flag(argc, argv, "--port")) {
    return run_headless(argc, argv);
  }

  // 未知参数
  std::fprintf(stderr, "未知参数。使用 --help 查看用法。\n");
  return 1;
}

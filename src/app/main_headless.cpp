// ShowMaster Headless 服务器模式入口
// 工控机/服务器无界面常驻运行：启动 Kernel(M1-M5引擎) + Web Server。
// 用法：sm_headless [port=8080]
// 浏览器访问 http://<服务器IP>:<port> 即可远程管控。
// 开机自启：注册为 Windows Service 或 systemd 单元。
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "engines/kernel.h"
#include "core/msg_bus.h"
#include "core/op_dict.h"
#include "engines/engine_registry.h"
#include "web/auth.h"
#include "web/http_server.h"
#include "web/remote_queue.h"
#include "web/web_gateway.h"

static std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

int main(int argc, char** argv) {
  int port = 8080;
  if (argc > 1) port = std::atoi(argv[1]);

  std::printf("=== ShowMaster Headless 服务器模式 ===\n");
  std::printf("端口: %d\n", port);

  if (!sm::web::net_init()) {
    std::fprintf(stderr, "[FATAL] 网络初始化失败\n");
    return 1;
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // ---- Kernel 初始化（M1-M5 全部引擎）----
  sm::Kernel kernel;
  kernel.init();
  kernel.start();

  std::printf("[kernel] M1-M5 引擎全部在线\n");
  std::printf("[kernel]   M1 素材库 / M2 媒体引擎 / M3 场景快照\n");
  std::printf("[kernel]   M4 播放列表 / M5 时间线调度器\n");

  // ---- Web 远控层 ----
  sm::web::Auth auth;
  auth.init_defaults();

  sm::web::RemoteQueue queue(256);

  sm::web::WebGateway gateway(auth, queue, kernel.heartbeat());
  sm::web::HttpServer server;

  // 状态提供者：从 Kernel 获取实时状态
  gateway.set_status_provider([&]() -> std::string {
    return kernel.get_status_json();
  });
  gateway.set_playlist_provider([&]() -> std::string {
    return kernel.get_playlist_json();
  });
  gateway.set_scene_list_provider([&]() -> std::string {
    return kernel.scene().list().dump();
  });

  // 快捷操作回调（直接执行，低延迟，不入队）
  gateway.set_scene_go_fn([&](const std::string& id, int fade) -> bool {
    return kernel.scene_go(id, fade);
  });
  gateway.set_transport_play_fn([&](const std::string& media_id) -> bool {
    return kernel.transport_play(media_id);
  });
  gateway.set_transport_stop_fn([&]() -> bool {
    return kernel.transport_stop();
  });
  gateway.set_transport_pause_fn([&]() -> bool {
    return kernel.transport_pause();
  });
  gateway.set_transport_resume_fn([&]() -> bool {
    return kernel.transport_resume();
  });
  gateway.set_playlist_go_fn([&]() -> bool {
    return kernel.playlist_go();
  });
  gateway.set_playlist_start_fn([&]() -> bool {
    return kernel.playlist_start();
  });
  gateway.set_playlist_stop_fn([&]() -> bool {
    return kernel.playlist_stop();
  });
  gateway.set_playlist_next_fn([&]() -> bool {
    return kernel.playlist_next();
  });

  gateway.register_routes(server);

  // 注册总线事件 → WebSocket 广播
  kernel.bus().register_sink("*", [&](const sm::Envelope& e) {
    std::string evt = sm::envelope_to_json(e);
    gateway.on_engine_event(evt);
  });

  // 启动 HTTP Server
  if (!server.start(port)) {
    std::fprintf(stderr, "[FATAL] 无法绑定端口 %d\n", port);
    return 1;
  }
  std::printf("[web] HTTP Server 监听 0.0.0.0:%d\n", port);
  std::printf("[web] 浏览器访问: http://0.0.0.0:%d\n", port);
  std::printf("[web] 默认账号: admin / admin123\n");
  std::printf("[web] 按 Ctrl+C 退出\n\n");

  // ---- 主循环：drain 远程队列 → 投递总线 + 心跳 tick ----
  auto last_tick = std::time(nullptr);
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
      kernel.bus().post(env);
    });

    // 心跳 tick（每 2 秒）
    auto now = std::time(nullptr);
    if (now - last_tick >= 2) {
      kernel.heartbeat().tick(now * 1000);
      for (const auto& desc : sm::engine_registry())
        kernel.heartbeat().note_heartbeat(desc.id, now * 1000);
      last_tick = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // ---- 清理 ----
  std::printf("\n[shutdown] 停止引擎...\n");
  kernel.stop();
  server.stop();
  sm::web::net_shutdown();
  std::printf("[shutdown] 完成\n");
  return 0;
}

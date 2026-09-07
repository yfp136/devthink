// ShowMaster Headless 服务器模式入口
// 工控机/服务器无界面常驻运行：启动内核 + 引擎空壳 + Web Server。
// 用法：sm_headless [port=8080]
// 浏览器访问 http://<服务器IP>:<port> 即可远程管控。
// 开机自启：注册为 Windows Service 或 systemd 单元。
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "core/envelope.h"
#include "core/msg_bus.h"
#include "core/op_dict.h"
#include "engines/engine_registry.h"
#include "engines/plugins/engine_noop.h"
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

  // 注册 Ctrl+C / SIGTERM
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  // ---- 内核初始化 ----
  sm::MsgBus bus;
  sm::HeartbeatMonitor hb;

  // 注册所有引擎到消息总线（Phase 1 空壳）
  std::vector<std::unique_ptr<sm::stub::NoopEngine>> engines;
  for (const auto& desc : sm::engine_registry()) {
    auto eng = std::make_unique<sm::stub::NoopEngine>(desc.id);
    EngineConfig cfg;
    eng->init(cfg);
    eng->start();
    eng->setSink(nullptr);  // 空壳不产生事件
    engines.push_back(std::move(eng));
    hb.note_heartbeat(desc.id, 0);  // 初始在线
    std::printf("[engine] %s online (%s)\n", desc.id, desc.module);
  }

  // 总线默认 sink：打印未路由的消息
  bus.set_default_sink([](const sm::Envelope& e) {
    // 远程指令到达总线后由引擎处理；空壳不处理，仅记录
    if (e.type == "cmd")
      std::printf("[bus] cmd %s -> %s\n", e.op.c_str(), e.dst.c_str());
  });

  // ---- Web 远控层 ----
  sm::web::Auth auth;
  auth.init_defaults();

  sm::web::RemoteQueue queue(256);

  sm::web::WebGateway gateway(auth, queue, hb);
  sm::web::HttpServer server;

  // 状态提供者：返回运行态 JSON
  gateway.set_status_provider([]() -> std::string {
    return R"({"mode":"headless","engines_running":7})";
  });
  gateway.set_playlist_provider([]() -> std::string {
    return R"({"items":[],"current_index":-1})";
  });

  gateway.register_routes(server);

  // 注册总线事件 → WebSocket 广播
  bus.register_sink("*", [&](const sm::Envelope& e) {
    // 引擎事件推给 WebSocket 客户端
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
      // 构造信封并投递总线
      nlohmann::json params = nlohmann::json::parse(cmd.params_json, nullptr, false);
      if (params.is_null()) params = nlohmann::json::object();

      // 根据 op 命名空间决定 dst
      std::string dst = "engine.media";  // 默认
      const char* ns = sm::op_namespace(cmd.op);
      if (ns) {
        std::string n = ns;
        if (n == "media" || n == "transport") dst = "engine.media";
        else if (n == "timeline") dst = "engine.timeline";
        else if (n == "scene" || n == "playlist") dst = "engine.timeline";
        else if (n == "sys") dst = "engine.ui";
      }

      sm::Envelope env = sm::make_cmd(cmd.src, dst, cmd.op, params);
      bus.post(env);
    });

    // 心跳 tick（每 2 秒检查一次）
    auto now = std::time(nullptr);
    if (now - last_tick >= 2) {
      hb.tick(now * 1000);
      // 空壳引擎保持心跳（真实引擎由各自线程上报）
      for (const auto& desc : sm::engine_registry())
        hb.note_heartbeat(desc.id, now * 1000);
      last_tick = now;
    }

    // 避免空转
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // ---- 清理 ----
  std::printf("\n[shutdown] 停止引擎...\n");
  for (auto& eng : engines)
    eng->stop();
  server.stop();
  sm::web::net_shutdown();
  std::printf("[shutdown] 完成\n");
  return 0;
}

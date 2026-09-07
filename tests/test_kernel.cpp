// Kernel 集成测试：验证 M1-M5 引擎互连 + 总线路由 + 回调链
#include <string>
#include <vector>

#include "engines/kernel.h"
#include "engines/media/media_engine.h"
#include "engines/playlist/playlist_executor.h"
#include "engines/scene/scene_store.h"
#include "engines/timeline/timeline_scheduler.h"
#include "test_common.h"

namespace {

using namespace sm;
using nlohmann::json;

// ---- Kernel 初始化后所有引擎在线 ----
void test_kernel_init() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  // 时间线调度器有 8 轨道
  SM_CHECK_EQ(kernel.timeline().scheduler().track_count(), std::size_t(8));
  // 媒体引擎在 idle
  SM_CHECK(kernel.media().state() == media::PlayState::idle);
  // 场景库为空
  SM_CHECK_EQ(kernel.scene().count(), std::size_t(0));
  // 节目单在 idle
  SM_CHECK(kernel.playlist().state() == playlist::ExecState::idle);

  kernel.stop();
}

// ---- 总线路由：timeline.* → engine.timeline ----
void test_bus_timeline_route() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  // 发送 transport.state 指令
  json rsp;
  kernel.bus().register_sink("test_client", [&](const Envelope& env) {
    if (env.type == "rsp") {
      rsp = env.params;
    }
  });

  auto env = sm::make_cmd("test_client", "engine.timeline",
                          "transport.state", json::object());
  kernel.bus().post(env);

  // 等待响应
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  SM_CHECK(rsp.contains("state"));
  SM_CHECK(rsp["state"].get<std::string>() == "stopped");

  kernel.stop();
}

// ---- 总线路由：scene.* → engine.scene ----
void test_bus_scene_route() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  json rsp;
  kernel.bus().register_sink("test_client", [&](const Envelope& env) {
    if (env.type == "rsp") rsp = env.params;
  });

  // 保存场景
  auto env = sm::make_cmd("test_client", "engine.scene", "scene.save",
      {{"scene_name", "测试场景"}, {"fade_ms", 500}, {"recall_mode", "fade"}});
  kernel.bus().post(env);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  SM_CHECK(rsp.contains("scene_id"));
  std::string scene_id = rsp["scene_id"].get<std::string>();
  SM_CHECK(!scene_id.empty());
  SM_CHECK_EQ(kernel.scene().count(), std::size_t(1));

  kernel.stop();
}

// ---- 总线路由：playlist.* → engine.playlist ----
void test_bus_playlist_route() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  json rsp;
  kernel.bus().register_sink("test_client", [&](const Envelope& env) {
    if (env.type == "rsp") rsp = env.params;
  });

  // 载入节目单
  json items = json::array();
  items.push_back({
    {"sort_index", 0}, {"type", "scene"}, {"ref_uuid", "scn1"},
    {"trigger", "auto"}
  });
  auto env = sm::make_cmd("test_client", "engine.playlist", "playlist.load",
      {{"items", items}});
  kernel.bus().post(env);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  SM_CHECK(rsp.contains("item_count"));
  SM_CHECK_EQ(rsp["item_count"].get<int>(), 1);

  kernel.stop();
}

// ---- 总线路由：media.* → engine.media ----
void test_bus_media_route() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  json rsp;
  kernel.bus().register_sink("test_client", [&](const Envelope& env) {
    if (env.type == "rsp") rsp = env.params;
  });

  // 查询素材库
  auto env = sm::make_cmd("test_client", "engine.media", "media.query",
      json::object());
  kernel.bus().post(env);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  SM_CHECK(rsp.contains("total"));
  SM_CHECK_EQ(rsp["total"].get<int>(), 0);

  kernel.stop();
}

// ---- 快捷操作：scene_go ----
void test_scene_go() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  // 先保存一个场景
  std::string id = kernel.scene().save("测试", "", 300, "fade");
  SM_CHECK(!id.empty());

  // 快捷召回
  SM_CHECK(kernel.scene_go(id, 500));

  kernel.stop();
}

// ---- 快捷操作：transport_play / stop ----
void test_transport_control() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  SM_CHECK(kernel.transport_play());
  SM_CHECK(kernel.timeline().scheduler().play_state() ==
           timeline::PlayState::playing);

  SM_CHECK(kernel.transport_pause());
  SM_CHECK(kernel.timeline().scheduler().play_state() ==
           timeline::PlayState::paused);

  SM_CHECK(kernel.transport_resume());
  SM_CHECK(kernel.timeline().scheduler().play_state() ==
           timeline::PlayState::playing);

  SM_CHECK(kernel.transport_stop());
  SM_CHECK(kernel.timeline().scheduler().play_state() ==
           timeline::PlayState::stopped);

  kernel.stop();
}

// ---- 状态查询 JSON ----
void test_status_json() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  std::string status = kernel.get_status_json();
  SM_CHECK(!status.empty());

  auto j = json::parse(status);
  SM_CHECK_EQ(j["mode"].get<std::string>(), std::string("headless"));
  SM_CHECK(j.contains("play_state"));
  SM_CHECK(j.contains("media_state"));
  SM_CHECK(j.contains("playlist_state"));
  SM_CHECK(j.contains("engines"));

  kernel.stop();
}

// ---- 节目单 JSON ----
void test_playlist_json() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  std::string pl = kernel.get_playlist_json();
  SM_CHECK(!pl.empty());

  auto j = json::parse(pl);
  SM_CHECK(j.contains("state"));
  SM_CHECK(j.contains("items"));

  kernel.stop();
}

// ---- 引擎间回调链：Timeline → MediaEngine ----
void test_callback_chain() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  // 设置 MediaEngine 回调（监测是否被 Timeline 调用）
  bool media_called = false;
  kernel.media().set_open_cb([&](const std::string&, int64_t) {
    media_called = true;
    return true;
  });
  kernel.media().set_start_cb([&](double, int64_t) {});
  kernel.media().set_stop_cb([&](int64_t) {});

  // 在时间线上插入一个媒体条目
  json rsp;
  kernel.bus().register_sink("test_client", [&](const Envelope& env) {
    if (env.type == "rsp") rsp = env.params;
  });

  auto env = sm::make_cmd("test_client", "engine.timeline", "timeline.item_insert",
      {{"track_index", 0}, {"start_ms", 0}, {"duration_ms", 100},
       {"ref_type", "media"}, {"ref_uuid", "test_media"}});
  kernel.bus().post(env);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  SM_CHECK(rsp.contains("item_id"));

  // 开始播放
  rsp.clear();
  env = sm::make_cmd("test_client", "engine.timeline", "transport.play",
      {{"from_ms", 0}});
  kernel.bus().post(env);

  // 等待调度线程 tick 几次
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // MediaEngine 应该被 TimelineEngine 调用 preload → play
  SM_CHECK(media_called);

  kernel.transport_stop();
  kernel.stop();
}

}  // namespace

int main() {
  test_kernel_init();
  test_bus_timeline_route();
  test_bus_scene_route();
  test_bus_playlist_route();
  test_bus_media_route();
  test_scene_go();
  test_transport_control();
  test_status_json();
  test_playlist_json();
  test_callback_chain();
  return smtest::finish("test_kernel");
}

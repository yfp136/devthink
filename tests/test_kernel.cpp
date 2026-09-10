// Kernel 集成测试：验证 M1-M5 引擎互连 + 总线路由 + 回调链
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/error_codes.h"
#include "core/op_dict.h"
#include "engines/engine_registry.h"
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

// ---- 总线探针：抓取最后一次 rsp/err 与全部 evt ----
// 注意：sink 可能被引擎自带线程（tick 线程广播 evt.transport.clock）并发调用，
// 故内部访问一律加锁。
struct BusProbe {
  json params;               // 最后一次 rsp/err 的载荷
  int code = -1;             // 最后一次 rsp/err 的错误码
  std::string type;          // "rsp" / "err"
  std::vector<std::pair<std::string, json>> events;

  void attach(Kernel& k) {
    k.bus().register_sink("test_client", [this](const Envelope& e) {
      std::lock_guard<std::mutex> lk(mu);
      if (e.type == "rsp" || e.type == "err") {
        params = e.params;
        code = e.code;
        type = e.type;
      } else if (e.type == "evt") {
        events.emplace_back(e.op, e.params);
      }
    });
  }
  void reset() {
    std::lock_guard<std::mutex> lk(mu);
    params = json();
    code = -1;
    type.clear();
    events.clear();
  }
  bool saw(const std::string& op) const {
    std::lock_guard<std::mutex> lk(mu);
    for (const auto& e : events)
      if (e.first == op) return true;
    return false;
  }
  json evt(const std::string& op) const {
    std::lock_guard<std::mutex> lk(mu);
    for (const auto& e : events)
      if (e.first == op) return e.second;
    return json::object();
  }
  // 同名事件出现次数（up/down/heartbeat 是「逐引擎」广播，需按条计数）
  std::size_t count(const std::string& op) const {
    std::lock_guard<std::mutex> lk(mu);
    std::size_t n = 0;
    for (const auto& e : events)
      if (e.first == op) ++n;
    return n;
  }
  // 同名事件的全部载荷（按到达顺序）
  std::vector<json> all(const std::string& op) const {
    std::lock_guard<std::mutex> lk(mu);
    std::vector<json> v;
    for (const auto& e : events)
      if (e.first == op) v.push_back(e.second);
    return v;
  }

 private:
  mutable std::mutex mu;
};

// 发一条 cmd 并等待同步响应（总线同步派发，留 80ms 余量覆盖调度线程）
void post_cmd(Kernel& k, BusProbe& probe, const std::string& dst,
              const std::string& op, const json& p = json::object()) {
  probe.reset();
  k.bus().post(make_cmd("test_client", dst, op, p));
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
}

// ---- §5.2 sink 生命周期：注销语义 + 内核析构摘除 ----
// 回归背景：Kernel 成员析构仍会产生事件（~PlaylistExecutor → stop() →
// evt.playlist.ended 广播）。若内核未在析构时摘除 sink，外部已析构对象的
// 回调会被命中（历史上表现为 pthread_mutex_lock 对已销毁互斥量 → EINVAL，
// libc++abi 抛 std::system_error 后 abort）。
void test_bus_sink_lifetime() {
  // 1) unregister_sink：注销后不再回调，重复注销返回 false
  MsgBus bus;
  int hits = 0;
  bus.register_sink("engine.media", [&](const Envelope&) { ++hits; });
  bus.post(make_cmd("ui", "engine.media", "media.play"));
  SM_CHECK_EQ(hits, 1);

  SM_CHECK(bus.unregister_sink("engine.media"));
  bus.post(make_cmd("ui", "engine.media", "media.play"));
  SM_CHECK_EQ(hits, 1);
  SM_CHECK(!bus.unregister_sink("engine.media"));

  // 2) Kernel 析构摘除 sink：析构期事件不得再派发给外部 sink
  auto evt_hits = std::make_shared<int>(0);
  {
    Kernel kernel;
    kernel.init();
    kernel.bus().register_sink("test_client",
                               [evt_hits](const Envelope& e) {
                                 if (e.type == "evt") ++*evt_hits;
                               });
    kernel.bus().post(make_event("engine.media", "evt.log"));
    SM_CHECK_EQ(*evt_hits, 1);
    // 离开作用域：Kernel 析构（先 stop() 再 clear_sinks()，随后引擎成员析构）
  }
  SM_CHECK_EQ(*evt_hits, 1);  // 析构期间未产生额外派发
}

// ---- §5.3 / §5.4：内核控制面 sys.ping ----
void test_sys_ping() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);
  post_cmd(kernel, probe, "engine.core", "sys.ping");

  SM_CHECK_EQ(probe.type, std::string("rsp"));
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(probe.params["pong"], true);
  SM_CHECK(probe.params.value("monotonic_ms", int64_t(0)) > 0);

  kernel.stop();
}

// ---- §5.3 sys.set_volume：ch 白名单 + gain_db ∈ [-60, 6]，越界一律 1002 ----
void test_sys_set_volume_bounds() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  // 三个合法通道
  for (const char* ch : {"master", "music", "mic"}) {
    post_cmd(kernel, probe, "engine.media", "sys.set_volume",
             {{"ch", std::string(ch)}, {"gain_db", -6.0}});
    SM_CHECK_EQ(probe.code, ec::OK);
    SM_CHECK_EQ(probe.params["ch"], std::string(ch));
  }

  // 边界值 -60 / 6 合法
  post_cmd(kernel, probe, "engine.media", "sys.set_volume",
           {{"ch", "master"}, {"gain_db", -60.0}});
  SM_CHECK_EQ(probe.code, ec::OK);
  post_cmd(kernel, probe, "engine.media", "sys.set_volume",
           {{"ch", "master"}, {"gain_db", 6.0}});
  SM_CHECK_EQ(probe.code, ec::OK);

  // 越界 → 1002
  post_cmd(kernel, probe, "engine.media", "sys.set_volume",
           {{"ch", "master"}, {"gain_db", -61.0}});
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);
  post_cmd(kernel, probe, "engine.media", "sys.set_volume",
           {{"ch", "master"}, {"gain_db", 7.0}});
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);

  // 非法通道 → 1002
  post_cmd(kernel, probe, "engine.media", "sys.set_volume",
           {{"ch", "monitor"}, {"gain_db", 0.0}});
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);

  // 非数值 / 缺字段 → 1002
  post_cmd(kernel, probe, "engine.media", "sys.set_volume",
           {{"ch", "master"}, {"gain_db", "loud"}});
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);
  post_cmd(kernel, probe, "engine.media", "sys.set_volume", {{"ch", "master"}});
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);

  kernel.stop();
}

// ---- §5.4 sys.get_state：rsp + evt.state.snapshot 全量快照 ----
void test_sys_get_state_snapshot() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);
  post_cmd(kernel, probe, "engine.core", "sys.get_state");

  SM_CHECK_EQ(probe.type, std::string("rsp"));
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK(probe.saw("evt.state.snapshot"));

  json snap = probe.evt("evt.state.snapshot");
  SM_CHECK(snap.contains("play_state"));
  SM_CHECK(snap.contains("media_state"));
  SM_CHECK(snap.contains("playlist_state"));
  SM_CHECK(snap.contains("engines"));
  SM_CHECK_EQ(snap["mode"], std::string("headless"));

  kernel.stop();
}

// ---- §9.5 [1014] remote.hello：握手应答携协议版本 + 能力表 ----
void test_remote_hello() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);
  post_cmd(kernel, probe, "engine.core", "remote.hello", {{"v", 1}});

  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(probe.params.value("v", -1), kEnvelopeVersion);
  SM_CHECK_EQ(probe.params.value("tcp_port", 0), 9000);
  SM_CHECK(probe.params["capabilities"].is_array());
  // 能力表逐条对应 §5.3 指令字典（33 条）
  SM_CHECK_EQ(probe.params["capabilities"].size(), cmd_ops().size());
  SM_CHECK_EQ(probe.params["capabilities"].size(), std::size_t(33));

  kernel.stop();
}

// ---- §5.3 路由：未知 op 落到 engine.core → 1001 ----
void test_engine_core_unknown_op() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);
  // media.query 是合法 op，但不属于 engine.core 命名空间
  post_cmd(kernel, probe, "engine.core", "media.query");

  SM_CHECK_EQ(probe.type, std::string("err"));
  SM_CHECK_EQ(probe.code, ec::UNKNOWN_OP);

  kernel.stop();
}

// ---- §10.3.3 [1171] scene.delete：1002 / 5001 / 5003 引用保护 ----
void test_scene_delete_error_codes() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  // 1) 缺 scene_id → 1002
  post_cmd(kernel, probe, "engine.scene", "scene.delete", json::object());
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);

  // 2) 场景不存在 → 5001
  post_cmd(kernel, probe, "engine.scene", "scene.delete",
           {{"scene_id", "scn_none"}});
  SM_CHECK_EQ(probe.code, ec::SCENE_NOT_FOUND);

  // 3) 被时间线条目引用 → 5003，并回传被引用条目明细
  std::string scene_id = kernel.scene().save("被引用场景", "", 300, "fade");
  SM_CHECK(!scene_id.empty());

  post_cmd(kernel, probe, "engine.timeline", "timeline.item_insert",
           {{"track_index", 0}, {"start_ms", 0}, {"duration_ms", 100},
            {"ref_type", "scene"}, {"ref_uuid", scene_id}});
  SM_CHECK_EQ(probe.code, ec::OK);
  std::string item_id = probe.params.value("item_id", std::string());
  SM_CHECK(!item_id.empty());

  post_cmd(kernel, probe, "engine.scene", "scene.delete",
           {{"scene_id", scene_id}});
  SM_CHECK_EQ(probe.code, ec::REFERENCED_OBJECT_MISSING);
  SM_CHECK(probe.params.contains("referencing_ids"));
  bool found = false;
  if (probe.params.contains("referencing_ids"))
    for (const auto& id : probe.params["referencing_ids"])
      if (id.get<std::string>() == item_id) found = true;
  SM_CHECK(found);
  SM_CHECK_EQ(kernel.scene().count(), std::size_t(1));  // 拒绝时场景仍在

  // 4) 解除引用后可正常删除
  post_cmd(kernel, probe, "engine.timeline", "timeline.item_remove",
           {{"item_ids", json::array({item_id})}});
  SM_CHECK_EQ(probe.code, ec::OK);
  post_cmd(kernel, probe, "engine.scene", "scene.delete",
           {{"scene_id", scene_id}});
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(kernel.scene().count(), std::size_t(0));

  kernel.stop();
}

// ---- §10.1.4 media.purge：2001 / 1002 / 5003 引用保护 ----
void test_media_purge_error_codes() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  // 用纯逻辑桩替换平台回调，避免单测依赖真实文件与目录
  kernel.media_lib().set_stat_cb([](const auto&) { return true; });
  kernel.media_lib().set_copy_cb([](const auto&, const auto&) { return true; });
  kernel.media_lib().set_probe_cb([](const auto&) { return json{}; });

  // 1) 缺 media_id → 1002
  post_cmd(kernel, probe, "engine.media", "media.purge", json::object());
  SM_CHECK_EQ(probe.code, ec::BAD_PARAM);

  // 2) 素材不存在 → 2001
  post_cmd(kernel, probe, "engine.media", "media.purge",
           {{"media_id", "med_none"}});
  SM_CHECK_EQ(probe.code, ec::FILE_MISSING);

  // 3) 导入一个素材（走总线，覆盖 media.import 路径）
  post_cmd(kernel, probe, "engine.media", "media.import",
           {{"paths", json::array({"/tmp/purge_test.mp4"})}});
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK(probe.params.contains("results"));
  std::string media_id;
  if (probe.params.contains("results") && probe.params["results"].is_array() &&
      !probe.params["results"].empty())
    media_id = probe.params["results"][0].value("media_id", std::string());
  SM_CHECK(!media_id.empty());
  SM_CHECK(probe.saw("evt.media.import_done"));

  // 4) 被时间线条目引用 → 5003，message 含被引用明细，且记录未被删
  post_cmd(kernel, probe, "engine.timeline", "timeline.item_insert",
           {{"track_index", 1}, {"start_ms", 0}, {"duration_ms", 1000},
            {"ref_type", "media"}, {"ref_uuid", media_id}});
  SM_CHECK_EQ(probe.code, ec::OK);
  std::string item_id = probe.params.value("item_id", std::string());
  SM_CHECK(!item_id.empty());

  post_cmd(kernel, probe, "engine.media", "media.purge",
           {{"media_id", media_id}});
  SM_CHECK_EQ(probe.code, ec::REFERENCED_OBJECT_MISSING);
  SM_CHECK(probe.params.contains("message"));
  SM_CHECK(probe.params["message"].get<std::string>().find("被引用明细") !=
           std::string::npos);
  SM_CHECK_EQ(kernel.media_lib().count(), std::size_t(1));

  // 5) 无引用后物理删除成功（同时校验 §10.3.2 stored_path 语义）
  auto rec = kernel.media_lib().find(media_id);
  SM_CHECK(rec != nullptr);
  if (rec)
    SM_CHECK_EQ(kernel.media_lib().stored_path(media_id),
                kernel.media_lib().media_root() + "/" + rec->stored_name);
  post_cmd(kernel, probe, "engine.timeline", "timeline.item_remove",
           {{"item_ids", json::array({item_id})}});
  SM_CHECK_EQ(probe.code, ec::OK);
  post_cmd(kernel, probe, "engine.media", "media.purge",
           {{"media_id", media_id}});
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(kernel.media_lib().count(), std::size_t(0));

  kernel.stop();
}

// ---- §5.3 / §10.6：transport.* 经 engine.timeline 全可达 + seek 3003 ----
void test_transport_ops_route() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  post_cmd(kernel, probe, "engine.timeline", "timeline.item_insert",
           {{"track_index", 2}, {"start_ms", 0}, {"duration_ms", 500},
            {"ref_type", "media"}, {"ref_uuid", "m_none"}});
  SM_CHECK_EQ(probe.code, ec::OK);

  // 状态机：stopped → playing → paused → playing → stopped
  post_cmd(kernel, probe, "engine.timeline", "transport.play",
           {{"from_ms", 0}});
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(probe.params["state"], std::string("playing"));
  SM_CHECK(probe.saw("evt.transport.state"));

  post_cmd(kernel, probe, "engine.timeline", "transport.pause");
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(probe.params["state"], std::string("paused"));

  post_cmd(kernel, probe, "engine.timeline", "transport.resume");
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(probe.params["state"], std::string("playing"));

  // §5.5：播放头越界 → 3003；边界内 → 0
  post_cmd(kernel, probe, "engine.timeline", "transport.seek",
           {{"to_ms", 600}});
  SM_CHECK_EQ(probe.code, ec::PLAYHEAD_OUT_OF_RANGE);
  post_cmd(kernel, probe, "engine.timeline", "transport.seek",
           {{"to_ms", 500}});
  SM_CHECK_EQ(probe.code, ec::OK);
  // 桌面 UI 兼容写法 pos_ms
  post_cmd(kernel, probe, "engine.timeline", "transport.seek",
           {{"pos_ms", 100}});
  SM_CHECK_EQ(probe.code, ec::OK);

  post_cmd(kernel, probe, "engine.timeline", "transport.stop");
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK_EQ(probe.params["state"], std::string("stopped"));

  kernel.stop();
}

// ---- §5.4 evt.playlist.playing / advance：起播→推进闭环 + playlist_id 注入 ----
// W1 要求 §5.4 每条事件都有代码入口并能经总线收发；本例覆盖此前缺失的两条：
// playing {playlist_id, index, item_id} 与 advance {index, trigger_mode}。
void test_playlist_playing_advance_events() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  // 两条 scene 条目：场景召回是瞬时动作（is_current_done 立即为真），
  // 故 tick 一次即可观测到 advance → 下一项 playing 的完整闭环。
  json items = json::array();
  items.push_back({{"sort_index", 0}, {"type", "scene"}, {"ref_uuid", "scn_a"},
                   {"trigger", "auto"}});
  items.push_back({{"sort_index", 1}, {"type", "scene"}, {"ref_uuid", "scn_b"},
                   {"trigger", "auto"}});
  post_cmd(kernel, probe, "engine.playlist", "playlist.load",
           {{"playlist_id", "pl_demo"}, {"items", items}});
  SM_CHECK_EQ(probe.code, ec::OK);
  // 宿主在 load 时把指令里的 playlist_id 注入执行器（M4 不持节目单库）
  SM_CHECK_EQ(kernel.playlist().playlist_id(), std::string("pl_demo"));

  // start：current_index_=0 → execute_current → playing{index 0}
  post_cmd(kernel, probe, "engine.playlist", "playlist.start");
  SM_CHECK_EQ(probe.code, ec::OK);
  SM_CHECK(probe.saw("evt.playlist.playing"));
  json playing0 = probe.evt("evt.playlist.playing");
  SM_CHECK_EQ(playing0["playlist_id"], std::string("pl_demo"));
  SM_CHECK_EQ(playing0["index"].get<int>(), 0);
  SM_CHECK(!playing0["item_id"].get<std::string>().empty());

  // tick：当前条目已完成 → advance{index 1} → 下一项 playing{index 1}
  probe.reset();
  kernel.playlist().tick(1000);
  SM_CHECK(probe.saw("evt.playlist.advance"));
  json adv = probe.evt("evt.playlist.advance");
  SM_CHECK_EQ(adv["index"].get<int>(), 1);
  SM_CHECK_EQ(adv["trigger_mode"], std::string("auto"));
  json playing1 = probe.evt("evt.playlist.playing");
  SM_CHECK_EQ(playing1["index"].get<int>(), 1);
  SM_CHECK_EQ(playing1["playlist_id"], std::string("pl_demo"));

  // 末项结束后：loop_mode=none → ended{reason:"end"}（状态机 running → ended）
  probe.reset();
  kernel.playlist().tick(2000);
  SM_CHECK(probe.saw("evt.playlist.ended"));
  SM_CHECK_EQ(probe.evt("evt.playlist.ended")["reason"], std::string("end"));
  SM_CHECK(kernel.playlist().state() == playlist::ExecState::ended);

  kernel.stop();
}

// ---- §5.4 evt.transport.bpm：去重 + 非法值丢弃 + confidence 夹取 ----
void test_transport_bpm_event() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  // 首次上报 → 广播 {bpm, confidence}
  probe.reset();
  kernel.timeline().report_bpm(128.0, 0.9);
  SM_CHECK(probe.saw("evt.transport.bpm"));
  json bpm = probe.evt("evt.transport.bpm");
  SM_CHECK_EQ(bpm["bpm"].get<double>(), 128.0);
  SM_CHECK_EQ(bpm["confidence"].get<double>(), 0.9);

  // 同值重发 → 去重（MIDI 时钟 24ppq 会把同一 BPM 反复上报）
  probe.reset();
  kernel.timeline().report_bpm(128.0, 0.9);
  SM_CHECK(!probe.saw("evt.transport.bpm"));

  // confidence 变化即视为新值
  probe.reset();
  kernel.timeline().report_bpm(128.0, 0.5);
  SM_CHECK(probe.saw("evt.transport.bpm"));

  // bpm<=0 一律丢弃（检测器冷启动阶段会给出 0 或负值）
  probe.reset();
  kernel.timeline().report_bpm(0.0, 1.0);
  kernel.timeline().report_bpm(-10.0, 1.0);
  SM_CHECK(!probe.saw("evt.transport.bpm"));

  // confidence 越界夹取到 [0,1]
  probe.reset();
  kernel.timeline().report_bpm(140.0, 5.0);
  SM_CHECK(probe.saw("evt.transport.bpm"));
  SM_CHECK_EQ(probe.evt("evt.transport.bpm")["confidence"].get<double>(), 1.0);
  probe.reset();
  kernel.timeline().report_bpm(141.0, -3.0);
  SM_CHECK(probe.saw("evt.transport.bpm"));
  SM_CHECK_EQ(probe.evt("evt.transport.bpm")["confidence"].get<double>(), 0.0);

  kernel.stop();
}

// ---- §5.4 evt.engine.up / down / heartbeat：边沿语义 + 逐引擎载荷契约 ----
void test_engine_heartbeat_events() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);
  // 注入确定性采样源（真实宿主用看门狗 RSS；此处固定值以便断言载荷）
  kernel.set_engine_metrics([]() { return 12.5; },
                            []() -> std::size_t { return 2048; });

  const std::size_t n_engines = engine_registry().size();
  SM_CHECK(n_engines > 0);

  // 第一次 tick：注册表内每个引擎首次判为在线 → 逐引擎发一次 up（边沿翻转）
  probe.reset();
  kernel.pump_heartbeat(1000);
  SM_CHECK_EQ(probe.count("evt.engine.up"), n_engines);
  SM_CHECK_EQ(probe.count("evt.engine.down"), std::size_t(0));
  SM_CHECK_EQ(probe.count("evt.engine.heartbeat"), n_engines);

  // up 载荷 = {engine_id}，且 id 全部来自注册表、无重复
  std::set<std::string> up_ids, reg_ids;
  for (const auto& desc : engine_registry()) reg_ids.insert(desc.id);
  for (const auto& p : probe.all("evt.engine.up"))
    up_ids.insert(p.value("engine_id", std::string()));
  SM_CHECK_EQ(up_ids.size(), n_engines);
  SM_CHECK(up_ids == reg_ids);

  // heartbeat 载荷 = {engine_id, load_pct, mem_mb}
  for (const auto& p : probe.all("evt.engine.heartbeat")) {
    SM_CHECK(p.contains("engine_id") && p.contains("load_pct") &&
             p.contains("mem_mb"));
    SM_CHECK_EQ(p["load_pct"].get<double>(), 12.5);
    SM_CHECK_EQ(p["mem_mb"].get<std::size_t>(), std::size_t(2048));
    SM_CHECK(reg_ids.count(p["engine_id"].get<std::string>()) == 1);
  }

  // 第二次 tick：在线态无变化 → 不再发 up（up/down 仅边沿触发），heartbeat 照发
  probe.reset();
  kernel.pump_heartbeat(2000);
  SM_CHECK_EQ(probe.count("evt.engine.up"), std::size_t(0));
  SM_CHECK_EQ(probe.count("evt.engine.down"), std::size_t(0));
  SM_CHECK_EQ(probe.count("evt.engine.heartbeat"), n_engines);

  // down 边沿：Phase 1 注册表静态（每 tick 都会替注册表内引擎记心跳），
  // 故 down 用非注册表 id 演练 —— 令其上线后再停跳，越过
  // kIntervalMs*kMaxLost = 6000ms 失联窗口即翻转为 down。
  kernel.heartbeat().note_heartbeat("engine.ghost", 1000);
  probe.reset();
  kernel.pump_heartbeat(1000);  // ghost 首次在线 → up
  SM_CHECK_EQ(probe.count("evt.engine.up"), std::size_t(1));
  SM_CHECK_EQ(probe.evt("evt.engine.up")["engine_id"], std::string("engine.ghost"));

  probe.reset();
  kernel.pump_heartbeat(8000);  // ghost 距末次心跳 7000ms > 6000ms → 失联
  SM_CHECK_EQ(probe.count("evt.engine.down"), std::size_t(1));
  json down = probe.evt("evt.engine.down");
  SM_CHECK_EQ(down["engine_id"], std::string("engine.ghost"));
  // 失联引擎不再发 heartbeat；注册表内 7 个仍在线
  SM_CHECK_EQ(probe.count("evt.engine.heartbeat"), n_engines);

  kernel.stop();
}

// ---- §5.4 evt.log {level, msg}：内核统一日志入口（宿主诊断并入总线） ----
void test_log_event() {
  Kernel kernel;
  kernel.init();
  kernel.start();

  BusProbe probe;
  probe.attach(kernel);

  for (const char* level : {"debug", "info", "warn", "error"}) {
    probe.reset();
    kernel.log_event(level, std::string("测试日志 ") + level);
    SM_CHECK_MSG(probe.saw("evt.log"), std::string("evt.log 应可观测: ") + level);
    json l = probe.evt("evt.log");
    SM_CHECK_EQ(l["level"], std::string(level));
    SM_CHECK_EQ(l["msg"], std::string("测试日志 ") + level);
  }

  kernel.stop();
}

}  // namespace

int main(int argc, char** argv) {
  struct Case {
    const char* name;
    void (*fn)();
  };
  // 用例登记表：序号即「单跑」时的命令行参数（./test_kernel 12 只跑第 12 例）
  static const Case kCases[] = {
      {"test_kernel_init", test_kernel_init},
      {"test_bus_timeline_route", test_bus_timeline_route},
      {"test_bus_scene_route", test_bus_scene_route},
      {"test_bus_playlist_route", test_bus_playlist_route},
      {"test_bus_media_route", test_bus_media_route},
      {"test_scene_go", test_scene_go},
      {"test_transport_control", test_transport_control},
      {"test_status_json", test_status_json},
      {"test_playlist_json", test_playlist_json},
      {"test_callback_chain", test_callback_chain},
      {"test_sys_ping", test_sys_ping},
      {"test_sys_set_volume_bounds", test_sys_set_volume_bounds},
      {"test_sys_get_state_snapshot", test_sys_get_state_snapshot},
      {"test_remote_hello", test_remote_hello},
      {"test_engine_core_unknown_op", test_engine_core_unknown_op},
      {"test_scene_delete_error_codes", test_scene_delete_error_codes},
      {"test_media_purge_error_codes", test_media_purge_error_codes},
      {"test_transport_ops_route", test_transport_ops_route},
      {"test_bus_sink_lifetime", test_bus_sink_lifetime},
      {"test_playlist_playing_advance_events", test_playlist_playing_advance_events},
      {"test_transport_bpm_event", test_transport_bpm_event},
      {"test_engine_heartbeat_events", test_engine_heartbeat_events},
      {"test_log_event", test_log_event},
  };
  const int only = (argc > 1) ? std::atoi(argv[1]) : -1;

  for (std::size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
    if (only > 0 && static_cast<int>(i) + 1 != only) continue;
    // stderr 无缓冲：用例崩溃时也能看到「最后一个进入的用例」
    std::fprintf(stderr, "[case %02zu] %s\n", i + 1, kCases[i].name);
    std::fflush(stderr);
    kCases[i].fn();
  }
  return smtest::finish("test_kernel");
}

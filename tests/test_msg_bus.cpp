// 消息总线与心跳监控单元测试（规格 §5.2）
#include <cstdint>
#include <string>

#include "core/msg_bus.h"
#include "test_common.h"

namespace {

using sm::Envelope;
using sm::make_cmd;
using sm::make_event;

void test_route_exact_and_default() {
  sm::MsgBus bus;
  int got_media = 0, got_ghost = 0, got_default = 0;
  bus.register_sink("engine.media", [&](const Envelope&) { ++got_media; });
  bus.register_sink("engine.ghost", [&](const Envelope&) { ++got_ghost; });
  bus.set_default_sink([&](const Envelope&) { ++got_default; });

  bus.post(make_cmd("engine.ui", "engine.media", "media.play"));
  bus.post(make_cmd("engine.ui", "engine.ghost", "sys.ping"));
  bus.post(make_cmd("engine.ui", "engine.no_one", "sys.ping"));  // 未注册 → 默认

  SM_CHECK_EQ(got_media, 1);
  SM_CHECK_EQ(got_ghost, 1);
  SM_CHECK_EQ(got_default, 1);
  SM_CHECK_EQ(bus.posted_count(), std::size_t(3));
}

void test_broadcast() {
  sm::MsgBus bus;
  int sinks = 0, def = 0;
  bus.register_sink("engine.media", [&](const Envelope&) { ++sinks; });
  bus.register_sink("engine.timeline", [&](const Envelope&) { ++sinks; });
  bus.set_default_sink([&](const Envelope&) { ++def; });

  const Envelope evt = make_event("engine.media", "evt.transport.clock");
  SM_CHECK_EQ(evt.dst, std::string("*"));
  bus.post(evt);  // 广播：全部注册 sink + 默认 sink

  SM_CHECK_EQ(sinks, 2);
  SM_CHECK_EQ(def, 1);
}

void test_ring_recent() {
  sm::MsgBus bus;
  bus.post(make_cmd("a", "b", "sys.ping"));
  bus.post(make_cmd("a", "b", "sys.ping"));
  bus.post(make_cmd("a", "b", "sys.ping"));
  SM_CHECK_EQ(bus.recent(2).size(), std::size_t(2));   // 只取最近 2 条
  SM_CHECK_EQ(bus.recent(99).size(), std::size_t(3));  // 超出容量则全量
  bus.clear();
  SM_CHECK_EQ(bus.posted_count(), std::size_t(0));
  SM_CHECK(bus.recent(5).empty());
}

void test_heartbeat_miss_threshold() {
  sm::HeartbeatMonitor hb;
  hb.note_heartbeat("engine.media", 0);
  SM_CHECK(hb.online("engine.media"));

  hb.note_heartbeat("engine.media", 3000);  // 间隔 >2s：missed=1
  SM_CHECK(hb.online("engine.media"));
  hb.note_heartbeat("engine.media", 6000);  // missed=2
  SM_CHECK(hb.online("engine.media"));
  hb.note_heartbeat("engine.media", 9000);  // missed=3 → 离线
  SM_CHECK(!hb.online("engine.media"));
  SM_CHECK_EQ(hb.offline_engines().size(), std::size_t(1));

  hb.note_heartbeat("engine.media", 9500);  // 心跳恢复 → 自动上线
  SM_CHECK(hb.online("engine.media"));
  SM_CHECK(hb.offline_engines().empty());
}

void test_heartbeat_normal_gap_resets() {
  sm::HeartbeatMonitor hb;
  hb.note_heartbeat("engine.media", 0);
  hb.note_heartbeat("engine.media", 2000);  // 恰好 2s → 不丢
  SM_CHECK(hb.online("engine.media"));
  hb.note_heartbeat("engine.media", 4000);  // 间隔正常 → missed 归零
  SM_CHECK(hb.online("engine.media"));
}

void test_heartbeat_tick_timeout() {
  sm::HeartbeatMonitor hb;
  hb.note_heartbeat("engine.timeline", 1000);
  hb.tick(1000);  // 未超时
  SM_CHECK(hb.online("engine.timeline"));
  hb.tick(7000);  // 距上次 6000+1ms > 2s*3
  SM_CHECK(!hb.online("engine.timeline"));
  SM_CHECK_EQ(hb.offline_engines().size(), std::size_t(1));
}

}  // namespace

int main() {
  test_route_exact_and_default();
  test_broadcast();
  test_ring_recent();
  test_heartbeat_miss_threshold();
  test_heartbeat_normal_gap_resets();
  test_heartbeat_tick_timeout();
  return smtest::finish("test_msg_bus");
}

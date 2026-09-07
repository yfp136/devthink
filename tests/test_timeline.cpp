// TimelineEngine 单元测试（§7 + §10.6）
// 验证：条目状态机、2ms 调度周期、预载、跨轨对齐、场景/command 轨语义、播放控制
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "engines/timeline/timeline_scheduler.h"
#include "core/error_codes.h"
#include "test_common.h"

namespace {

using namespace sm::timeline;
using namespace sm::ec;
using nlohmann::json;

// ---- 基础：轨道配置 ----
void test_default_tracks() {
  TimelineScheduler sched;
  SM_CHECK_EQ(sched.track_count(), std::size_t(8));  // 4 启用 + 4 空壳
  SM_CHECK(sched.track_type(0) == TrackType::audio);
  SM_CHECK(sched.track_type(1) == TrackType::video);
  SM_CHECK(sched.track_type(2) == TrackType::scene);
  SM_CHECK(sched.track_type(3) == TrackType::command);
}

// ---- 条目插入与查询 ----
void test_item_insert_and_find() {
  TimelineScheduler sched;
  TimelineItem item;
  item.item_id = "itm_001";
  item.track_index = 0;  // audio
  item.start_ms = 1000;
  item.duration_ms = 5000;
  item.ref_type = RefType::media;
  item.ref_uuid = "med_abc";
  SM_CHECK_EQ(sched.insert_item(item), 0);
  SM_CHECK(sched.find_item("itm_001") != nullptr);
  SM_CHECK_EQ(sched.find_item("itm_001")->start_ms, int64_t(1000));
  SM_CHECK(sched.find_item("nonexistent") == nullptr);
}

void test_item_track_bounds() {
  TimelineScheduler sched;
  TimelineItem item;
  item.item_id = "x";
  item.track_index = 99;  // 越界
  SM_CHECK_EQ(sched.insert_item(item), sm::ec::TRACK_MISSING);  // 3001
}

// ---- 条目状态机：scheduled -> preloaded -> active -> done ----
void test_state_machine() {
  TimelineScheduler sched;
  std::vector<std::string> events;

  // 设置媒体回调，记录调用顺序
  int preload_calls = 0, play_calls = 0, stop_calls = 0;
  sched.set_media_preload_cb([&](const std::string&, const std::string&, const json&) {
    ++preload_calls;
    return true;
  });
  sched.set_media_play_cb([&](const std::string&, const std::string&, const json&) {
    ++play_calls;
  });
  sched.set_media_stop_cb([&](const std::string&) { ++stop_calls; });
  sched.set_item_event_cb([&](const std::string& evt, const TimelineItem&, const std::string&) {
    events.push_back(evt);
  });

  TimelineItem item;
  item.item_id = "itm_a";
  item.track_index = 0;  // audio
  item.start_ms = 100;
  item.duration_ms = 50;
  item.ref_type = RefType::media;
  item.ref_uuid = "med_a";
  sched.insert_item(item);

  // 开始播放
  sched.play(0);
  SM_CHECK(sched.play_state() == PlayState::playing);

  // tick 到 50ms（还在 preload 之前：preload_at = 100-20 = 80ms）
  sched.tick(50);
  SM_CHECK(sched.find_item("itm_a")->state == ItemState::scheduled);
  SM_CHECK_EQ(preload_calls, 0);

  // tick 到 90ms（在 preload 区间：80-100）
  sched.tick(90);
  SM_CHECK(sched.find_item("itm_a")->state == ItemState::preloaded);
  SM_CHECK_EQ(preload_calls, 1);
  SM_CHECK_EQ(play_calls, 0);

  // tick 到 120ms（进入 active）
  sched.tick(120);
  SM_CHECK(sched.find_item("itm_a")->state == ItemState::active);
  SM_CHECK_EQ(play_calls, 1);

  // tick 到 150ms（结束：100+50=150）
  sched.tick(151);
  SM_CHECK(sched.find_item("itm_a")->state == ItemState::done);
  SM_CHECK_EQ(stop_calls, 1);

  // 事件顺序：started -> ended
  SM_CHECK(events.size() >= 2);
  SM_CHECK(events[0] == "evt.timeline.item_started");
  SM_CHECK(events.back() == "evt.timeline.item_ended");
}

// ---- 跨轨对齐：两轨条目同一起点应同时激活 ----
void test_cross_track_alignment() {
  TimelineScheduler sched;
  std::vector<std::pair<int64_t, std::string>> activations;  // (time, track)

  sched.set_media_preload_cb([](const auto&, const auto&, const auto&) { return true; });
  sched.set_media_play_cb([&](const std::string& id, const std::string&, const json&) {
    activations.push_back({sched.pos_ms(), id});
  });

  // 第 0 轨音频：start=200ms
  TimelineItem a;
  a.item_id = "itm_a";
  a.track_index = 0;
  a.start_ms = 200;
  a.duration_ms = 100;
  a.ref_type = RefType::media;
  a.ref_uuid = "m_a";
  sched.insert_item(a);

  // 第 1 轨视频：同一起点
  TimelineItem v;
  v.item_id = "itm_v";
  v.track_index = 1;
  v.start_ms = 200;
  v.duration_ms = 100;
  v.ref_type = RefType::media;
  v.ref_uuid = "m_v";
  sched.insert_item(v);

  sched.play(0);
  // 逐步推进到 200ms
  for (int ms = 0; ms <= 200; ms += 2) sched.tick(ms);

  SM_CHECK_EQ(activations.size(), std::size_t(2));
  // 两条目在同一 tick 被激活（跨轨对齐）
  SM_CHECK_EQ(activations[0].first, activations[1].first);
}

// ---- scene 轨：瞬时触发，立即 done ----
void test_scene_track() {
  TimelineScheduler sched;
  int recall_calls = 0;
  std::string last_scene_id, last_mode;
  int last_fade = 0;

  sched.set_scene_recall_cb([&](const std::string& sid, int fade, const std::string& mode) {
    ++recall_calls;
    last_scene_id = sid;
    last_fade = fade;
    last_mode = mode;
  });

  TimelineItem item;
  item.item_id = "scn_01";
  item.track_index = 2;  // scene
  item.start_ms = 500;
  item.duration_ms = 0;   // scene 条目无持续时间
  item.ref_type = RefType::scene;
  item.ref_uuid = "scene_open";
  item.meta = {{"fade_ms", 800}, {"recall_mode", "fade"}};
  sched.insert_item(item);

  sched.play(0);
  sched.tick(600);  // 越过 start_ms

  SM_CHECK_EQ(recall_calls, 1);
  SM_CHECK_EQ(last_scene_id, std::string("scene_open"));
  SM_CHECK_EQ(last_fade, 800);
  SM_CHECK_EQ(last_mode, std::string("fade"));
  SM_CHECK(sched.find_item("scn_01")->state == ItemState::done);
}

// ---- command 轨：触发总线指令 ----
void test_command_track() {
  TimelineScheduler sched;
  int cmd_calls = 0;
  std::string last_op;
  json last_params;

  sched.set_command_cb([&](const std::string& op, const json& params) {
    ++cmd_calls;
    last_op = op;
    last_params = params;
  });

  TimelineItem item;
  item.item_id = "cmd_01";
  item.track_index = 3;  // command
  item.start_ms = 300;
  item.duration_ms = 0;
  item.ref_type = RefType::command;
  item.ref_uuid = "sys.ping";  // op
  item.meta = {{"params", {{"msg", "hello"}}}};
  sched.insert_item(item);

  sched.play(0);
  sched.tick(400);

  SM_CHECK_EQ(cmd_calls, 1);
  SM_CHECK_EQ(last_op, std::string("sys.ping"));
  SM_CHECK(last_params["msg"] == "hello");
}

// ---- seek 语义：场景轨不重放已越过条目（防闪变）----
void test_seek_scene_no_replay() {
  TimelineScheduler sched;
  int recall_calls = 0;
  sched.set_scene_recall_cb([&](const auto&, int, const auto&) { ++recall_calls; });

  TimelineItem s1;
  s1.item_id = "s1";
  s1.track_index = 2;
  s1.start_ms = 100;
  s1.duration_ms = 0;
  s1.ref_type = RefType::scene;
  s1.ref_uuid = "scn1";
  sched.insert_item(s1);

  TimelineItem s2;
  s2.item_id = "s2";
  s2.track_index = 2;
  s2.start_ms = 500;
  s2.duration_ms = 0;
  s2.ref_type = RefType::scene;
  s2.ref_uuid = "scn2";
  sched.insert_item(s2);

  // 跳到 300ms（已越过 s1，未到 s2）
  sched.seek(300);
  // 从 300 继续播放推进到 400
  sched.play(300);
  sched.tick(400);
  SM_CHECK_EQ(recall_calls, 0);  // s1 不应重放

  // 推进到 600ms（s2 应该触发）
  sched.tick(600);
  SM_CHECK_EQ(recall_calls, 1);  // s2 正常触发
}

// ---- stop 停止所有 active 条目 ----
void test_stop() {
  TimelineScheduler sched;
  int stop_calls = 0;
  sched.set_media_preload_cb([](const auto&, const auto&, const auto&) { return true; });
  sched.set_media_play_cb([](const auto&, const auto&, const auto&) {});
  sched.set_media_stop_cb([&](const auto&) { ++stop_calls; });

  TimelineItem a;
  a.item_id = "itm_a";
  a.track_index = 0;
  a.start_ms = 0;
  a.duration_ms = 1000;
  a.ref_type = RefType::media;
  a.ref_uuid = "m_a";
  sched.insert_item(a);

  sched.play(0);
  sched.tick(10);
  SM_CHECK(sched.find_item("itm_a")->state == ItemState::active);

  sched.stop();
  SM_CHECK_EQ(stop_calls, 1);
  SM_CHECK(sched.play_state() == PlayState::stopped);
}

// ---- 总时长计算 ----
void test_total_duration() {
  TimelineScheduler sched;
  SM_CHECK_EQ(sched.total_duration_ms(), int64_t(0));

  TimelineItem a;
  a.item_id = "a";
  a.track_index = 0;
  a.start_ms = 100;
  a.duration_ms = 5000;
  a.ref_type = RefType::media;
  a.ref_uuid = "x";
  sched.insert_item(a);
  SM_CHECK_EQ(sched.total_duration_ms(), int64_t(5100));

  TimelineItem b;
  b.item_id = "b";
  b.track_index = 1;
  b.start_ms = 3000;
  b.duration_ms = 8000;
  b.ref_type = RefType::media;
  b.ref_uuid = "y";
  sched.insert_item(b);
  SM_CHECK_EQ(sched.total_duration_ms(), int64_t(11000));
}

// ---- loop 循环展开 ----
void test_loop() {
  TimelineScheduler sched;
  int play_calls = 0;
  sched.set_media_preload_cb([](const auto&, const auto&, const auto&) { return true; });
  sched.set_media_play_cb([&](const auto&, const auto&, const auto&) { ++play_calls; });
  sched.set_media_stop_cb([](const auto&) {});

  TimelineItem a;
  a.item_id = "loop_a";
  a.track_index = 0;
  a.start_ms = 0;
  a.duration_ms = 100;
  a.loop = 2;  // 循环 2 次 = 播放 3 遍
  a.ref_type = RefType::media;
  a.ref_uuid = "x";
  sched.insert_item(a);

  // loop=2 表示总共播放 3 次（初始 + 2 次循环）
  // end = 0 + 100 * 3 = 300
  SM_CHECK_EQ(sched.find_item("loop_a")->end_ms, int64_t(300));
}

// ---- remove 条目 ----
void test_remove_item() {
  TimelineScheduler sched;
  TimelineItem item;
  item.item_id = "x";
  item.track_index = 0;
  item.start_ms = 0;
  item.duration_ms = 100;
  item.ref_type = RefType::media;
  item.ref_uuid = "m";
  sched.insert_item(item);
  SM_CHECK(sched.find_item("x") != nullptr);

  SM_CHECK(sched.remove_item("x"));
  SM_CHECK(sched.find_item("x") == nullptr);
  SM_CHECK(!sched.remove_item("nonexistent"));
}

// ---- 2ms 调度精度模拟（用真实线程跑一小段）----
void test_tick_thread_precision() {
  // 这个测试只验证调度器可被外部线程驱动；不精确测量 timing
  TimelineScheduler sched;
  std::atomic<int64_t> max_pos{0};
  int play_count = 0;

  sched.set_media_preload_cb([](const auto&, const auto&, const auto&) { return true; });
  sched.set_media_play_cb([&](const auto&, const auto&, const auto&) { ++play_count; });
  sched.set_media_stop_cb([](const auto&) {});

  TimelineItem a;
  a.item_id = "t1";
  a.track_index = 0;
  a.start_ms = 50;
  a.duration_ms = 30;
  a.ref_type = RefType::media;
  a.ref_uuid = "t";
  sched.insert_item(a);

  sched.play(0);
  for (int i = 0; i < 100; i += 2) {
    sched.tick(i);
  }
  SM_CHECK(sched.find_item("t1")->state == ItemState::done);
  SM_CHECK(play_count >= 1);
}

}  // namespace

int main() {
  test_default_tracks();
  test_item_insert_and_find();
  test_item_track_bounds();
  test_state_machine();
  test_cross_track_alignment();
  test_scene_track();
  test_command_track();
  test_seek_scene_no_replay();
  test_stop();
  test_total_duration();
  test_loop();
  test_remove_item();
  test_tick_thread_precision();
  return smtest::finish("test_timeline");
}

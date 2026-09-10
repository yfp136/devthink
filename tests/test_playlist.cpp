// PlaylistExecutor 单元测试（§10.5）
// 验证：载入/状态机/trigger_mode(auto/go/delay)/loop_mode/all 下一项
#include <string>
#include <vector>

#include "engines/playlist/playlist_executor.h"
#include "test_common.h"

namespace {

using namespace sm::playlist;
using nlohmann::json;

// ---- 基础载入 ----
void test_load() {
  PlaylistExecutor exec;
  SM_CHECK(exec.state() == ExecState::idle);

  std::vector<PlaylistItem> items = {
    {"i1", 1, ItemType::media, "m1", TriggerMode::auto_, 0, {}, ""},
    {"i2", 0, ItemType::media, "m2", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  SM_CHECK(exec.state() == ExecState::loaded);
  SM_CHECK_EQ(exec.item_count(), std::size_t(2));
  // 排序后 i2 在前
  SM_CHECK_EQ(exec.items()[0].item_id, std::string("i2"));
}

// ---- auto 模式：自动推进 ----
void test_auto_advance() {
  PlaylistExecutor exec;
  std::vector<std::string> played;
  std::vector<std::string> events;
  bool media_playing = false;

  exec.set_play_media_cb([&](const std::string& mid, const json&) {
    played.push_back(mid);
    media_playing = true;
  });
  exec.set_is_media_done_cb([&]() { return !media_playing; });
  exec.set_stop_media_cb([&]() { media_playing = false; });
  exec.set_event_cb([&](const std::string& evt, const json&) {
    events.push_back(evt);
  });

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::auto_, 0, {}, ""},
    {"i2", 1, ItemType::media, "m2", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  exec.start();
  SM_CHECK(exec.state() == ExecState::running);
  SM_CHECK_EQ(played.size(), std::size_t(1));
  SM_CHECK_EQ(played[0], std::string("m1"));

  // 媒体还在播放 → 不推进
  exec.tick(100);
  SM_CHECK_EQ(played.size(), std::size_t(1));

  // 媒体播放完毕 → tick 推进到下一项
  media_playing = false;
  exec.tick(200);
  SM_CHECK_EQ(played.size(), std::size_t(2));
  SM_CHECK_EQ(played[1], std::string("m2"));

  // 第二项也播完 → 节目单结束
  media_playing = false;
  exec.tick(300);
  SM_CHECK(exec.state() == ExecState::ended);
}

// ---- go 模式：手动 GO 推进 ----
void test_go_mode() {
  PlaylistExecutor exec;
  std::vector<std::string> played;
  bool media_playing = false;

  exec.set_play_media_cb([&](const std::string& mid, const json&) {
    played.push_back(mid);
    media_playing = true;
  });
  exec.set_is_media_done_cb([&]() { return !media_playing; });
  exec.set_stop_media_cb([&]() { media_playing = false; });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::go, 0, {}, ""},
    {"i2", 1, ItemType::media, "m2", TriggerMode::go, 0, {}, ""},
  };
  exec.load(items);
  exec.start();

  // 第一项播完 → 进入 waiting_go
  media_playing = false;
  exec.tick(100);
  SM_CHECK(exec.state() == ExecState::waiting_go);

  // 不手动 go → 不会推进
  exec.tick(200);
  SM_CHECK_EQ(played.size(), std::size_t(1));

  // 手动 go → 推进
  exec.go();
  SM_CHECK(exec.state() == ExecState::running);
  SM_CHECK_EQ(played.size(), std::size_t(2));
}

// ---- delay 模式：内容结束后额外等待 ----
void test_delay_mode() {
  PlaylistExecutor exec;
  std::vector<std::string> played;
  bool media_playing = false;

  exec.set_play_media_cb([&](const std::string& mid, const json&) {
    played.push_back(mid);
    media_playing = true;
  });
  exec.set_is_media_done_cb([&]() { return !media_playing; });
  exec.set_stop_media_cb([&]() { media_playing = false; });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::delay, 500, {}, ""},
    {"i2", 1, ItemType::media, "m2", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  exec.start();

  // 媒体播完
  media_playing = false;
  exec.tick(1000);
  // 进入 delay 等待（500ms）
  SM_CHECK_EQ(played.size(), std::size_t(1));  // 还没推进

  // 等待未到
  exec.tick(1300);
  SM_CHECK_EQ(played.size(), std::size_t(1));

  // 等待到
  exec.tick(1600);
  SM_CHECK_EQ(played.size(), std::size_t(2));
}

// ---- loop_mode=all：循环 ----
void test_loop_all() {
  PlaylistExecutor exec;
  std::vector<std::string> played;
  bool media_playing = false;

  exec.set_play_media_cb([&](const std::string& mid, const json&) {
    played.push_back(mid);
    media_playing = true;
  });
  exec.set_is_media_done_cb([&]() { return !media_playing; });
  exec.set_stop_media_cb([&]() { media_playing = false; });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  exec.set_loop_mode(LoopMode::all);
  exec.start();

  // 第一项播完 → 循环回第一项
  media_playing = false;
  exec.tick(100);
  SM_CHECK_EQ(played.size(), std::size_t(2));  // m1 播了 2 次
  SM_CHECK_EQ(played[1], std::string("m1"));
}

// ---- loop_mode=current：单曲循环 ----
void test_loop_current() {
  PlaylistExecutor exec;
  std::vector<std::string> played;
  bool media_playing = false;

  exec.set_play_media_cb([&](const std::string& mid, const json&) {
    played.push_back(mid);
    media_playing = true;
  });
  exec.set_is_media_done_cb([&]() { return !media_playing; });
  exec.set_stop_media_cb([&]() { media_playing = false; });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::auto_, 0, {}, ""},
    {"i2", 1, ItemType::media, "m2", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  exec.set_loop_mode(LoopMode::current);
  exec.start();

  // 第一项播完 → 循环自身
  media_playing = false;
  exec.tick(100);
  SM_CHECK_EQ(played.size(), std::size_t(2));
  SM_CHECK_EQ(played[0], std::string("m1"));
  SM_CHECK_EQ(played[1], std::string("m1"));  // 还是 m1
}

// ---- scene 条目 ----
void test_scene_item() {
  PlaylistExecutor exec;
  std::string recalled_scene;
  int recalled_fade = 0;

  exec.set_recall_scene_cb([&](const std::string& sid, int fade) {
    recalled_scene = sid;
    recalled_fade = fade;
  });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::scene, "scn_open", TriggerMode::auto_, 0,
     {{"fade_ms", 800}}, ""},
  };
  exec.load(items);
  exec.start();

  SM_CHECK_EQ(recalled_scene, std::string("scn_open"));
  SM_CHECK_EQ(recalled_fade, 800);

  // scene 瞬时完成 → 自动推进 → 结束
  exec.tick(100);
  SM_CHECK(exec.state() == ExecState::ended);
}

// ---- command 条目 ----
void test_command_item() {
  PlaylistExecutor exec;
  std::string sent_op;
  json sent_params;

  exec.set_send_command_cb([&](const std::string& op, const json& params) {
    sent_op = op;
    sent_params = params;
  });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::command, "sys.ping", TriggerMode::auto_, 0,
     {{"params", {{"msg", "hello"}}}}, ""},
  };
  exec.load(items);
  exec.start();

  SM_CHECK_EQ(sent_op, std::string("sys.ping"));
  SM_CHECK(sent_params["msg"] == "hello");

  // command 瞬时 → 自动结束
  exec.tick(100);
  SM_CHECK(exec.state() == ExecState::ended);
}

// ---- next：强制跳过 ----
void test_next_skip() {
  PlaylistExecutor exec;
  std::vector<std::string> played;
  bool media_playing = false;

  exec.set_play_media_cb([&](const std::string& mid, const json&) {
    played.push_back(mid);
    media_playing = true;
  });
  exec.set_is_media_done_cb([&]() { return !media_playing; });
  exec.set_stop_media_cb([&]() { media_playing = false; });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::auto_, 0, {}, ""},
    {"i2", 1, ItemType::media, "m2", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  exec.start();

  // 在播放中强制 next
  exec.next();
  SM_CHECK_EQ(played.size(), std::size_t(2));
  SM_CHECK_EQ(played[1], std::string("m2"));
}

// ---- stop ----
void test_stop() {
  PlaylistExecutor exec;
  bool stopped = false;
  exec.set_play_media_cb([](const auto&, const auto&) {});
  exec.set_stop_media_cb([&]() { stopped = true; });
  exec.set_event_cb([](const auto&, const auto&) {});

  std::vector<PlaylistItem> items = {
    {"i1", 0, ItemType::media, "m1", TriggerMode::auto_, 0, {}, ""},
  };
  exec.load(items);
  exec.start();
  exec.stop();
  SM_CHECK(exec.state() == ExecState::ended);
  SM_CHECK(stopped);
}

// ---- 空节目单 ----
void test_empty_playlist() {
  PlaylistExecutor exec;
  bool got_error = false;
  bool payload_ok = false;
  exec.set_event_cb([&](const std::string& evt, const json& p) {
    if (evt != "evt.error" || p.value("code", 0) != 5002) return;
    got_error = true;
    // §5.4 evt.error 契约载荷 {code, msg, source}
    payload_ok = p.contains("msg") && p.contains("source") &&
                 p.value("source", std::string()) == "engine.playlist" &&
                 !p.value("msg", std::string()).empty();
  });
  exec.load({});
  exec.start();
  SM_CHECK(got_error);
  SM_CHECK(payload_ok);
  SM_CHECK(exec.state() == ExecState::loaded);  // 未变 running
}

}  // namespace

int main() {
  test_load();
  test_auto_advance();
  test_go_mode();
  test_delay_mode();
  test_loop_all();
  test_loop_current();
  test_scene_item();
  test_command_item();
  test_next_skip();
  test_stop();
  test_empty_playlist();
  return smtest::finish("test_playlist");
}

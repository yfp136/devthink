// MediaEngine 单元测试（§10.2）
// 验证：状态机、增益/淡变包络计算、预载→播放→停止流程、单源互斥
#include <string>
#include <vector>

#include "engines/media/media_engine.h"
#include "test_common.h"

namespace {

using namespace sm::media;
using nlohmann::json;

// ---- 状态机：idle → loading → playing → stopped → idle ----
void test_state_machine() {
  MediaEngine eng;
  SM_CHECK(eng.state() == PlayState::idle);

  // 预载
  json meta = {{"gain_db", -3.0}, {"fade_in_ms", 100}};
  SM_CHECK(eng.preload("item1", "media1", meta));
  SM_CHECK(eng.state() == PlayState::loading);
  SM_CHECK_EQ(eng.current_item(), std::string("item1"));

  // 播放
  eng.play("item1", "media1", meta);
  SM_CHECK(eng.state() == PlayState::playing);

  // 停止
  eng.stop("item1");
  SM_CHECK(eng.state() == PlayState::idle);
}

// ---- 增益/淡变包络计算 ----
void test_gain_calculation() {
  // 0dB 增益，无淡入淡出 → 恒定 1.0
  double g = MediaEngine::calc_linear_gain(500, 10000, 0.0, 0, 0);
  SM_CHECK(std::abs(g - 1.0) < 0.001);

  // -6dB ≈ 0.501
  g = MediaEngine::calc_linear_gain(500, 10000, -6.0, 0, 0);
  SM_CHECK(std::abs(g - 0.501) < 0.01);

  // 淡入：pos=0 → 增益=0
  g = MediaEngine::calc_linear_gain(0, 10000, 0.0, 500, 0);
  SM_CHECK(std::abs(g - 0.0) < 0.001);

  // 淡入中段：pos=250, fade_in=500 → 增益≈0.5
  g = MediaEngine::calc_linear_gain(250, 10000, 0.0, 500, 0);
  SM_CHECK(std::abs(g - 0.5) < 0.01);

  // 淡入结束：pos=500 → 增益≈1.0
  g = MediaEngine::calc_linear_gain(500, 10000, 0.0, 500, 0);
  SM_CHECK(std::abs(g - 1.0) < 0.001);

  // 淡出起点：pos=9500, fade_out=500 → 增益≈1.0（刚开始淡出）
  g = MediaEngine::calc_linear_gain(9500, 10000, 0.0, 500, 500);
  SM_CHECK(std::abs(g - 1.0) < 0.01);

  // 淡出中段：pos=9750, fade_out=500 → 增益≈0.5
  g = MediaEngine::calc_linear_gain(9750, 10000, 0.0, 0, 500);
  SM_CHECK(std::abs(g - 0.5) < 0.01);

  // 淡出终点：pos=9999, fade_out=500 → 增益≈0
  g = MediaEngine::calc_linear_gain(9999, 10000, 0.0, 0, 500);
  SM_CHECK(std::abs(g - 0.0) < 0.01);
}

// ---- dB → 线性转换 ----
void test_db_to_linear() {
  SM_CHECK(std::abs(MediaEngine::db_to_linear(0.0) - 1.0) < 0.001);
  SM_CHECK(std::abs(MediaEngine::db_to_linear(-6.0) - 0.501) < 0.01);
  SM_CHECK(std::abs(MediaEngine::db_to_linear(-20.0) - 0.1) < 0.001);
  SM_CHECK_EQ(MediaEngine::db_to_linear(-60.0), 0.0);  // 静音阈值
  SM_CHECK(std::abs(MediaEngine::db_to_linear(6.0) - 2.0) < 0.01);  // +6dB = 2x
}

// ---- 防爆音斜坡 ----
void test_ramp_gain() {
  // 起点 0，终点 1，2ms 斜坡
  SM_CHECK(std::abs(MediaEngine::ramp_gain(0.0, 1.0, 0, 2) - 0.0) < 0.001);
  SM_CHECK(std::abs(MediaEngine::ramp_gain(0.0, 1.0, 1, 2) - 0.5) < 0.001);
  SM_CHECK(std::abs(MediaEngine::ramp_gain(0.0, 1.0, 2, 2) - 1.0) < 0.001);
  SM_CHECK(std::abs(MediaEngine::ramp_gain(0.0, 1.0, 3, 2) - 1.0) < 0.001);  // clamp
}

// ---- 单源互斥：新条目抢占旧条目 ----
void test_single_source_exclusive() {
  MediaEngine eng;
  std::vector<std::string> events;

  eng.set_open_cb([&](const std::string&, int64_t) { return true; });
  eng.set_start_cb([&](double, int64_t) {});
  eng.set_stop_cb([&](int64_t) {});
  eng.set_close_cb([&]() {});

  json meta1 = {{"gain_db", 0.0}, {"fade_in_ms", 0}};
  eng.preload("item1", "media1", meta1);
  eng.play("item1", "media1", meta1);
  SM_CHECK(eng.state() == PlayState::playing);

  // 新条目抢占
  json meta2 = {{"gain_db", -6.0}, {"fade_in_ms", 200}};
  eng.preload("item2", "media2", meta2);
  SM_CHECK(eng.state() == PlayState::loading);
  eng.play("item2", "media2", meta2);
  SM_CHECK(eng.state() == PlayState::playing);
  SM_CHECK_EQ(eng.current_item(), std::string("item2"));
}

// ---- 预载失败 → error 状态 ----
void test_preload_failure() {
  MediaEngine eng;
  eng.set_open_cb([&](const std::string&, int64_t) { return false; });  // 失败

  bool got_error = false;
  eng.set_ended_cb([&](const std::string&, const std::string& reason) {
    if (reason.find("error") != std::string::npos) got_error = true;
  });

  json meta = {{"gain_db", 0.0}};
  bool ok = eng.preload("item1", "media1", meta);
  SM_CHECK(!ok);
  SM_CHECK(eng.state() == PlayState::idle);  // error 后自动回 idle
  SM_CHECK(got_error);
}

// ---- 配置解析 ----
void test_config_parsing() {
  MediaEngine eng;
  eng.set_open_cb([&](const std::string&, int64_t) { return true; });

  json meta = {
    {"trim_in_ms", 5000},
    {"duration_ms", 30000},
    {"gain_db", -3.0},
    {"fade_in_ms", 1000},
    {"fade_out_ms", 2000},
    {"loop", 2},
    {"output", "pgm"}
  };
  eng.preload("item1", "media1", meta);
  eng.play("item1", "media1", meta);
  SM_CHECK(eng.state() == PlayState::playing);
  SM_CHECK_EQ(eng.current_item(), std::string("item1"));
}

// ---- 预览模式 ----
void test_preview_mode() {
  MediaEngine eng;
  eng.set_open_cb([&](const std::string&, int64_t) { return true; });
  eng.set_start_cb([&](double, int64_t) {});

  eng.preview("/path/to/media.mp4");
  SM_CHECK(eng.state() == PlayState::playing);
  SM_CHECK(eng.current_item().empty());  // 预览模式 item_id 为空

  // 预览中时间线条目可抢占
  json meta = {{"gain_db", 0.0}};
  eng.preload("tl_item", "media2", meta);
  eng.play("tl_item", "media2", meta);
  SM_CHECK(eng.state() == PlayState::playing);
  SM_CHECK_EQ(eng.current_item(), std::string("tl_item"));
}

}  // namespace

int main() {
  test_state_machine();
  test_gain_calculation();
  test_db_to_linear();
  test_ramp_gain();
  test_single_source_exclusive();
  test_preload_failure();
  test_config_parsing();
  test_preview_mode();
  return smtest::finish("test_media");
}

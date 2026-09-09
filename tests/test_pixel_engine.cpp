// 8K 播控引擎单元测试（engine.pixel）
// 覆盖：默认输出探测 / 拼接模式几何（2x2/3x1/1x3/2x1/1x2/Single） /
//       输出增删 / 播放状态机（play/pause/resume/stop/seek） / status JSON
#include <string>
#include <vector>

#include "engines/pixel/pixel_engine.h"
#include "test_common.h"

using sm::pixel::DisplayOutput;
using sm::pixel::PixelEngine;
using sm::pixel::PlayState;
using sm::pixel::TilingMode;

namespace {

DisplayOutput make_out(int idx, int w, int h, int x = 0, int y = 0) {
  DisplayOutput o;
  o.output_index = idx;
  o.resolution.width = w;
  o.resolution.height = h;
  o.resolution.refresh_rate = 60;
  o.x_offset = x;
  o.y_offset = y;
  o.enabled = true;
  return o;
}

bool has_output(const std::vector<DisplayOutput>& outs, int idx, int w, int h,
                int x, int y) {
  for (const auto& o : outs) {
    if (o.output_index == idx && o.resolution.width == w &&
        o.resolution.height == h && o.x_offset == x && o.y_offset == y &&
        o.enabled)
      return true;
  }
  return false;
}

void test_init_default_output() {
  PixelEngine eng;
  SM_CHECK(eng.init());
  // 无预置输出时自动探测 1 路 1080p
  const auto outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(1));
  SM_CHECK_EQ(outs[0].resolution.width, 1920);
  SM_CHECK_EQ(outs[0].resolution.height, 1080);
  SM_CHECK(outs[0].enabled);
}

void test_tiling_geometries() {
  PixelEngine eng;
  eng.init();

  // 2x2 → 4×4K
  eng.set_tiling_mode(TilingMode::Grid2x2);
  SM_CHECK(eng.get_tiling_mode() == TilingMode::Grid2x2);
  auto outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(4));
  SM_CHECK(has_output(outs, 0, 3840, 2160, 0, 0));
  SM_CHECK(has_output(outs, 1, 3840, 2160, 3840, 0));
  SM_CHECK(has_output(outs, 2, 3840, 2160, 0, 2160));
  SM_CHECK(has_output(outs, 3, 3840, 2160, 3840, 2160));

  // 3x1 → 3×QHD 横向
  eng.set_tiling_mode(TilingMode::Grid3x1);
  outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(3));
  SM_CHECK(has_output(outs, 1, 2560, 1440, 2560, 0));
  SM_CHECK(has_output(outs, 2, 2560, 1440, 5120, 0));

  // 1x3 → 3×QHD 纵向
  eng.set_tiling_mode(TilingMode::Grid1x3);
  outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(3));
  SM_CHECK(has_output(outs, 1, 2560, 1440, 0, 1440));
  SM_CHECK(has_output(outs, 2, 2560, 1440, 0, 2880));

  // 2x1 / 1x2
  eng.set_tiling_mode(TilingMode::Grid2x1);
  outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(2));
  SM_CHECK(has_output(outs, 1, 3840, 2160, 3840, 0));

  eng.set_tiling_mode(TilingMode::Grid1x2);
  outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(2));
  SM_CHECK(has_output(outs, 1, 3840, 2160, 0, 2160));

  // Single → 单路 8K
  eng.set_tiling_mode(TilingMode::Single);
  outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(1));
  SM_CHECK_EQ(outs[0].resolution.width, 7680);
  SM_CHECK_EQ(outs[0].resolution.height, 4320);
}

void test_add_remove_outputs() {
  PixelEngine eng;
  eng.add_output(make_out(0, 800, 600));
  eng.add_output(make_out(1, 1024, 768));
  eng.add_output(make_out(2, 1280, 720));
  // 有预置输出时 init 不覆盖
  eng.init();
  SM_CHECK_EQ(eng.get_outputs().size(), std::size_t(3));

  eng.remove_output(1);
  const auto outs = eng.get_outputs();
  SM_CHECK_EQ(outs.size(), std::size_t(2));
  SM_CHECK_EQ(outs[1].output_index, 2);  // 原 index2 前移

  eng.remove_output(99);  // 越界忽略
  SM_CHECK_EQ(eng.get_outputs().size(), std::size_t(2));
}

void test_playback_state_machine() {
  PixelEngine eng;
  eng.init();
  eng.set_tiling_mode(TilingMode::Grid2x2);

  SM_CHECK(eng.state() == PlayState::Idle);
  SM_CHECK(eng.play("media://clip/1001"));
  SM_CHECK(eng.state() == PlayState::Playing);

  eng.pause();
  SM_CHECK(eng.state() == PlayState::Paused);

  eng.resume();
  SM_CHECK(eng.state() == PlayState::Playing);

  eng.seek(12000);
  SM_CHECK_EQ(eng.position_ms(), int64_t(12000));

  eng.stop_playback();
  SM_CHECK(eng.state() == PlayState::Stopped);
  SM_CHECK_EQ(eng.position_ms(), int64_t(0));

  eng.sync_frames();  // 冒烟：不应崩溃
}

void test_status_json() {
  PixelEngine eng;
  eng.init();
  eng.set_tiling_mode(TilingMode::Grid2x2);
  eng.play("media://clip/2001");
  eng.seek(500);

  const nlohmann::json j = eng.status();
  SM_CHECK_EQ(j["state"].get<int>(), static_cast<int>(PlayState::Playing));
  SM_CHECK_EQ(j["outputs"].size(), std::size_t(4));
  const auto& o0 = j["outputs"][0];
  SM_CHECK_EQ(o0["width"].get<int>(), 3840);
  SM_CHECK_EQ(o0["height"].get<int>(), 2160);
  SM_CHECK_EQ(o0["enabled"].get<bool>(), true);
  SM_CHECK_EQ(j["tiling"].get<int>(), static_cast<int>(TilingMode::Grid2x2));
}

}  // namespace

int main() {
  test_init_default_output();
  test_tiling_geometries();
  test_add_remove_outputs();
  test_playback_state_machine();
  test_status_json();
  return smtest::finish("test_pixel_engine");
}

// LED 灰度灯带双模式引擎单元测试（engine.led）
// 覆盖：静态效果（纯色/闪烁/呼吸/追逐/彩虹/频闪）/
//       视频流模式像素采样（含反向段）/ DMX 512 通道映射 / 发送回调 / status JSON
#include <cstdint>
#include <vector>

#include "engines/led/led_engine.h"
#include "test_common.h"

using sm::led::ColorOrder;
using sm::led::LedConfig;
using sm::led::LedEngine;
using sm::led::LedMode;
using sm::led::LedSegment;
using sm::led::Pixel;
using sm::led::StaticEffect;

namespace {

LedConfig cfg_static(int total, int start_idx = 0, int count = 0,
                     ColorOrder order = ColorOrder::RGB) {
  LedConfig cfg;
  cfg.mode = LedMode::StaticEffect;
  cfg.total_leds = total;
  cfg.grayscale_bits = 8;
  cfg.update_rate_hz = 60;
  LedSegment seg;
  seg.start_index = start_idx;
  seg.led_count = (count == 0) ? total : count;
  seg.color_order = order;
  seg.dmx_start_addr = 1;
  seg.dmx_universe = 0;
  seg.reverse = false;
  cfg.segments.push_back(seg);
  return cfg;
}

bool all_eq(const std::vector<Pixel>& px, const Pixel& expect) {
  for (const auto& p : px)
    if (p.r != expect.r || p.g != expect.g || p.b != expect.b ||
        p.w != expect.w)
      return false;
  return true;
}

void test_solid_and_status() {
  LedEngine eng;
  LedConfig cfg = cfg_static(8);
  SM_CHECK(eng.init(cfg));
  eng.set_static_effect(StaticEffect::SolidColor, {200, 100, 50, 0}, 50);
  eng.start();

  const std::vector<Pixel> px = eng.update(0);
  SM_CHECK_EQ(px.size(), std::size_t(8));
  SM_CHECK(all_eq(px, Pixel{200, 100, 50, 0}));

  const nlohmann::json j = eng.status();
  SM_CHECK_EQ(j["total_leds"].get<int>(), 8);
  SM_CHECK_EQ(j["mode"].get<std::string>(), std::string("static"));
  SM_CHECK_EQ(j["running"].get<bool>(), true);
}

void test_blink_breathe_strobe() {
  LedEngine eng;
  eng.init(cfg_static(4));

  // Blink：phase=(elapsed/speed)，(phase/2)%2==0 亮
  eng.set_static_effect(StaticEffect::Blink, {255, 255, 255, 0}, 50);
  SM_CHECK(all_eq(eng.update(0), Pixel{255, 255, 255, 0}));    // phase 0
  SM_CHECK(all_eq(eng.update(150), Pixel{0, 0, 0, 0}));        // phase 3 → 灭
  SM_CHECK(all_eq(eng.update(250), Pixel{255, 255, 255, 0}));  // phase 5 → 亮

  // Breathe：elapsed=0 → intensity=0.5 → 颜色减半
  eng.set_static_effect(StaticEffect::Breathe, {200, 100, 50, 0}, 50);
  const std::vector<Pixel> breathe = eng.update(0);
  SM_CHECK_EQ(breathe[0].r, 100);
  SM_CHECK_EQ(breathe[0].g, 50);
  SM_CHECK_EQ(breathe[0].b, 25);

  // Strobe：phase%2==0 亮
  eng.set_static_effect(StaticEffect::Strobe, {10, 20, 30, 0}, 50);
  SM_CHECK(all_eq(eng.update(0), Pixel{10, 20, 30, 0}));
  SM_CHECK(all_eq(eng.update(60), Pixel{0, 0, 0, 0}));  // phase 1 → 灭
}

void test_chase_rainbow() {
  LedEngine eng;
  eng.init(cfg_static(8));

  // Chase：头部在 phase%n，向前 3 颗亮
  eng.set_static_effect(StaticEffect::Chase, {255, 0, 0, 0}, 50);
  const std::vector<Pixel> chase = eng.update(0);  // pos 0
  SM_CHECK_EQ(chase[0].r, 255);
  SM_CHECK_EQ(chase[2].r, 255);
  SM_CHECK_EQ(chase[3].r, 0);
  SM_CHECK_EQ(chase[7].r, 0);
  const std::vector<Pixel> chase2 = eng.update(100);  // pos 2
  SM_CHECK_EQ(chase2[2].r, 255);
  SM_CHECK_EQ(chase2[0].r, 0);

  // Rainbow：LED0 hue=0 → 红
  eng.set_static_effect(StaticEffect::Rainbow, {255, 255, 255, 0}, 50);
  const std::vector<Pixel> rb = eng.update(0);
  SM_CHECK_EQ(rb[0].r, 255);
  SM_CHECK_EQ(rb[0].g, 0);
  SM_CHECK_EQ(rb[0].b, 0);
}

void test_video_stream_sampling() {
  // 帧宽 4 高 2：左半红、右半蓝（中线 y=1）
  std::vector<uint8_t> frame(4 * 2 * 3);
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      uint8_t* p = frame.data() + (y * 4 + x) * 3;
      p[0] = (x < 2) ? 255 : 0;
      p[1] = 0;
      p[2] = (x < 2) ? 0 : 255;
    }
  }

  // 正向段：LED0 采 x=0（红），LED1 采 x=2（蓝）
  LedEngine fwd;
  LedConfig cfg = cfg_static(2);
  cfg.mode = LedMode::VideoStream;
  fwd.init(cfg);
  fwd.process_video_frame(frame.data(), 4, 2);
  std::vector<Pixel> px = fwd.update(0);
  SM_CHECK_EQ(px[0].r, 255);
  SM_CHECK_EQ(px[0].b, 0);
  SM_CHECK_EQ(px[1].b, 255);

  // 反向段：LED0 采 x=3（蓝），LED1 采 x=1（红）
  LedEngine rev;
  LedConfig rcfg = cfg_static(2);
  rcfg.mode = LedMode::VideoStream;
  rcfg.segments[0].reverse = true;
  rev.init(rcfg);
  rev.process_video_frame(frame.data(), 4, 2);
  px = rev.update(0);
  SM_CHECK_EQ(px[0].b, 255);
  SM_CHECK_EQ(px[0].r, 0);
  SM_CHECK_EQ(px[1].r, 255);

  // 静态模式下视频帧被忽略
  LedEngine stat;
  stat.init(cfg_static(2));
  stat.set_static_effect(StaticEffect::SolidColor, {7, 7, 7, 0}, 50);
  stat.process_video_frame(frame.data(), 4, 2);
  SM_CHECK(all_eq(stat.update(0), Pixel{7, 7, 7, 0}));
}

void test_dmx_mapping() {
  // RGB 段：dmx[0..2] = r,g,b
  LedEngine eng;
  LedConfig cfg = cfg_static(2);
  eng.init(cfg);
  eng.set_static_effect(StaticEffect::SolidColor, {200, 100, 50, 0}, 50);
  eng.update(0);

  const std::vector<uint8_t> dmx = eng.get_dmx_output(0);
  SM_CHECK_EQ(dmx.size(), std::size_t(512));
  SM_CHECK_EQ(dmx[0], 200);  // LED0 r
  SM_CHECK_EQ(dmx[1], 100);  // LED0 g
  SM_CHECK_EQ(dmx[2], 50);   // LED0 b
  SM_CHECK_EQ(dmx[3], 200);  // LED1 r
  SM_CHECK_EQ(dmx[4], 100);
  SM_CHECK_EQ(dmx[5], 50);

  // BRG 段：通道顺序 b,r,g
  LedEngine beng;
  LedConfig bcfg = cfg_static(1, 0, 1, ColorOrder::BRG);
  beng.init(bcfg);
  beng.set_static_effect(StaticEffect::SolidColor, {200, 100, 50, 0}, 50);
  beng.update(0);
  const std::vector<uint8_t> bdmx = beng.get_dmx_output(0);
  SM_CHECK_EQ(bdmx[0], 50);   // b
  SM_CHECK_EQ(bdmx[1], 200);  // r
  SM_CHECK_EQ(bdmx[2], 100);  // g

  // GRB 段：通道顺序 g,r,b
  LedEngine geng;
  LedConfig gcfg = cfg_static(1, 0, 1, ColorOrder::GRB);
  geng.init(gcfg);
  geng.set_static_effect(StaticEffect::SolidColor, {200, 100, 50, 0}, 50);
  geng.update(0);
  const std::vector<uint8_t> gdmx = geng.get_dmx_output(0);
  SM_CHECK_EQ(gdmx[0], 100);
  SM_CHECK_EQ(gdmx[1], 200);
  SM_CHECK_EQ(gdmx[2], 50);

  // 未定义的 universe 返回全 0
  const std::vector<uint8_t> other = eng.get_dmx_output(7);
  SM_CHECK_EQ(other.size(), std::size_t(512));
  SM_CHECK_EQ(other[0], 0);
}

void test_dmx_send_callback() {
  LedEngine eng;
  eng.init(cfg_static(2));
  int sent_universe = -1;
  int sent_len = -1;
  eng.set_dmx_send_fn(
      [&](int universe, const uint8_t*, int len) {
        sent_universe = universe;
        sent_len = len;
        return true;
      });
  eng.set_static_effect(StaticEffect::SolidColor, {1, 2, 3, 0}, 50);
  eng.update(0);

  SM_CHECK_EQ(sent_universe, 0);
  SM_CHECK_EQ(sent_len, 6);  // 2 颗 LED × 3 通道
}

}  // namespace

int main() {
  test_solid_and_status();
  test_blink_breathe_strobe();
  test_chase_rainbow();
  test_video_stream_sampling();
  test_dmx_mapping();
  test_dmx_send_callback();
  return smtest::finish("test_led_engine");
}

// VJ 实时 GPU 特效引擎单元测试（engine.vjfx）
// 覆盖：init/start/stop 状态 / 特效链 set-get /
//       CPU 特效精确数值（亮度/对比度/饱和度/像素化/暗角/链顺序） /
//       透明度 / GPU 回调优先 / 转场进度机 / status JSON
#include <cstdint>
#include <string>
#include <vector>

#include "engines/vjfx/vjfx_engine.h"
#include "test_common.h"

using sm::vjfx::BlendMode;
using sm::vjfx::EffectChain;
using sm::vjfx::EffectParam;
using sm::vjfx::EffectType;
using sm::vjfx::TransitionType;
using sm::vjfx::VjfxEngine;

namespace {

// 1x1 RGB24 帧 (r,g,b)
std::vector<uint8_t> px(uint8_t r, uint8_t g, uint8_t b) {
  return {r, g, b};
}

EffectParam eff(EffectType t, double intensity) {
  EffectParam p;
  p.type = t;
  p.intensity = intensity;
  return p;
}

bool is_px(const std::vector<uint8_t>& out, uint8_t r, uint8_t g, uint8_t b) {
  return out.size() == 3 && out[0] == r && out[1] == g && out[2] == b;
}

void test_lifecycle() {
  VjfxEngine eng;
  SM_CHECK(!eng.status()["running"].get<bool>());
  SM_CHECK(eng.init());
  eng.start();
  SM_CHECK(eng.status()["running"].get<bool>());
  eng.stop();
  SM_CHECK(!eng.status()["running"].get<bool>());
}

void test_effect_chain_roundtrip() {
  VjfxEngine eng;
  eng.init();

  EffectChain empty = eng.get_effect_chain();
  SM_CHECK_EQ(empty.effects.size(), std::size_t(0));
  SM_CHECK(empty.blend_mode == BlendMode::Normal);
  SM_CHECK_EQ(empty.opacity, 1.0);

  EffectChain chain;
  chain.blend_mode = BlendMode::Add;
  chain.opacity = 0.7;
  chain.effects = {eff(EffectType::Brightness, 0.8),
                   eff(EffectType::GaussianBlur, 0.4),
                   eff(EffectType::vignette, 0.6)};
  eng.set_effect_chain(chain);
  EffectChain got = eng.get_effect_chain();
  SM_CHECK_EQ(got.effects.size(), std::size_t(3));
  SM_CHECK(got.blend_mode == BlendMode::Add);
  SM_CHECK_EQ(got.opacity, 0.7);
  SM_CHECK(got.effects[0].type == EffectType::Brightness);
  SM_CHECK_EQ(got.effects[0].intensity, 0.8);
  SM_CHECK(got.effects[1].type == EffectType::GaussianBlur);
  SM_CHECK(got.effects[2].type == EffectType::vignette);
}

void test_brightness() {
  VjfxEngine eng;
  eng.init();
  EffectChain chain;
  chain.effects = {eff(EffectType::Brightness, 1.0)};
  eng.set_effect_chain(chain);

  // delta = int(255-128)=127：黑场提亮为 127
  auto out = eng.process_frame_cpu(px(0, 0, 0).data(), 1, 1);
  SM_CHECK(is_px(out, 127, 127, 127));

  // 200→255(截断)、100→227、0→127
  out = eng.process_frame_cpu(px(200, 100, 0).data(), 1, 1);
  SM_CHECK(is_px(out, 255, 227, 127));
}

void test_contrast() {
  VjfxEngine eng;
  eng.init();
  EffectChain chain;
  chain.effects = {eff(EffectType::Contrast, 1.0)};
  eng.set_effect_chain(chain);

  // factor=3.0：(100-128)*3+128=44；128 不变；200→255
  auto out = eng.process_frame_cpu(px(100, 128, 200).data(), 1, 1);
  SM_CHECK(is_px(out, 44, 128, 255));
}

void test_saturation() {
  VjfxEngine eng;
  eng.init();

  // intensity=0 → 灰度 (200,100,50) 加权灰 = 124
  EffectChain g;
  g.effects = {eff(EffectType::Saturation, 0.0)};
  eng.set_effect_chain(g);
  auto out = eng.process_frame_cpu(px(200, 100, 50).data(), 1, 1);
  SM_CHECK(is_px(out, 124, 124, 124));

  // intensity=1 → 恒等
  EffectChain id;
  id.effects = {eff(EffectType::Saturation, 1.0)};
  eng.set_effect_chain(id);
  out = eng.process_frame_cpu(px(200, 100, 50).data(), 1, 1);
  SM_CHECK(is_px(out, 200, 100, 50));

  // intensity=0.5 → 灰向原色插值
  EffectChain half;
  half.effects = {eff(EffectType::Saturation, 0.5)};
  eng.set_effect_chain(half);
  out = eng.process_frame_cpu(px(200, 100, 50).data(), 1, 1);
  SM_CHECK(is_px(out, 162, 112, 87));
}

void test_pixelate() {
  VjfxEngine eng;
  eng.init();

  // 2x2 帧，intensity=1 → block=31 覆盖整帧为左上角颜色
  EffectChain p;
  p.effects = {eff(EffectType::Pixelate, 1.0)};
  eng.set_effect_chain(p);
  const uint8_t frame[] = {10, 20, 30, 200, 0, 0, 90, 90, 90, 1, 2, 3};
  auto out = eng.process_frame_cpu(frame, 2, 2);
  SM_CHECK_EQ(out.size(), std::size_t(12));
  const uint8_t expect[] = {10, 20, 30, 10, 20, 30, 10, 20, 30, 10, 20, 30};
  bool same = true;
  for (int i = 0; i < 12; ++i)
    same = same && out[i] == expect[i];
  SM_CHECK(same);

  // intensity=0 → block=1 恒等
  EffectChain no;
  no.effects = {eff(EffectType::Pixelate, 0.0)};
  eng.set_effect_chain(no);
  out = eng.process_frame_cpu(frame, 2, 2);
  same = true;
  for (int i = 0; i < 12; ++i)
    same = same && out[i] == frame[i];
  SM_CHECK(same);
}

void test_vignette() {
  VjfxEngine eng;
  eng.init();
  EffectChain chain;
  chain.effects = {eff(EffectType::vignette, 0.5)};
  eng.set_effect_chain(chain);

  // 3x3 全 100：中心 (1,1) 不变 100；角 (0,0) 减半 50；边中点 (0,1) 75
  std::vector<uint8_t> f(27, 100);
  auto out = eng.process_frame_cpu(f.data(), 3, 3);
  SM_CHECK_EQ(out.size(), std::size_t(27));
  const auto px_at = [&](int x, int y) {
    return out[(y * 3 + x) * 3];
  };
  SM_CHECK_EQ(px_at(1, 1), 100);  // 中心
  SM_CHECK_EQ(px_at(0, 0), 50);   // 角
  SM_CHECK_EQ(px_at(0, 1), 75);   // 边缘中点
}

void test_chain_order() {
  VjfxEngine eng;
  eng.init();

  // 对比度(1.0) 再 亮度(1.0)：44+127=171
  EffectChain a;
  a.effects = {eff(EffectType::Contrast, 1.0),
               eff(EffectType::Brightness, 1.0)};
  eng.set_effect_chain(a);
  auto out = eng.process_frame_cpu(px(100, 100, 100).data(), 1, 1);
  SM_CHECK(is_px(out, 171, 171, 171));

  // 反序：100→227 再对比度 3x → 255
  EffectChain b;
  b.effects = {eff(EffectType::Brightness, 1.0),
               eff(EffectType::Contrast, 1.0)};
  eng.set_effect_chain(b);
  out = eng.process_frame_cpu(px(100, 100, 100).data(), 1, 1);
  SM_CHECK(is_px(out, 255, 255, 255));
}

void test_opacity() {
  VjfxEngine eng;
  eng.init();
  EffectChain chain;
  chain.opacity = 0.5;  // 无特效，仅透明度
  eng.set_effect_chain(chain);
  auto out = eng.process_frame_cpu(px(200, 100, 50).data(), 1, 1);
  SM_CHECK(is_px(out, 100, 50, 25));
}

void test_process_fn_priority() {
  VjfxEngine eng;
  eng.init();

  // GPU 回调优先于 CPU：注册后返回回调产物，且回调收到原始输入帧
  const uint8_t raw[] = {9, 8, 7};
  bool got_original = false;
  eng.set_process_fn([&](const uint8_t* rgb_in, int w, int h) {
    got_original = (w == 1 && h == 1 && rgb_in[0] == 9 && rgb_in[1] == 8 &&
                    rgb_in[2] == 7);
    return std::vector<uint8_t>{255, 0, 255};
  });

  EffectChain chain;
  chain.effects = {eff(EffectType::Brightness, 1.0)};
  eng.set_effect_chain(chain);
  auto out = eng.process_frame_cpu(raw, 1, 1);
  SM_CHECK_EQ(out.size(), std::size_t(3));
  SM_CHECK(is_px(out, 255, 0, 255));
  SM_CHECK(got_original);

  // 清除回调 → 回退 CPU 路径（亮度 +127 → 136,135,134）
  eng.set_process_fn(nullptr);
  out = eng.process_frame_cpu(raw, 1, 1);
  SM_CHECK(is_px(out, 136, 135, 134));
}

void test_transition() {
  VjfxEngine eng;
  eng.init();

  SM_CHECK(!eng.is_transition_done());  // 默认 progress=0，未完成
  SM_CHECK_EQ(eng.status()["transition_progress"].get<double>(), 0.0);
  eng.start_transition(TransitionType::WipeLeft, 1000);
  SM_CHECK(!eng.is_transition_done());
  SM_CHECK_EQ(eng.status()["transition_progress"].get<double>(), 0.0);

  eng.update_transition(250);
  SM_CHECK_EQ(eng.status()["transition_progress"].get<double>(), 0.25);
  SM_CHECK(!eng.is_transition_done());

  eng.update_transition(500);
  SM_CHECK_EQ(eng.status()["transition_progress"].get<double>(), 0.75);

  eng.update_transition(1000);  // 超时封顶 1.0
  SM_CHECK_EQ(eng.status()["transition_progress"].get<double>(), 1.0);
  SM_CHECK(eng.is_transition_done());

  // duration<=0 → 一次 update 即完成
  eng.start_transition(TransitionType::Crossfade, 0);
  eng.update_transition(1);
  SM_CHECK(eng.is_transition_done());
}

void test_status_json() {
  VjfxEngine eng;
  eng.init();
  eng.start();

  EffectChain chain;
  chain.opacity = 0.8;
  chain.effects = {eff(EffectType::Brightness, 0.6),
                   eff(EffectType::FilmGrain, 0.1)};
  eng.set_effect_chain(chain);
  eng.start_transition(TransitionType::ZoomIn, 2000);
  eng.update_transition(500);

  const auto j = eng.status();
  SM_CHECK_EQ(j["running"].get<bool>(), true);
  SM_CHECK_EQ(j["opacity"].get<double>(), 0.8);
  SM_CHECK_EQ(j["effects_count"].get<int>(), 2);
  SM_CHECK_EQ(j["transition_progress"].get<double>(), 0.25);
}

}  // namespace

int main() {
  test_lifecycle();
  test_effect_chain_roundtrip();
  test_brightness();
  test_contrast();
  test_saturation();
  test_pixelate();
  test_vignette();
  test_chain_order();
  test_opacity();
  test_process_fn_priority();
  test_transition();
  test_status_json();
  return smtest::finish("test_vjfx_engine");
}

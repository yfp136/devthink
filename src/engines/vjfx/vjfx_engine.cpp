// VJ 实时 GPU 特效引擎实现
// CPU 回退实现（GPU 着色器在 Phase 3 集成 D3D11 时编译）
#include "engines/vjfx/vjfx_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sm {
namespace vjfx {

VjfxEngine::VjfxEngine() = default;
VjfxEngine::~VjfxEngine() { stop(); }

bool VjfxEngine::init() {
  initialized_ = true;
  return true;
}

void VjfxEngine::start() {
  running_ = true;
}

void VjfxEngine::stop() {
  running_ = false;
}

void VjfxEngine::set_effect_chain(const EffectChain& chain) {
  chain_ = chain;
}

EffectChain VjfxEngine::get_effect_chain() const {
  return chain_;
}

void VjfxEngine::start_transition(TransitionType type, int64_t duration_ms) {
  transition_.type = type;
  transition_.duration_ms = duration_ms;
  transition_.elapsed_ms = 0;
  transition_.progress = 0.0;
}

void VjfxEngine::update_transition(int64_t delta_ms) {
  if (transition_.duration_ms <= 0) {
    transition_.progress = 1.0;
    return;
  }
  transition_.elapsed_ms += delta_ms;
  transition_.progress = static_cast<double>(transition_.elapsed_ms) /
                          transition_.duration_ms;
  if (transition_.progress > 1.0) transition_.progress = 1.0;
}

bool VjfxEngine::is_transition_done() const {
  return transition_.progress >= 1.0;
}

void VjfxEngine::set_process_fn(ProcessFrameFn fn) {
  process_fn_ = std::move(fn);
}

// ---- CPU 特效实现 ----

static inline uint8_t clamp8(int v) {
  return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v));
}

static void apply_brightness(uint8_t* rgb, int n, double intensity) {
  int delta = static_cast<int>(intensity * 255 - 128);
  for (int i = 0; i < n; ++i)
    rgb[i] = clamp8(rgb[i] + delta);
}

static void apply_contrast(uint8_t* rgb, int n, double intensity) {
  double factor = 1.0 + intensity * 2.0;
  for (int i = 0; i < n; ++i)
    rgb[i] = clamp8(static_cast<int>((rgb[i] - 128) * factor + 128));
}

static void apply_saturation(uint8_t* rgb, int pixel_count, double intensity) {
  for (int i = 0; i < pixel_count; ++i) {
    uint8_t* p = rgb + i * 3;
    int gray = (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
    p[0] = clamp8(static_cast<int>(gray + (p[0] - gray) * intensity));
    p[1] = clamp8(static_cast<int>(gray + (p[1] - gray) * intensity));
    p[2] = clamp8(static_cast<int>(gray + (p[2] - gray) * intensity));
  }
}

static void apply_pixelate(uint8_t* rgb, int w, int h, double intensity) {
  int block = static_cast<int>(1 + intensity * 30);
  for (int y = 0; y < h; y += block) {
    for (int x = 0; x < w; x += block) {
      int idx = (y * w + x) * 3;
      uint8_t r = rgb[idx], g = rgb[idx + 1], b = rgb[idx + 2];
      for (int dy = 0; dy < block && y + dy < h; ++dy) {
        for (int dx = 0; dx < block && x + dx < w; ++dx) {
          int i = ((y + dy) * w + (x + dx)) * 3;
          rgb[i] = r;
          rgb[i + 1] = g;
          rgb[i + 2] = b;
        }
      }
    }
  }
}

static void apply_vignette(uint8_t* rgb, int w, int h, double intensity) {
  int cx = w / 2, cy = h / 2;
  double max_dist = std::sqrt(cx * cx + cy * cy);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double dx = x - cx, dy = y - cy;
      double dist = std::sqrt(dx * dx + dy * dy) / max_dist;
      double scale = 1.0 - intensity * dist * dist;
      int idx = (y * w + x) * 3;
      rgb[idx] = clamp8(static_cast<int>(rgb[idx] * scale));
      rgb[idx + 1] = clamp8(static_cast<int>(rgb[idx + 1] * scale));
      rgb[idx + 2] = clamp8(static_cast<int>(rgb[idx + 2] * scale));
    }
  }
}

static void apply_gaussian_blur(uint8_t* rgb, int w, int h, double intensity) {
  // 简化 box blur（近似高斯）
  int radius = static_cast<int>(1 + intensity * 10);
  std::vector<uint8_t> tmp(rgb, rgb + w * h * 3);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int r = 0, g = 0, b = 0, count = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          int nx = x + dx, ny = y + dy;
          if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
            int idx = (ny * w + nx) * 3;
            r += tmp[idx];
            g += tmp[idx + 1];
            b += tmp[idx + 2];
            ++count;
          }
        }
      }
      int idx = (y * w + x) * 3;
      rgb[idx] = r / count;
      rgb[idx + 1] = g / count;
      rgb[idx + 2] = b / count;
    }
  }
}

static void apply_film_grain(uint8_t* rgb, int n, double intensity) {
  for (int i = 0; i < n; ++i) {
    int noise = (std::rand() % 64) - 32;
    rgb[i] = clamp8(rgb[i] + static_cast<int>(noise * intensity));
  }
}

std::vector<uint8_t> VjfxEngine::process_frame_cpu(
    const uint8_t* rgb_in, int width, int height) {
  int n = width * height * 3;
  std::vector<uint8_t> rgb(rgb_in, rgb_in + n);

  // 按特效链顺序应用
  for (const auto& eff : chain_.effects) {
    switch (eff.type) {
      case EffectType::Brightness:
        apply_brightness(rgb.data(), n, eff.intensity);
        break;
      case EffectType::Contrast:
        apply_contrast(rgb.data(), n, eff.intensity);
        break;
      case EffectType::Saturation:
        apply_saturation(rgb.data(), width * height, eff.intensity);
        break;
      case EffectType::GaussianBlur:
        apply_gaussian_blur(rgb.data(), width, height, eff.intensity);
        break;
      case EffectType::Pixelate:
        apply_pixelate(rgb.data(), width, height, eff.intensity);
        break;
      case EffectType::vignette:
        apply_vignette(rgb.data(), width, height, eff.intensity);
        break;
      case EffectType::FilmGrain:
        apply_film_grain(rgb.data(), n, eff.intensity);
        break;
      default:
        // 其他特效 Phase 3 实现
        break;
    }
  }

  // 应用透明度
  if (chain_.opacity < 1.0) {
    for (int i = 0; i < n; ++i)
      rgb[i] = clamp8(static_cast<int>(rgb[i] * chain_.opacity));
  }

  // 如果有 GPU 处理回调，优先调用
  if (process_fn_)
    return process_fn_(rgb_in, width, height);

  return rgb;
}

nlohmann::json VjfxEngine::status() const {
  nlohmann::json j;
  j["running"] = running_;
  j["opacity"] = chain_.opacity;
  j["effects_count"] = chain_.effects.size();
  j["transition_progress"] = transition_.progress;
  return j;
}

} // namespace vjfx
} // namespace sm

// LED 灰度灯带双模式引擎实现
#include "engines/led/led_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace sm {
namespace led {

LedEngine::LedEngine() = default;
LedEngine::~LedEngine() { stop(); }

bool LedEngine::init(const LedConfig& cfg) {
  config_ = cfg;
  leds_.resize(cfg.total_leds);
  std::fill(leds_.begin(), leds_.end(), Pixel{0, 0, 0, 0});
  return true;
}

void LedEngine::start() { running_ = true; }
void LedEngine::stop() { running_ = false; }

void LedEngine::set_static_effect(StaticEffect type, const Pixel& color,
                                   int speed_ms) {
  effect_ = type;
  effect_color_ = color;
  effect_speed_ms_ = speed_ms;
}

void LedEngine::process_video_frame(const uint8_t* rgb, int width, int height) {
  // 从视频帧采样像素映射到灯带
  if (config_.mode != LedMode::VideoStream) return;
  if (!rgb || width <= 0 || height <= 0) return;

  for (const auto& seg : config_.segments) {
    for (int i = 0; i < seg.led_count; ++i) {
      // 采样：沿灯带长度方向取像素
      int sample_x = static_cast<int>((double)i / seg.led_count * width);
      int sample_y = height / 2;  // 取中线

      if (seg.reverse)
        sample_x = width - 1 - sample_x;

      sample_x = std::max(0, std::min(width - 1, sample_x));
      int idx = (sample_y * width + sample_x) * 3;

      int led_idx = seg.start_index + i;
      if (led_idx >= 0 && led_idx < static_cast<int>(leds_.size())) {
        leds_[led_idx] = {rgb[idx], rgb[idx + 1], rgb[idx + 2], 0};
      }
    }
  }
}

Pixel LedEngine::color_blend(const Pixel& a, const Pixel& b, double t) {
  t = std::max(0.0, std::min(1.0, t));
  return {
    static_cast<uint8_t>(a.r * (1 - t) + b.r * t),
    static_cast<uint8_t>(a.g * (1 - t) + b.g * t),
    static_cast<uint8_t>(a.b * (1 - t) + b.b * t),
    static_cast<uint8_t>(a.w * (1 - t) + b.w * t)
  };
}

void LedEngine::apply_static_effect(int64_t elapsed_ms) {
  int n = static_cast<int>(leds_.size());
  if (n == 0) return;

  int phase = static_cast<int>(elapsed_ms / effect_speed_ms_);

  switch (effect_) {
    case StaticEffect::SolidColor:
      for (auto& p : leds_) p = effect_color_;
      break;

    case StaticEffect::Blink:
      if ((phase / 2) % 2 == 0) {
        for (auto& p : leds_) p = effect_color_;
      } else {
        for (auto& p : leds_) p = {0, 0, 0, 0};
      }
      break;

    case StaticEffect::Gradient: {
      Pixel c2 = {static_cast<uint8_t>(255 - effect_color_.r),
                  static_cast<uint8_t>(255 - effect_color_.g),
                  static_cast<uint8_t>(255 - effect_color_.b), 0};
      for (int i = 0; i < n; ++i) {
        double pos = static_cast<double>(i) / n;
        leds_[i] = color_blend(effect_color_, c2, pos);
      }
      break;
    }

    case StaticEffect::Rainbow: {
      for (int i = 0; i < n; ++i) {
        double hue = fmod(static_cast<double>(i) / n * 360 + phase * 10, 360);
        double h = hue / 60;
        double s = 1.0, v = 1.0;
        int hi = static_cast<int>(h) % 6;
        double f = h - static_cast<int>(h);
        uint8_t p = static_cast<uint8_t>(v * (1 - s) * 255);
        uint8_t q = static_cast<uint8_t>(v * (1 - s * f) * 255);
        uint8_t tt = static_cast<uint8_t>(v * (1 - s * (1 - f)) * 255);
        switch (hi) {
          case 0: leds_[i] = {static_cast<uint8_t>(v * 255), tt, p, 0}; break;
          case 1: leds_[i] = {q, static_cast<uint8_t>(v * 255), p, 0}; break;
          case 2: leds_[i] = {p, static_cast<uint8_t>(v * 255), tt, 0}; break;
          case 3: leds_[i] = {p, q, static_cast<uint8_t>(v * 255), 0}; break;
          case 4: leds_[i] = {tt, p, static_cast<uint8_t>(v * 255), 0}; break;
          case 5: leds_[i] = {static_cast<uint8_t>(v * 255), p, q, 0}; break;
        }
      }
      break;
    }

    case StaticEffect::Chase: {
      int pos = phase % n;
      for (int i = 0; i < n; ++i) {
        int dist = (i - pos + n) % n;
        if (dist < 3)
          leds_[i] = effect_color_;
        else
          leds_[i] = {0, 0, 0, 0};
      }
      break;
    }

    case StaticEffect::Breathe: {
      double intensity = 0.5 + 0.5 * sin(elapsed_ms * 0.003);
      for (auto& p : leds_) {
        p.r = static_cast<uint8_t>(effect_color_.r * intensity);
        p.g = static_cast<uint8_t>(effect_color_.g * intensity);
        p.b = static_cast<uint8_t>(effect_color_.b * intensity);
      }
      break;
    }

    case StaticEffect::Strobe: {
      bool on = (phase % 2) == 0;
      Pixel c = on ? effect_color_ : Pixel{0, 0, 0, 0};
      for (auto& p : leds_) p = c;
      break;
    }

    case StaticEffect::Comet: {
      for (auto& p : leds_) p = {0, 0, 0, 0};
      int pos = phase % n;
      for (int i = 0; i < 8; ++i) {
        int idx = (pos - i + n) % n;
        double intensity = 1.0 - static_cast<double>(i) / 8;
        leds_[idx].r = static_cast<uint8_t>(effect_color_.r * intensity);
        leds_[idx].g = static_cast<uint8_t>(effect_color_.g * intensity);
        leds_[idx].b = static_cast<uint8_t>(effect_color_.b * intensity);
      }
      break;
    }

    case StaticEffect::ColorWipe: {
      int fill = (phase % (n * 2));
      if (fill < n) {
        for (int i = 0; i <= fill && i < n; ++i) leds_[i] = effect_color_;
      } else {
        int clear_n = fill - n;
        for (int i = 0; i < clear_n && i < n; ++i) leds_[i] = {0, 0, 0, 0};
      }
      break;
    }

    case StaticEffect::TheaterChase: {
      for (int i = 0; i < n; ++i) {
        if ((i + phase) % 3 == 0)
          leds_[i] = effect_color_;
        else
          leds_[i] = {0, 0, 0, 0};
      }
      break;
    }
  }
}

std::vector<Pixel> LedEngine::update(int64_t elapsed_ms) {
  if (config_.mode == LedMode::StaticEffect)
    apply_static_effect(elapsed_ms);

  convert_to_dmx();
  return leds_;
}

void LedEngine::convert_to_dmx() {
  if (!dmx_send_fn_) return;

  // 按 universe 分组发送 DMX
  for (const auto& seg : config_.segments) {
    int channels_per_led = (seg.color_order == ColorOrder::RGBW) ? 4 : 3;
    int buf_size = seg.led_count * channels_per_led;
    if (buf_size > 512) buf_size = 512;  // DMX 最多 512 通道

    std::vector<uint8_t> dmx(512, 0);  // DMX start code + 512 channels

    for (int i = 0; i < seg.led_count && i < 512 / channels_per_led; ++i) {
      int led_idx = seg.start_index + i;
      if (led_idx < 0 || led_idx >= static_cast<int>(leds_.size())) continue;

      const Pixel& p = leds_[led_idx];
      int dmx_idx = (seg.dmx_start_addr - 1) + i * channels_per_led;

      switch (seg.color_order) {
        case ColorOrder::RGB:
          if (dmx_idx + 2 < 512) {
            dmx[dmx_idx] = p.r;
            dmx[dmx_idx + 1] = p.g;
            dmx[dmx_idx + 2] = p.b;
          }
          break;
        case ColorOrder::RGBW:
          if (dmx_idx + 3 < 512) {
            dmx[dmx_idx] = p.r;
            dmx[dmx_idx + 1] = p.g;
            dmx[dmx_idx + 2] = p.b;
            dmx[dmx_idx + 3] = p.w;
          }
          break;
        case ColorOrder::BRG:
          if (dmx_idx + 2 < 512) {
            dmx[dmx_idx] = p.b;
            dmx[dmx_idx + 1] = p.r;
            dmx[dmx_idx + 2] = p.g;
          }
          break;
        case ColorOrder::GRB:
          if (dmx_idx + 2 < 512) {
            dmx[dmx_idx] = p.g;
            dmx[dmx_idx + 1] = p.r;
            dmx[dmx_idx + 2] = p.b;
          }
          break;
        case ColorOrder::WRGB:
          if (dmx_idx + 3 < 512) {
            dmx[dmx_idx] = p.w;
            dmx[dmx_idx + 1] = p.r;
            dmx[dmx_idx + 2] = p.g;
            dmx[dmx_idx + 3] = p.b;
          }
          break;
      }
    }

    dmx_send_fn_(seg.dmx_universe, dmx.data() + 1, std::min(buf_size, 512));
  }
}

std::vector<uint8_t> LedEngine::get_dmx_output(int universe) const {
  std::vector<uint8_t> dmx(512, 0);
  for (const auto& seg : config_.segments) {
    if (seg.dmx_universe != universe) continue;
    int channels_per_led = (seg.color_order == ColorOrder::RGBW) ? 4 : 3;
    for (int i = 0; i < seg.led_count && i < 512 / channels_per_led; ++i) {
      int led_idx = seg.start_index + i;
      if (led_idx < 0 || led_idx >= static_cast<int>(leds_.size())) continue;
      const Pixel& p = leds_[led_idx];
      int dmx_idx = (seg.dmx_start_addr - 1) + i * channels_per_led;
      if (seg.color_order == ColorOrder::RGBW || seg.color_order == ColorOrder::WRGB) {
        if (dmx_idx + 3 < 512) {
          if (seg.color_order == ColorOrder::RGBW) {
            dmx[dmx_idx] = p.r; dmx[dmx_idx + 1] = p.g;
            dmx[dmx_idx + 2] = p.b; dmx[dmx_idx + 3] = p.w;
          } else {
            dmx[dmx_idx] = p.w; dmx[dmx_idx + 1] = p.r;
            dmx[dmx_idx + 2] = p.g; dmx[dmx_idx + 3] = p.b;
          }
        }
      } else {
        if (dmx_idx + 2 < 512) {
          if (seg.color_order == ColorOrder::BRG) {
            dmx[dmx_idx] = p.b; dmx[dmx_idx + 1] = p.r; dmx[dmx_idx + 2] = p.g;
          } else if (seg.color_order == ColorOrder::GRB) {
            dmx[dmx_idx] = p.g; dmx[dmx_idx + 1] = p.r; dmx[dmx_idx + 2] = p.b;
          } else {
            dmx[dmx_idx] = p.r; dmx[dmx_idx + 1] = p.g; dmx[dmx_idx + 2] = p.b;
          }
        }
      }
    }
  }
  return dmx;
}

void LedEngine::set_dmx_send_fn(DmxSendFn fn) {
  dmx_send_fn_ = std::move(fn);
}

nlohmann::json LedEngine::status() const {
  nlohmann::json j;
  j["running"] = running_;
  j["total_leds"] = config_.total_leds;
  j["segments"] = config_.segments.size();
  j["mode"] = config_.mode == LedMode::VideoStream ? "video" : "static";
  return j;
}

} // namespace led
} // namespace sm

// LED 灰度灯带双模式引擎
// 需求：灰度灯带双模式（视频流模式 + 静态效果模式）
// 功能：
//   1. 灯带拓扑定义（DMX 地址分配、分段、颜色顺序 RGB/RGBW）
//   2. 视频流模式：从视频帧提取像素 → 映射到灯带
//   3. 静态效果模式：预设动画（渐变、追逐、闪烁、彩虹）
//   4. DMX 输出（Art-Net / sACN E1.31）
//   5. 灰度等级控制（8bit/16bit）
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace led {

// 灯带模式
enum class LedMode {
  VideoStream,  // 视频流模式：从视频帧映射像素
  StaticEffect  // 静态效果模式：预设动画
};

// 颜色顺序
enum class ColorOrder {
  RGB,
  RGBW,
  BRG,
  GRB,
  WRGB
};

// 静态效果类型
enum class StaticEffect {
  SolidColor,     // 纯色
  Gradient,       // 渐变
  Chase,          // 追逐
  Blink,          // 闪烁
  Rainbow,        // 彩虹
  Breathe,        // 呼吸
  Strobe,         // 频闪
  Comet,          // 彗星
  ColorWipe,      // 逐行填充
  TheaterChase    // 戏院追逐
};

// 灯段定义
struct LedSegment {
  int start_index;          // 灯段起始 LED 序号
  int led_count;            // LED 数量
  ColorOrder color_order;   // 颜色顺序
  int dmx_start_addr;       // DMX 起始地址（1-512）
  int dmx_universe;         // Art-Net universe
  bool reverse;             // 是否反向
};

// 灯带配置
struct LedConfig {
  LedMode mode = LedMode::StaticEffect;
  std::vector<LedSegment> segments;
  int total_leds = 0;
  int grayscale_bits = 8;  // 8 or 16
  int update_rate_hz = 60;
};

// RGB 像素值
struct Pixel {
  uint8_t r, g, b;
  uint8_t w = 0;  // 白色通道（RGBW）
};

class LedEngine {
public:
  LedEngine();
  ~LedEngine();

  // 初始化
  bool init(const LedConfig& cfg);
  void start();
  void stop();

  // 设置静态效果
  void set_static_effect(StaticEffect type, const Pixel& color,
                         int speed_ms = 50);

  // 视频流模式：输入一帧 RGB24
  void process_video_frame(const uint8_t* rgb, int width, int height);

  // 更新（按 update_rate_hz 调用）
  // 返回当前所有 LED 的像素值
  std::vector<Pixel> update(int64_t elapsed_ms);

  // 获取 DMX 输出缓冲
  std::vector<uint8_t> get_dmx_output(int universe) const;

  // 设置 DMX 发送回调
  using DmxSendFn = std::function<bool(int universe, const uint8_t* data, int len)>;
  void set_dmx_send_fn(DmxSendFn fn);

  // 获取状态
  nlohmann::json status() const;

private:
  LedConfig config_;
  StaticEffect effect_ = StaticEffect::SolidColor;
  Pixel effect_color_{255, 0, 0, 0};
  int effect_speed_ms_ = 50;
  std::vector<Pixel> leds_;
  DmxSendFn dmx_send_fn_;
  bool running_ = false;

  void apply_static_effect(int64_t elapsed_ms);
  Pixel color_blend(const Pixel& a, const Pixel& b, double t);
  void convert_to_dmx();
};

} // namespace led
} // namespace sm

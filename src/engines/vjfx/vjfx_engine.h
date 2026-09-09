// VJ 实时 GPU 特效引擎
// 需求：VJ 实时 GPU 特效（视觉特效、混合模式、着色器、转场）
// 功能：
//   1. 特效链管线（多特效串联）
//   2. GPU 着色器加载与编译（HLSL / GLSL 条件编译）
//   3. 混合模式（Normal / Add / Multiply / Screen / Overlay）
//   4. 实时参数调节（亮度/对比度/饱和度/色相/模糊/扭曲）
//   5. 转场特效（Crossfade / Wipe / Slide / Zoom）
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace vjfx {

// 混合模式
enum class BlendMode {
  Normal,     // src
  Add,        // src + dst
  Multiply,   // src * dst
  Screen,     // 1 - (1-src) * (1-dst)
  Overlay,    // 条件混合
  Subtract,   // dst - src
  Difference  // |src - dst|
};

// 特效类型
enum class EffectType {
  Brightness,    // 亮度
  Contrast,      // 对比度
  Saturation,    // 饱和度
  HueShift,      // 色相偏移
  GaussianBlur,  // 高斯模糊
  ZoomBlur,      // 缩放模糊
  Pixelate,      // 像素化
  Distort,       // 扭曲
  Kaleidoscope,  // 万花筒
  ColorBalance,  // 色彩平衡
  vignette,      // 暗角
  FilmGrain      // 胶片颗粒
};

// 转场类型
enum class TransitionType {
  Crossfade,
  WipeLeft, WipeRight, WipeUp, WipeDown,
  SlideLeft, SlideRight,
  ZoomIn, ZoomOut,
  Dissolve
};

// 特效参数
struct EffectParam {
  EffectType type;
  double intensity = 1.0;  // 0.0 ~ 1.0
  double param1 = 0.0;     // 额外参数
  double param2 = 0.0;
};

// 特效链
struct EffectChain {
  std::vector<EffectParam> effects;
  BlendMode blend_mode = BlendMode::Normal;
  double opacity = 1.0;
};

// 转场状态
struct Transition {
  TransitionType type = TransitionType::Crossfade;
  int64_t duration_ms = 1000;
  int64_t elapsed_ms = 0;
  double progress = 0.0;  // 0.0 ~ 1.0
};

class VjfxEngine {
public:
  VjfxEngine();
  ~VjfxEngine();

  // 初始化（Windows: D3D11 着色器编译；macOS: stub）
  bool init();
  void start();
  void stop();

  // 设置当前特效链
  void set_effect_chain(const EffectChain& chain);
  EffectChain get_effect_chain() const;

  // 启动转场
  void start_transition(TransitionType type, int64_t duration_ms);
  // 更新转场进度
  void update_transition(int64_t delta_ms);
  // 转场是否完成
  bool is_transition_done() const;

  // 注册 GPU 帧回调（处理后的帧数据回传）
  // 输入帧为 RGB24 格式，输出为处理后的 RGB24
  using ProcessFrameFn = std::function<std::vector<uint8_t>(
      const uint8_t* rgb_in, int width, int height)>;
  void set_process_fn(ProcessFrameFn fn);

  // CPU 侧处理一帧（GPU 不可用时回退到 CPU 实现）
  std::vector<uint8_t> process_frame_cpu(
      const uint8_t* rgb_in, int width, int height);

  // 获取当前状态
  nlohmann::json status() const;

private:
  EffectChain chain_;
  Transition transition_;
  ProcessFrameFn process_fn_;
  bool initialized_ = false;
  bool running_ = false;
};

} // namespace vjfx
} // namespace sm

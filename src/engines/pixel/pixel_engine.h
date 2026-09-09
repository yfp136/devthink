// 8K 播控引擎
// 需求：8K 播控（8K 分辨率视频播放、多屏拼接、同步播放）
// 功能：
//   1. 8K 视频解码与渲染（D3D12 纹管）
//   2. 多屏拼接（2x2 / 3x1 / 1x3 等拼接模式）
//   3. 多屏同步播放（帧同步，避免撕裂）
//   4. 输出分辨率管理（8K / 4K / 1080p 自适应）
//   5. 帧率匹配（24/30/60 fps）
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace pixel {

// 拼接模式
enum class TilingMode {
  Single,     // 单屏
  Grid2x2,   // 2x2 拼接
  Grid3x1,   // 3x1 横向
  Grid1x3,   // 1x3 纵向
  Grid2x1,   // 2x1
  Grid1x2,   // 1x2
  Custom     // 自定义
};

// 输出分辨率
struct Resolution {
  int width;
  int height;
  int refresh_rate;  // Hz
};

// 显示输出
struct DisplayOutput {
  int output_index;       // 输出序号
  Resolution resolution;
  int x_offset;           // 拼接偏移 X
  int y_offset;           // 拼接偏移 Y
  bool enabled;
};

// 播放状态
enum class PlayState {
  Idle,
  Playing,
  Paused,
  Stopped
};

class PixelEngine {
public:
  PixelEngine();
  ~PixelEngine();

  // 初始化
  bool init();
  void start();
  void stop();

  // 输出管理
  void add_output(const DisplayOutput& out);
  void remove_output(int index);
  std::vector<DisplayOutput> get_outputs() const;

  // 拼接模式
  void set_tiling_mode(TilingMode mode);
  TilingMode get_tiling_mode() const;

  // 播放控制
  bool play(const std::string& media_id);
  void pause();
  void resume();
  void stop_playback();
  void seek(int64_t position_ms);

  // 获取播放状态
  PlayState state() const;
  int64_t position_ms() const;
  int64_t duration_ms() const;

  // 帧同步（多输出帧对齐）
  void sync_frames();

  // 获取当前帧的 PGM 输出
  // 返回每个输出的 RGB 缓冲
  using FrameCallback = std::function<void(int output_index,
      const uint8_t* rgb, int width, int height)>;
  void set_frame_callback(FrameCallback cb);

  // 获取状态
  nlohmann::json status() const;

private:
  std::vector<DisplayOutput> outputs_;
  TilingMode tiling_ = TilingMode::Single;
  PlayState play_state_ = PlayState::Idle;
  std::string current_media_;
  int64_t position_ms_ = 0;
  int64_t duration_ms_ = 0;
  FrameCallback frame_cb_;
  bool running_ = false;
};

} // namespace pixel
} // namespace sm

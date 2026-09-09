// 8K 播控引擎实现
#include "engines/pixel/pixel_engine.h"

#include <algorithm>
#include <cstdio>

namespace sm {
namespace pixel {

PixelEngine::PixelEngine() = default;
PixelEngine::~PixelEngine() { stop(); }

bool PixelEngine::init() {
  // 检测可用显示输出
  // Windows: EnumDisplayMonitors
  // 当前：默认 1 个 1080p 输出
  if (outputs_.empty()) {
    outputs_.push_back({0, {1920, 1080, 60}, 0, 0, true});
  }
  return true;
}

void PixelEngine::start() { running_ = true; }
void PixelEngine::stop() { running_ = false; }

void PixelEngine::add_output(const DisplayOutput& out) {
  outputs_.push_back(out);
}

void PixelEngine::remove_output(int index) {
  if (index >= 0 && index < static_cast<int>(outputs_.size()))
    outputs_.erase(outputs_.begin() + index);
}

std::vector<DisplayOutput> PixelEngine::get_outputs() const {
  return outputs_;
}

void PixelEngine::set_tiling_mode(TilingMode mode) {
  tiling_ = mode;
  // 根据模式重新计算偏移
  switch (mode) {
    case TilingMode::Single:
      outputs_.clear();
      outputs_.push_back({0, {7680, 4320, 60}, 0, 0, true});  // 8K
      break;
    case TilingMode::Grid2x2:
      outputs_.clear();
      for (int i = 0; i < 4; ++i) {
        int x = (i % 2) * 3840;
        int y = (i / 2) * 2160;
        outputs_.push_back({i, {3840, 2160, 60}, x, y, true});
      }
      break;
    case TilingMode::Grid3x1:
      outputs_.clear();
      for (int i = 0; i < 3; ++i)
        outputs_.push_back({i, {2560, 1440, 60}, i * 2560, 0, true});
      break;
    case TilingMode::Grid1x3:
      outputs_.clear();
      for (int i = 0; i < 3; ++i)
        outputs_.push_back({i, {2560, 1440, 60}, 0, i * 1440, true});
      break;
    case TilingMode::Grid2x1:
      outputs_.clear();
      for (int i = 0; i < 2; ++i)
        outputs_.push_back({i, {3840, 2160, 60}, i * 3840, 0, true});
      break;
    case TilingMode::Grid1x2:
      outputs_.clear();
      for (int i = 0; i < 2; ++i)
        outputs_.push_back({i, {3840, 2160, 60}, 0, i * 2160, true});
      break;
    default:
      break;
  }
}

TilingMode PixelEngine::get_tiling_mode() const {
  return tiling_;
}

bool PixelEngine::play(const std::string& media_id) {
  current_media_ = media_id;
  play_state_ = PlayState::Playing;
  std::printf("[pixel] 播放: %s (输出: %zu)\n",
              media_id.c_str(), outputs_.size());
  return true;
}

void PixelEngine::pause() { play_state_ = PlayState::Paused; }
void PixelEngine::resume() { play_state_ = PlayState::Playing; }
void PixelEngine::stop_playback() {
  play_state_ = PlayState::Stopped;
  position_ms_ = 0;
}

void PixelEngine::seek(int64_t pos_ms) {
  position_ms_ = pos_ms;
}

PlayState PixelEngine::state() const { return play_state_; }
int64_t PixelEngine::position_ms() const { return position_ms_; }
int64_t PixelEngine::duration_ms() const { return duration_ms_; }

void PixelEngine::sync_frames() {
  // 多输出帧对齐：等待所有输出完成当前帧渲染
  // D3D12: 使用 fence 等待
  // 当前：空实现（单输出不需要同步）
}

void PixelEngine::set_frame_callback(FrameCallback cb) {
  frame_cb_ = std::move(cb);
}

nlohmann::json PixelEngine::status() const {
  nlohmann::json j;
  j["running"] = running_;
  j["state"] = static_cast<int>(play_state_);
  j["outputs"] = nlohmann::json::array();
  for (const auto& o : outputs_) {
    j["outputs"].push_back({
      {"index", o.output_index},
      {"width", o.resolution.width},
      {"height", o.resolution.height},
      {"rate", o.resolution.refresh_rate},
      {"enabled", o.enabled}
    });
  }
  j["tiling"] = static_cast<int>(tiling_);
  return j;
}

} // namespace pixel
} // namespace sm

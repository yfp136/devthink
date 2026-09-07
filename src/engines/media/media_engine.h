// M2 MediaEngine（§10.2）媒体播放引擎
// 职责：媒体解码播放、音量/淡变、帧输出。P1 纯逻辑状态机 + 回调接口。
// 平台层（Windows: FFmpeg + WASAPI + D3D11；macOS: stub）通过回调注入。
//
// 状态机（§10.2.4）：idle → loading → playing ⇄ paused → stopped → idle
//                    任意状态 → error → idle
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "nlohmann/json.hpp"

namespace sm {
namespace media {

enum class PlayState {
  idle,
  loading,
  playing,
  paused,
  stopped,
  error,
};

const char* play_state_name(PlayState s);

// 媒体条目播放参数（§10.2.4 条目语义）
struct ItemPlayConfig {
  std::string media_id;
  std::string media_path;       // 媒体文件路径
  int64_t trim_in_ms = 0;       // 素材内入点偏移
  int64_t duration_ms = 0;      // 播放时长（0=素材全长）
  double gain_db = 0.0;         // 增益 dB（-60..6）
  int64_t fade_in_ms = 0;       // 淡入
  int64_t fade_out_ms = 0;      // 淡出
  int loop = 0;                 // 循环次数
  std::string output = "pgm";   // 输出目标（P1 仅 pgm）
};

// 平台回调（由实际 FFmpeg/WASAPI/D3D 实现注入）
// 打开媒体文件（异步），返回 true 表示开始加载
using OpenMediaFn = std::function<bool(const std::string& media_path,
                                        int64_t trim_in_ms)>;
// 开始播放（已加载完成）
using StartPlaybackFn = std::function<void(double gain_db, int64_t fade_in_ms)>;
// 停止播放
using StopPlaybackFn = std::function<void(int64_t fade_out_ms)>;
// Seek 到指定位置
using SeekFn = std::function<void(int64_t pos_ms)>;
// 获取当前播放位置（ms）
using GetPosFn = std::function<int64_t()>;
// 获取媒体时长（ms）
using GetDurationFn = std::function<int64_t(const std::string& media_path)>;
// 释放资源
using CloseFn = std::function<void()>;

// 播放完成回调
using PlaybackEndedFn = std::function<void(const std::string& item_id,
                                            const std::string& reason)>;

class MediaEngine {
 public:
  MediaEngine();
  ~MediaEngine();

  // ---- 平台回调注入 ----
  void set_open_cb(OpenMediaFn cb) { open_cb_ = std::move(cb); }
  void set_start_cb(StartPlaybackFn cb) { start_cb_ = std::move(cb); }
  void set_stop_cb(StopPlaybackFn cb) { stop_cb_ = std::move(cb); }
  void set_seek_cb(SeekFn cb) { seek_cb_ = std::move(cb); }
  void set_pos_cb(GetPosFn cb) { pos_cb_ = std::move(cb); }
  void set_duration_cb(GetDurationFn cb) { duration_cb_ = std::move(cb); }
  void set_close_cb(CloseFn cb) { close_cb_ = std::move(cb); }
  void set_ended_cb(PlaybackEndedFn cb) { ended_cb_ = std::move(cb); }

  // ---- 条目播放控制（由 M5 TimelineScheduler 调用）----
  // 预载（§10.2.5：条目开始前 20ms 调用）
  bool preload(const std::string& item_id, const std::string& media_id,
               const nlohmann::json& meta);
  // 开始播放
  void play(const std::string& item_id, const std::string& media_id,
            const nlohmann::json& meta);
  // 停止
  void stop(const std::string& item_id);
  // 素材预览（§10.2.5 media.preview）
  void preview(const std::string& media_path);

  // ---- 状态查询 ----
  PlayState state() const { return state_.load(); }
  std::string current_item() const { return current_item_; }
  int64_t pos_ms() const;
  int64_t duration_ms() const;

  // ---- 增益/淡变计算（纯逻辑，可供测试验证）----
  // 给定播放进度和配置，返回当前应应用的线性增益（0.0-1.0+）
  // 用于验证 fade 包络正确性
  static double calc_linear_gain(int64_t pos_ms, int64_t duration_ms,
                                 double gain_db, int64_t fade_in_ms,
                                 int64_t fade_out_ms);
  // dB → 线性增益
  static double db_to_linear(double db);
  // 线性斜坡（防爆音 §10.2.2：斜坡 ≤ 2ms）
  static double ramp_gain(double from, double to, int64_t elapsed_ms,
                          int64_t ramp_ms = 2);

 private:
  std::atomic<PlayState> state_{PlayState::idle};
  std::string current_item_;
  std::string current_media_;
  ItemPlayConfig current_config_;

  OpenMediaFn open_cb_;
  StartPlaybackFn start_cb_;
  StopPlaybackFn stop_cb_;
  SeekFn seek_cb_;
  GetPosFn pos_cb_;
  GetDurationFn duration_cb_;
  CloseFn close_cb_;
  PlaybackEndedFn ended_cb_;

  // 从 meta 解析播放配置
  static ItemPlayConfig parse_config(const std::string& media_id,
                                     const nlohmann::json& meta);
  // 进入 error 状态
  void enter_error(const std::string& reason);
  // 重置到 idle
  void reset_to_idle();
};

}  // namespace media
}  // namespace sm

// MediaEngine 实现（§10.2）
#include "engines/media/media_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sm {
namespace media {

const char* play_state_name(PlayState s) {
  switch (s) {
    case PlayState::idle:    return "idle";
    case PlayState::loading: return "loading";
    case PlayState::playing: return "playing";
    case PlayState::paused:  return "paused";
    case PlayState::stopped: return "stopped";
    case PlayState::error:   return "error";
  }
  return "unknown";
}

MediaEngine::MediaEngine() = default;
MediaEngine::~MediaEngine() { reset_to_idle(); }

// ---- dB / 增益计算（§10.2.2 防爆音）----
double MediaEngine::db_to_linear(double db) {
  // 0dB = 1.0, -6dB ≈ 0.5, -20dB ≈ 0.1, -60dB ≈ 0.001（静音阈值）
  if (db <= -60.0) return 0.0;
  return std::pow(10.0, db / 20.0);
}

double MediaEngine::ramp_gain(double from, double to, int64_t elapsed_ms,
                              int64_t ramp_ms) {
  if (ramp_ms <= 0) return to;
  double t = std::clamp(double(elapsed_ms) / double(ramp_ms), 0.0, 1.0);
  return from + (to - from) * t;
}

double MediaEngine::calc_linear_gain(int64_t pos_ms, int64_t duration_ms,
                                     double gain_db, int64_t fade_in_ms,
                                     int64_t fade_out_ms) {
  double target = db_to_linear(gain_db);

  // 淡入阶段
  if (fade_in_ms > 0 && pos_ms < fade_in_ms) {
    double t = double(pos_ms) / double(fade_in_ms);
    return target * t;  // 线性淡入
  }

  // 淡出阶段
  if (fade_out_ms > 0 && pos_ms > duration_ms - fade_out_ms) {
    double remaining = double(duration_ms - pos_ms) / double(fade_out_ms);
    return target * std::clamp(remaining, 0.0, 1.0);
  }

  return target;
}

// ---- 配置解析 ----
ItemPlayConfig MediaEngine::parse_config(
    const std::string& media_id, const nlohmann::json& meta) {
  ItemPlayConfig cfg;
  cfg.media_id = media_id;
  cfg.trim_in_ms = meta.value("trim_in_ms", 0);
  cfg.duration_ms = meta.value("duration_ms", 0);
  cfg.gain_db = meta.value("gain_db", 0.0);
  cfg.fade_in_ms = meta.value("fade_in_ms", 0);
  cfg.fade_out_ms = meta.value("fade_out_ms", 0);
  cfg.loop = meta.value("loop", 0);
  cfg.output = meta.value("output", std::string("pgm"));
  return cfg;
}

// ---- 条目播放控制 ----
bool MediaEngine::preload(const std::string& item_id,
                          const std::string& media_id,
                          const nlohmann::json& meta) {
  // §10.2.5：预览播放中收到时间线条目起播请求时，自动停止预览
  if (state_.load() == PlayState::playing &&
    current_item_.empty()) {  // 预览模式（无 item_id）
    stop("");
  }

  if (state_.load() == PlayState::idle || state_.load() == PlayState::stopped) {
    state_ = PlayState::loading;
    current_item_ = item_id;
    current_media_ = media_id;
    current_config_ = parse_config(media_id, meta);

    if (open_cb_) {
      if (!open_cb_(current_config_.media_path.empty() ? media_id
                                                        : current_config_.media_path,
                     current_config_.trim_in_ms)) {
        enter_error("open_failed");
        return false;
      }
    }
    // open_cb_ 异步加载；加载完成后由平台层通知（此处简化为同步完成）
    return true;
  }

  // 已在播放其他条目 → 自动停止再加载（§10.2.5 单源互斥）
  if (state_.load() == PlayState::playing ||
      state_.load() == PlayState::paused) {
    stop(current_item_);
    state_ = PlayState::loading;
    current_item_ = item_id;
    current_media_ = media_id;
    current_config_ = parse_config(media_id, meta);
    if (open_cb_) {
      open_cb_(current_config_.media_path.empty() ? media_id
                                                   : current_config_.media_path,
               current_config_.trim_in_ms);
    }
    return true;
  }

  return false;
}

void MediaEngine::play(const std::string& item_id,
                       const std::string& media_id,
                       const nlohmann::json& meta) {
  // 如果未预载，先预载
  if (state_.load() == PlayState::idle ||
      state_.load() == PlayState::stopped ||
      current_item_ != item_id) {
    preload(item_id, media_id, meta);
  }

  if (state_.load() == PlayState::loading || state_.load() == PlayState::paused) {
    state_ = PlayState::playing;
    if (start_cb_)
      start_cb_(current_config_.gain_db, current_config_.fade_in_ms);
  }
}

void MediaEngine::stop(const std::string& item_id) {
  if (state_.load() == PlayState::playing ||
      state_.load() == PlayState::paused) {
    if (stop_cb_)
      stop_cb_(current_config_.fade_out_ms);
  }
  reset_to_idle();
}

void MediaEngine::preview(const std::string& media_path) {
  // §10.2.5：预览与时间线共用同一实例，互斥
  if (state_.load() == PlayState::playing && !current_item_.empty()) {
    // 时间线正在播放 → 不抢占（由调用方决定）
    if (ended_cb_)
      ended_cb_("", "busy");
    return;
  }

  reset_to_idle();
  state_ = PlayState::loading;
  current_item_ = "";  // 空表示预览模式
  current_media_ = media_path;
  if (open_cb_)
    open_cb_(media_path, 0);
  // 加载完成后由平台层通知转 playing
  state_ = PlayState::playing;
  if (start_cb_)
    start_cb_(0.0, 0);
}

int64_t MediaEngine::pos_ms() const {
  if (pos_cb_) return pos_cb_();
  return 0;
}

int64_t MediaEngine::duration_ms() const {
  if (duration_cb_ && !current_media_.empty())
    return duration_cb_(current_media_);
  return current_config_.duration_ms;
}

void MediaEngine::enter_error(const std::string& reason) {
  state_ = PlayState::error;
  if (ended_cb_ && !current_item_.empty())
    ended_cb_(current_item_, "error:" + reason);
  // 错误后回到 idle
  reset_to_idle();
}

void MediaEngine::reset_to_idle() {
  if (close_cb_) close_cb_();
  state_ = PlayState::idle;
  current_item_.clear();
  current_media_.clear();
  current_config_ = ItemPlayConfig{};
}

}  // namespace media
}  // namespace sm

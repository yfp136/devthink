// 平台适配层实现
// macOS：stub（不链接外部库）
// Windows：条件编译，在 SM_HAS_FFMPEG / SM_HAS_D3D11 / SM_HAS_WASAPI 时链接真实实现
#include "platform/media_backend.h"

#include <cstdio>

namespace sm {
namespace platform {

// ---- 平台能力查询 ----
bool has_media_backend() {
#if defined(SM_HAS_FFMPEG)
  return true;
#else
  return false;
#endif
}

// ---- macOS stub 实现 ----
#if !defined(SM_HAS_FFMPEG)

nlohmann::json probe_media(const std::string& file_path) {
  // stub：返回空对象，上层回退到手动元信息
  (void)file_path;
  return nlohmann::json::object();
}

bool open_media_file(const std::string& file_path, int64_t trim_in_ms) {
  (void)file_path;
  (void)trim_in_ms;
  return true;  // stub：假装成功
}

void start_media_playback(double gain_db, int64_t fade_in_ms) {
  (void)gain_db;
  (void)fade_in_ms;
}

void stop_media_playback(int64_t fade_out_ms) {
  (void)fade_out_ms;
}

int64_t get_media_pos_ms() {
  return 0;
}

int64_t get_media_duration_ms(const std::string& file_path) {
  (void)file_path;
  return 0;
}

void close_media_file() {}

std::string generate_thumbnail(const std::string& file_path,
                                int64_t duration_ms) {
  (void)file_path;
  (void)duration_ms;
  return "";  // stub：无缩略图
}

std::string capture_pgm_frame_jpeg() {
  return "";  // stub：无 PGM 帧
}

bool init_audio_output() {
  return true;  // stub：假装成功
}

void shutdown_audio_output() {}

// ---- Windows FFmpeg 真实实现 ----
#else

// FFmpeg 头文件在 CMakeLists.txt 中通过 target_include_directories 设置
// 链接库在 target_link_libraries 中配置
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

nlohmann::json probe_media(const std::string& file_path) {
  nlohmann::json result;
  AVFormatContext* fmt_ctx = nullptr;

  if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0) {
    return result;  // 打开失败
  }

  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    avformat_close_input(&fmt_ctx);
    return result;
  }

  // 时长（微秒 → 毫秒）
  result["duration_ms"] = fmt_ctx->duration / 1000;

  // 遍历流
  int video_stream = -1, audio_stream = -1;
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
    const AVCodecParameters* par = fmt_ctx->streams[i]->codecpar;
    if (par->codec_type == AVMEDIA_TYPE_VIDEO && video_stream < 0) {
      video_stream = int(i);
      result["width"] = par->width;
      result["height"] = par->height;
      AVRational fr = fmt_ctx->streams[i]->avg_frame_rate;
      result["fps"] = fr.den > 0 ? double(fr.num) / fr.den : 0.0;
      result["codec"] = avcodec_get_name(par->codec_id);
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream < 0) {
      audio_stream = int(i);
      result["audio_sr"] = par->sample_rate;
      result["audio_ch"] = par->channels;
    }
  }

  avformat_close_input(&fmt_ctx);
  return result;
}

bool open_media_file(const std::string& file_path, int64_t trim_in_ms) {
  // 简化实现：仅打开文件验证可用性
  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0)
    return false;
  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    avformat_close_input(&fmt_ctx);
    return false;
  }
  avformat_close_input(&fmt_ctx);
  (void)trim_in_ms;
  return true;
}

void start_media_playback(double gain_db, int64_t fade_in_ms) {
  // 完整实现需要：打开解码器、创建 WASAPI 输出、启动解码线程
  // Phase 2 逐步实现
  (void)gain_db;
  (void)fade_in_ms;
}

void stop_media_playback(int64_t fade_out_ms) {
  (void)fade_out_ms;
}

int64_t get_media_pos_ms() {
  return 0;
}

int64_t get_media_duration_ms(const std::string& file_path) {
  AVFormatContext* fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0)
    return 0;
  int64_t dur = fmt_ctx->duration / 1000;
  avformat_close_input(&fmt_ctx);
  return dur;
}

void close_media_file() {}

std::string generate_thumbnail(const std::string& file_path,
                                int64_t duration_ms) {
  // 完整实现：seek 到 1s、解码一帧、缩放 320px、JPEG 编码 → base64
  // Phase 2 逐步实现
  (void)file_path;
  (void)duration_ms;
  return "";
}

std::string capture_pgm_frame_jpeg() {
  // 完整实现：D3D11 抓取后台缓冲 → JPEG 编码 → base64
  // Phase 2 逐步实现
  return "";
}

bool init_audio_output() {
  // WASAPI 初始化
  return true;
}

void shutdown_audio_output() {}

#endif  // SM_HAS_FFMPEG

}  // namespace platform
}  // namespace sm

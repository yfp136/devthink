// Phase 2 平台适配层：FFmpeg / WASAPI / D3D11 条件编译
// Windows：真实实现（链接 FFmpeg avcodec/avformat/avutil + WASAPI + D3D11）
// macOS/Linux：stub 编译（不链接任何外部库）
//
// 本文件提供统一的接口，kernel.cpp 通过这些接口注入真实回调。
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "nlohmann/json.hpp"

namespace sm {
namespace platform {

// 判断当前平台是否有真实媒体后端（FFmpeg）
bool has_media_backend();

// ---- FFmpeg 媒体探测 ----
// 返回 {duration_ms, width, height, fps, codec, audio_sr, audio_ch}
nlohmann::json probe_media(const std::string& file_path);

// ---- FFmpeg 打开 + 解码 ----
// 打开媒体文件，返回是否成功。trim_in_ms 为素材内入点偏移。
bool open_media_file(const std::string& file_path, int64_t trim_in_ms);

// 开始播放（已打开的文件）
void start_media_playback(double gain_db, int64_t fade_in_ms);

// 停止播放（fade_out_ms 淡出后关闭）
void stop_media_playback(int64_t fade_out_ms);

// 获取当前播放位置（ms）
int64_t get_media_pos_ms();

// 获取媒体时长（ms）
int64_t get_media_duration_ms(const std::string& file_path);

// 关闭已打开的媒体文件
void close_media_file();

// ---- 缩略图生成 ----
// 返回 JPEG base64（抓取视频第 1 秒或图片缩放至 320px 宽）
std::string generate_thumbnail(const std::string& file_path, int64_t duration_ms);

// ---- D3D11 PGM 帧捕获 ----
// 返回当前 PGM 帧的 JPEG base64（用于 WebSocket 推送到浏览器）
// 如果无 D3D11 渲染上下文，返回空字符串
std::string capture_pgm_frame_jpeg();

// ---- WASAPI 音频初始化 ----
// 初始化默认音频输出设备
bool init_audio_output();

// 释放音频输出
void shutdown_audio_output();

}  // namespace platform
}  // namespace sm

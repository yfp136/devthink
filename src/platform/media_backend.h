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

// ---- D3D11 本地窗口输出（规格第 2 章 1080p60 输出视口 / P1-2 收尾）----
// 语义：把 PGM 合成结果直出到调用方提供的原生窗口（Windows = HWND），
// 与 capture_pgm_frame_jpeg() 共用同一次合成（一次合成 → 窗口直出 + 预监回读）。
//
// 线程模型（与 rhi.h 的单所有者约束一致，调用方须遵守）：
//   · 本组接口线程安全（内部互斥），GUI 线程可随时投递句柄/尺寸；
//   · 交换链（IOutputSurface）与 IDevice 同属**渲染所有者线程**，即
//     capture_pgm_frame_jpeg() 的调用线程 —— 链的创建/重建/呈现全部在该线程
//     的捕获路径内延迟完成，故 GUI 线程（Qt）从不触碰 DXGI 对象。
//
// 失败语义：无窗口输出能力 / 参数非法 / 建链或呈现失败 → 自动降级为
// 「仅离屏 + 预监」，**不得视为致命错误**（headless、CI 无显示环境、远程桌面
// 与老驱动均属预期场景）；原因可用 last_output_window_error() 取证。

// 当前平台是否有本地窗口输出能力（Windows + D3D11 → true）。
// 无能力时 open_output_window() 恒返回 false，调用方据此静默降级。
bool has_output_window_capability();

// 请求在 native_window 上输出 PGM 画面（宽高为输出视口像素，如 1920x1080）。
// 返回 false = 参数非法（空句柄 / 尺寸越界，见 rhi.h is_valid_output_window_desc）
// 或无窗口输出能力；返回 true = 请求已登记，实际交换链在所有者线程首次捕获时
// 建立（建立失败亦只降级，不影响 capture 与预监）。
// 重复调用为幂等（同句柄同尺寸不重建；句柄或尺寸变化则重建）。
bool open_output_window(void* native_window, int width, int height);

// 关闭输出窗口（幂等，任意线程可调用）：停止后续呈现并释放交换链。
void close_output_window();

// 是否处于「已请求输出且未判定失败」状态。窗口在所有者线程建立失败后转为
// false（已降级）；再次 open_output_window() 可重新尝试（清除失败标记）。
bool is_output_window_open();

// 窗口尺寸变化通知（异步）：仅登记最新尺寸，实际 ResizeBuffers 由所有者
// 线程下一次捕获时执行；失败时按 rhi.h 契约保持旧尺寸。
void resize_output_window(int width, int height);

// 最近一次输出窗口失败原因（空串 = 无失败）。用于日志/CI 取证。
std::string last_output_window_error();

// ---- WASAPI 音频初始化 ----
// 初始化默认音频输出设备
bool init_audio_output();

// 释放音频输出
void shutdown_audio_output();

}  // namespace platform
}  // namespace sm

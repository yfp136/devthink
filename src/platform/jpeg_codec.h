// 自包含 baseline JPEG 编码器（Phase 2 平台适配层）
//
// 无第三方依赖：RGB24 → JFIF baseline JPEG（YCbCr 4:2:0，固定 85 质量表，
// 标准 Annex K.3 Huffman 表）。在 Windows(FFmpeg 后端) 与 macOS/Linux(stub)
// 上编译同一份源码，纯确定性整数/浮点数学，可 headless 单元测试。
//
// 用途：generate_thumbnail() 与 capture_pgm_frame_jpeg() 把解码得到的 RGB
// 帧编码为浏览器可直接显示的 JPEG，再由上层 base64 封装下发。
#pragma once

#include <cstdint>
#include <vector>

namespace sm {
namespace platform {

// 将 RGB24（自顶向下、逐行连续、无填充）编码为 baseline JPEG 字节流。
// rgb 为空、width/height 非法或超限时返回空 vector。
// row_stride：源图像每行字节数（含填充）；默认 width*3。
std::vector<uint8_t> encode_jpeg_rgb24(const uint8_t* rgb, int width, int height,
                                       int row_stride = -1);

}  // namespace platform
}  // namespace sm

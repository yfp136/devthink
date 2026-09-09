// test_jpeg_codec.cpp - 自包含 baseline JPEG 编码器单元测试。
// 覆盖：参数校验、输出结构（SOI/JFIF/DQT/DHT/SOF0/SOS/EOI）、确定性、多尺寸。
#include "platform/jpeg_codec.h"
#include "test_common.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// 构造渐变 RGB24 图（含彩色与亮度变化）。
std::vector<uint8_t> make_gradient(int w, int h, int seed = 1) {
  std::vector<uint8_t> img(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint8_t* p = &img[(static_cast<size_t>(y) * w + x) * 3];
      p[0] = static_cast<uint8_t>((x * 255 + seed * 13) / (w > 1 ? w - 1 : 1));
      p[1] = static_cast<uint8_t>((y * 255) / (h > 1 ? h - 1 : 1));
      p[2] = static_cast<uint8_t>((x + y) & 0xFF);
    }
  }
  return img;
}

std::vector<uint8_t> make_solid(int w, int h, uint8_t v) {
  std::vector<uint8_t> img(static_cast<size_t>(w) * h * 3);
  std::memset(img.data(), v, img.size());
  return img;
}

bool has_bytes(const std::vector<uint8_t>& d, const uint8_t* pat, size_t n) {
  if (d.size() < n) return false;
  for (size_t i = 0; i + n <= d.size(); ++i) {
    if (std::memcmp(&d[i], pat, n) == 0) return true;
  }
  return false;
}

}  // namespace

int main() {
  using sm::platform::encode_jpeg_rgb24;

  // ---- 参数校验 ----
  {
    std::vector<uint8_t> tiny(64, 0);
    SM_CHECK(encode_jpeg_rgb24(nullptr, 8, 8).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), 0, 8).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), 8, 0).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), -1, 8).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), 8, -3).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), 65536, 8).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), 8, 70000).empty());
    SM_CHECK(encode_jpeg_rgb24(tiny.data(), 8, 8, 23).empty());  // stride 过小
  }

  // ---- 常规尺寸：结构与确定性 ----
  {
    std::vector<uint8_t> img = make_gradient(320, 180);
    auto a = encode_jpeg_rgb24(img.data(), 320, 180);
    auto b = encode_jpeg_rgb24(img.data(), 320, 180);
    SM_CHECK(!a.empty());
    SM_CHECK(a == b);  // 确定性：同输入必同输出

    const uint8_t soi[2] = {0xFF, 0xD8};
    const uint8_t eoi[2] = {0xFF, 0xD9};
    const uint8_t jfif[4] = {'J', 'F', 'I', 'F'};
    const uint8_t sof0[2] = {0xFF, 0xC0};
    const uint8_t sos[2] = {0xFF, 0xDA};

    SM_CHECK(a.size() >= 2 && a[0] == soi[0] && a[1] == soi[1]);
    SM_CHECK(a.size() >= 2 &&
             a[a.size() - 2] == eoi[0] && a[a.size() - 1] == eoi[1]);
    // EOI 必须位于文件末尾。
    SM_CHECK(has_bytes(a, eoi, 2) && a[a.size() - 2] == 0xFF);
    SM_CHECK(has_bytes(a, jfif, 4));
    SM_CHECK(has_bytes(a, sof0, 2));
    SM_CHECK(has_bytes(a, sos, 2));
    size_t ndqt = 0, ndht = 0;
    for (size_t i = 0; i + 1 < a.size(); ++i) {
      if (a[i] == 0xFF && a[i + 1] == 0xDB) ++ndqt;
      if (a[i] == 0xFF && a[i + 1] == 0xC4) ++ndht;
    }
    SM_CHECK_EQ(ndqt, size_t(2));
    SM_CHECK_EQ(ndht, size_t(4));

    // 不同内容 → 不同输出。
    auto solid = encode_jpeg_rgb24(make_solid(64, 64, 200).data(), 64, 64);
    auto grad = encode_jpeg_rgb24(make_gradient(64, 64, 2).data(), 64, 64);
    SM_CHECK(!solid.empty() && !grad.empty());
    SM_CHECK(solid != grad);
  }

  // ---- 边界尺寸（非 8/16 对齐）----
  {
    auto r = encode_jpeg_rgb24(make_gradient(17, 13).data(), 17, 13);
    SM_CHECK(!r.empty());
    SM_CHECK(r.size() >= 2 && r[r.size() - 2] == 0xFF && r[r.size() - 1] == 0xD9);

    auto one = encode_jpeg_rgb24(make_solid(1, 1, 128).data(), 1, 1);
    SM_CHECK(!one.empty());

    // 满色纯图（红/绿/蓝）不崩溃且结构完整。
    std::vector<uint8_t> red(48 * 32 * 3);
    for (size_t i = 0; i < red.size(); i += 3) { red[i] = 255; red[i + 1] = 0; red[i + 2] = 0; }
    auto rr = encode_jpeg_rgb24(red.data(), 48, 32);
    SM_CHECK(!rr.empty() && rr[0] == 0xFF && rr[1] == 0xD8);
  }

  // ---- row_stride 显式支持（带填充行）----
  {
    const int w = 40, h = 24, pad = 4;
    std::vector<uint8_t> img((size_t)w * h * 3, 0);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        uint8_t* p = &img[((size_t)y * w + x) * 3];
        p[0] = static_cast<uint8_t>(x * 6);
        p[1] = static_cast<uint8_t>(y * 10);
        p[2] = 99;
      }
    // 构造带 pad 的缓冲。
    const int stride = w * 3 + pad;
    std::vector<uint8_t> padded((size_t)stride * h, 0xAA);
    for (int y = 0; y < h; ++y)
      std::memcpy(&padded[(size_t)y * stride], &img[(size_t)y * w * 3],
                  (size_t)w * 3);

    auto packed = encode_jpeg_rgb24(padded.data(), w, h, stride);
    auto dense = encode_jpeg_rgb24(img.data(), w, h);
    SM_CHECK(!packed.empty());
    SM_CHECK(packed == dense);  // 行填充被正确跳过
  }

  return smtest::finish("test_jpeg_codec");
}

// test_rhi_geometry.cpp - RHI 输出几何与窗口描述校验单元测试（P1-2）。
// 覆盖：拉伸铺满 / 等比居中（letterbox / pillarbox）/ 尺寸退化 / 不变量
// （不越界、对称、纵横比守恒）/ is_valid_output_window_desc 边界。
// 这些是纯逻辑（rhi.h 内联），可在无 D3D11 的平台上直接验证 —— 因此本
// 用例是"窗口输出几何"在本机（macOS/Linux stub 分支）唯一的自动化防线。
#include "platform/rhi/rhi.h"
#include "test_common.h"

#include <cmath>
#include <cstdint>

namespace {

using sm::rhi::compute_ndc_rect;
using sm::rhi::is_valid_output_window_desc;
using sm::rhi::NdcRect;
using sm::rhi::OutputWindowDesc;

bool near(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

// 全屏（恒等）矩形：x 铺满 [-1,1]、y 铺满 [-1,1]（NDC +y 朝上）。
bool is_identity(const NdcRect& r) {
  return near(r.x0, -1.0f) && near(r.x1, 1.0f) && near(r.y0, 1.0f) &&
         near(r.y1, -1.0f);
}

// 断言矩形四边（逐字段断言，失败信息自带期望值）。
void check_rect(const NdcRect& r, float x0, float x1, float y0, float y1) {
  SM_CHECK(near(r.x0, x0));
  SM_CHECK(near(r.x1, x1));
  SM_CHECK(near(r.y0, y0));
  SM_CHECK(near(r.y1, y1));
}

}  // namespace

int main() {
  // ---- 1. 拉伸铺满（fit_center=false）：恒为恒等矩形，与源尺寸无关 ----
  SM_CHECK(is_identity(compute_ndc_rect(1024, 768, 1920, 1080, false)));
  SM_CHECK(is_identity(compute_ndc_rect(7, 3, 64, 64, false)));
  SM_CHECK(is_identity(compute_ndc_rect(1920, 1080, 1, 1, false)));
  SM_CHECK(is_identity(compute_ndc_rect(0, 0, 1920, 1080, false)));

  // ---- 2. 等比居中 + 同比例：无留边（等价于铺满）----
  SM_CHECK(is_identity(compute_ndc_rect(1920, 1080, 1920, 1080, true)));
  SM_CHECK(is_identity(compute_ndc_rect(1280, 720, 1920, 1080, true)));  // 同为 16:9
  SM_CHECK(is_identity(compute_ndc_rect(960, 540, 1920, 1080, true)));   // 16:9 缩放

  // ---- 3. 4:3 源进 16:9 视口：左右留边（pillarbox）----
  // s = min(1920/1024, 1080/768) = 1080/768 = 1.40625
  // disp = 1440x1080 → ox = 240 → x = ±0.75，y 铺满。
  check_rect(compute_ndc_rect(1024, 768, 1920, 1080, true), -0.75f, 0.75f, 1.0f,
             -1.0f);

  // ---- 4. 16:9 源进 4:3 视口：上下留边（letterbox）----
  // s = min(1024/1920, 768/1080) = 1024/1920 → disp = 1024x576
  // oy = 96 → y = ±0.75，x 铺满。
  check_rect(compute_ndc_rect(1920, 1080, 1024, 768, true), -1.0f, 1.0f, 0.75f,
             -0.75f);

  // ---- 5. 竖屏源进横屏视口：左右留边且左右对称 ----
  // s = 1080/1920 = 0.5625 → disp = 607.5x1080 → ox = 656.25
  // x = ±(81/256) ≈ ±0.31640625，y 铺满。
  {
    const NdcRect r = compute_ndc_rect(1080, 1920, 1920, 1080, true);
    check_rect(r, -0.31640625f, 0.31640625f, 1.0f, -1.0f);
  }

  // ---- 6. 视口尺寸为 0：返回恒等矩形（不缩放、不崩溃、不产生 NaN）----
  SM_CHECK(is_identity(compute_ndc_rect(1920, 1080, 0, 1080, true)));
  SM_CHECK(is_identity(compute_ndc_rect(1920, 1080, 1920, 0, true)));
  SM_CHECK(is_identity(compute_ndc_rect(1920, 1080, 0, 0, true)));
  SM_CHECK(is_identity(compute_ndc_rect(0, 0, 0, 0, true)));

  // ---- 7. 源尺寸为 0（未知源）+ 等比居中：退化为铺满 ----
  SM_CHECK(is_identity(compute_ndc_rect(0, 1080, 1920, 1080, true)));
  SM_CHECK(is_identity(compute_ndc_rect(1920, 0, 1920, 1080, true)));

  // ---- 8. 不变量：铺满性之外的通用性质，遍历代表性组合 ----
  const std::uint32_t srcs[][2] = {
      {1920, 1080}, {1080, 1920}, {1024, 768}, {768, 1024}, {3840, 2160},
      {2560, 1080}, {1, 1},       {640, 480},  {1919, 1079}};
  const std::uint32_t dsts[][2] = {
      {1920, 1080}, {1080, 1920}, {1024, 768}, {1280, 720}, {640, 480}, {1, 1}};
  for (const auto& s : srcs) {
    for (const auto& d : dsts) {
      const NdcRect r = compute_ndc_rect(s[0], s[1], d[0], d[1], true);
      // 8.1 非退化：x0 < x1 且 y0 > y1（NDC +y 朝上）
      SM_CHECK(r.x0 < r.x1);
      SM_CHECK(r.y0 > r.y1);
      // 8.2 不越界：完全落在 [-1, 1]（1e-4 容差吸收 float 舍入）
      SM_CHECK(r.x0 >= -1.0f - 1e-4f && r.x1 <= 1.0f + 1e-4f);
      SM_CHECK(r.y1 >= -1.0f - 1e-4f && r.y0 <= 1.0f + 1e-4f);
      // 8.3 居中对称：x0 == -x1、y0 == -y1
      SM_CHECK(near(r.x0, -r.x1));
      SM_CHECK(near(r.y0, -r.y1));
      // 8.4 纵横比守恒：NDC 尺寸换算回像素后与源纵横比一致（相对 ±0.1%）
      const double ndc_w = (static_cast<double>(r.x1) - r.x0) / 2.0 *
                           static_cast<double>(d[0]);
      const double ndc_h = (static_cast<double>(r.y0) - r.y1) / 2.0 *
                           static_cast<double>(d[1]);
      const double src_ar = static_cast<double>(s[0]) / s[1];
      const double disp_ar = ndc_w / ndc_h;
      SM_CHECK(std::fabs(disp_ar - src_ar) / src_ar < 1e-3);
      // 8.5 完整装入：不裁切 → 显示宽高都不超过视口
      SM_CHECK(ndc_w <= static_cast<double>(d[0]) * (1.0 + 1e-5));
      SM_CHECK(ndc_h <= static_cast<double>(d[1]) * (1.0 + 1e-5));
    }
  }

  // ---- 9. is_valid_output_window_desc 边界 ----
  void* const handle = reinterpret_cast<void*>(0x1);  // 仅作"非空"占位
  OutputWindowDesc ok{handle, 1920, 1080};
  SM_CHECK(is_valid_output_window_desc(ok));

  OutputWindowDesc null_handle{nullptr, 1920, 1080};
  SM_CHECK(!is_valid_output_window_desc(null_handle));

  OutputWindowDesc zero_w{handle, 0, 1080};
  SM_CHECK(!is_valid_output_window_desc(zero_w));

  OutputWindowDesc zero_h{handle, 1920, 0};
  SM_CHECK(!is_valid_output_window_desc(zero_h));

  OutputWindowDesc zero_both{handle, 0, 0};
  SM_CHECK(!is_valid_output_window_desc(zero_both));

  // 上限恰为 kMaxOutputDimension：16384 合法，16385 越界
  SM_CHECK(sm::rhi::kMaxOutputDimension == 16384u);
  OutputWindowDesc at_max{handle, 16384, 16384};
  SM_CHECK(is_valid_output_window_desc(at_max));

  OutputWindowDesc over_w{handle, 16385, 1080};
  SM_CHECK(!is_valid_output_window_desc(over_w));

  OutputWindowDesc over_h{handle, 1920, 16385};
  SM_CHECK(!is_valid_output_window_desc(over_h));

  // 最小合法尺寸 1x1
  OutputWindowDesc minimal{handle, 1, 1};
  SM_CHECK(is_valid_output_window_desc(minimal));

  // 默认构造（句柄空 + 尺寸 0）必须判非法 —— 后端据此拒绝创建
  SM_CHECK(!is_valid_output_window_desc(OutputWindowDesc{}));

  return smtest::finish("test_rhi_geometry");
}

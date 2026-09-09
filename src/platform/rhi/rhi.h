// RHI（渲染硬件接口）抽象层 —— 规格书第 2.3 章
//
// 引擎层只依赖本文件，不感知具体图形 API。Phase 1 实现为 D3D11 后端
// （rhi_d3d11.cpp），Phase 2 起可并行开发 D3D12 后端而不改上层代码。
//
// RHI 能力集（规格 2.3）：纹理上传、GPU 滤镜 Pass、合成、采样器、
// 顶点/像素着色器。P1 只落地其中"纹理上传 + 合成"两个子集：4 层基础
// 混合（含混合模式/蒙版占位，规格 R7）；滤镜 Pass / 着色器特效库属
// Phase 2，不在本接口之外暴露任何平台类型。
//
// 线程模型：IDevice 及其产物非线程安全，调用方需串行（P1 调用方为
// headless 主循环线程或后续 UI 渲染线程，均为单一所有者）。
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace sm {
namespace rhi {

// ---- 像素格式（CPU 侧布局与 D3D11 交换格式一一对应）----
enum class PixelFormat : uint8_t {
  Rgba8 = 0,  // 内存序 R,G,B,A（DXGI_FORMAT_R8G8B8A8_UNORM）
  Bgra8 = 1,  // 内存序 B,G,R,A（DXGI_FORMAT_B8G8R8A8_UNORM）
};

// 基础混合模式（Phase 1 4 层基础混合）
enum class BlendOp : uint8_t {
  Replace = 0,  // 直通覆盖（不混合；作为底层的默认模式）
  Over = 1,     // 标准 alpha 覆盖（视频层/字幕层）
  Add = 2,      // 相加发光（glow）
  Multiply = 3, // 相乘压暗
};

struct BlendConfig {
  bool enable = true;          // false = 整层跳过（不参与合成）
  BlendOp op = BlendOp::Over;  // 混合模式
  float opacity = 1.0f;        // 0..1 不透明度（Over 作用于 alpha；Add/Multiply 作用于色值）
  std::uint32_t mask_key = 0;  // 蒙版占位（Phase 2 特效库）；P1 恒 0
};

// 纹理创建描述
struct Texture2DDesc {
  PixelFormat format = PixelFormat::Rgba8;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool render_target = false;  // 可绑定为合成目标（PGM 离屏）
  bool cpu_readback = false;   // 支持 CPU 回读（PGM 捕获）
  bool shared = false;         // 预留：DXGI 共享句柄（10.2.3 UI 帧通道）
};

// ---- 纹理（上传的视频帧 / 离屏渲染目标共用）----
class ITexture {
 public:
  virtual ~ITexture() = default;

  virtual std::uint32_t width() const = 0;
  virtual std::uint32_t height() const = 0;
  virtual PixelFormat format() const = 0;

  // 上传 CPU 像素到 GPU 纹理。像素按 format 内存序，4 字节/像素连续存放
  // （行无填充）。仅非 render_target 纹理可调用；失败返回 false。
  virtual bool upload(const void* pixels, std::uint32_t row_bytes) = 0;

  // 回读 GPU 纹理到 CPU（仅 render_target + cpu_readback 纹理）。
  // out 按纹理 format 内存序输出（Rgba8 → R,G,B,A）。失败返回 false。
  virtual bool readback(std::vector<std::uint8_t>& out) = 0;

  // 原生句柄（D3D11 = ID3D11Texture2D*；stub/不可用 = nullptr）
  virtual void* native_handle() = 0;
};

// ---- PGM 合成器（4 层基础混合，规格 R7）----
// 内部持有离屏渲染目标（width x height）。合成顺序 = 层索引升序，
// 底层先绘制，上层覆盖；全部层由采样器经着色器采样输出。
class IPgmMixer {
 public:
  virtual ~IPgmMixer() = default;

  static constexpr int kMaxLayers = 4;  // Phase 1：4 层基础混合

  virtual std::uint32_t width() const = 0;
  virtual std::uint32_t height() const = 0;

  // 绑定第 index 层（0..kMaxLayers-1）。tex 必须来自同一 IDevice 且与
  // 合成器生命周期内保持有效；传 nullptr 释放该层。
  // fit_center=true 按源等比缩放并居中（letterbox），false 拉伸铺满全屏。
  virtual bool set_layer(int index, ITexture* tex, const BlendConfig& cfg,
                         bool fit_center = true) = 0;

  // 合成当前层栈到内部渲染目标。bg_rgba = 清屏底色 0xRRGGBBAA。
  virtual bool compose(std::uint32_t bg_rgba) = 0;

  // 渲染目标纹理（含 readback 能力），供上层回读 / 绑定共享句柄。
  virtual ITexture* output() = 0;

  // 便捷封装：合成后直接回读（RGBA8，尺寸 = width() x height()）。
  virtual bool compose_and_readback(std::uint32_t bg_rgba,
                                    std::vector<std::uint8_t>& rgba_out) = 0;
};

// ---- 设备（图形后端门面）----
class IDevice {
 public:
  virtual ~IDevice() = default;

  virtual const char* backend_name() const = 0;  // "d3d11" / stub 无实例

  // 创建普通纹理（视频帧上传；shared=true 时为后续共享句柄预留）
  virtual std::shared_ptr<ITexture> create_texture(
      const Texture2DDesc& desc) = 0;

  // 创建 PGM 离屏渲染目标（render_target + cpu_readback）
  virtual std::shared_ptr<ITexture> create_render_target(
      std::uint32_t width, std::uint32_t height) = 0;

  // 创建 width x height 的 PGM 合成器
  virtual std::shared_ptr<IPgmMixer> create_pgm_mixer(
      std::uint32_t width, std::uint32_t height) = 0;

  // 原生设备句柄（D3D11 = ID3D11Device*）
  virtual void* native_device() = 0;

  // 提交待执行命令（回读前确保 GPU 完成）
  virtual void flush() = 0;
};

// ---- 工厂 ----
// 创建当前平台可用的渲染后端。无可用后端或初始化失败返回 nullptr
// （"无渲染上下文"语义由调用方据此处理：返回空串 / 跳过渲染）。
// macOS/Linux（未定义 SM_HAS_D3D11）恒返回 nullptr，保证编译干净。
std::shared_ptr<IDevice> create_device();

}  // namespace rhi
}  // namespace sm

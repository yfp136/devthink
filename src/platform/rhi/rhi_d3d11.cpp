// rhi_d3d11.cpp —— D3D11 后端实现（规格书 2.3 / P1-2）
//
// Phase 1 语义：
//   1) 离屏渲染（无窗口）：设备 = 硬件（失败自动回退 WARP 软件设备）；
//      PGM 合成目标 = CPU 可回读的离屏纹理；帧通道 = staging 纹理
//      CopyResource + Map 读回（供 capture_pgm_frame_jpeg 编码）。
//   2) 本地窗口输出：在调用方给定的 HWND 上建立 DXGI 交换链，把 PGM 合成
//      结果（或任意同设备纹理）作为全屏四边形绘制到后缓冲并垂直同步
//      Present —— 即规格第 2 章的 1080p60 输出视口。窗口本身由调用方创建
//      与持有，本层不抽消息循环、不参与窗口生命周期。
//
// PGM 合成与窗口输出共用同一套「四边形 + 采样 + 混合」绘制路径
// （draw_texture_quad）与同一份 fit_center 几何（rhi.h 的 compute_ndc_rect），
// 以保证预监与输出视口画面逐像素一致。
//
// 编译开关：仅 _WIN32 && SM_HAS_D3D11 编译真实现；其他平台（macOS/Linux
// 或未启用 D3D11 的构建）本 TU 只提供返回 nullptr 的 stub，保证全平台
// 编译干净（对应 rhi.h 的"无渲染上下文"语义）。
#include "platform/rhi/rhi.h"
#include "rhi_d3d11.h"

#if defined(_WIN32) && defined(SM_HAS_D3D11)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>  // IDXGIFactory2 / IDXGISwapChain1（CreateSwapChainForHwnd）
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace sm {
namespace rhi {
namespace {

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
inline void log_err(const char* msg, HRESULT hr = S_OK) {
  if (FAILED(hr)) {
    std::fprintf(stderr, "[rhi_d3d11] %s (hr=0x%08X)\n", msg,
                 static_cast<unsigned>(hr));
  } else {
    std::fprintf(stderr, "[rhi_d3d11] %s\n", msg);
  }
}

// ---------------------------------------------------------------------------
// 全屏四边形顶点（PGM 合成与输出窗口共用同一布局）
//   POSITION float4（NDC xyzw）+ TEXCOORD float2（0..1）
// ---------------------------------------------------------------------------
struct QuadVertex {
  float pos[4];  // NDC xyzw
  float uv[2];
};
inline constexpr UINT kQuadStride = sizeof(QuadVertex);
inline constexpr UINT kQuadOffset = 0;

// 内部扩展：ITexture 的 D3D11 侧私有点（SRV / staging），仅供同 TU 的
// 合成器使用，不进入公开抽象头文件。
class D3D11Texture;
class D3D11Device;
class D3D11PgmMixer;
class D3D11OutputSurface;

// ---------------------------------------------------------------------------
// 内建着色器（全屏四边形 + 纹理采样 + 层调制）
//   每层绘制前 CPU 计算 NDC 四边形顶点（fit_center=letterbox / 拉伸铺满），
//   经动态顶点缓冲提交；混合模式由 4 个预置 BlendState 决定；opacity 与
//   模式标志经每层常量缓冲传入像素着色器。
// ---------------------------------------------------------------------------
struct BuiltinShaders {
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11InputLayout> il;
  ComPtr<ID3D11Buffer> layer_cb;      // float4( opacity, mode, 0, 0 )
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Buffer> quad_vb;       // dynamic，每层 4 顶点覆盖写
  ComPtr<ID3D11BlendState> blend[4];  // 索引 = BlendOp
  ComPtr<ID3D11RasterizerState> rs;   // 关闭背面剔除（四边形绕序无关）

  static const char* kHlsl() {
    return R"(
      cbuffer LayerCB : register(b0) {
        float4 mod;   // x = opacity, y = mode(0..3)
      };
      Texture2D tex : register(t0);
      SamplerState samp : register(s0);

      struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

      VSOut VSMain(float4 pos : POSITION, float2 uv : TEXCOORD0) {
        VSOut o;
        o.pos = pos;
        o.uv = uv;
        return o;
      }

      float4 PSMain(VSOut i) : SV_TARGET {
        float4 c = tex.Sample(samp, i.uv);
        float opacity = mod.x;
        int mode = (int)mod.y;
        if (mode == 0) {          // Replace：直通覆盖，忽略源 alpha
          c.a = 1.0;
        } else if (mode == 1) {   // Over：标准 alpha 覆盖，opacity 作用于 alpha
          c.a *= opacity;
        } else if (mode == 2) {   // Add：发光相加，opacity 作用于色值
          c.rgb *= opacity;
          c.a = 1.0;
        } else {                  // Multiply：相乘压暗，opacity 作用于色值
          c.rgb *= opacity;
          c.a = 1.0;
        }
        return c;
      }
    )";
  }
};

// ---------------------------------------------------------------------------
// 共享绘制路径（PGM 合成与窗口输出共用，保证两处画面逐像素一致）
// ---------------------------------------------------------------------------
// bg_rgba = 0xRRGGBBAA → ClearRenderTargetView 用的 float4
inline void rgba_to_clear(std::uint32_t bg_rgba, float (&out)[4]) {
  out[0] = static_cast<float>((bg_rgba >> 24) & 0xFF) / 255.0f;
  out[1] = static_cast<float>((bg_rgba >> 16) & 0xFF) / 255.0f;
  out[2] = static_cast<float>((bg_rgba >> 8) & 0xFF) / 255.0f;
  out[3] = static_cast<float>(bg_rgba & 0xFF) / 255.0f;
}

// 把 src 的一个 NDC 四边形画到 rtv（视口 dst_w x dst_h）：
//   清屏 → 设置目标/视口/光栅化 → 绑定输入装配与着色器 → 覆写动态顶点缓冲
//   → 设置混合态与层常量 → Draw(4) → 解绑 SRV。
// bg_clear == nullptr 表示"不清屏"（合成器第 2..N 层，避免抹掉已画内容）。
// rect 由 rhi.h 的 compute_ndc_rect 产出；op/opacity 语义同 BlendConfig。
void draw_texture_quad(ID3D11DeviceContext* ctx,
                       const std::shared_ptr<BuiltinShaders>& sh,
                       ID3D11RenderTargetView* rtv, std::uint32_t dst_w,
                       std::uint32_t dst_h, ID3D11ShaderResourceView* srv,
                       const NdcRect& rect, BlendOp op, float opacity,
                       const float* bg_clear) {
  if (ctx == nullptr || sh == nullptr || rtv == nullptr || srv == nullptr) {
    return;
  }
  if (dst_w == 0 || dst_h == 0) return;

  if (bg_clear != nullptr) ctx->ClearRenderTargetView(rtv, bg_clear);

  ID3D11RenderTargetView* rtvs[1] = {rtv};
  ctx->OMSetRenderTargets(1, rtvs, nullptr);
  D3D11_VIEWPORT vp{};
  vp.Width = static_cast<float>(dst_w);
  vp.Height = static_cast<float>(dst_h);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  ctx->RSSetViewports(1, &vp);
  ctx->RSSetState(sh->rs.Get());

  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  ctx->IASetInputLayout(sh->il.Get());
  ctx->VSSetShader(sh->vs.Get(), nullptr, 0);
  ctx->PSSetShader(sh->ps.Get(), nullptr, 0);
  ctx->PSSetSamplers(0, 1, sh->sampler.GetAddressOf());
  ctx->PSSetConstantBuffers(0, 1, sh->layer_cb.GetAddressOf());
  ctx->IASetVertexBuffers(0, 1, sh->quad_vb.GetAddressOf(), &kQuadStride,
                          &kQuadOffset);
  ID3D11ShaderResourceView* srvs[1] = {srv};
  ctx->PSSetShaderResources(0, 1, srvs);

  // 动态顶点缓冲：三角形带 4 顶点（左上 → 右上 → 左下 → 右下），
  // 顶点序与 uv 对应关系固定（v0=左上 uv(0,0)，v3=右下 uv(1,1)）。
  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr =
      ctx->Map(sh->quad_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    log_err("Map(quad_vb) failed", hr);
    ID3D11ShaderResourceView* null_srv[1] = {nullptr};
    ctx->PSSetShaderResources(0, 1, null_srv);
    return;
  }
  auto* v = static_cast<QuadVertex*>(mapped.pData);
  v[0] = {{rect.x0, rect.y0, 0.f, 1.f}, {0.f, 0.f}};
  v[1] = {{rect.x1, rect.y0, 0.f, 1.f}, {1.f, 0.f}};
  v[2] = {{rect.x0, rect.y1, 0.f, 1.f}, {0.f, 1.f}};
  v[3] = {{rect.x1, rect.y1, 0.f, 1.f}, {1.f, 1.f}};
  ctx->Unmap(sh->quad_vb.Get(), 0);

  // 混合模式索引 = BlendOp（4 个预置 BlendState）；越界则退回 Replace
  int mode = static_cast<int>(op);
  if (mode < 0 || mode > 3) mode = static_cast<int>(BlendOp::Replace);
  ctx->OMSetBlendState(sh->blend[mode].Get(), nullptr, 0xFFFFFFFF);
  const float cb[4] = {opacity, static_cast<float>(mode), 0.f, 0.f};
  ctx->UpdateSubresource(sh->layer_cb.Get(), 0, nullptr, cb, 0, 0);

  ctx->Draw(4, 0);

  // 解绑 SRV：避免同一纹理同时被绑定为输入与后续渲染目标
  ID3D11ShaderResourceView* null_srv[1] = {nullptr};
  ctx->PSSetShaderResources(0, 1, null_srv);
}

// ---------------------------------------------------------------------------
// D3D11 纹理
// ---------------------------------------------------------------------------
class D3D11Texture final : public ITexture {
 public:
  // usage 分支：
  //   upload 纹理  -> USAGE_DEFAULT + BIND_SHADER_RESOURCE（可 UpdateSubresource）
  //   渲染目标     -> USAGE_DEFAULT + BIND_RENDER_TARGET|SHADER_RESOURCE，
  //                   同时创建同规格 USAGE_STAGING 纹理用于回读。
  D3D11Texture(ID3D11Device* device, ID3D11DeviceContext* ctx,
               const Texture2DDesc& desc)
      : device_(device), ctx_(ctx), desc_(desc) {}

  bool create() {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = desc_.width;
    td.Height = desc_.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = (desc_.format == PixelFormat::Bgra8)
                    ? DXGI_FORMAT_B8G8R8A8_UNORM
                    : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = desc_.render_target
                       ? (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)
                       : D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = 0;
    // shared=true：Phase 2 预留（10.2.3 共享句柄）。P1 不建立 NT handle，
    // 仍按普通 GPU 纹理创建；上层 P1 不会请求 shared。
    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &tex2d_);
    if (FAILED(hr)) {
      log_err("CreateTexture2D failed", hr);
      return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    hr = device_->CreateShaderResourceView(tex2d_.Get(), &sd, &srv_);
    if (FAILED(hr)) {
      log_err("CreateShaderResourceView failed", hr);
      return false;
    }
    if (desc_.render_target) {
      hr = device_->CreateRenderTargetView(tex2d_.Get(), nullptr, &rtv_);
      if (FAILED(hr)) {
        log_err("CreateRenderTargetView failed", hr);
        return false;
      }
      if (desc_.cpu_readback) {
        D3D11_TEXTURE2D_DESC st{};
        st.Width = desc_.width;
        st.Height = desc_.height;
        st.MipLevels = 1;
        st.ArraySize = 1;
        st.Format = td.Format;
        st.SampleDesc.Count = 1;
        st.Usage = D3D11_USAGE_STAGING;
        st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device_->CreateTexture2D(&st, nullptr, &staging_);
        if (FAILED(hr)) {
          log_err("CreateTexture2D(staging) failed", hr);
          return false;
        }
      }
    }
    return true;
  }

  std::uint32_t width() const override { return desc_.width; }
  std::uint32_t height() const override { return desc_.height; }
  PixelFormat format() const override { return desc_.format; }

  bool upload(const void* pixels, std::uint32_t row_bytes) override {
    if (desc_.render_target || pixels == nullptr) return false;
    if (row_bytes < desc_.width * 4) return false;
    ctx_->UpdateSubresource(tex2d_.Get(), 0, nullptr, pixels, row_bytes, 0);
    return true;
  }

  bool readback(std::vector<std::uint8_t>& out) override {
    if (!desc_.render_target || !desc_.cpu_readback) return false;
    if (staging_ == nullptr) return false;
    ctx_->CopyResource(staging_.Get(), tex2d_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
      log_err("Map(staging) failed", hr);
      return false;
    }
    const std::uint32_t w = desc_.width;
    const std::uint32_t h = desc_.height;
    out.resize(static_cast<size_t>(w) * h * 4);
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    std::uint8_t* dst = out.data();
    for (std::uint32_t y = 0; y < h; ++y) {
      std::memcpy(dst + static_cast<size_t>(y) * w * 4,
                  src + static_cast<size_t>(y) * mapped.RowPitch,
                  static_cast<size_t>(w) * 4);
    }
    ctx_->Unmap(staging_.Get(), 0);
    return true;
  }

  void* native_handle() override { return tex2d_.Get(); }

  // ---- D3D11 私有访问（同 TU 合成器使用）----
  ID3D11Texture2D* d3d_tex() const { return tex2d_.Get(); }
  ID3D11ShaderResourceView* srv() const { return srv_.Get(); }
  ID3D11RenderTargetView* rtv() const { return rtv_.Get(); }
  const Texture2DDesc& desc() const { return desc_; }

 private:
  ID3D11Device* device_;
  ID3D11DeviceContext* ctx_;
  Texture2DDesc desc_;
  ComPtr<ID3D11Texture2D> tex2d_;
  ComPtr<ID3D11ShaderResourceView> srv_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11Texture2D> staging_;
};

// ---------------------------------------------------------------------------
// 设备
// ---------------------------------------------------------------------------
class D3D11Device final : public IDevice {
 public:
  D3D11Device(const ComPtr<ID3D11Device>& device,
              const ComPtr<ID3D11DeviceContext>& ctx,
              const std::shared_ptr<BuiltinShaders>& shaders)
      : device_(device), ctx_(ctx), shaders_(shaders) {}

  const char* backend_name() const override { return "d3d11"; }

  std::shared_ptr<ITexture> create_texture(const Texture2DDesc& desc) override {
    if (desc.width == 0 || desc.height == 0) return nullptr;
    auto tex = std::make_shared<D3D11Texture>(device_.Get(), ctx_.Get(), desc);
    if (!tex->create()) return nullptr;
    return tex;
  }

  std::shared_ptr<ITexture> create_render_target(std::uint32_t width,
                                                 std::uint32_t height) override {
    Texture2DDesc desc{};
    desc.format = PixelFormat::Rgba8;
    desc.width = width;
    desc.height = height;
    desc.render_target = true;
    desc.cpu_readback = true;
    return create_texture(desc);
  }

  std::shared_ptr<IPgmMixer> create_pgm_mixer(std::uint32_t width,
                                              std::uint32_t height) override;

  std::shared_ptr<IOutputSurface> create_output_surface(
      const OutputWindowDesc& desc) override;

  void* native_device() override { return device_.Get(); }
  void flush() override { ctx_->Flush(); }

  ID3D11Device* d3d() const { return device_.Get(); }
  ID3D11DeviceContext* ctx() const { return ctx_.Get(); }
  const std::shared_ptr<BuiltinShaders>& shaders() const { return shaders_; }

 private:
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> ctx_;
  std::shared_ptr<BuiltinShaders> shaders_;
};

// ---------------------------------------------------------------------------
// 输出窗口（DXGI 交换链呈现）—— 规格第 2 章 1080p60 输出视口
//   窗口由调用方创建与持有（本层只拿 HWND 建交换链，不抽消息循环、
//   不销毁窗口）；呈现路径与 PGM 合成共用 draw_texture_quad + compute_ndc_rect，
//   故"预监看到的"与"输出视口显示的"几何完全一致。
// ---------------------------------------------------------------------------
class D3D11OutputSurface final : public IOutputSurface {
 public:
  D3D11OutputSurface(D3D11Device* device, HWND hwnd, std::uint32_t width,
                     std::uint32_t height)
      : device_(device), hwnd_(hwnd), width_(width), height_(height) {}

  bool init() {
    if (device_ == nullptr || hwnd_ == nullptr) return false;
    if (!acquire_factory()) return false;
    if (!create_swap_chain(width_, height_)) return false;
    if (!create_back_buffer_rtv()) return false;
    // 交换链归本层，窗口归调用方：禁止 DXGI 自行接管 Alt+Enter 全屏，
    // 避免改动调用方的窗口样式（失败无碍，仅少一层 DXGI 行为接管）。
    factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    return true;
  }

  bool is_open() const override {
    return swap_ != nullptr && rtv_ != nullptr;
  }
  std::uint32_t width() const override { return width_; }
  std::uint32_t height() const override { return height_; }

  bool resize(std::uint32_t width, std::uint32_t height) override {
    if (!is_open()) return false;
    if (width == 0 || height == 0 || width > kMaxOutputDimension ||
        height > kMaxOutputDimension) {
      log_err("resize: illegal size rejected");
      return false;
    }
    if (width == width_ && height == height_) return true;  // 幂等

    // ResizeBuffers 前置：必须先释放全部后缓冲引用（RTV + 输出合并绑定）。
    rtv_.Reset();
    ID3D11RenderTargetView* null_rtv[1] = {nullptr};
    device_->ctx()->OMSetRenderTargets(1, null_rtv, nullptr);
    device_->ctx()->Flush();

    const HRESULT hr = swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) {
      width_ = width;
      height_ = height;
      if (create_back_buffer_rtv()) return true;
      log_err("back buffer RTV recreation failed — output surface closed");
      swap_.Reset();
      return false;
    }

    // ResizeBuffers 失败：新尺寸未生效（width_/height_ 保持不变）。此时 RTV
    // 已被释放，先按**旧尺寸**重建后缓冲 RTV，尽量让输出面继续可用
    // （返回 false 仅表示"新尺寸未采用"）；连旧尺寸都恢复不了才关闭输出面，
    // 由调用方降级为"仅离屏 + 预监"或重建输出面。
    log_err("ResizeBuffers failed — keeping previous size", hr);
    if (create_back_buffer_rtv()) return false;
    log_err("previous back buffer unrecoverable — output surface closed");
    swap_.Reset();
    return false;
  }

  bool present(ITexture* src, std::uint32_t bg_rgba,
               bool fit_center) override {
    if (!is_open()) return false;
    auto* tex = dynamic_cast<D3D11Texture*>(src);
    if (tex == nullptr || tex->srv() == nullptr) {
      log_err("present: source texture not from this device — skipped");
      return false;
    }
    float clear[4];
    rgba_to_clear(bg_rgba, clear);
    const NdcRect rect = compute_ndc_rect(tex->width(), tex->height(), width_,
                                         height_, fit_center);
    // 输出面语义 = 把源整幅呈现到视口：固定 Replace + 不透明，
    // 不做二次混合（混合已在 PGM 合成阶段完成）。
    draw_texture_quad(device_->ctx(), device_->shaders(), rtv_.Get(), width_,
                      height_, tex->srv(), rect, BlendOp::Replace, 1.0f, clear);

    // Present(1, 0) = 垂直同步：本调用被节流到显示刷新率
    // （1080p60 视口 → 60 Hz 上限），调用方无需额外限帧。
    const HRESULT hr = swap_->Present(1, 0);
    if (hr == DXGI_STATUS_OCCLUDED) {
      // 窗口最小化 / 被完全遮挡：DXGI 自动降帧，非错误
      return true;
    }
    if (FAILED(hr)) {
      log_err("Present failed", hr);
      if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        // 设备丢失：关闭输出面（离屏路径由调用方另行重建设备）
        rtv_.Reset();
        swap_.Reset();
      }
      return false;
    }
    return true;
  }

  void* native_handle() override { return swap_.Get(); }

 private:
  // device → IDXGIDevice → IDXGIAdapter → IDXGIFactory2
  bool acquire_factory() {
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = device_->d3d()->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (FAILED(hr)) {
      log_err("QueryInterface(IDXGIDevice) failed", hr);
      return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) {
      log_err("IDXGIDevice::GetAdapter failed", hr);
      return false;
    }
    hr = adapter->GetParent(IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
      log_err("IDXGIAdapter::GetParent(IDXGIFactory2) failed", hr);
      return false;
    }
    return true;
  }

  // 优先翻转模型（FLIP_DISCARD，Win10+ 高效路径）；失败退回 bitblt 模型
  // （部分虚拟机 / 远程桌面 / 旧驱动不支持翻转模型，CI 环境尤需兜底）。
  bool create_swap_chain(std::uint32_t width, std::uint32_t height) {
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.Stereo = FALSE;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_NONE;  // 缩放统一交给 compute_ndc_rect
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = 0;

    HRESULT hr = factory_->CreateSwapChainForHwnd(device_->d3d(), hwnd_, &sd,
                                                  nullptr, nullptr, &swap_);
    if (SUCCEEDED(hr)) return true;
    log_err("CreateSwapChainForHwnd(FLIP_DISCARD) failed — trying bitblt", hr);
    swap_.Reset();

    sd.BufferCount = 1;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    hr = factory_->CreateSwapChainForHwnd(device_->d3d(), hwnd_, &sd, nullptr,
                                         nullptr, &swap_);
    if (FAILED(hr)) {
      log_err("CreateSwapChainForHwnd(DISCARD) failed", hr);
      return false;
    }
    return true;
  }

  bool create_back_buffer_rtv() {
    rtv_.Reset();
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) {
      log_err("IDXGISwapChain::GetBuffer failed", hr);
      return false;
    }
    hr = device_->d3d()->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                                &rtv_);
    if (FAILED(hr)) {
      log_err("CreateRenderTargetView(back buffer) failed", hr);
      return false;
    }
    return true;
  }

  D3D11Device* device_ = nullptr;  // 生命周期由调用方保证（同 PGM 合成器）
  HWND hwnd_ = nullptr;            // 归调用方所有，本层不销毁
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  ComPtr<IDXGIFactory2> factory_;
  ComPtr<IDXGISwapChain1> swap_;
  ComPtr<ID3D11RenderTargetView> rtv_;  // 当前后缓冲视图
};

std::shared_ptr<IOutputSurface> D3D11Device::create_output_surface(
    const OutputWindowDesc& desc) {
  // 非法描述必须显式拒绝：不得静默降级为"能显示但尺寸错误"的交换链。
  if (!is_valid_output_window_desc(desc)) {
    log_err("create_output_surface: invalid OutputWindowDesc — rejected");
    return nullptr;
  }
  auto surface = std::make_shared<D3D11OutputSurface>(
      this, static_cast<HWND>(desc.native_window), desc.width, desc.height);
  // 创建失败（平台无窗口能力 / 交换链不可用）→ 返回 nullptr，
  // 调用方据此降级为"仅离屏 + 预监"（headless 与 CI 无显示环境均属此类）。
  if (!surface->init()) return nullptr;
  return surface;
}

// ---------------------------------------------------------------------------
// PGM 合成器（4 层基础混合，规格 R7）
// ---------------------------------------------------------------------------
class D3D11PgmMixer final : public IPgmMixer {
 public:
  D3D11PgmMixer(D3D11Device* device, std::uint32_t width,
                std::uint32_t height)
      : device_(device), width_(width), height_(height) {}

  bool init() {
    rt_ = device_->create_render_target(width_, height_);
    if (!rt_) return false;
    return true;
  }

  std::uint32_t width() const override { return width_; }
  std::uint32_t height() const override { return height_; }

  bool set_layer(int index, ITexture* tex, const BlendConfig& cfg,
                 bool fit_center) override {
    if (index < 0 || index >= kMaxLayers) return false;
    layers_[index].tex = tex;
    layers_[index].cfg = cfg;
    layers_[index].fit_center = fit_center;
    return true;
  }

  bool compose(std::uint32_t bg_rgba) override {
    auto* rt = static_cast<D3D11Texture*>(rt_.get());
    ID3D11DeviceContext* ctx = device_->ctx();
    const auto& sh = device_->shaders();
    if (rt == nullptr || rt->rtv() == nullptr || ctx == nullptr ||
        sh == nullptr) {
      return false;
    }

    // 清屏底色（bg_rgba = 0xRRGGBBAA）：由首层绘制时一次性清屏，
    // 后续层传 nullptr 避免抹掉已画内容。
    float clear[4];
    rgba_to_clear(bg_rgba, clear);

    // 逐层绘制：层索引升序 = 先画底层，上层覆盖。
    // 每层走同一条绘制路径与同一份 fit_center 几何（compute_ndc_rect），
    // 与窗口输出（present）严格一致。
    bool drew_any = false;
    for (int i = 0; i < kMaxLayers; ++i) {
      const LayerSlot& slot = layers_[i];
      if (slot.tex == nullptr || !slot.cfg.enable) continue;
      // 动态转型：slot.tex 由调用方传入，可能来自其它后端（或非本设备的
      // 实现），必须在此拦下，不能让 static_cast 把错误类型当成本类型用。
      auto* ltex = dynamic_cast<D3D11Texture*>(slot.tex);
      if (ltex == nullptr || ltex->srv() == nullptr) {
        log_err("set_layer texture not from this device — skipped");
        continue;
      }
      const NdcRect rect = compute_ndc_rect(ltex->width(), ltex->height(),
                                           width_, height_, slot.fit_center);
      draw_texture_quad(ctx, sh, rt->rtv(), width_, height_, ltex->srv(), rect,
                        slot.cfg.op, slot.cfg.opacity,
                        drew_any ? nullptr : clear);
      drew_any = true;
    }
    if (!drew_any) {
      // 无有效层：仍需清屏，保证 output() = 底色而非上一帧残留。
      ctx->ClearRenderTargetView(rt->rtv(), clear);
    }

    // 解绑渲染目标：回读走 CopyResource 不受影响，同时避免污染后续
    // 交换链路径的绑定状态。
    ID3D11RenderTargetView* null_rtv[1] = {nullptr};
    ctx->OMSetRenderTargets(1, null_rtv, nullptr);
    return true;
  }

  ITexture* output() override { return rt_.get(); }

  bool compose_and_readback(std::uint32_t bg_rgba,
                            std::vector<std::uint8_t>& rgba_out) override {
    if (!compose(bg_rgba)) return false;
    // 提交 GPU 命令并确保完成（readback 前置）
    device_->flush();
    return rt_->readback(rgba_out);
  }

 private:
  struct LayerSlot {
    ITexture* tex = nullptr;
    BlendConfig cfg;
    bool fit_center = true;
  };

  D3D11Device* device_;
  std::uint32_t width_;
  std::uint32_t height_;
  std::shared_ptr<ITexture> rt_;  // D3D11Texture（render_target + readback）
  LayerSlot layers_[kMaxLayers];
};

std::shared_ptr<IPgmMixer> D3D11Device::create_pgm_mixer(std::uint32_t width,
                                                         std::uint32_t height) {
  if (width == 0 || height == 0) return nullptr;
  auto mixer = std::make_shared<D3D11PgmMixer>(this, width, height);
  if (!mixer->init()) return nullptr;
  return mixer;
}

// ---------------------------------------------------------------------------
// 初始化助手
// ---------------------------------------------------------------------------
bool create_blend_state(ID3D11Device* device, BlendOp op,
                        ID3D11BlendState** out) {
  D3D11_BLEND_DESC bd{};
  bd.AlphaToCoverageEnable = FALSE;
  bd.IndependentBlendEnable = FALSE;
  D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  rt.BlendEnable = TRUE;
  switch (op) {
    case BlendOp::Replace:
      rt.SrcBlend = D3D11_BLEND_ONE;
      rt.DestBlend = D3D11_BLEND_ZERO;
      rt.BlendOp = D3D11_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D11_BLEND_ONE;
      rt.DestBlendAlpha = D3D11_BLEND_ZERO;
      rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      break;
    case BlendOp::Over:
      rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
      rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
      rt.BlendOp = D3D11_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D11_BLEND_ONE;
      rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
      rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      break;
    case BlendOp::Add:
      rt.SrcBlend = D3D11_BLEND_ONE;
      rt.DestBlend = D3D11_BLEND_ONE;
      rt.BlendOp = D3D11_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D11_BLEND_ONE;
      rt.DestBlendAlpha = D3D11_BLEND_ONE;
      rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      break;
    case BlendOp::Multiply:
    default:
      rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
      rt.DestBlend = D3D11_BLEND_ZERO;
      rt.BlendOp = D3D11_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
      rt.DestBlendAlpha = D3D11_BLEND_ZERO;
      rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      break;
  }
  return SUCCEEDED(device->CreateBlendState(&bd, out));
}

// 创建并编译内建着色器（vs_4_0 / ps_4_0，兼容 Feature Level 10+ 硬件与 WARP）
std::shared_ptr<BuiltinShaders> create_builtin_shaders(ID3D11Device* device) {
  auto sh = std::make_shared<BuiltinShaders>();
  ComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;

  HRESULT hr = D3DCompile(BuiltinShaders::kHlsl(), std::strlen(BuiltinShaders::kHlsl()),
                          nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0,
                          vs_blob.GetAddressOf(), err_blob.GetAddressOf());
  if (FAILED(hr)) {
    if (err_blob) {
      log_err(static_cast<const char*>(err_blob->GetBufferPointer()), hr);
    } else {
      log_err("D3DCompile(vs) failed", hr);
    }
    return nullptr;
  }
  hr = D3DCompile(BuiltinShaders::kHlsl(), std::strlen(BuiltinShaders::kHlsl()),
                  nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0,
                  ps_blob.GetAddressOf(), err_blob.GetAddressOf());
  if (FAILED(hr)) {
    if (err_blob) {
      log_err(static_cast<const char*>(err_blob->GetBufferPointer()), hr);
    } else {
      log_err("D3DCompile(ps) failed", hr);
    }
    return nullptr;
  }

  if (FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(),
                                        vs_blob->GetBufferSize(), nullptr,
                                        sh->vs.GetAddressOf()))) {
    log_err("CreateVertexShader failed");
    return nullptr;
  }
  if (FAILED(device->CreatePixelShader(ps_blob->GetBufferPointer(),
                                       ps_blob->GetBufferSize(), nullptr,
                                       sh->ps.GetAddressOf()))) {
    log_err("CreatePixelShader failed");
    return nullptr;
  }

  D3D11_INPUT_ELEMENT_DESC elems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (FAILED(device->CreateInputLayout(
          elems, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
          sh->il.GetAddressOf()))) {
    log_err("CreateInputLayout failed");
    return nullptr;
  }

  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(float) * 4;  // float4(opacity, mode, 0, 0)
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  if (FAILED(device->CreateBuffer(&bd, nullptr, sh->layer_cb.GetAddressOf()))) {
    log_err("CreateBuffer(layer_cb) failed");
    return nullptr;
  }

  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(device->CreateSamplerState(&sd, sh->sampler.GetAddressOf()))) {
    log_err("CreateSamplerState failed");
    return nullptr;
  }

  bd = {};
  // 动态四边形顶点缓冲：容量固定 4 顶点，跨度为共享 kQuadStride
  // （PGM 合成与窗口输出共用同一缓冲布局）。
  bd.ByteWidth = kQuadStride * 4;
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device->CreateBuffer(&bd, nullptr, sh->quad_vb.GetAddressOf()))) {
    log_err("CreateBuffer(quad_vb) failed");
    return nullptr;
  }

  for (int i = 0; i < 4; ++i) {
    if (!create_blend_state(device, static_cast<BlendOp>(i),
                            sh->blend[i].GetAddressOf())) {
      log_err("CreateBlendState failed");
      return nullptr;
    }
  }

  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;  // 四边形三角形带无绕序依赖
  rd.DepthClipEnable = TRUE;
  if (FAILED(device->CreateRasterizerState(&rd, sh->rs.GetAddressOf()))) {
    log_err("CreateRasterizerState failed");
    return nullptr;
  }
  return sh;
}

}  // namespace

// ---------------------------------------------------------------------------
// 工厂：硬件设备 → 失败回退 WARP
// ---------------------------------------------------------------------------
std::shared_ptr<IDevice> create_d3d11_device() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> ctx;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                D3D_FEATURE_LEVEL_10_1,
                                D3D_FEATURE_LEVEL_10_0};
  // BGRA 支持：Bgra8 视频帧可直接作为纹理上传。
  // 不加 D3D11_CREATE_DEVICE_DEBUG：无调试层的机器上设备创建会失败，
  // P1 离屏渲染无需调试信息。
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  const UINT level_count = sizeof(levels) / sizeof(levels[0]);

  auto try_create = [&](D3D_DRIVER_TYPE type) -> HRESULT {
    return D3D11CreateDevice(nullptr, type, nullptr, flags, levels,
                             level_count, D3D11_SDK_VERSION,
                             device.GetAddressOf(), nullptr,
                             ctx.GetAddressOf());
  };

  HRESULT hr = try_create(D3D_DRIVER_TYPE_HARDWARE);
  if (FAILED(hr)) {
    log_err("hardware device unavailable — falling back to WARP", hr);
    hr = try_create(D3D_DRIVER_TYPE_WARP);
    if (FAILED(hr)) {
      log_err("WARP device creation failed", hr);
      return nullptr;
    }
  }

  auto shaders = create_builtin_shaders(device.Get());
  if (!shaders) {
    log_err("builtin shader setup failed");
    return nullptr;
  }
  auto dev = std::make_shared<D3D11Device>(device, ctx, shaders);
  return dev;
}

}  // namespace rhi
}  // namespace sm

#else  // !(_WIN32 && SM_HAS_D3D11)

// 非 Windows / 未启用 D3D11：无可用渲染上下文。
namespace sm {
namespace rhi {
std::shared_ptr<IDevice> create_d3d11_device() { return nullptr; }
}  // namespace rhi
}  // namespace sm

#endif  // _WIN32 && SM_HAS_D3D11

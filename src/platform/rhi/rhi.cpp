// RHI 工厂（规格书 2.3）
//
// 引擎层调用 sm::rhi::create_device() 获取当前平台可用后端：
//   - _WIN32 + SM_HAS_D3D11：返回 D3D11 设备（Phase 1 后端）
//   - 其他平台 / 未启用：返回 nullptr（"无渲染上下文"语义，
//     调用方据此返回空串或跳过渲染）
#include "platform/rhi/rhi.h"
#include "platform/rhi/rhi_d3d11.h"

namespace sm {
namespace rhi {

std::shared_ptr<IDevice> create_device() {
#if defined(_WIN32) && defined(SM_HAS_D3D11)
  return create_d3d11_device();
#else
  // macOS/Linux 或未启用 D3D11 的构建：无可用渲染后端。
  // 上层（如 capture_pgm_frame_jpeg）依据 nullptr 走空串 / 解码帧回退。
  return nullptr;
#endif
}

}  // namespace rhi
}  // namespace sm

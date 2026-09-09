// D3D11 后端私有工厂声明（rhi_d3d11.cpp 实现）
//
// 仅 rhi.cpp 的 create_device() 需要引用，用于把平台判断与后端实例化
// 解耦：本头文件不包含任何 Windows/D3D 类型，可在任意平台安全包含。
// 后端是否可用由编译开关（_WIN32 + SM_HAS_D3D11）在实现 TU 内决定，
// 不可用（macOS/Linux/未启用）时 create_d3d11_device() 返回 nullptr。
#pragma once

#include <memory>

namespace sm {
namespace rhi {

class IDevice;

// 创建 D3D11 后端设备（硬件失败自动回退 WARP 软件设备）。
// 初始化失败或无渲染上下文返回 nullptr。
std::shared_ptr<IDevice> create_d3d11_device();

}  // namespace rhi
}  // namespace sm

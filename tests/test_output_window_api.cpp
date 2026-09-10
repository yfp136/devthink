// test_output_window_api.cpp - 本地窗口输出 API 契约单元测试（P1-2 收尾 / sm_desktop 接线）。
// -----------------------------------------------------------------------------
// 这是「桌面端输出视口接线」在**三种编译分支**下都成立的契约防线：
//   A. Windows + D3D11（真实现，能力 true）
//   B. Windows 有 FFmpeg 但未编 D3D11（降级空操作）
//   C. macOS/Linux stub（无媒体后端，降级空操作）
// 断言只依赖 media_backend.h 公开契约，不依赖 rhi_d3d11.cpp 的私有细节，
// 因此本机（stub 分支）即可验证「参数非法一律拒绝 + 永不崩溃 + 失败可取证」，
// 这正是 UI 侧（output_window.cpp / ui_controller.cpp）降级路径所依赖的前置条件：
//   create_output_surface / open 失败 → 仅离屏 + 预监，绝不致命。
//
// 说明：交换链的真实建立与每帧 present 只在 Windows CI（desktop job 的
// sm_desktop --smoke）与真机验证清单中覆盖，本用例不构造真实 HWND。
// =============================================================================
#include "platform/media_backend.h"
#include "test_common.h"

#include <cstdint>
#include <string>

namespace {

using sm::platform::close_output_window;
using sm::platform::has_output_window_capability;
using sm::platform::is_output_window_open;
using sm::platform::last_output_window_error;
using sm::platform::open_output_window;
using sm::platform::resize_output_window;

// 伪造的非空句柄：仅用于「尺寸非法」用例 —— 两种分支都必须在触碰句柄之前
// 就因尺寸越界而拒绝（stub 分支因无能力拒绝，非 Windows 分支因描述非法拒绝）。
void* const kFakeHandle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));

}  // namespace

int main() {
  // ---- 1. 初始态：未登记输出视口时 is_* 恒为 false（不依赖平台分支）----
  close_output_window();  // 归零：保证用例可重入
  SM_CHECK_EQ(is_output_window_open(), false);
  SM_CHECK_EQ(is_output_window_open(), false);  // 幂等查询

  // ---- 2. 空句柄一律拒绝（能力再强也不能建链）----
  SM_CHECK_EQ(open_output_window(nullptr, 1920, 1080), false);
  SM_CHECK_EQ(is_output_window_open(), false);
  SM_CHECK_EQ(open_output_window(nullptr, 0, 0), false);
  SM_CHECK_EQ(is_output_window_open(), false);

  // ---- 3. 失败必须可取证：拒绝后 last_output_window_error() 必须非空 ----
  // （UI 侧降级日志与 CI 取证都依赖这一条，三个分支均须满足）
  SM_CHECK(!last_output_window_error().empty());

  // ---- 4. 尺寸越界一律拒绝：0 / 负数 / 超过 kMaxOutputDimension(16384) ----
  SM_CHECK_EQ(open_output_window(kFakeHandle, 0, 1080), false);
  SM_CHECK_EQ(open_output_window(kFakeHandle, 1920, 0), false);
  SM_CHECK_EQ(open_output_window(kFakeHandle, -1920, -1080), false);
  SM_CHECK_EQ(open_output_window(kFakeHandle, 16385, 1080), false);
  SM_CHECK_EQ(open_output_window(kFakeHandle, 1920, 16385), false);
  SM_CHECK_EQ(open_output_window(kFakeHandle, 20000, 20000), false);
  SM_CHECK_EQ(is_output_window_open(), false);

  // ---- 5. 无能力平台：合法尺寸 + 非空句柄同样拒绝（静默降级，非致命）----
  const bool capable = has_output_window_capability();
  if (!capable) {
    SM_CHECK_EQ(open_output_window(kFakeHandle, 1920, 1080), false);
    SM_CHECK_EQ(open_output_window(kFakeHandle, 1280, 720), false);
    SM_CHECK_EQ(is_output_window_open(), false);
    SM_CHECK(!last_output_window_error().empty());
  } else {
    // 有能力平台：合法描述须被登记（真实验证见 Windows CI / 真机清单）。
    // 此处不构造真实 HWND，故仅断言「能力查询 == true」这一自洽性。
    SM_CHECK(capable);
  }

  // ---- 6. resize / close 在未登记状态下必须是安全空操作（GUI 线程可能
  //         在引擎尚未捕获时先收到尺寸变化或关闭事件）----
  resize_output_window(640, 360);       // 未登记 → 忽略
  resize_output_window(0, 0);           // 非法 → 忽略
  resize_output_window(-1, -1);         // 非法 → 忽略
  resize_output_window(1 << 20, 1 << 20);  // 越界 → 忽略
  SM_CHECK_EQ(is_output_window_open(), false);

  close_output_window();
  close_output_window();  // 幂等
  SM_CHECK_EQ(is_output_window_open(), false);

  // ---- 7. 重复拒绝不会破坏状态机：错误文本可持续取证 ----
  SM_CHECK_EQ(open_output_window(nullptr, 1920, 1080), false);
  SM_CHECK(!last_output_window_error().empty());
  close_output_window();
  SM_CHECK_EQ(is_output_window_open(), false);

  return smtest::finish("test_output_window_api");
}

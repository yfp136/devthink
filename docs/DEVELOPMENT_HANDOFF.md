# ShowMaster 开发状态交接文档

日期：2026-09-10（更新版）
用途：把 ShowMaster 工程的代码布局、构建方式与当前完成状态交接给下一个开发任务。剩余工作请直接以 `docs/TASKS_REMAINING.md` 为执行清单；本文档只描述静态事实（位置、结构、纪律），不再承载“待办主线”。

> 文档纪律：本文档随代码变动同步更新。当本文档与 `docs/TASKS_REMAINING.md` 或源码不一致时，以源码为准，并立即修正本文档（属 H-1/H-2 类卫生项）。

## 1. 工程身份与位置

工程名 **ShowMaster**，对应《ShowMaster工程开发规格书 V2.0 可落地版》。语言标准 C++20，零第三方运行时依赖（仅随仓库分发 `nlohmann/json.hpp` 单头库，数据库用系统/平台 SQLite3）。

源码根目录：

```
/Users/yxiao88/Library/Application Support/TRAE SOLO CN/ModularData/ai-agent/work-mode-projects/6a9e6a7e48915cce9293e7ba/showmaster-src
```

规格书 docx 位于源码目录上一级（`ShowMaster工程开发规格书V2.0_可落地版.docx`），业务语义以它为最高依据。

## 2. 代码布局

| 构件 | 目录 | 职责 |
| --- | --- | --- |
| `sm_core`（静态库） | `src/core` `src/db` `src/project` | 信封/指令字典/错误码、消息总线与心跳、SHA-256/UUID/时间工具、SQLite 存储与迁移、工程清单、state_json |
| `sm_engines`（静态库） | `src/engines/*` `src/platform` | 引擎注册表 + 10 个引擎子目录（timeline/media/scene/playlist/media_lib/vjfx/led/light/pixel/device）+ kernel + 平台适配层（`media_backend.*`、`jpeg_codec.*`、`rhi/`） |
| `sm_web`（静态库） | `src/web` | HTTP/WebSocket 服务、认证、远程队列、网关、内嵌 HTML UI |
| `sm_remote`（静态库） | `src/remote` | Windows Service 管理、看门狗 |
| `sm_protocols`（静态库） | `src/protocols` | TCP / MIDI / OSC 适配器 |
| `sm_kernel_smoke`（exe） | `src/app/main.cpp` | 内核冒烟程序 |
| `sm_headless`（exe） | `src/app/main_headless.cpp` | Headless 服务器（工控机常驻、Web 远控入口）；主循环以 ~10Hz 驱动 PGM 预监推流 |
| `sm_desktop`（exe，可选） | `src/app/main_qt.cpp` `src/app/qt/` | Qt 6.7 + QML 桌面端（P1-1）：`kernel_host.{h,cpp}` 桥接、`ui_controller.{h,cpp}` 装配、`qml/Main.qml` 六面板 + `qml.qrc`；仅 `SM_BUILD_DESKTOP=ON` 时构建（默认 OFF） |
| 单元测试 | `tests/` | 每个用例一个独立可执行文件，断言失败打印 `[FAIL]` 并以非 0 退出 |
| 第三方 | `third_party/nlohmann/json.hpp` | 单头 JSON（MIT） |

Windows 专属：7 个引擎插件 DLL（MediaEngine/TimelineEngine/VjfxEngine/LedEngine/LightEngine/PixelEngine/DeviceEngine），由 `src/engines/plugins/engine_plugin.cpp` 模板生成，当前为 NoopEngine 空壳；真实功能在宿主静态库内实现。

## 3. 构建与测试

本机（macOS/Linux）开发循环：

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Windows：在 "x64 Native Tools Command Prompt for VS 2022" 中执行 `cmake --preset windows-msvc-debug -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake`，随后 `--build` 与 `ctest` 同 preset；vcpkg 提供 `sqlite3` 与 `ffmpeg`。`find_package(FFMPEG)` 成功时定义 `SM_HAS_FFMPEG=1`，平台层走真实实现；找不到 FFmpeg 时自动降级为 stub。

当前实测：本机（macOS）`ctest` **24/24 全绿**，这是目前唯一可在本机复现的绿灯基线；`README.md`、`docs/BUILD_WINDOWS.md`、`docs/VERIFY_WINDOWS_MEDIA.md` 中的计数均已同步为 24/24。

Windows CI（`.github/workflows/windows-build.yml`）含两个 job：`build`（vcpkg `sqlite3`+`ffmpeg` → MSVC debug → `ctest` → headless 冒烟登录）与 `desktop`（aqt 预编译 Qt 6.7.2 → `SM_BUILD_DESKTOP=ON` Release → `windeployqt --qmldir` 部署 platforms/qml → `sm_desktop --smoke` 退出码断言）。**2026-09-10 实测：run #34434959091（commit `97760ad`）两个 job 全绿**；其中 `desktop` job 在后续 run（commit `1b17e97`）再次全绿，**首次真编译了 P1-2 的 D3D11 输出窗口代码**（步骤 8 Build sm_desktop / 10 Smoke test 均 success），即该部分已在 Windows/MSVC 下编译链接通过。后续绿灯状态请以 GitHub Actions 页面为准 —— 本机无 Qt/vcpkg 环境，无法本地复现 `desktop` job。

构建纪律（全仓强制）：MSVC 用 `/W4 /permissive-`，其他平台 `-Wall -Wextra`，新代码必须零告警；测试全绿才算完成。

新增测试必须在 `tests/CMakeLists.txt` 里调用一次 `sm_add_unit_test(name libs)`；新增 `src/platform/*.cpp` 由顶层 CMake 的 `file(GLOB ... CONFIGURE_DEPENDS)` 自动收集，无需改 CMake。

## 4. 平台层现状（2026-09-10 按源码核对）

### 4.1 `media_backend.h`（接口已定稿）

对外函数：`has_media_backend` / `probe_media` / `open_media_file` / `start_media_playback` / `stop_media_playback` / `get_media_pos_ms` / `get_media_duration_ms` / `close_media_file` / `generate_thumbnail`（JPEG base64）/ `capture_pgm_frame_jpeg`（JPEG base64）/ `init_audio_output` / `shutdown_audio_output`。

### 4.2 `media_backend.cpp`（实现现状）

文件按 `#if !defined(SM_HAS_FFMPEG)` 分成两支：

- stub 分支：macOS/Linux 编译，函数为空实现或返回空/`true`。
- FFmpeg 分支（Windows）：解码/播放/定位/关闭、后台解码线程、视频帧队列（保留最新一帧，RGB24，宽上限 1280）均已实现。
- 自研 JPEG 编码器 `sm::platform::encode_jpeg_rgb24`（`src/platform/jpeg_codec.cpp`）已实现并接线：`generate_thumbnail` 与 `capture_pgm_frame_jpeg` 均返回真实 JPEG base64，不再是空串。
- WASAPI 音频：`init_audio_output` 已做真实默认设备探测（`__uuidof(MMDeviceEnumerator)` / `IID_IAudioClient` 编译期 GUID），非占位。
- `capture_pgm_frame_jpeg` 的接口语义（`media_backend.h`）：有渲染上下文时返回渲染帧的 JPEG base64；**无渲染上下文（stub / 设备不可用 / 无帧）返回空串**。
- 实现（`media_backend.cpp` 尾部，规格 2.3 / P1-2）：在 `_WIN32 && SM_HAS_D3D11` 下走**渲染路径** —— 锁内拷贝队列最新帧 → RGB24 转 RGBA8 → `upload` 到 layer0（`BlendOp::Replace` 直通）→ 显式解绑 layer1–3（防残影）→ `compose_and_readback`（不透明黑底）→ 回读渲染帧编码 JPEG base64。已无“解码帧直出”旁路。
- 渲染后端不在本文件：抽象层与 D3D11 后端见 §4.3，`media_backend.cpp` 只作为 RHI 的 PGM 层调用方。

### 4.3 `rhi/`（渲染抽象层与 D3D11 后端）

- 接口 `src/platform/rhi/rhi.h`：`PixelFormat` / `BlendOp` / `BlendConfig`（`enable`/`op`/`opacity`/`mask_key`）/ `Texture2DDesc`、`ITexture`（`upload`/`readback`/`native_handle`）、`IPgmMixer`（`kMaxLayers=4`，`compose`/`compose_and_readback`）、`IDevice`，以及 P1-2 新增的输出窗口契约：`NdcRect` + `compute_ndc_rect()`（合成与窗口输出共用的等比居中/拉伸几何，纯头文件内联）、`OutputWindowDesc` + `is_valid_output_window_desc()`（`kMaxOutputDimension = 16384`）、`IOutputSurface`（`is_open`/`width`/`height`/`resize`/`present`/`native_handle`）、`IDevice::create_output_surface()`。RHI 只消费调用方传入的窗口句柄，自身不创建窗口、不跑消息循环。
- 工厂 `src/platform/rhi/rhi.cpp`：`create_device()` 在 `_WIN32 && SM_HAS_D3D11` 时返回 D3D11 设备，否则返回 `nullptr`（“无渲染上下文”语义，调用方据此返回空串）。
- 后端 `src/platform/rhi/rhi_d3d11.cpp`（+ `.h`）：双模式 ——（1）**离屏**设备（HW→WARP 回退），`UpdateSubresource` 上传，离屏渲染目标 + staging 纹理 `CopyResource`/`Map` 回读；（2）**本地窗口输出**（P1-2 落地）——在调用方 HWND 上创建 DXGI 交换链（`CreateSwapChainForHwnd`，`FLIP_DISCARD`/2 buffer 失败自动回退 `DISCARD`/1 buffer；`MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)` 保证不改动调用方窗口，`OCCLUDED` 视为成功、设备丢失关闭输出面，`resize` 重建后缓冲且失败时按旧尺寸尽力恢复），`present` 以 vsync 收尾（1080p60 视口 → 60 Hz 上限），创建失败返回 `nullptr` 由调用方降级为“仅离屏 + 预监”。两条路径共用同一 `draw_texture_quad` + `compute_ndc_rect`，故预监与输出视口像素一致。混合由 blend state 实现（Replace/Over/Add/Multiply × opacity）；每层四边形按 `fit_center` 自适应。
- 构建接线：顶层 `CMakeLists.txt` 的 `file(GLOB ...)` 已纳入 `src/platform/rhi/*.cpp`；`if(WIN32)` 下 `sm_engines` PUBLIC 链接 `d3d11 dxgi d3dcompiler`、PRIVATE 定义 `SM_HAS_D3D11=1`（与 `SM_HAS_FFMPEG` 同模式；Windows SDK 自带这三个 lib，无需 `find_package`）。
- 调用方接线（P1-2 收尾，已完成）：桌面端 `src/app/qt/output_window.{h,cpp}` 是**唯一调用方**——把独立顶层 QWindow 的 `winId()` 与客户区尺寸经 `platform::open_output_window()` 登记给引擎，`widthChanged`/`heightChanged` 转发 `resize_output_window()`，关闭/析构调 `close_output_window()`。由 `UiController` 持有（声明顺序置于最后，故析构最先执行 `close()`，此时 worker 仍在运行）。交换链的建立/重建/每帧 `present(PGM 合成结果)` 全在引擎渲染所有者线程（= `KernelWorker` 捕获线程）内完成，**GUI 线程不出现任何 D3D/DXGI 类型**，两侧无共享 RHI 对象（`IDevice` 及其产物非线程安全，单一所有者）。刻意使用**独立窗口**而非挂在 1600×900 六区 QML 主窗口上：DXGI `Present` 会覆盖整个客户区，挂主窗口会把界面刷掉；独立顶层窗口同时符合广播切换台 Program 输出的语义（可拖到第二显示器）。
- 持久化契约与降级：失败一律**非致命**并降级为「仅离屏 + 预监」——无窗口输出能力（非 Windows/未编 D3D11）、句柄或尺寸非法、建链失败三种情况都只让输出视口不可用，预监（`frameReady` JPEG）与离屏合成不受影响；失败被闩存（不逐帧重试）并记录到 `last_output_window_error()`，`OutputWindow` 把原因转成可读文本经 `degraded` 信号上抛为告警日志；`SM_NO_OUTPUT_WINDOW` 可显式跳过装配。headless/CI/RDP/老驱动返回 `nullptr` 属**预期行为**而非缺陷。
- 已知边界：代码链路已闭合，剩余为**人工实机验收**——Windows 真机 `-DSM_BUILD_DESKTOP=ON` 构建后确认输出视口出现、随播放刷新、拖到第二显示器可全屏，且 1080p60 帧率达标（验收步骤见 `docs/BUILD_WINDOWS.md` §7 与 `docs/VERIFY_WINDOWS_MEDIA.md`）。vjfx 引擎仍为 CPU 回退（GPU 着色器属 Phase 3）。

### 4.4 实时预监（PGM）链路

链路由通、帧源待渲染上下文：

- 服务端：`web_gateway.cpp` 的 `/ws/preview` WebSocket 与 `on_pgm_frame`（声明见 `web_gateway.h`）。
- 驱动方：`src/app/main_headless.cpp` 主循环（约 50ms/次）在媒体处于 playing/paused 时以 ~10Hz 调 `capture_pgm_frame_jpeg()`，经 `gateway.on_pgm_frame` 推送；空 JPEG 不推、同帧去重、离开播放态复位。
- 桌面端：`src/app/qt/kernel_host.cpp` 亦调用 `capture_pgm_frame_jpeg()`，经 `frameReady` 信号送 `qml/panels/PreviewStage.qml`（无画面时维持引导空态）。
- 页面侧：`web_ui.cpp` 内嵌页面有 `<img id="pgmPreview">` 接收 `pgm_frame`。
- 剩余缺口：macOS/Linux stub 或 Windows 未启用 `SM_HAS_D3D11` 时 `capture_pgm_frame_jpeg()` 返回空串，故预监要等 P1-2 的 Windows 真分支才有实际画面（见 TASKS_REMAINING P1-3）。

### 4.5 协议层现状

- TCP 适配器：真实实现（`src/protocols/tcp_adapter.cpp`，accept 循环）。
- MIDI 适配器：stub（无 MIDI 库，`midi_adapter.cpp` 有 TODO 未接线分发）。

## 5. 剩余工作

以 `docs/TASKS_REMAINING.md` 为唯一执行清单：Phase 1 收尾（桌面端真机冒烟、D3D11 窗口输出、PGM 画面闭环、传输接口 501、M1–M5 验收审计、冒烟/性能门禁、安装包）→ Phase 2 创作增强 → Phase 3 专业外设 → Phase 4 智能化。全规格完成度估算约 25%，其中 Phase 1 内核约 85%、交付形态 0%。

## 6. 文档卫生记录

- 2026-09-10（H-1 / H-2）：核对确认 `README.md`、`docs/BUILD_WINDOWS.md`、`docs/VERIFY_WINDOWS_MEDIA.md` 的 ctest 计数均已为 22/22，无需再改。本文档 §4 按源码重写，作废此前两处过期论断 ——「全仓无 `SM_HAS_D3D11`、无 `d3d11.h`/`dxgi.h` 使用、无 d3d11/dxgi 链接，D3D11 渲染输出属未开始」与「没有调用方驱动 `capture_pgm_frame_jpeg`」；并移除 §3 中已过时的「最近一次全绿 run 基于 commit e21c17b」（其后已有 16 个提交，含 P1-1 桌面端与 CI `desktop` job）。
- 2026-09-10（P1-2 输出窗口）：新增 `test_rhi_geometry`（`compute_ndc_rect` 几何 + `is_valid_output_window_desc` 校验），测试总数 22 → **23**，故全仓 ctest 计数由 `22/22` 统一改为 `23/23`（`README.md`、`docs/BUILD_WINDOWS.md`、`docs/VERIFY_WINDOWS_MEDIA.md`、`docs/TASKS_REMAINING.md` 与本文档 §3）。同时作废 §4.3 中「**无交换链**」与「单视口本地窗口输出（1080p60）尚未落地」两处过期论断 —— D3D11 后端已实现 DXGI 交换链窗口输出。
- 2026-09-10（P1-2 收尾 / `sm_desktop` 接线）：新增 `src/app/qt/output_window.{h,cpp}` 与 `test_output_window_api`，测试总数 23 → **24**，全仓 ctest 计数统一为 `24/24`（同上四处 + 本文档 §3）。同时作废本文档 §4.3 中「窗口输出交换链**尚无调用方接线** —— `sm_desktop` 尚未把 Qt 窗口句柄交给 `create_output_surface()`」这一过期论断：接线条已落地（`QWindow::winId()` → `platform::open_output_window()` → 渲染线程每帧 `present`，失败降级「仅离屏 + 预监」），剩余仅 Windows 实机人工验收。

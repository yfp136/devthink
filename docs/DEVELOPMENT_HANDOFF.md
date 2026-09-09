# ShowMaster 开发状态交接文档

日期：2026-09-09（更新版）
用途：把 ShowMaster 工程的代码布局、构建方式与当前完成状态交接给下一个开发任务。剩余工作请直接以 `docs/TASKS_REMAINING.md` 为执行清单；本文档只描述静态事实（位置、结构、纪律），不再承载"待办主线"。

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
| `sm_engines`（静态库） | `src/engines/*` `src/platform` | 引擎注册表 + 10 个引擎子目录（timeline/media/scene/playlist/media_lib/vjfx/led/light/pixel/device）+ kernel + 平台适配层（`media_backend.*`、`jpeg_codec.*`） |
| `sm_web`（静态库） | `src/web` | HTTP/WebSocket 服务、认证、远程队列、网关、内嵌 HTML UI |
| `sm_remote`（静态库） | `src/remote` | Windows Service 管理、看门狗 |
| `sm_protocols`（静态库） | `src/protocols` | TCP / MIDI / OSC 适配器 |
| `sm_kernel_smoke`（exe） | `src/app/main.cpp` | 内核冒烟程序 |
| `sm_headless`（exe） | `src/app/main_headless.cpp` | Headless 服务器（工控机常驻、Web 远控入口） |
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

当前实测：本机 `ctest` 与 Windows CI 均 **22/22 全绿**。Windows CI（`.github/workflows/windows-build.yml`）最近一次全绿 run 基于 commit e21c17b（含 headless 冒烟登录测试）。

编译纪律（全仓强制）：MSVC 用 `/W4 /permissive-`，其他平台 `-Wall -Wextra`，新代码必须零告警；测试全绿才算完成。

新增测试必须在 `tests/CMakeLists.txt` 里调用一次 `sm_add_unit_test(name libs)`；新增 `src/platform/*.cpp` 由顶层 CMake 的 `file(GLOB ... CONFIGURE_DEPENDS)` 自动收集，无需改 CMake。

## 4. 平台层现状（2026-09-09 核对）

### 4.1 `media_backend.h`（接口已定稿）

对外函数：`has_media_backend` / `probe_media` / `open_media_file` / `start_media_playback` / `stop_media_playback` / `get_media_pos_ms` / `get_media_duration_ms` / `close_media_file` / `generate_thumbnail`（JPEG base64）/ `capture_pgm_frame_jpeg`（JPEG base64）/ `init_audio_output` / `shutdown_audio_output`。

### 4.2 `media_backend.cpp`（实现现状）

文件按 `#if !defined(SM_HAS_FFMPEG)` 分成两支：

- stub 分支：macOS/Linux 编译，函数为空实现或返回空/`true`。
- FFmpeg 分支（Windows）：解码/播放/定位/关闭、后台解码线程、视频帧队列（保留最新一帧，RGB24，宽上限 1280）均已实现。
- 自研 JPEG 编码器 `sm::platform::encode_jpeg_rgb24`（`src/platform/jpeg_codec.cpp`）已实现并接线：`generate_thumbnail` 与 `capture_pgm_frame_jpeg` 均返回真实 JPEG base64，不再是空串。
- WASAPI 音频：`init_audio_output` 已做真实默认设备探测（`__uuidof(MMDeviceEnumerator)` / `IID_IAudioClient` 编译期 GUID），非占位。
- `capture_pgm_frame_jpeg` 的语义是"取解码线程最新一帧 → JPEG"，**不涉及 D3D11/DXGI**；全仓无 `SM_HAS_D3D11`、无 `d3d11.h`/`dxgi.h` 使用、无 d3d11/dxgi 链接。D3D11 渲染输出属未开始（见 TASKS_REMAINING P1-2）。
- 实时预监（PGM）链路各段存在（`web_gateway.cpp` 的 `/ws/preview` 与 `on_pgm_frame`、`web_ui.cpp` 内嵌页面），但**没有调用方驱动** `capture_pgm_frame_jpeg`（见 TASKS_REMAINING P1-3）。

### 4.3 协议层现状

- TCP 适配器：真实实现（`src/protocols/tcp_adapter.cpp`，accept 循环）。
- MIDI 适配器：stub（无 MIDI 库，`midi_adapter.cpp` 有 TODO 未接线分发）。

## 5. 剩余工作

以 `docs/TASKS_REMAINING.md` 为唯一执行清单：Phase 1 收尾（桌面端、D3D11 渲染与帧通道、PGM 链路、传输接口 501、M1–M5 验收审计、冒烟/性能门禁、安装包）→ Phase 2 创作增强 → Phase 3 专业外设 → Phase 4 智能化。全规格完成度估算约 25%。

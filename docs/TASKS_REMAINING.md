# ShowMaster 剩余任务清单

基线：2026-09-10（HEAD `97760ad`）。本机 macOS 侧 `ctest` 22/22 全绿；Windows CI（`.github/workflows/windows-build.yml`）的 `build`（vcpkg 依赖 → MSVC 编译 → ctest 22/22 → headless 冒烟）与 `desktop`（Qt 6.7 → `sm_desktop --smoke`）两个 job 的绿灯状态**请以 GitHub Actions 页面为准**（本机无 Qt/vcpkg，无法本地复现）。规格书《ShowMaster工程开发规格书V2.0_可落地版.docx》为业务语义最高依据；本清单的状态来自源码逐项核对，标注三类：已核实完成 / 已核实缺口（附证据）/ 待验收核对。当本清单与源码不一致时，以源码为准，并立即修正本清单。

## 1. 总体结论

按规格书 Phase 1–4 全范围估算，整体完成约 25%。Phase 1 的引擎内核与 Windows 平台链路已通，但规格书锁定的交付形态（Qt 桌面界面 + D3D11 本地窗口输出 + 安装包）尚未开始，这是离"能上台演出的单机软件"的最大缺口。

| 阶段 | 目标 | 完成度 | 主要缺口 |
| --- | --- | --- | --- |
| Phase 1 MVP | 演出编排与执行主闭环 | ~85%（内核）/ 0%（交付形态） | 桌面端与 D3D11 输出未做 |
| Phase 2 创作增强 | 剪辑器 / 8 层特效 / 字幕 / MIDI / ASIO | ~10% | 基本未开始 |
| Phase 3 专业外设 | LED/NDI/灯光 DMX/灯带/中控 | ~5% | 仅引擎目录占位 |
| Phase 4 智能化 | AI 灯光 / 8K / 多实例 | 0% | 未开始 |

## 2. Phase 1 收尾任务（当前主线）

### P1-1 桌面端形态决策与落地

规格书锁定 Qt 6.7 + QML 界面、D3D11 本地窗口输出 1080p60、Inno Setup 安装包；当前实际只有 headless 服务器 + 内嵌网页控制台（`src/web/web_ui.cpp`）。

- 现状（2026-09-10 更新）：桌面端 P1-1 骨架**代码级完成**，待 Windows Qt 6.7 实机编译冒烟（DoD 未达成）。
  - `CMakeLists.txt` 新增 `SM_BUILD_DESKTOP`（默认 OFF）与 `sm_desktop` 目标：`Qt6 6.7` 组件 `Core Gui Qml Quick QuickControls2`，AUTOMOC/AUTORCC + `qt_finalize_executable`（WIN32 子系统）。
  - 入口 `src/app/main_qt.cpp`；桥接 `src/app/qt/kernel_host.{h,cpp}`（`kernelBridge` 上下文属性：下行 JSON 指令 `postCommand`/transport/playlist 透传，上行信号 busEvent/statusChanged/playlistChanged/frameReady/kernelReady/fatalError）；装配 `src/app/qt/ui_controller.{h,cpp}`。
  - QML `src/app/qt/qml/`：`Main.qml`（1600×900，六区锚点 + 事件分发）+ `panels/` 六面板 + `UiStyle.js`（`.pragma library` 引擎级样式单例）+ `qml.qrc`（前缀 `/qml`，AUTORCC 打包）。
  - 已验证到静态级：qrc↔磁盘↔CMake↔include↔契约交叉核对通过；`UiStyle` 257 处引用差集校验无悬空；`Main.qml` → 六面板 `callPanel` 分发对照 24/24 命中（自动化脚本扫描，无悬空分派）；Singleton 目录导入语义修正为 JS 库（详见 Git 工作区改动，本机无 Qt 故未编译）。实机构建步骤已写入 `docs/BUILD_WINDOWS.md` §7。
  - CI 冒烟就位（2026-09-10）：入口新增 `--smoke` 自检（`src/app/main_qt.cpp`：QML 装配失败返回 1；成功进入事件循环 2.5s 让首轮上行信号真实执行后退出码 0）；`.github/workflows/windows-build.yml` 新增 `desktop` job（vcpkg Qt6 → `SM_BUILD_DESKTOP=ON` Release → `windeployqt --qmldir` → `sm_desktop --smoke` 退出码断言，详见 `docs/BUILD_WINDOWS.md` §6）。首次 run 待推送触发（本机无 Qt 环境，未本地执行）。
- 落点：Windows 实机按 `docs/BUILD_WINDOWS.md` §7 执行 `-DSM_BUILD_DESKTOP=ON` 构建与冒烟（出口项 E2/E3）；CI `desktop` job（路径 A，§6）提供自动化断言与产物。规格书第 1、2、9 章。
- 依赖：先于 P1-2（渲染窗口需要 D3D11 交换链）。
- DoD：Windows 上启动应用出现可操作主窗口；布局锚点符合第 9 章语义；本地输出视口 1080p60。

### P1-2 D3D11 渲染与帧通道（规格书 M2 10.2.3 / 第 2.3 章 RHI）

- 现状：离屏渲染与帧通道部分完成（2026-09-10 复核：macOS 侧 stub 分支 configure + 编译 + ctest 22/22 通过；Windows 真分支待 CI/真机编译验证）。
  - 新增 RHI 抽象层 `src/platform/rhi/rhi.h`：`PixelFormat`/`BlendOp`/`BlendConfig`/`Texture2DDesc`、`ITexture`（upload/readback/native_handle）、`IPgmMixer`（`kMaxLayers=4` 层混合 + `compose`/`compose_and_readback`）、`IDevice`（`create_device()` 工厂）。
  - D3D11 后端 `src/platform/rhi/rhi_d3d11.cpp`：离屏设备（HW→WARP 回退，无交换链）；纹理 `UpdateSubresource` 上传；离屏渲染目标 + staging 纹理 `CopyResource`/`Map` 回读；混合由 blend state 实现（Replace/Over/Add/Multiply × opacity）；每层四边形按 `fit_center` 自适应。非 Windows/未启用 `SM_HAS_D3D11` 时提供返回 `nullptr` 的 stub。
  - `CMakeLists.txt`：glob 纳入 `src/platform/rhi/*.cpp`；`if(WIN32)` 下 `sm_engines` PUBLIC 链接 `d3d11 dxgi d3dcompiler`、PRIVATE 定义 `SM_HAS_D3D11=1`（同 `SM_HAS_FFMPEG` 模式）。
  - `media_backend.cpp`：`capture_pgm_frame_jpeg()` 改为经渲染路径——无渲染上下文时返回空串，有上下文时最新解码帧上传 layer0 → 4 层合成 → 回读 → JPEG-Base64（对齐 `media_backend.h` 头注释语义）。
- 剩余缺口：单视口本地窗口输出（1080p60）依赖 P1-1 交换链；vjfx 引擎仍为 CPU 回退（GPU 着色器属 Phase 3 范畴）；Windows + `SM_HAS_D3D11` 真分支尚未在 CI/真机编译回归。
- 依赖：P1-1 窗口交换链（仅窗口输出部分）。
- DoD（离屏部分已达成）：4 层混合可用；`capture_pgm_frame_jpeg` 从渲染帧读取，无渲染上下文时返回空串。待验收：Windows 真分支编译 + 离屏合成像素级冒烟。

### P1-3 实时预监（PGM）链路打通

`web_gateway.cpp` 已实现 `/ws/preview` WebSocket 与 `on_pgm_frame` 推送，`web_ui.cpp` 内嵌页面已有 `<img id="pgmPreview">` 接收 `pgm_frame`；`src/app/main_headless.cpp` 主循环（约 50ms/次）在媒体处于 playing/paused 时以 ~10Hz 调 `capture_pgm_frame_jpeg()` 并经 `gateway.on_pgm_frame` 推送，空 JPEG 不推、同帧去重、离开播放态复位。

- 现状：headless 驱动已存在（main_headless.cpp 主循环，注释标记 P1-3）。
- 剩余缺口：画面内容依赖渲染上下文——macOS/Linux stub 或 Windows 未启用 `SM_HAS_D3D11` 时 `capture_pgm_frame_jpeg()` 返回空串（无渲染上下文语义），预监在真渲染帧通道（P1-2 Windows 分支 + 媒体解码）就绪前无画面。
- 依赖：P1-2 渲染帧通道（否则仍是"解码帧直出"而非渲染输出）。
- DoD：Windows 真机上浏览器打开内嵌控制台可见预监画面随播放刷新；无媒体时静默不推送。

### P1-4 传输控制接口补齐（web_gateway 501）

`web_gateway.cpp` 中 `/api/transport/pause` 与 `/api/transport/resume` 在回调未注入时返回 501 not implemented。

- 现状：已核实缺口（需确认 kernel/headless 是否注入 `transport_pause_fn_`/`transport_resume_fn_`，未注入则补）。
- 落点：`src/app/main_headless.cpp` 或 kernel 接线。
- DoD：暂停/恢复经 HTTP 可达且状态正确；与 10.5.3 仲裁语义一致。

### P1-5 工程文件（.showproj）与 M1–M5 验收审计

规格书 Phase 1 交付 M1 素材库、M2 媒体播放、M3 场景快照、M4 播放列表、M5 时间线调度，另有 .showproj（明文 zip + manifest + SHA-256，第 8 章）。现有单测已覆盖 engine 逻辑（test_media/test_scene/test_playlist/test_media_lib/test_timeline 等 22 项），但工程文件打包/校验与各模块 DoD 尚未按规格书逐条验收。

- 现状：待验收核对（勿假设完成）。
- 落点：按规格书 11.4.2 的 WP 级 DoD 表逐项过；缺什么补什么。
- DoD：M1–M5 各状态机可达、错误码正确；工程文件 zip/manifest/SHA-256 行为与第 8 章一致。

### P1-6 端到端冒烟与性能门禁（11.5 / 11.6）

规格书要求一键冒烟脚本输出结构化报告、性能指标可重复采集并与基线对比。

- 现状：CI 已有 headless 冒烟步骤（HTTP 请求级），但非规格书 11.5 的结构化脚本；11.6 门禁（起播延迟、防爆音、跨轨对齐 ±10ms）未采集。
- 落点：新增脚本与指标采集；录入 CI。
- DoD：冒烟一键运行输出结构化报告；11.6 指标有基线可对比。

### P1-7 安装包

规格书要求 Inno Setup 单机安装包（含 Qt 与 FFmpeg 运行时）；绿色免安装包为开发期产物。

- 现状：未开始。
- 落点：新增打包脚本（构建机）。
- 依赖：P1-1/P1-2 落地后才有完整可分发形态；可与 P1-1 并行准备脚本骨架。
- DoD：装包在干净 Windows 10/11 x64 可安装、启动、跑通最小演出流程。

## 3. Phase 2 创作增强（规格书第 12 章）

| ID | 任务 | 现状证据 | 说明 |
| --- | --- | --- | --- |
| P2-1 | MIDI 真实接入与分发 | `src/protocols/midi_adapter.cpp` 为 stub，L49 有 `TODO: thread-safe dispatch to process_midi_msg` | 无 MIDI 库；需选型（winmm/免驱）并接线到内核 |
| P2-2 | VJ 8 层实时特效 | vjfx 引擎为 CPU 回退 | 依赖 P1-2 的 D3D11 着色器通道 |
| P2-3 | 内置无损剪辑器 | 未开始 | Phase 2 新增能力 |
| P2-4 | 动态字幕 | 未开始 | 同上 |
| P2-5 | 音频联动 | 未开始 | 依赖 WASAPI 帧对齐能力 |
| P2-6 | ASIO 可选后端 | 未开始 | 规格书列为 Phase 2 可选 |
| P2-7 | 工程 AES 加密加固 | 未开始 | R10，依赖 P1-5 先落地明文形态 |

## 4. Phase 3 专业外设（规格书第 12 章）

| ID | 任务 | 现状证据 |
| --- | --- | --- |
| P3-1 | LED 多窗口 / NDI / 采集 | `src/engines/led` 仅引擎占位 |
| P3-2 | MTC 时间码 | 未开始 |
| P3-3 | 灯光引擎（GDTF / 国产 XML 灯库 / ArtNet / sACN / DMX） | `src/engines/light` 仅引擎占位 |
| P3-4 | 像素灯带 | `src/engines/pixel` 仅引擎占位 |
| P3-5 | 硬件中控 | `src/engines/device` 仅引擎占位 |

## 5. Phase 4 智能化（规格书第 12 章）

AI 灯光自动生成、8K 输出、多实例、数据服务。全部未开始。

## 6. 工程卫生（顺手项）

| ID | 任务 | 说明 |
| --- | --- | --- |
| H-1 | ✅ 已完成（2026-09-10） | 核对结论：`README.md`（第 27 行）、`docs/BUILD_WINDOWS.md`（第 46 行）、`docs/VERIFY_WINDOWS_MEDIA.md`（第 27/68/77 行）的 ctest 计数**均已是 22/22**，本项无实际残留；`jpeg_codec.cpp` 已落地并有 `test_jpeg_codec`，`docs/DEVELOPMENT_HANDOFF.md` 中相关描述亦已同步 |
| H-2 | ✅ 已完成（2026-09-10） | `docs/DEVELOPMENT_HANDOFF.md` 已重写为当前基线：日期更新至 09-10；§2 补入 `sm_desktop`（Qt 6.7 桌面端）与 `rhi/`；§3 移除过期的「最近一次全绿 run 基于 e21c17b」（其后已有 16 个提交）；§4 新增 `rhi/` 渲染抽象层与 PGM 链路两节，并作废「D3D11 渲染输出属未开始」与「没有调用方驱动 `capture_pgm_frame_jpeg`」两处过期论断；新增 §6 文档卫生记录 |

## 7. 建议执行顺序

1. H-1 / H-2 ✅ 已完成（2026-09-10，文档误导已消除，见 §6）。
2. P1-1 + P1-2（桌面与渲染是本阶段核心，串行推进，P1-7 脚本骨架可并行）。
3. P1-3（渲染帧通道就绪后打通预监）、P1-4（传输控制补齐）。
4. P1-5 审计 + P1-6 门禁，作为 Phase 1 发布验收。
5. Phase 2 起按 P2-x 顺序，P2-1 MIDI 不依赖渲染可提前插入。

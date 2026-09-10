# ShowMaster 剩余任务清单

基线：2026-09-10（本机 HEAD `128fc89`；工作区含 P1-5 容器层新增的 `src/project/project_zip.{h,cpp}`、`src/project/project_archive.{h,cpp}` 与 `tests/test_project_zip.cpp`）。Windows CI（`.github/workflows/windows-build.yml`）`build` 与 `desktop` 两个 job 在 run #34434959091（commit `97760ad`，2026-09-10）**全绿**：`build` = vcpkg 依赖 → MSVC 编译 → ctest（该 run 时点为 22/22）→ headless 冒烟；`desktop` = aqt Qt 6.7.2 → `SM_BUILD_DESKTOP=ON` Release 构建 `sm_desktop` → `windeployqt` → `--smoke` 退出码 0。run #34436939684（commit `1b17e97`）的 `desktop` job **再次全绿**（步骤 1–11 + 4 个 post 步骤全部 success，含步骤 8 Build sm_desktop、步骤 10 Smoke test），即 **P1-2 的 D3D11 输出窗口代码已在 Windows/MSVC 下真编译通过**；同 run 的 `build` job 仍在执行（vcpkg `ffmpeg` 安装耗时较长，步骤 4 进行中，其后才轮到 ctest 与 headless 冒烟）。run #34437790755（commit `f05e31b`，即 P1-2 收尾把 `sm_desktop` 输出视口**调用方接线**并入构建的那次提交）的 `desktop` job 在步骤 8 `Build sm_desktop` **失败**（步骤 9/10/11 skipped）：`src/app/qt/output_window.cpp(60/82/105): error C2027: use of undefined type 'QDebug'` —— 根因是该文件的包含集仅 `<QString>/<QWindow>/<QtGlobal>`（只**前向声明** `QDebug`），而 `qWarning().noquote() <<` / `qInfo().noquote() <<` 需要 `QDebug` **完整类型**；`ui_controller.cpp` 用同样写法却能编译，只是因为 `QQmlApplicationEngine`（QtQml）顺带把 `QDebug` 传递包含进来了。**已修复并复验通过**：补 `#include <QDebug>`（commit `a94c26f`）后，run #34438302868（commit `128fc89`）的 `desktop` job **全绿**——步骤 1–11 + 4 个 post 步骤全部 success，含步骤 8 `Build sm_desktop` 与步骤 10 `Smoke test sm_desktop (--smoke, expect exit code 0)`；同 run 的 `build` job 于本次核对时仍在执行（步骤 4 vcpkg 依赖安装，`ffmpeg` 编译耗时较长，其后才轮到 ctest 与 headless 冒烟）。本机（macOS）侧 `ctest` **26/26** 全绿（stub 分支，无 Qt；P1-2 收尾新增 `test_output_window_api` 后 23 → 24，P1-4 收尾新增 `test_web_ui` 后 24 → 25，P1-5 容器层新增 `test_project_zip` 后 25 → 26，全仓计数同步为 `26/26`）——注意 `desktop` job 无法在本机复现，其后续绿灯状态请以 GitHub Actions 页面为准。规格书《ShowMaster工程开发规格书V2.0_可落地版.docx》为业务语义最高依据；本清单的状态来自源码逐项核对，标注三类：已核实完成 / 已核实缺口（附证据）/ 待验收核对。当本清单与源码不一致时，以源码为准，并立即修正本清单。

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

- 现状（2026-09-10 更新）：桌面端 P1-1 骨架**代码级完成**，且已由 Windows CI `desktop` job 完成**真编译 + 真冒烟**：run #34434959091（commit `97760ad`）该 job 全部步骤 success，含 aqt Qt 6.7.2 安装、`-DSM_BUILD_DESKTOP=ON` Release 构建、`windeployqt --qmldir` 部署（带 `platforms/`+`qml/` 存在性断言）、`sm_desktop --smoke` 退出码 0（即 QML 装配成功且事件循环真实跑过首轮上行信号）；run #34436939684（commit `1b17e97`）该 job **再次全绿**，且此时 `sm_desktop` 已含 P1-2 的输出视口代码，故 P1-1 与 P1-2 的接线在 Windows 上编译链接无阻。剩余仅人工实机验收项：窗口视觉/布局锚点符合第 9 章语义、1080p60 输出视口（依赖 P1-2 窗口输出）。
  - `CMakeLists.txt` 新增 `SM_BUILD_DESKTOP`（默认 OFF）与 `sm_desktop` 目标：`Qt6 6.7` 组件 `Core Gui Qml Quick QuickControls2`，AUTOMOC/AUTORCC + `qt_finalize_executable`（WIN32 子系统）。
  - 入口 `src/app/main_qt.cpp`；桥接 `src/app/qt/kernel_host.{h,cpp}`（`kernelBridge` 上下文属性：下行 JSON 指令 `postCommand`/transport/playlist 透传，上行信号 busEvent/statusChanged/playlistChanged/frameReady/kernelReady/fatalError）；装配 `src/app/qt/ui_controller.{h,cpp}`。
  - QML `src/app/qt/qml/`：`Main.qml`（1600×900，六区锚点 + 事件分发）+ `panels/` 六面板 + `UiStyle.js`（`.pragma library` 引擎级样式单例）+ `qml.qrc`（前缀 `/qml`，AUTORCC 打包）。
  - 已验证到静态级：qrc↔磁盘↔CMake↔include↔契约交叉核对通过；`UiStyle` 257 处引用差集校验无悬空；`Main.qml` → 六面板 `callPanel` 分发对照 24/24 命中（自动化脚本扫描，无悬空分派）；Singleton 目录导入语义修正为 JS 库（详见 Git 工作区改动，本机无 Qt 故未编译）。实机构建步骤已写入 `docs/BUILD_WINDOWS.md` §7。
  - CI 冒烟就位（2026-09-10）：入口新增 `--smoke` 自检（`src/app/main_qt.cpp`：QML 装配失败返回 1；成功进入事件循环 2.5s 让首轮上行信号真实执行后退出码 0）；`.github/workflows/windows-build.yml` 新增 `desktop` job（aqt 预编译 Qt 6.7.2（§7.1 方式 A）→ `SM_BUILD_DESKTOP=ON` Release → `windeployqt --qmldir` → `sm_desktop --smoke` 退出码断言，详见 `docs/BUILD_WINDOWS.md` §6）。首次 run 已于 2026-09-10 触发并**全绿**（run #34434959091，见上）；本机无 Qt 环境，故不做本地执行。
- 落点：Windows 实机按 `docs/BUILD_WINDOWS.md` §7 执行 `-DSM_BUILD_DESKTOP=ON` 构建与冒烟（出口项 E2/E3）；CI `desktop` job（路径 A，§6）提供自动化断言与产物。规格书第 1、2、9 章。
- 依赖：先于 P1-2（渲染窗口需要 D3D11 交换链）。
- DoD：Windows 上启动应用出现可操作主窗口；布局锚点符合第 9 章语义；本地输出视口 1080p60。

### P1-2 D3D11 渲染与帧通道（规格书 M2 10.2.3 / 第 2.3 章 RHI）

- 现状：离屏渲染 + 帧通道 + **输出窗口（DXGI 交换链）+ `sm_desktop` 接线** 完成（2026-09-10 复核：macOS 侧 stub 分支 configure + 编译 + ctest **25/25** 通过；Windows 真分支（`_WIN32 && SM_HAS_D3D11`）已由 CI `desktop` job 在 commit `1b17e97`（run #34436939684）**真编译通过**——该 job 步骤 8 Build sm_desktop 与步骤 10 Smoke test 均 success，这是 D3D11 输出窗口代码首次经 MSVC 编译链接；**但**把调用方接线并入构建的 commit `f05e31b`（run #34437790755）在步骤 8 `Build sm_desktop` **失败**（`output_window.cpp: error C2027: use of undefined type 'QDebug'`），已补 `#include <QDebug>` 修复，推送后须以新 run 的 `desktop`/`build` 结果为准）。
  - 新增 RHI 抽象层 `src/platform/rhi/rhi.h`：`PixelFormat`/`BlendOp`/`BlendConfig`/`Texture2DDesc`、`ITexture`（upload/readback/native_handle）、`IPgmMixer`（`kMaxLayers=4` 层混合 + `compose`/`compose_and_readback`）、`IDevice`（`create_device()` 工厂）；P1-2 另增输出窗口契约 `NdcRect`/`compute_ndc_rect()`（合成与窗口输出共用的等比居中/拉伸几何）、`OutputWindowDesc`/`is_valid_output_window_desc()`、`IOutputSurface`/`IDevice::create_output_surface()`。
  - D3D11 后端 `src/platform/rhi/rhi_d3d11.cpp`：离屏设备（HW→WARP 回退）+ **本地窗口输出**（调用方 HWND 上建 DXGI 交换链；`FLIP_DISCARD`→`DISCARD` 自动回退、`OCCLUDED` 视为成功、设备丢失关面、`resize` 重建后缓冲且失败时按旧尺寸尽力恢复、vsync 收尾节流至刷新率）；纹理 `UpdateSubresource` 上传；离屏渲染目标 + staging 纹理 `CopyResource`/`Map` 回读；混合由 blend state 实现（Replace/Over/Add/Multiply × opacity）；每层四边形按 `fit_center` 自适应（与窗口输出共用同一几何函数与顶点缓冲）。非 Windows/未启用 `SM_HAS_D3D11` 时提供返回 `nullptr` 的 stub。
  - `CMakeLists.txt`：glob 纳入 `src/platform/rhi/*.cpp`；`if(WIN32)` 下 `sm_engines` PUBLIC 链接 `d3d11 dxgi d3dcompiler`、PRIVATE 定义 `SM_HAS_D3D11=1`（同 `SM_HAS_FFMPEG` 模式）。
  - `media_backend.cpp`：`capture_pgm_frame_jpeg()` 改为经渲染路径——无渲染上下文时返回空串，有上下文时最新解码帧上传 layer0 → 4 层合成 → 回读 → JPEG-Base64（对齐 `media_backend.h` 头注释语义）。
  - 新增 `tests/test_rhi_geometry.cpp`（P1-2）：`compute_ndc_rect` 几何（拉伸铺满 / 等比居中 letterbox+pillarbox / 尺寸退化 / 不变量：不越界·对称·纵横比守恒·完整装入）与 `is_valid_output_window_desc` 边界（空句柄 / 0 尺寸 / `kMaxOutputDimension` 上下界 / 默认构造非法）共 522 项检查 —— 纯逻辑在 `rhi.h` 内联，故是本机（无 D3D11）唯一自动化防线；已在 `tests/CMakeLists.txt` 注册（测试总数 22 → 23）。
  - 新增 `tests/test_output_window_api.cpp`（P1-2 收尾）：锁定 `sm_desktop` 接线所依赖的**降级前置条件** —— 空句柄 / 0 尺寸 / 负尺寸 / `16385` / `20000`（超 `kMaxOutputDimension`）一律 `open_output_window()` 返回 false，且 `last_output_window_error()` 有可读文本（失败可取证）；`resize_output_window()` 在未登记时安全空操作（含 0/负/超大尺寸）；`close_output_window()` 可重入幂等；无能力分支（非 Windows / 未编 `SM_HAS_D3D11`）永不谎报成功。三个编译分支（真实现 / 无 D3D11 / stub）均须通过；已在 `tests/CMakeLists.txt` 注册（测试总数 23 → 24）。
  - `sm_desktop` 调用方接线（P1-2 收尾，本轮落地）：新增 `src/app/qt/output_window.{h,cpp}`（唯一调用方）+ `UiController` 持有并装配（`degraded` 信号转告警日志、`SM_NO_OUTPUT_WINDOW` 环境变量可跳过、失败非致命）；登记顺序与线程模型见 §4.3 与 `DEVELOPMENT_HANDOFF.md`。
  - 新增 `tests/test_web_ui.cpp`（P1-4 收尾）：内嵌 Web 控制台（`kWebUIHtml`）是**裸 HTML 字符串，编译器不做任何检查**，故用静态一致性断言钉住三类静默缺陷 —— 页面基础（尺寸/`DOCTYPE`/`</html>`/`charset="utf-8"`/`lang="zh-CN"`/localStorage 令牌/`doLogout`）、`onclick` 处理器**必有**同名 `function NAME(` 定义、页面内 `/api/*` 与 `/ws/*` 字面量**必属** `web_gateway.cpp::register_routes` 的 25 条已注册路由白名单；并单独校验传输控制 `play/stop/pause/resume` 四条链路端到端齐全（路径被引用 + 路由已注册 + 函数有定义 + `onclick` 引用，且存在 `.btn-resume`）；已在 `tests/CMakeLists.txt` 注册（测试总数 24 → 25）。
- 剩余缺口：**代码链路已闭合** —— RHI 侧「输出窗口 + DXGI 交换链」已实现（`rhi_d3d11.cpp`），`sm_desktop` 侧接线已落地（`src/app/qt/output_window.{h,cpp}`：`QWindow::winId()` → `platform::open_output_window()` → 渲染线程每帧 `present(PGM 合成结果)`；`resize`/`close` 分别转发 `resize_output_window()`/`close_output_window()`），并具备掉线降级：`create_output_surface()` 返回 `nullptr`（无能力 / 参数非法 / 建链失败）时自动降级为「仅离屏 + 预监」并闩存失败原因，**不是致命错误**。仅剩：推送后的 CI 新 run 真编译确认 + Windows 实机 1080p60 人工验收。vjfx 引擎仍为 CPU 回退（GPU 着色器属 Phase 3 范畴）。
- 依赖：P1-1 提供的窗口句柄（已由 `sm_desktop` 接线满足）。
- DoD（代码已达成，待实机验收）：4 层混合可用；`capture_pgm_frame_jpeg` 从渲染帧读取，无渲染上下文时返回空串；`create_output_surface()` 可在调用方 HWND 上建交换链并 `present` PGM 合成结果，失败返回 `nullptr` 由调用方降级；`sm_desktop` 已把 Qt 窗口句柄接入并转发 resize/close。剩余验收：Windows 实机确认输出视口出现并随播放刷新、1080p60 帧率达标（CI `desktop` job 已确认编译链接与启动无阻）。

### P1-3 实时预监（PGM）链路打通

`web_gateway.cpp` 已实现 `/ws/preview` WebSocket 与 `on_pgm_frame` 推送，`web_ui.cpp` 内嵌页面已有 `<img id="pgmPreview">` 接收 `pgm_frame`；`src/app/main_headless.cpp` 主循环（约 50ms/次）在媒体处于 playing/paused 时以 ~10Hz 调 `capture_pgm_frame_jpeg()` 并经 `gateway.on_pgm_frame` 推送，空 JPEG 不推、同帧去重、离开播放态复位。

- 现状：headless 驱动已存在（main_headless.cpp 主循环，注释标记 P1-3）。
- 剩余缺口：画面内容依赖渲染上下文——macOS/Linux stub 或 Windows 未启用 `SM_HAS_D3D11` 时 `capture_pgm_frame_jpeg()` 返回空串（无渲染上下文语义），预监在真渲染帧通道（P1-2 Windows 分支 + 媒体解码）就绪前无画面。
- 依赖：P1-2 渲染帧通道（否则仍是"解码帧直出"而非渲染输出）。
- DoD：Windows 真机上浏览器打开内嵌控制台可见预监画面随播放刷新；无媒体时静默不推送。

### P1-4 传输控制接口补齐（web_gateway 501）

`web_gateway.cpp` 中 `/api/transport/pause` 与 `/api/transport/resume` 在回调未注入时返回 501 not implemented。

- 现状：**已核实完成（2026-09-10 源码逐项核对）**，原「501 缺口」不成立。证据链：`src/app/main_headless.cpp:185-189` 同时注入 `transport_pause_fn` 与 `transport_resume_fn`（Qt 桌面侧为 `src/app/qt/kernel_host.cpp:118/123`）→ `web_gateway.cpp:304-330` 在回调存在时**直接执行并返回成功**，仅当回调为空才走 `json_err(501,"not implemented")`（该分支在 headless/桌面两种装配下均不可达）→ `Kernel::transport_pause()/transport_resume()`（`kernel.cpp:388/393`）转 `timeline_->scheduler().pause()/resume()`；`tests/test_kernel.cpp::test_transport_control` 已断言 play → paused → resume → playing → stopped 全状态迁移。本轮补上的**唯一真实缺口在前端**：内嵌控制台原本只有 `sendPause()` 而没有恢复入口（而 `web_ui.cpp` 头注释早已宣称「暂停/恢复」）——已新增 `function sendResume(){fetch('/api/transport/resume',…)}`、`续播` 按钮与 `.btn-resume` 样式，并由 `tests/test_web_ui.cpp` 钉住四条传输链路端到端齐全。
- 落点：已完成（`src/web/web_ui.cpp` + `tests/test_web_ui.cpp`）。
- DoD：暂停/恢复经 HTTP 可达且状态正确（✅ 服务端已具备 + 前端入口已补齐；真机浏览器点击验证并入 P1-1 人工验收）；与 10.5.3 仲裁语义一致（✅ 快速动作直连调度器，不经 RemoteQueue）。

### P1-5 工程文件（.showproj）与 M1–M5 验收审计

规格书 Phase 1 交付 M1 素材库、M2 媒体播放、M3 场景快照、M4 播放列表、M5 时间线调度，另有 .showproj（明文 zip + manifest + SHA-256，第 8 章）。现有单测已覆盖 engine 逻辑（test_media/test_scene/test_playlist/test_media_lib/test_timeline 等 26 项），但工程文件打包/校验与各模块 DoD 尚未按规格书逐条验收。

- 现状（2026-09-10 复校）：**工程文件容器层已落地，M1–M5 的 WP 级 DoD 逐项审计仍待完成**。此前 `src/project/project_file.{h,cpp}` 头注释自述「zip 容器读写（minizip-ng）属平台里程碑，本模块为纯逻辑内核版，只负责 manifest.json 的结构校验与行集抽取/装配」——即第 8 章的容器读写这一环是**真实缺口**，本轮补齐。
  - 新增 `src/project/project_zip.{h,cpp}`：自包含 **STORE（不压缩，method 0）** 的 zip 编解码器。**取舍说明**：规格书只要求「明文 zip」（可被任意解压工具读出），未强制具体库；已压媒体再 deflate 收益极低，且引入 minizip-ng/zlib 会把内核里程碑绑死在 vcpkg 上。故用 ~330 行零第三方依赖实现替代原计划的 minizip-ng，收益是**内核里程碑在 macOS 与 Windows CI 上均可本机自测**。写出侧同时打本地头 + 中央目录 + EOCD，条目名置 UTF-8 标志位（`0x0800`，中文素材名可被标准工具正确解码），DOS 日期固定为 `1980-01-01` 以使**同一输入产出逐字节确定的包**（便于哈希比对与回归）；超出 zip32 上限（条目数 > 65535、单条目 ≥ 4 GiB）**显式报错而非静默截断**。读取侧反向搜索 EOCD（含最长 65535 字节注释），解析中央目录后逐条回查本地头（`crc`/`size` 不一致即「中央目录与本地头信息不一致」）并复算 CRC-32，**任一不符即整体失败**（不做「尽力而为」的部分解析）。
  - 新增 `src/project/project_archive.{h,cpp}`：按 §8.3/§8.4 编排 save/load。`save_project` 五步——pack 模式素材入库（`sha256_file_hex` → 包内名 `media/<sha256前16位>_<原文件名>`，同名同内容幂等跳过、同名异内容报「包内命名冲突」；`media_files` 以有序 map 重建以保证输出确定性）→ 写 `saved_at` + `dump(2)` + `validate_manifest`（语义校验仍归 `project_file`，此处不重复实现）→ 写 `.tmp` → **回读 `.tmp` 复验**（zip 完整性 + 清单复校 + 打包素材 SHA-256 复校）→ `backups/` 轮转（按 mtime 保留最近 5 份）→ 原子 `rename`（跨卷时 `copy_file(overwrite_existing)` 兜底）。**任何失败路径都不动既有工程文件**。`load_project` 严格**全有或全无**：只有完整校验通过的 manifest 才交付，失败一律返回空对象（不存在「半加载」状态）；错误码 `PROJECT_CORRUPT=4001` / `PROJECT_VERSION_TOO_NEW=4002` / `PACKED_MEDIA_MISSING=4003`。
  - 新增 `tests/test_project_zip.cpp`（102 项检查）：CRC-32 标准向量 `crc32("123456789")==0xCBF43926`、zip 往返（含二进制与空条目）、损坏包拒绝（截断/坏签名/篡改 CRC）、ref 与 pack 两种 save→load 往返、4001/4002/4003 错误码、保存失败后原文件逐字节不变且无 `.tmp` 残留、`backups/` 恰好保留 5 份且最新的可加载、素材引用去重、打包素材 SHA-256 不匹配 → 4001；已在 `tests/CMakeLists.txt` 注册（测试总数 25 → 26）。
  - **本机验证**：`cmake --build --preset debug` 通过，`ctest` **26/26** 全绿（`test_project_zip` 102 项检查通过）。另做**独立互操作验证**（非仓库代码，用系统工具交叉验证「明文 zip」规格达成）：用 `project_archive` 写出一份 pack 模式 `.showproj` 后，系统 `unzip -t` 报 `No errors detected`、Python `zipfile.testzip()` 返回 `None`、`flag_bits==0x0800`、`compress_type==0`、中文条目名 `media/29499e32a39fb416_片头.mp4` 正确解码，且清单内 `media_files[0].sha256` 与实际条目内容的 SHA-256 逐位一致。
- 落点（已完成部分）：`src/project/project_zip.{h,cpp}` + `src/project/project_archive.{h,cpp}` + `tests/test_project_zip.cpp`（无需改根 `CMakeLists.txt`——`sm_core` 已 glob `project/*.cpp`）。
- M1–M5 的 11.4.2 WP 级 DoD 审计（2026-09-10 完成，本轮闭环）：按 W1/W8/W11 三条逐项对照源码与用例，共查处 **6 处真实缺口**并全部补齐，均以新增回归用例钉住：
  - **W1（§5.3 33 op / §5.4 16 evt / §5.5 错误码全部有代码入口且能经总线收发）**——发现 5 条 §5.4 事件在 `op_dict` 中已登记但**全仓无任何 emit 点**（`evt.playlist.playing`、`evt.playlist.advance`、`evt.transport.bpm`、`evt.engine.up`/`down`、`evt.engine.heartbeat`、`evt.log`），即「字典有、代码无」。补齐方式：M4 `PlaylistExecutor` 在 `start()`/`advance()`/结束路径按 §5.4 契约载荷上报 playing/advance；M5 传输面新增 BPM 上报入口（含**同值去重**与 confidence 钳制到 `[0,1]`，避免稳态下高频空转刷总线）；内核新增统一入口 `Kernel::pump_heartbeat()`（记心跳 → 推进失联判定 → 按**边沿变化**产出 `up`/`down`，再对在线引擎逐条产出 `heartbeat{engine_id, load_pct, mem_mb}`，载荷取自可注入的 `set_engine_metrics` 采样器）+ `Kernel::log_event()`。**关键缺陷修复**：`src/app/main_headless.cpp` 的心跳 tick 仍直接调 `heartbeat().tick()` + 手工 `note_heartbeat()` 循环，而 `HeartbeatMonitor` 本身**不产生任何事件**，故 headless 宿主此前**永远不会广播 `evt.engine.*`**；已改为统一走 `kernel->pump_heartbeat()`。另修正 `evt.engine.up`/`down`/`heartbeat` 的**语义层次**：`evt_ops_spec()` = §5.4 表核心集 17 条（表 16 行，`engine.up`/`down` 同行拆两条），`evt_ops()` = 核心集 + 实现增补 5 条（`playlist.loaded/item_started/item_ended/waiting_go`、`scene.saved`）= 22 条，两层分离后「规格同源」与「实现超集」不再互相掩盖（此前 17 的硬编码断言即因此转红）。
  - **W1 载荷契约**——`evt.error` 原只发 `{code, message, module, item_id}` 而 §5.4 契约为 `{code, msg, source}`，`evt.media.import_done` 原只发 `{total, ok_count, failed_count}` 而契约为 `{media_ids:[]}`；均已**补齐契约字段并保留原扩展字段（超集而非替换）**，不破坏既有消费方。
  - **W8（状态机全可达）**——新增内核级用例实测 M4 `idle→loaded→running→(waiting_go|paused)→ended` 推进、`evt.playlist.advance` 的 `trigger_mode` 取值、`evt.playlist.ended` 的 `reason` 分流，以及 M5 BPM 上报的取值/去重/钳制边界。
  - **W11（§9.5 [1015] 白名单子集可经 TCP 完成状态查询与控制、断线重连不残留半状态）**——落地 `is_remote_control_op()` 策略（`transport.*` / `playlist.*` 前缀族 + `scene.recall` / `sys.set_volume` / `sys.ping` 单例，其余一律 1002），**前缀判定要求 `op.size() > prefix.size()`**，故裸 `"transport."` / `"playlist."` 与非白名单 `sys.shutdown` / `media.*` / `timeline.load` / `scene.save|delete|list` / `remote.hello` / `evt.*` 全部拒绝并有边界用例覆盖；headless 侧接入握手门禁、`[1016]` 观测面转发（`evt.transport.state` / `evt.playlist.*` / `evt.scene.recalled` / `evt.error`，按规格原文**不含** `evt.log` 与 `evt.engine.*`）与断线清理。
  - **11.4.1 通用 DoD「零新增告警」**——顺手清掉两处**既有**编译告警（`MediaEngine::stop()` 未使用形参 `item_id`、`MediaLibrary::gen_media_id()` 未使用局部量 `ms`），现全仓编译告警为空，仅剩既有的 `ld: ignoring duplicate libraries` 链接告警。
  - 钉住手段：`tests/test_op_dict.cpp`（计数改为三层钉：`cmd_ops()==33` / `evt_ops_spec()==17` / `evt_ops()==22`，新增 `test_evt_superset()` 校验核心集⊆全集且增补集恰好 5 条且全集无重复、新增 `test_remote_whitelist()` 覆盖 12 前缀 + 3 单例放行与 12 条拒绝 + 5 条边界）、`tests/test_kernel.cpp`（新增 4 个用例、BusProbe 增 `count()`/`all()`，用例数 19 → 23）、`src/app/main.cpp`（冒烟程序同步两层计数断言 + 白名单双向断言）、`tests/test_media_lib.cpp` / `tests/test_playlist.cpp`（契约载荷断言）。
- 剩余缺口：无（M1–M5 的 11.4.2 WP 级 DoD 本轮已逐条过完）。若后续新增 op/evt，须同步两处计数钉与字典（§11.4.1「op/evt 变更须文档同源更新」）。
- DoD：M1–M5 各状态机可达、错误码正确；工程文件 zip/manifest/SHA-256 行为与第 8 章一致 —— **全部达成**（容器层 + WP 级审计均已完成，本机 ctest **26/26** 全绿）。

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
| H-1 | ✅ 已完成（2026-09-10；2026-09-10 P1-2/P1-4 收尾复校） | 核对结论：`README.md`（第 27 行）、`docs/BUILD_WINDOWS.md`（第 46 行）、`docs/VERIFY_WINDOWS_MEDIA.md`（第 27/68/77 行）、`docs/DEVELOPMENT_HANDOFF.md`（第 49 行）的 ctest 计数已随 `test_rhi_geometry`（23）、`test_output_window_api`（24）、`test_web_ui`（25）三次落地统一更新为 **25/25**，本项无实际残留；`jpeg_codec.cpp` 已落地并有 `test_jpeg_codec`，相关描述亦已同步 |
| H-2 | ✅ 已完成（2026-09-10） | `docs/DEVELOPMENT_HANDOFF.md` 已重写为当前基线：日期更新至 09-10；§2 补入 `sm_desktop`（Qt 6.7 桌面端）与 `rhi/`；§3 移除过期的「最近一次全绿 run 基于 e21c17b」（其后已有 16 个提交）；§4 新增 `rhi/` 渲染抽象层与 PGM 链路两节，并作废「D3D11 渲染输出属未开始」与「没有调用方驱动 `capture_pgm_frame_jpeg`」两处过期论断；新增 §6 文档卫生记录 |

## 7. 建议执行顺序

1. H-1 / H-2 ✅ 已完成（2026-09-10，文档误导已消除，见 §6）。
2. P1-1 + P1-2（桌面与渲染是本阶段核心，串行推进，P1-7 脚本骨架可并行）——**代码级完成，待验收**：P1-2 的 RHI 输出窗口（DXGI 交换链）+ 几何单测 + `sm_desktop` 接线（Qt 窗口句柄 → `open_output_window()` → 渲染线程每帧 `present`，失败降级「仅离屏 + 预监」）均已落地，本机 ctest **26/26**；剩余为推送 `<QDebug>` 修复后 CI 新 run 的真编译确认（✅ 已复验：run #34438302868 的 `desktop` job 全绿）与 Windows 实机 1080p60 人工验收（D3D11 输出窗口代码本身已在 commit `1b17e97` 的 `desktop` job 编译链接通过；调用方接线那次提交 `f05e31b` 因缺 `QDebug` 完整类型失败，已修复）。
3. P1-3（渲染帧通道就绪后打通预监）、P1-4（传输控制补齐）✅ 已完成（服务端本就注入回调，本轮补齐前端续播入口并以 `test_web_ui` 钉住）。
4. P1-5 审计 + P1-6 门禁，作为 Phase 1 发布验收 —— **P1-5 ✅ 全部完成**：容器层（.showproj zip + manifest + SHA-256）通过本机 26/26 与系统 unzip/Python zipfile 交叉验证；M1–M5 的 11.4.2 WP 级 DoD（W1/W8/W11）已逐条审计并闭环，共补 6 处真实缺口（5 条无 emit 点的 §5.4 事件、headless 心跳 tick 语义错接、`evt.error`/`evt.media.import_done` 契约载荷缺字段、§9.5 [1015] 白名单策略、零新增告警），全部以新增回归用例钉住，本机 ctest **26/26** 全绿。剩余为 P1-6 冒烟/性能门禁。
5. Phase 2 起按 P2-x 顺序，P2-1 MIDI 不依赖渲染可提前插入。

# Windows 真实媒体后端验证手册

适用范围：`src/platform/media_backend.cpp` 的 **Windows 真实分支**（FFmpeg 8 解码 + WASAPI 事件驱动渲染）。
macOS/Linux 只编译 stub 分支（`#if !defined(SM_HAS_FFMPEG)`），**无法在本机验证真实分支**；
真实分支的编译正确性、WASAPI 握手与音频链路只能以 Windows 编译器 + 真实声卡环境为准。

> 等价自动化：`.github/workflows/windows-build.yml`（push/PR 触发，windows-latest）。
> 本手册是同一流程的本机可执行版，另含 `[audio]` 启动日志断言与可听播放冒烟。

## 1. 一键验证（推荐）

在装有 VS2022、CMake、Ninja 与 vcpkg 的 Windows 10/11 x64 机器上，于
**"x64 Native Tools Command Prompt for VS 2022"** 中执行：

```bat
powershell -ExecutionPolicy Bypass -File scripts\verify_windows_media.ps1
```

脚本按 CI 顺序执行并断言：

1. `vcpkg install sqlite3:x64-windows ffmpeg:x64-windows`（已装可加 `-SkipVcpkgInstall`）；
   未设置 `VCPKG_ROOT` 时**立即失败退出**（不进入构建，避免拼出无效 toolchain 路径）；
2. `cmake --preset windows-msvc-debug`，**断言 configure 日志出现 `FFmpeg found` 子串**
   （CMake 原文为 `FFmpeg found — enabling real media backend`，其中破折号是 U+2014，在非
   UTF-8 控制台代码页下会被替换成 `?`；脚本只匹配 ASCII 子串以免门禁静默失效）；
   出现 `stub media backend` 即失败退出；
3. `cmake --build` + `ctest`，期望 **25/25 通过**（媒体真实分支随 sm_engines 一并编译链接）；
4. 启动 `sm_headless.exe --port 8099` → **先 `POST /api/login`（admin/admin123）换会话
   token，再带 `Authorization: Bearer <token>` 请求 `GET /api/status`，必须 200（硬门槛）**；
   裸请求 `/api/status` 会 401（鉴权见 `src/web/web_gateway.cpp` 的 `check_auth`）。
   stdout 中的 `[audio] WASAPI 默认渲染端点可用` 为尽力而为断言
   （stdout 重定向到文件时为全缓冲，未见该行但 HTTP 正常不算失败，见 §4）。

退出码：0 = 全绿；非 0 = 失败项。

## 2. 手动步骤（等价）

```bat
cd showmaster-src
set VCPKG_ROOT=C:\vcpkg
cmake --preset windows-msvc-debug -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
build\windows-msvc-debug\sm_headless.exe --port 8099
```

Release 同理替换 preset 为 `windows-msvc-release`。

## 3. 可听播放冒烟（交互桌面会话必做一次）

CI/Session 0/无声卡环境无默认音频端点，`wasapi_available=false` → 纯视频模式是**预期行为**。
要验证真实音频渲染，必须在**带声卡且已登录的交互桌面**上：

1. 启动 headless，浏览器访问 `http://127.0.0.1:8099`（admin / admin123）；
2. 准备一段**含音轨**的媒体（mp4/mov/wav 皆可），导入并播放；
3. 预期：~1s 内出声；音量随增益（`gain_db`）与淡入包络变化；
4. 连续执行 open → start → stop → close 若干次：
   - 无崩溃、无报错；
   - 任务管理器观察进程句柄数/线程数**不随循环单调增长**（句柄/COM 泄漏检查）；
   - stop 后 ~几百 ms 内无声（淡出尾音，可接受）。

## 4. 断言点与失败排查

| 现象 | 含义 / 处置 |
| --- | --- |
| configure 日志 `stub media backend` | vcpkg 未提供 ffmpeg 或 toolchain 未生效 → 检查 `VCPKG_ROOT` 与 `vcpkg install ffmpeg:x64-windows` |
| 编译错（真实分支内） | 类型/API 与 MSVC/FFmpeg 头不匹配 → 以 MSVC 报错为准修正 |
| `ctest` 非 25/25 | 媒体/引擎回归 → 先看 `--output-on-failure` 明细 |
| HTTP `/api/status` 非 200 | 服务未起/端口占用 → 查 stderr 日志 |
| 启动未见 `[audio]` 行 | 脚本把 stdout 重定向到文件 → CRT 全缓冲；启动横幅总量远小于 4KB 缓冲，且进程由 `Stop-Process -Force` 终止（不触发 flush），故该行基本不会落盘。**这是预期现象，不代表端点不可用**：要判 `[audio]` 就在终端前台直接运行（不重定向）；服务健康一律以 HTTP 200 为准 |
| 启动见 `[audio] 音频端点不可用` | 当前会话无默认渲染端点（CI/Session 0/无声卡）→ 属预期，换交互桌面会话复测 |
| 播放有画面无声 / 声卡忙 | 端点被独占占用 / 格式不受支持 → 代码仅接受 PCM16/48k/2ch 等价格式，其余走 `audio_drop` 纯视频兜底 |

## 5. 完成标准（DoD）

- [ ] configure 日志出现 `FFmpeg found`（完整原文 `FFmpeg found — enabling real media backend`）
- [ ] `ctest` 25/25 全绿
- [ ] headless `GET /api/status` 200（须带 `/api/login` 换来的 Bearer token）
- [ ] 交互桌面启动日志含 `[audio] WASAPI 默认渲染端点可用`
- [ ] 真实媒体可听播放通过，重复 open/start/stop/close 无句柄增长、无崩溃

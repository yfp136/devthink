# ShowMaster Windows 构建说明（Phase 1 + Phase 2）

适用版本：《ShowMaster工程开发规格书 V2.0 可落地版》第 4 章仓库结构与构建。
在 Windows 上以 **MSVC 2022 + Ninja** 构建，预设已内置于 `CMakePresets.json`
（`windows-msvc-debug` / `windows-msvc-release`）。

Phase 2 新增：FFmpeg 媒体后端、Headless 服务器模式、Windows Service 开机自启、
7x24 稳定性看门狗。

## 1. 前置环境

| 组件 | 版本要求 | 说明 |
| --- | --- | --- |
| Windows | 10/11 x64 | ARM64 未在 CI 覆盖 |
| Visual Studio | 2022（含"使用 C++ 的桌面开发"工作负载） | 提供 MSVC x64 工具链 |
| CMake | ≥ 3.27 | VS2022 自带或独立安装 |
| Ninja | ≥ 1.11 | VS2022 自带或独立安装 |
| Git | 任意 | 拉取仓库与 vcpkg |
| vcpkg | 最新 | 提供 sqlite3 + ffmpeg |
| Qt 6.7+ | ≥ 6.7（仅桌面端需要） | 构建 `sm_desktop`（P1-1）时安装；见第 7 章 |

## 2. 准备依赖（vcpkg）

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sqlite3:x64-windows
C:\vcpkg\vcpkg install ffmpeg:x64-windows
set VCPKG_ROOT=C:\vcpkg
```

## 3. 构建与测试（开发者本机）

在 **"x64 Native Tools Command Prompt for VS 2022"** 中执行：

```bat
cd showmaster-src
cmake --preset windows-msvc-debug ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Release 同理：把三个命令中的 `windows-msvc-debug` 换成 `windows-msvc-release`。

预期结果：`ctest` 输出 **25/25 通过**。
DoD 规则（规格 4.2）：ctest 全绿方可视为本里程碑完成。

## 4. 产物与目录约定

| 路径 | 内容 |
| --- | --- |
| `build/.../bin/*.dll` | 7 个引擎插件 DLL |
| `build/.../sm_kernel_smoke.exe` | 内核冒烟程序 |
| `build/.../sm_headless.exe` | Headless 服务器模式（工控机常驻，Web 远控） |

## 5. Headless 服务器模式

### 5.1 独立运行（调试用）

```bat
sm_headless.exe --port 8080
```

浏览器访问 `http://127.0.0.1:8080`，默认账号 `admin / admin123`。

### 5.2 注册为 Windows Service（开机自启）

以管理员身份运行：

```bat
:: 安装服务（自动启动 + 崩溃恢复）
sm_headless.exe --install --port 8080

:: 启动服务
sm_headless.exe --start
:: 或：net start ShowMasterSvc

:: 停止服务
sm_headless.exe --stop
:: 或：net stop ShowMasterSvc

:: 查询状态
sm_headless.exe --status

:: 卸载服务
sm_headless.exe --uninstall
```

服务配置：
- **自动启动**：`SERVICE_AUTO_START` + 延迟自动启动（等待网络就绪后 30 秒启动）
- **崩溃恢复**：第1次 5s / 第2次 10s / 第3次+ 30s 自动重启
- **运行账户**：LocalSystem（Session 0，无界面）
- **依赖**：Tcpip（TCP/IP 协议栈就绪后启动）

### 5.3 7x24 稳定性看门狗

Headless 模式内置看门狗，监控：
- **心跳超时**：10 秒无心跳 → 判定卡死 → 自动重建 Kernel
- **内存监控**：RSS > 4GB → 告警
- **线程监控**：线程数 > 256 → 告警
- **死锁检测**：心跳停滞 → 触发引擎重启

### 5.4 远程管控

服务启动后，任意设备（手机/电脑/平板）通过浏览器访问：

```
http://<服务器IP>:8080
```

可完整控制：节目单播放、GO触发、场景切换、灯光控制、视频控制、硬件控制。
支持远程实时预览PGM画面、账号密码登录、权限校验、访问日志。
远程指令独立队列，不影响本地演出毫秒级精度。

## 6. CI（GitHub Actions）

推送/PR 自动在 `windows-latest` 上执行 `.github/workflows/windows-build.yml`，
含两个相互独立的并行 job（各自全新 runner）：

- `build`（原流程）：vcpkg 安装 sqlite3 + ffmpeg → MSVC 环境 → cmake
  （`windows-msvc-debug`）→ ctest → headless 冒烟测试
  （start → `/api/login` 换 token → `/api/status`）→ 上传内核与引擎 DLL 产物。
- `desktop`（2026-09-10 新增，P1-1 冒烟覆盖，对应出口项 E2/E3）：按 §7.1 **方式 A**
  安装 Qt 6.7（aqt 官方预编译二进制，架构 `win64_msvc2019_64`，其默认归档已含
  `Qml`/`Quick`/`QuickControls2`），sqlite3 仍由 vcpkg 提供 → `-DSM_BUILD_DESKTOP=ON`
  + `windows-msvc-release` 配置
  （`-DCMAKE_PREFIX_PATH=<Qt 根目录>`）→ 构建 `sm_desktop`
  → `windeployqt --qmldir src\app\qt\qml` 部署 → `sm_desktop --smoke` 启动自检
  （退出码 0 = QML 装配 + 首轮事件冒烟通过；非 0 = 装配失败或事件循环期崩溃，
  语义见 `src/app/main_qt.cpp`）→ 上传桌面产物（含部署好的 Qt 运行库）。
  > 历史：最初用 §7.1 方式 B（vcpkg 从源码编译 qtbase+qtdeclarative）实现该 job，
  > 但在 `windows-latest`（2 vCPU、磁盘很小）上编译 >2 小时仍把磁盘写满而失败
  > （`There is not enough space on the disk`）。故改用方式 A：数分钟即可装好，
  > 速度快且稳定；job `timeout-minutes` 相应回落到 60。

## 7. Qt 桌面端 sm_desktop（P1-1）构建与部署

桌面端目标 `sm_desktop` 由 `CMakeLists.txt` 的 `SM_BUILD_DESKTOP` 开关控制（默认 OFF），
组件要求：`Qt6 6.7+` 的 `Core Gui Qml Quick QuickControls2`（见 CMake `find_package` 声明）。
代码位于 `src/app/`（入口 `main_qt.cpp`、桥接 `qt/kernel_host.*`、装配 `qt/ui_controller.*`），
QML 资源在 `src/app/qt/qml/`（`qml.qrc`，前缀 `/qml`，AUTORCC 打包）。

### 7.1 安装 Qt（二选一）

方式 A：官方预编译二进制（推荐，免编译、快）

```bat
pip install aqtinstall
aqt install-qt windows desktop 6.7.2 win64_msvc2019_64
```

该架构的默认归档已涵盖 CMake 所需全部组件，无需再 `-m` 指定模块：
`qtbase` 提供 `Qt6::Core/Gui`，`qtdeclarative` 提供 `Qt6::Qml/Quick/QuickControls2`
（Qt 6 中 Qt Quick Controls 2 并入 qtdeclarative），另有 `qtsvg`/`qttools`/`qttranslations`。

> 为什么是 `win64_msvc2019_64` 而不是 `msvc2022`：aqt 对 Qt 6.7.2 只发布
> `win64_msvc2019_64`、`win64_msvc2019_arm64`、`win64_mingw`、`win64_llvm_mingw`，
> 并无 `win64_msvc2022_64`（该架构自 Qt 6.8 起才提供）。MSVC 2019 与 2022
> 二进制兼容（同为 v14x ABI），在 VS 2022 下可直接链接使用——这也是
> `install-qt-action` 对 `5.15 <= Qt < 6.8` 的默认架构。

方式 B：vcpkg（与既有 sqlite3/ffmpeg 管线一致，但需从源码编译 Qt，耗时较长）

```bat
C:\vcpkg\vcpkg install qtbase qtdeclarative qtquickcontrols2:x64-windows
```

### 7.2 配置与构建

在 **"x64 Native Tools Command Prompt for VS 2022"** 中，按第 3 章的方式，另加开关：

```bat
cmake --preset windows-msvc-debug ^
  -DSM_BUILD_DESKTOP=ON ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-msvc-debug --target sm_desktop
```

方式 A（aqt）安装时 Qt 自身不需要 toolchain 文件，改为 `-DCMAKE_PREFIX_PATH=<aqt 安装的 Qt 根目录>`；
但 sqlite3 由 vcpkg 提供，故 `-DCMAKE_TOOLCHAIN_FILE` 这行仍需保留。CI 的 `desktop` job 即此组合。

### 7.3 部署与冒烟

```bat
windeployqt --qmldir src\app\qt\qml build\...\sm_desktop.exe
build\...\sm_desktop.exe
```

- 预期：出现 1600×900 可操作主窗口，六区面板（顶栏/媒体库/预监/检查器/时间线/状态栏）
  与内核经 `kernelBridge` 桥接通信，无媒体时为空态。
- 顶层 QML 资源与面板清单变更时，须同步登记 `src/app/qt/qml/qml.qrc`（AUTORCC 打包依据）。

## 8. 已知边界

- 引擎插件 DLL 为空壳（NoopEngine）；FFmpeg/WASAPI 真实实现已落在宿主静态库的 platform 层
- D3D11 渲染输出未开始（见 `docs/TASKS_REMAINING.md` P1-2）
- macOS / Linux 不构建 DLL：宿主侧由 `test_engine_stub` 做 ABI 冒烟
- FFmpeg 未找到时自动降级为 stub 媒体后端（功能受限但不崩溃）
- Windows Service 功能在非 Windows 平台生成 systemd unit 文件供参考

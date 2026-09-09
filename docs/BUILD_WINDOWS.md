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

预期结果：`ctest` 输出 **15/15 通过**。
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

推送/PR 自动在 `windows-latest` 上执行
`.github/workflows/windows-build.yml`：
安装 sqlite3 + ffmpeg → MSVC 环境 → cmake → ctest → headless 冒烟测试 → 上传产物。

## 7. 已知边界

- 引擎 DLL 为空壳：真正的 WASAPI / D3D11 / FFmpeg 引擎在 Phase 2/3 充实
- macOS / Linux 不构建 DLL：宿主侧由 `test_engine_stub` 做 ABI 冒烟
- FFmpeg 未找到时自动降级为 stub 媒体后端（功能受限但不崩溃）
- Windows Service 功能在非 Windows 平台生成 systemd unit 文件供参考

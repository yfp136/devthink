# ShowMaster Windows 构建说明（Phase 1 内核骨架）

适用版本：《ShowMaster工程开发规格书 V2.0 可落地版》第 4 章仓库结构与构建。
本里程碑（纯逻辑内核 + 引擎空壳 DLL）在 Windows 上以 **MSVC 2022 + Ninja** 构建，
预设已内置于 `CMakePresets.json`（`windows-msvc-debug` / `windows-msvc-release`）。

> 规格中的 Windows 专属能力（WASAPI 音频、D3D11 视频、FFmpeg 解码、Qt 面板）
> 属于 Phase 2/3，本里程碑不构建这些依赖。当前 Windows 交付物 =
> 可运行的内核（`sm_kernel_smoke.exe` + 全部单元测试）+ 7 个空壳引擎 DLL。

## 1. 前置环境

| 组件 | 版本要求 | 说明 |
| --- | --- | --- |
| Windows | 10/11 x64 | ARM64 未在 CI 覆盖 |
| Visual Studio | 2022（含“使用 C++ 的桌面开发”工作负载） | 提供 MSVC x64 工具链 |
| CMake | ≥ 3.27 | VS2022 自带或独立安装 |
| Ninja | ≥ 1.11 | VS2022 自带或独立安装 |
| Git | 任意 | 拉取仓库与 vcpkg |
| vcpkg | 最新 | 提供 sqlite3（macOS 用系统 SDK，Windows 需显式提供） |

## 2. 准备 sqlite3（vcpkg）

内核的 SQLite 依赖在非 Apple 平台先 `find_package(SQLite3)`，找不到时回退
`find_library(sqlite3 REQUIRED)`，因此 Windows 上必须先用 vcpkg 提供 sqlite3：

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sqlite3:x64-windows
set VCPKG_ROOT=C:\vcpkg
```

（CI 使用 GitHub `windows-latest` 自带 vcpkg，无需克隆，见 §5。）

## 3. 构建与测试（开发者本机）

在 **“x64 Native Tools Command Prompt for VS 2022”** 中执行（预设 `architecture: x64`
依赖 MSVC 环境变量）：

```bat
cd showmaster-src
cmake --preset windows-msvc-debug ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Release 同理：把三个命令中的 `windows-msvc-debug` 换成 `windows-msvc-release`。

预期结果：`ctest` 输出 **8/8 通过**（test_envelope / test_op_dict / test_util /
test_msg_bus / test_db / test_project / test_engine_registry / test_engine_stub）。
DoD 规则（规格 4.2）：ctest 全绿方可视为本里程碑完成。

## 4. 产物与目录约定

| 路径 | 内容 |
| --- | --- |
| `build/windows-msvc-debug/bin/*.exe` | `sm_kernel_smoke.exe`（构建类型配置为 Debug 时在对应子目录） |
| `build/windows-msvc-debug/bin/*.dll` | 7 个空壳引擎插件，文件名与注册表一致 |
| `tools/migrations/` | 数据库迁移脚本目录（规格 §6.1 按文件名序执行，含 `schema_v2.sql` 基线后追加 `m00x_*.sql`） |

引擎 DLL 与注册表映射（`engine_registry.cpp`）：

| DLL | 总线 id | 阶段 |
| --- | --- | --- |
| `MediaEngine.dll` | `engine.media` | Phase 1 实现（空壳） |
| `TimelineEngine.dll` | `engine.timeline` | Phase 1 实现（空壳） |
| `VjfxEngine.dll` | `engine.vjfx` | Phase 1 空壳 |
| `LedEngine.dll` | `engine.led` | Phase 1 空壳 |
| `LightEngine.dll` | `engine.light` | Phase 1 空壳 |
| `PixelEngine.dll` | `engine.pixel` | Phase 1 空壳 |
| `DeviceEngine.dll` | `engine.device` | Phase 1 空壳 |

DLL 源码在 `src/engines/plugins/`：`engine_noop.h`（空壳实现）与
`engine_plugin.cpp`（统一 C 工厂，编译期以 `SM_PLUGIN_ID` 标识身份）。
宿主动态加载仅依赖 `engine_api.h` 的 C 入口 `sm_create_engine` / `sm_destroy_engine`。

## 5. CI（GitHub Actions）

推送/PR 自动在 `windows-latest` 上执行
`.github/workflows/windows-build.yml`：安装 sqlite3 → MSVC 环境 →
`cmake --preset windows-msvc-debug` → `ctest`，并上传 7 个 DLL 与内核 exe 为构建产物。
CI 全部通过即等价于 §3 的本机 DoD。

## 6. 已知边界（本里程碑）

- 引擎 DLL 为空壳：`init`/`start` 成功、命令无操作、不产生事件；真正的
  WASAPI / D3D11 / FFmpeg 引擎在 Phase 2 于 `src/engines/plugins/*.cpp` 内充实，
  宿主与 ABI 不变。
- macOS / Linux 不构建 DLL：宿主侧由 `test_engine_stub` 对同一
  `engine_noop.h` 做 ABI 冒烟，保证跨平台行为一致。
- SQLite 版本：开发机与 CI 均为较新 sqlite3（≥ 3.31 以支持 WAL2 无关特性与
  `PRAGMA journal_mode=WAL`），如遇旧版本请升级 vcpkg 包。

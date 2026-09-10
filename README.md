# ShowMaster（showmaster-src）

《ShowMaster工程开发规格书 V2.0 可落地版》Phase 1 **可落地内核骨架**的代码仓库。
目标：把规格中与平台解耦的纯逻辑内核先落地为可构建、可测试、可提交的 C++ 工程，
Windows 专属能力（WASAPI / D3D11 / FFmpeg / Qt）在后续阶段按本骨架扩展。

## 里程碑范围（DoD：`ctest` 全绿）

| 模块 | 目录 | 对应规格 |
| --- | --- | --- |
| 消息信封 / 指令字典 / 错误码 | `src/core/` | §5.1–5.5（33 op / 16 evt / 1xxx–5xxx） |
| 消息总线与心跳监控 | `src/core/msg_bus.*` | §5.2 |
| 工具（SHA-256 / UUID / 时间） | `src/core/util.*` | §8.1 等 |
| SQLite 存储（WAL + 17 表 DDL + 迁移） | `src/db/` | §6.1–6.2 |
| 工程文件校验（manifest + 行集） | `src/project/project_file.*` | §8.2–8.3 |
| 场景状态契约（state_json v1） | `src/project/state_json.*` | §10.4 |
| 引擎 ABI 与注册表（7 引擎） | `src/engines/` | §3.2–3.3 |
| 引擎空壳插件模板（Windows DLL） | `src/engines/plugins/` | §3.3 |

单元测试位于 `tests/`，每个用例一个可执行文件；断言失败打印 `[FAIL]` 并以非 0 退出。

## 构建（macOS / Linux 本机）

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug          # 预期 24/24 全绿
```

## 构建（Windows）

环境与步骤见 [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md)；预设
`windows-msvc-debug` / `windows-msvc-release` 已内置，CI 见
[.github/workflows/windows-build.yml](.github/workflows/windows-build.yml)。

## 目录约定

- `tools/migrations/`：数据库增量迁移脚本（按文件名序执行，见目录内 README）。
- `build/`：本机构建产物（已忽略，不提交）。
- `third_party/nlohmann/json.hpp`：单头 JSON 库（MIT，随仓库分发）。

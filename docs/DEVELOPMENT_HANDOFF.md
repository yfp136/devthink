# ShowMaster 开发状态交接文档

日期：2026-09-08
用途：把 ShowMaster 工程的代码布局、构建方式与未完成主线（Phase 2 平台层 JPEG 编码链）交接给下一个开发任务。文档末尾附一段可直接发给下一任务的原话指令。

## 1. 工程身份与位置

工程名 **ShowMaster**，对应《ShowMaster工程开发规格书 V2.0 可落地版》。当前仓库实现了规格书 Phase 1 纯逻辑内核骨架，并已开始 Phase 2 平台适配层（FFmpeg 媒体后端 / JPEG 编码 / WASAPI 音频 / D3D11 帧捕获）。语言标准 C++20，零第三方运行时依赖（仅随仓库分发 `nlohmann/json.hpp` 单头库，数据库用系统/平台 SQLite3）。

源码根目录（绝对路径）：

```
/Users/yxiao88/Library/Application Support/TRAE SOLO CN/ModularData/ai-agent/work-mode-projects/6a9e6a7e48915cce9293e7ba/showmaster-src
```

同一目录层级另有规格书 docx（`ShowMaster工程开发规格书V2.0_可落地版.docx`），需要查业务语义时以它为最高依据。

## 2. 代码布局

| 构件 | 目录 | 职责 |
| --- | --- | --- |
| `sm_core`（静态库） | `src/core` `src/db` `src/project` | 信封/指令字典/错误码、消息总线与心跳、SHA-256/UUID/时间工具、SQLite 存储与迁移、工程清单、state_json |
| `sm_engines`（静态库） | `src/engines/*` `src/platform` | 引擎注册表 + 九个引擎子目录（timeline/media/scene/playlist/media_lib/vjfx/led/light/pixel/device）+ 平台适配层（`media_backend.*`、`jpeg_codec.h`） |
| `sm_web`（静态库） | `src/web` | HTTP/WebSocket 服务、认证、远程队列、网关、内嵌 UI |
| `sm_remote`（静态库） | `src/remote` | Windows Service 管理、看门狗 |
| `sm_protocols`（静态库） | `src/protocols` | TCP / MIDI / OSC 适配器 |
| `sm_kernel_smoke`（exe） | `src/app/main.cpp` | 内核冒烟程序 |
| `sm_headless`（exe） | `src/app/main_headless.cpp` | Headless 服务器（工控机常驻、Web 远控入口） |
| 单元测试 | `tests/` | 每个用例一个独立可执行文件，断言失败打印 `[FAIL]` 并以非 0 退出 |
| 第三方 | `third_party/nlohmann/json.hpp` | 单头 JSON（MIT） |

Windows 专属：7 个引擎插件 DLL（`MediaEngine.dll`/`TimelineEngine.dll`/`VjfxEngine.dll`/`LedEngine.dll`/`LightEngine.dll`/`PixelEngine.dll`/`DeviceEngine.dll`），由 `src/engines/plugins/engine_plugin.cpp` 模板加 `SM_PLUGIN_ID` 编译期标识生成，输出到 `build/<preset>/bin`。

## 3. 构建与测试

本机（macOS/Linux）开发循环：

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

2026-09-08 实测：本机 `ctest` **21/21 全部通过**（约 0.8s）。

Windows：在 "x64 Native Tools Command Prompt for VS 2022" 中执行 `cmake --preset windows-msvc-debug -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake`，随后 `--build` 与 `ctest` 同 preset；vcpkg 提供 `sqlite3` 与 `ffmpeg`。`find_package(FFMPEG)` 成功时定义 `SM_HAS_FFMPEG=1` 并链接 avcodec/avformat/avutil，平台层走真实实现；找不到 FFmpeg 时自动降级为 stub。

两个文档里的旧文案需注意：`README.md` 写“预期 8/8 全绿”、`docs/BUILD_WINDOWS.md` 写“15/15 通过”，均为早期里程碑残留；当前实际注册并全绿的是 21 个测试。改动代码后若顺手，可把这两处改为“21/21”。

编译纪律（全仓强制）：MSVC 用 `/W4 /permissive-`，其他平台 `-Wall -Wextra`，新代码必须零告警；测试全绿才算完成。

## 4. 平台层现状（重点交接区域）

### 4.1 `media_backend.h`（接口已定稿，勿改）

对外函数：`has_media_backend` / `probe_media` / `open_media_file` / `start_media_playback` / `stop_media_playback` / `get_media_pos_ms` / `get_media_duration_ms` / `close_media_file` / `generate_thumbnail`（返回 JPEG base64）/ `capture_pgm_frame_jpeg`（返回 JPEG base64）/ `init_audio_output` / `shutdown_audio_output`。

### 4.2 `media_backend.cpp`（实现现状）

文件按 `#if !defined(SM_HAS_FFMPEG)` 分成两支：

- stub 分支（L38–62，macOS/Linux 编译）：所有函数为空实现或返回空/`true`，不链接任何外部库。
- FFmpeg 分支（`#else` 起，Windows）：`has_media_backend`/`probe`/`open`/`start`/`stop`/`get_pos`/`get_duration`/`close` 已实现，含 `MediaState` 全局状态、后台解码线程、视频帧/音频采样队列。
- 文件头部（L16–31）已有静态 `base64_encode(const uint8_t*, size_t)` 工具，JPEG 字节流可直接喂给它。
- **未完成点 1** `generate_thumbnail`（L429 起）：已能 seek 到 1 秒、解码一帧并缩放到 320px 宽 RGB24（`sws_scale` 到 `AV_PIX_FMT_RGB24`），随后即放弃——注释写着“简化 JPEG 编码…先返回空字符串”“TODO: 集成 libjpeg 编码”，函数返回空串。RGB 帧数据已就绪，只差调用自研编码器。
- **未完成点 2** `capture_pgm_frame_jpeg`（L524 起）：已取 `vqueue` 最新帧，但同样直接返回空串，注释表明编码器未就绪。
- **未完成点 3** 音频（L541 起）：`init_audio_output` 仅返回 `true`，`shutdown_audio_output` 空函数体；WASAPI 真实输出尚未实现。

### 4.3 `jpeg_codec.h`（接口已定稿，有头无实现）

`src/platform/jpeg_codec.h` 声明唯一公开函数：

```cpp
namespace sm { namespace platform {
std::vector<uint8_t> encode_jpeg_rgb24(const uint8_t* rgb, int width, int height,
                                       int row_stride = -1);
}}
```

头注释约定的语义：自包含、无第三方依赖；RGB24（自顶向下、逐行连续、无填充）→ JFIF baseline JPEG（YCbCr 4:2:0，固定 85 质量表，标准 Annex K.3 Huffman 表）；Windows(FFmpeg 后端) 与 macOS/Linux(stub) 编译同一份源码；纯确定性整数数学，可 headless 单测；输入非法（`rgb` 为空、宽高非法或超限）返回空 `vector`；`row_stride` 默认 `width*3`。

**当前 `src/platform/` 下只有 `jpeg_codec.h`，没有 `jpeg_codec.cpp`——整个编码器实现尚未开始。**

### 4.4 构建接入方式

顶层 `CMakeLists.txt` 中 `sm_engines` 通过 `file(GLOB ... CONFIGURE_DEPENDS)` 收集 `${SM_SRC_DIR}/platform/*.cpp`，因此新建 `src/platform/jpeg_codec.cpp` 后无需改动 CMake 即可自动编入 `sm_engines`。新增测试则必须在 `tests/CMakeLists.txt` 里调用一次 `sm_add_unit_test(name libs)`（可参考 `sm_add_unit_test(test_media sm_engines)` 的写法）。

## 5. 待完成主线：JPEG 编码链

这是当前交接的核心工作，按依赖顺序排列：

| 任务 | 落点 | 内容 |
| --- | --- | --- |
| JPEG 编码器实现 | `src/platform/jpeg_codec.cpp`（新建） | 按 `jpeg_codec.h` 语义实现 `encode_jpeg_rgb24`，见第 6 节硬性约束 |
| 缩略图接线 | `media_backend.cpp` FFmpeg 分支 `generate_thumbnail`（L505 附近） | 把已缩放的 RGB24 帧交给 `encode_jpeg_rgb24`，结果再经 `base64_encode` 返回；替换掉“返回空字符串”占位 |
| PGM 帧捕获接线 | `media_backend.cpp` FFmpeg 分支 `capture_pgm_frame_jpeg`（L531 附近） | 取 `vqueue` 最新帧 → 编码 → base64 返回；替换空串占位 |
| 单元测试 | `tests/test_jpeg_codec.cpp`（新建）+ `tests/CMakeLists.txt` 注册 | 见第 7 节 |
| WASAPI 音频 | `media_backend.cpp` 音频两函数 | 默认设备初始化、渲染线程、增益/淡变，接入 play/stop/close（与 JPEG 链无耦合，可后续单开任务） |
| 全量验证 | 本机构建 + `ctest` + ffmpeg | 见第 7 节 |

缩略图侧与编码器解耦的接线要点：`generate_thumbnail` 拿到的是 stride 可能带对齐的 RGB 缓冲，若行字节数与 `width*3` 不一致，必须把 `av_image_fill_arrays` 产生的 `linesize[0]` 作为 `row_stride` 传给编码器；`capture_pgm_frame_jpeg` 取出的帧同样按 `rgb_w`/`rgb_h` 与对应 stride 处理。

## 6. JPEG 编码器硬性约束

- 单文件自包含，不引入第三方库、不依赖 FFmpeg；Windows 与 macOS/Linux 编译同一份源码。
- 输出 JFIF baseline JPEG：YCbCr 4:2:0、质量 85。量化表按 libjpeg 规则用整数比例缩放（`200 - 2*quality = 30`），Huffman 表用标准 Annex K（K.3）亮度/色度 DC、AC 共四张表；此前已抓取 libjpeg `jcparam.c` / `jstdhuff.c` 原始表作为权威比对源，实现时可直接以这两份表为准。
- 编码结果必须确定：同一输入字节序列在任意平台产生完全一致的输出，以便测试做字节级断言。
- 参数校验严格：`rgb == nullptr`、`width/height <= 0`、尺寸超限（防止整数溢出，如宽高乘积上限）时返回空 `vector`，不得崩溃。
- 头文件 `jpeg_codec.h` 的接口签名已定稿，不要改。

## 7. 验证标准（DoD）

1. 本机 `cmake --build --preset debug` 零告警通过。
2. 新增 `test_jpeg_codec`，至少断言：输出以 `FF D8` 开头、以 `FF D9` 结尾；含 `SOF0`（baseline）标记；指定宽高的图像输出长度合理且可解析；同输入两次编码字节一致；非法输入（空指针、0 尺寸）返回空；`row_stride` 带填充时结果正确。
3. `ctest --preset debug` 保持全绿（当前 21 个 + 新增）。
4. 用系统 `ffmpeg` 对编码输出实测可解码（`ffmpeg -f image2 -i out.jpg -vframes 1 -f null -` 无报错即视为通过）。

## 8. 可直接发给下一任务的原话指令

> 继续完成 ShowMaster 的 Phase 2 平台层 JPEG 编码链，代码在 `/Users/yxiao88/Library/Application Support/TRAE SOLO CN/ModularData/ai-agent/work-mode-projects/6a9e6a7e48915cce9293e7ba/showmaster-src`。
> 现状：`src/platform/jpeg_codec.h` 接口已定稿但 `jpeg_codec.cpp` 未实现；`media_backend.cpp` 的 FFmpeg 分支里 `generate_thumbnail` 已解码并缩放到 320px 宽 RGB24、`capture_pgm_frame_jpeg` 已取出最新帧，两处都因没有编码器而返回空字符串，且文件内已有 `base64_encode` 静态工具可直接复用。
> 请按 `docs/DEVELOPMENT_HANDOFF.md` 第 5–7 节完成：新建 `src/platform/jpeg_codec.cpp` 实现 `sm::platform::encode_jpeg_rgb24`（自包含 JFIF baseline、YCbCr 4:2:0、质量 85、确定性输出、非法输入返回空 vector），把两处占位接线为“编码 + base64 返回”，新增 `tests/test_jpeg_codec.cpp` 并在 `tests/CMakeLists.txt` 注册（测试关注 SOI/EOI/SOF0 标记、确定性、row_stride、非法输入）。
> 收尾验证：`cmake --build --preset debug` 零告警、`ctest --preset debug` 全绿（保持原有 21 个不回归）、用系统 ffmpeg 实测编码输出可解码。平台层音频 WASAPI 与 JPEG 链无耦合，本轮不必处理。不要改动已定稿的头文件接口。

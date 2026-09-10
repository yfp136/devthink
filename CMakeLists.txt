# ============================================================================
# ShowMaster 顶层构建
# 依据《ShowMaster工程开发规格书 V2.0 可落地版》第 4 章（仓库结构与构建）
# 本里程碑 = 仓库骨架 + 纯逻辑内核（消息总线 / 指令字典 / 错误码 / 数据库 /
#            .showproj 清单与哈希 / state_json / 六引擎壳）
# Windows 专属组件（WASAPI / D3D11 / FFmpeg / Qt）在 Windows 机器或 CI 上构建，
# 见 docs/BUILD_WINDOWS.md 与 .github/workflows/windows-build.yml
# ============================================================================
cmake_minimum_required(VERSION 3.27)

project(ShowMaster
  VERSION 0.1.0
  DESCRIPTION "ShowMaster 全域智能舞美管控平台（Phase 1 内核骨架）"
  LANGUAGES CXX)

# ---- 全局编译标准：C++20（规格 2.1）------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

option(SM_BUILD_TESTS "构建单元测试（DoD 前提：ctest 全绿）" ON)
option(SM_BUILD_APP  "构建内核冒烟程序 sm_kernel_smoke" ON)
option(SM_BUILD_HEADLESS "构建 headless 服务器 sm_headless（Web 远控）" ON)
option(SM_BUILD_DESKTOP "构建 Qt 桌面端 sm_desktop（P1-1，需 Qt 6.7+）" OFF)

# ---- 目录定义 -------------------------------------------------------------
set(SM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SM_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---- 编译器警告 -----------------------------------------------------------
if(MSVC)
  # /utf-8：本仓库源码一律 UTF-8（注释与日志字面量大量含中文）。MSVC 默认按
  # 系统 ANSI 代码页解析源文件，会把中文字面量编译成乱码（运行期日志出现
  # "??????"），且 UTF-8 多字节序列中若出现映射为反斜杠的字节还会误触发续行、
  # 吞掉下一行代码。/utf-8 等价于 /source-charset:utf-8 /execution-charset:utf-8。
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ============================================================================
# sm_core：纯逻辑内核静态库（core + db + project）
# ============================================================================
file(GLOB SM_CORE_SOURCES CONFIGURE_DEPENDS
  "${SM_SRC_DIR}/core/*.cpp"
  "${SM_SRC_DIR}/db/*.cpp"
  "${SM_SRC_DIR}/project/*.cpp")

add_library(sm_core STATIC ${SM_CORE_SOURCES})
target_include_directories(sm_core PUBLIC
  "${SM_SRC_DIR}"
  "${SM_THIRD_PARTY_DIR}")
# 运行时定位 schema 与迁移目录（第 6.1 节：tools/migrations 按序执行）
target_compile_definitions(sm_core PUBLIC
  "SM_SCHEMA_SQL=\"${SM_SRC_DIR}/db/schema_v2.sql\""
  "SM_MIGRATIONS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tools/migrations\"")

# SQLite3：macOS 用系统 SDK；Windows 优先 find_package（vcpkg 提供）
if(APPLE)
  find_library(SM_SQLITE_LIB sqlite3 REQUIRED)
  add_library(sm_sqlite3 UNKNOWN IMPORTED)
  set_target_properties(sm_sqlite3 PROPERTIES IMPORTED_LOCATION "${SM_SQLITE_LIB}")
else()
  find_package(SQLite3 QUIET)
  if(SQLite3_FOUND)
    add_library(sm_sqlite3 ALIAS SQLite::SQLite3)
  else()
    find_library(SM_SQLITE_LIB sqlite3 REQUIRED)
    add_library(sm_sqlite3 UNKNOWN IMPORTED)
    set_target_properties(sm_sqlite3 PROPERTIES IMPORTED_LOCATION "${SM_SQLITE_LIB}")
  endif()
endif()
target_link_libraries(sm_core PUBLIC sm_sqlite3)

# ============================================================================
# sm_engines：内核引擎静态库
# Phase 1：timeline/media/scene/playlist/media_lib + 平台后端（含 FFmpeg 条件）
# Phase 2：追加 vjfx/led/light/pixel/device 真实引擎（无 UI，headless 可测）
# ============================================================================
file(GLOB SM_ENGINE_SOURCES CONFIGURE_DEPENDS "${SM_SRC_DIR}/engines/*.cpp")
file(GLOB SM_ENGINE_SUB_SOURCES CONFIGURE_DEPENDS
  "${SM_SRC_DIR}/engines/timeline/*.cpp"
  "${SM_SRC_DIR}/engines/media/*.cpp"
  "${SM_SRC_DIR}/engines/scene/*.cpp"
  "${SM_SRC_DIR}/engines/playlist/*.cpp"
  "${SM_SRC_DIR}/engines/media_lib/*.cpp"
  "${SM_SRC_DIR}/engines/vjfx/*.cpp"
  "${SM_SRC_DIR}/engines/led/*.cpp"
  "${SM_SRC_DIR}/engines/light/*.cpp"
  "${SM_SRC_DIR}/engines/pixel/*.cpp"
  "${SM_SRC_DIR}/engines/device/*.cpp"
  "${SM_SRC_DIR}/platform/*.cpp"
  "${SM_SRC_DIR}/platform/rhi/*.cpp")
list(APPEND SM_ENGINE_SOURCES ${SM_ENGINE_SUB_SOURCES})
add_library(sm_engines STATIC ${SM_ENGINE_SOURCES})
target_link_libraries(sm_engines PUBLIC sm_core)

# ---- D3D11 RHI 后端条件编译（仅 Windows，规格 2.3 / P1-2）----
# sm_engines 为静态库：d3d11/dxgi/d3dcompiler 为系统库，须 PUBLIC 传递到
# 最终可执行链接行；SM_HAS_D3D11 只对库内 TU（rhi.cpp 工厂开关 +
# rhi_d3d11.cpp 真实现）有意义 → PRIVATE（与 SM_HAS_FFMPEG 同模式）。
# Windows SDK 默认自带 d3d11.lib/dxgi.lib/d3dcompiler.lib，无需 find_package。
if(WIN32)
  target_link_libraries(sm_engines PUBLIC d3d11 dxgi d3dcompiler)
  target_compile_definitions(sm_engines PRIVATE SM_HAS_D3D11=1)
endif()

# ---- Phase 2: FFmpeg 条件链接（仅 Windows + vcpkg 可用时）----
if(WIN32)
  find_package(FFMPEG QUIET
    COMPONENTS avcodec avformat avutil swresample swscale)
  if(FFMPEG_FOUND)
    # PUBLIC：sm_engines 为静态库，FFmpeg 库/头文件须传递到最终
    # 可执行（sm_headless）与插件 DLL 的链接行与编译环境；
    # SM_HAS_FFMPEG 仅对库内 TU（media_backend.cpp）有意义 → PRIVATE。
    # ole32：COM/WASAPI（CoInitializeEx/CoCreateInstance）。
    # uuid：通用 COM GUID（IID_IUnknown 等）。注意：WASAPI 的
    # CLSID_MMDeviceEnumerator/IID_IAudioClient 不在 uuid.lib，也不响应
    # INITGUID——media_backend.cpp 统一用 __uuidof() 编译期求值，勿改回
    # 符号引用，否则链接报 LNK2001。
    target_link_libraries(sm_engines PUBLIC
      ${FFMPEG_LIBRARIES} ole32 uuid)
    target_include_directories(sm_engines PUBLIC ${FFMPEG_INCLUDE_DIRS})
    target_compile_definitions(sm_engines PRIVATE SM_HAS_FFMPEG=1)
    message(STATUS "FFmpeg found — enabling real media backend")
  else()
    message(STATUS "FFmpeg not found — using stub media backend")
  endif()
endif()

# ---- Windows 引擎插件 DLL（规格 §3.3，仅 WIN32 构建）----------------------
# 每个引擎一个独立 DLL，均实例化同一空壳模板（engine_plugin.cpp），
# 编译期以 SM_PLUGIN_ID 标识身份。输出文件名与 engine_registry() 的 dll 列
# 完全一致，宿主据此动态加载；非 Windows 平台用 test_engine_stub 校验 ABI。
function(sm_add_engine_plugin target dll_name plugin_id)
  add_library(${target} SHARED "${SM_SRC_DIR}/engines/plugins/engine_plugin.cpp")
  target_compile_definitions(${target} PRIVATE
    SM_ENGINE_BUILD_DLL=1
    SM_PLUGIN_ID="${plugin_id}")
  target_include_directories(${target} PRIVATE
    "${SM_SRC_DIR}"
    "${SM_THIRD_PARTY_DIR}")
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${dll_name}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()

if(WIN32)
  sm_add_engine_plugin(sm_engine_media     MediaEngine.dll     "engine.media")
  sm_add_engine_plugin(sm_engine_timeline  TimelineEngine.dll  "engine.timeline")
  sm_add_engine_plugin(sm_engine_vjfx      VjfxEngine.dll      "engine.vjfx")
  sm_add_engine_plugin(sm_engine_led       LedEngine.dll       "engine.led")
  sm_add_engine_plugin(sm_engine_light     LightEngine.dll     "engine.light")
  sm_add_engine_plugin(sm_engine_pixel     PixelEngine.dll     "engine.pixel")
  sm_add_engine_plugin(sm_engine_device    DeviceEngine.dll    "engine.device")
endif()

# ============================================================================
# sm_kernel_smoke：内核冒烟程序（app/main.cpp）
# ============================================================================
if(SM_BUILD_APP)
  add_executable(sm_kernel_smoke "${SM_SRC_DIR}/app/main.cpp")
  target_link_libraries(sm_kernel_smoke PRIVATE sm_engines)
endif()

# ============================================================================
# sm_web：Web 远控层（HTTP/WebSocket Server + Auth + RemoteQueue + Gateway）
# ============================================================================
file(GLOB SM_WEB_SOURCES CONFIGURE_DEPENDS "${SM_SRC_DIR}/web/*.cpp")
add_library(sm_web STATIC ${SM_WEB_SOURCES})
target_link_libraries(sm_web PUBLIC sm_core sm_engines)
target_include_directories(sm_web PUBLIC "${SM_SRC_DIR}" "${SM_THIRD_PARTY_DIR}")

# 平台 socket 库：Windows 需 ws2_32；macOS/Linux 无需额外库
if(WIN32)
  target_link_libraries(sm_web PUBLIC ws2_32)
endif()

# ============================================================================
# sm_remote：远程管控/服务管理（Windows Service + Watchdog + Auto-start）
# ============================================================================
file(GLOB SM_REMOTE_SOURCES CONFIGURE_DEPENDS "${SM_SRC_DIR}/remote/*.cpp")
add_library(sm_remote STATIC ${SM_REMOTE_SOURCES})
target_link_libraries(sm_remote PUBLIC sm_core)
target_include_directories(sm_remote PUBLIC "${SM_SRC_DIR}" "${SM_THIRD_PARTY_DIR}")

# Windows 服务管理 + 看门狗需要 advapi32（SCM）和 psapi（内存信息）
if(WIN32)
  target_link_libraries(sm_remote PUBLIC advapi32 psapi)
endif()

# ============================================================================
# sm_protocols：硬件协议适配器（TCP / MIDI / OSC）
# ============================================================================
file(GLOB SM_PROTO_SOURCES CONFIGURE_DEPENDS "${SM_SRC_DIR}/protocols/*.cpp")
add_library(sm_protocols STATIC ${SM_PROTO_SOURCES})
target_link_libraries(sm_protocols PUBLIC sm_web sm_core)
target_include_directories(sm_protocols PUBLIC
  "${SM_SRC_DIR}" "${SM_THIRD_PARTY_DIR}")
# Windows: MIDI 需要 winmm；所有协议需要 ws2_32（已通过 sm_web 传递）
if(WIN32)
  target_link_libraries(sm_protocols PUBLIC winmm)
endif()

# ============================================================================
# sm_headless：Headless 服务器模式（工控机常驻，Web 远控入口）
# ============================================================================
if(SM_BUILD_HEADLESS)
  add_executable(sm_headless "${SM_SRC_DIR}/app/main_headless.cpp")
  target_link_libraries(sm_headless PRIVATE
    sm_web sm_core sm_engines sm_remote sm_protocols)
  if(WIN32)
    target_link_libraries(sm_headless PRIVATE ws2_32)
  endif()
endif()

# ============================================================================
# sm_desktop：Qt 桌面端（P1-1，目标平台 Windows）
# 规格 §2.1 技术栈：Qt 6.7+ / QML / Qt Quick Controls 2 / 自动 moc/rcc。
# 线程模型：Kernel 与 MsgBus 运行在专用 worker 线程（MsgBus 非线程安全），
#           Qt GUI 线程仅经队列信号收发 JSON 状态/事件/帧（见 kernel_host.h）。
# 默认 OFF：本机无 Qt 时不影响 sm_kernel_smoke / sm_headless / ctest 构建。
# Windows 实机构建与 windeployqt 部署步骤见 docs/BUILD_WINDOWS.md。
# ============================================================================
if(SM_BUILD_DESKTOP)
  find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

  add_executable(sm_desktop
    "${SM_SRC_DIR}/app/main_qt.cpp"
    "${SM_SRC_DIR}/app/qt/kernel_host.cpp"
    "${SM_SRC_DIR}/app/qt/kernel_host.h"
    "${SM_SRC_DIR}/app/qt/ui_controller.cpp"
    "${SM_SRC_DIR}/app/qt/ui_controller.h"
    "${SM_SRC_DIR}/app/qt/qml/qml.qrc")
  target_link_libraries(sm_desktop PRIVATE
    sm_core sm_engines
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  target_include_directories(sm_desktop PRIVATE
    "${SM_SRC_DIR}" "${SM_THIRD_PARTY_DIR}")
  set_target_properties(sm_desktop PROPERTIES
    AUTOMOC ON
    AUTORCC ON)
  if(WIN32)
    # 无控制台子系统；配合 qt_finalize_executable 生成 windeployqt 部署规则
    set_target_properties(sm_desktop PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
  qt_finalize_executable(sm_desktop)
endif()

# ============================================================================
# 测试
# ============================================================================
if(SM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- 构建完成提示 ----------------------------------------------------------
message(STATUS "ShowMaster 内核骨架配置完成：C++${CMAKE_CXX_STANDARD}，构建类型=${CMAKE_BUILD_TYPE}")

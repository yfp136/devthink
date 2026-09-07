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

# ---- 目录定义 -------------------------------------------------------------
set(SM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SM_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---- 编译器警告 -----------------------------------------------------------
if(MSVC)
  add_compile_options(/W4 /permissive-)
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
# sm_engines：六引擎骨架（Phase 1 空壳 + registry）
# ============================================================================
file(GLOB SM_ENGINE_SOURCES CONFIGURE_DEPENDS "${SM_SRC_DIR}/engines/*.cpp")
file(GLOB SM_ENGINE_SUB_SOURCES CONFIGURE_DEPENDS "${SM_SRC_DIR}/engines/timeline/*.cpp" "${SM_SRC_DIR}/engines/media/*.cpp" "${SM_SRC_DIR}/engines/scene/*.cpp")
list(APPEND SM_ENGINE_SOURCES ${SM_ENGINE_SUB_SOURCES})
add_library(sm_engines STATIC ${SM_ENGINE_SOURCES})
target_link_libraries(sm_engines PUBLIC sm_core)

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
# sm_headless：Headless 服务器模式（工控机常驻，Web 远控入口）
# ============================================================================
if(SM_BUILD_HEADLESS)
  add_executable(sm_headless "${SM_SRC_DIR}/app/main_headless.cpp")
  target_link_libraries(sm_headless PRIVATE sm_web)
  if(WIN32)
    target_link_libraries(sm_headless PRIVATE ws2_32)
  endif()
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

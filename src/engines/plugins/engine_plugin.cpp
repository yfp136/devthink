// Windows 引擎插件 DLL 统一工厂（规格 §3.3：每个引擎独立 DLL + C 入口）
// 同一模板实例化注册表全部 7 个 DLL：编译期以宏指定插件身份，CMake 目标见
// 顶层 CMakeLists.txt 的 WIN32 分支（sm_add_engine_plugin）。DLL 文件名与
// engine_registry() 的 dll 列一致，宿主按注册表名字动态加载。
//
// 用法（由 CMake 提供）：
//   -DSM_ENGINE_BUILD_DLL=1              导出宏（engine_api.h）
//   -DSM_PLUGIN_ID="engine.media"        本 DLL 的总线地址（id 权威取值）
//
// 本文件仅 7 个 DLL 在 Windows/CI 编译；macOS 宿主侧用 test_engine_stub
// 对 engine_noop.h 做同等 ABI 校验。
#include "engines/api/engine_api.h"
#include "engines/plugins/engine_noop.h"

#ifndef SM_PLUGIN_ID
#error "engine_plugin.cpp 必须以 -DSM_PLUGIN_ID=\"engine.xxx\" 编译（见 CMakeLists WIN32 分支）"
#endif

extern "C" {

SM_ENGINE_API IEngine* sm_create_engine(void) {
  return new sm::stub::NoopEngine(SM_PLUGIN_ID);
}

SM_ENGINE_API void sm_destroy_engine(IEngine* engine) { delete engine; }

}  // extern "C"

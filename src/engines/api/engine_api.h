// 引擎 ABI（规格 §3.3，宿主与引擎 DLL 的唯一二进制契约）
// 引擎内部实现必须无 UI 依赖、提供 headless 模式以便单元测试。
// 本里程碑为纯逻辑内核：仅在宿主侧编译校验本头 + 引擎注册表；
// 真正的插件 DLL 构建在 Windows/CI 上进行（见 docs/BUILD_WINDOWS.md）。
#pragma once

// ---- 导出宏：Windows DLL ↔ 非 Windows 默认可见性 ----
#if defined(_WIN32)
#  if defined(SM_ENGINE_BUILD_DLL)
#    define SM_ENGINE_API __declspec(dllexport)
#  else
#    define SM_ENGINE_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define SM_ENGINE_API __attribute__((visibility("default")))
#else
#  define SM_ENGINE_API
#endif

// 引擎启动配置（字段语义见规格 §3.3）
struct EngineConfig {
  const char* engine_dir = nullptr;      // 引擎资源目录
  const char* db_path = nullptr;         // SQLite 库文件路径
  const char* log_dir = nullptr;         // 日志目录
  const char* msgbus_connect = nullptr;  // 总线接入点
  void* platform = nullptr;              // 平台句柄（预留）
};

// 引擎向总线发事件/响应的回灌通道（实现须线程安全，由宿主提供）
class IMsgSink {
 public:
  virtual ~IMsgSink() = default;
  virtual void post(const char* json_envelope) = 0;
};

// 引擎接口（所有引擎 DLL 导出的统一形态）
class IEngine {
 public:
  virtual ~IEngine() = default;
  virtual const char* id() const = 0;                          // 例：engine.media
  virtual bool init(const EngineConfig& config) = 0;
  virtual bool start() = 0;
  virtual void stop() = 0;
  virtual void handleCommand(const char* json_envelope) = 0;   // 总线异步投递
  virtual void setSink(IMsgSink* sink) = 0;                    // 引擎向总线发事件
};

extern "C" {
// 插件统一入口：返回新引擎实例（失败返回 nullptr），调用方持有所有权
SM_ENGINE_API IEngine* sm_create_engine(void);
// 释放由 sm_create_engine 创建的实例
SM_ENGINE_API void sm_destroy_engine(IEngine* engine);
}

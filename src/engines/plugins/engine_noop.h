// 引擎 Phase 1 最小实现（空壳）模板（规格 §3.2/§3.3）
// 由两类使用者共享：
//   * src/engines/plugins/engine_plugin.cpp —— Windows 插件 DLL 工厂；
//   * tests/test_engine_stub.cpp —— 本机 ABI 冒烟（host 侧编译校验 IEngine 头）。
// 行为对齐 engine_registry.h 的说明：init 成功、无操作；Phase 1 空壳在
// handleCommand 不做业务处理（仅总线路由与心跳已由内核完成），stop 幂等；
// sink 仅保留供后续阶段上报事件，本空壳不主动产生事件。
#pragma once

#include <string>

#include "engines/api/engine_api.h"

namespace sm {
namespace stub {

class NoopEngine final : public IEngine {
 public:
  // plugin_id 即总线地址权威取值（§5.1），例 "engine.media"
  explicit NoopEngine(const char* plugin_id) : id_(plugin_id ? plugin_id : "") {}

  const char* id() const override { return id_.c_str(); }

  bool init(const EngineConfig& /*config*/) override { return true; }

  bool start() override { return true; }

  void stop() override {}  // 空壳无资源可释放；幂等

  // Phase 1 空壳：不执行业务，仅保证总线投递安全（含空指针防御）。
  void handleCommand(const char* /*json_envelope*/) override {}

  void setSink(IMsgSink* sink) override { sink_ = sink; }

 private:
  std::string id_;
  IMsgSink* sink_ = nullptr;
};

}  // namespace stub
}  // namespace sm

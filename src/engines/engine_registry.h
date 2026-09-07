// 引擎注册表（规格 §3.2）
// Phase 1 即建立全部引擎骨架与注册机制：已启用引擎提供最小实现，
// 未启用引擎为空壳（init 成功、无操作、事件照常上报），
// 后续阶段逐个充实而不改变宿主代码。
// id 即总线地址（信封 dst/src 中 engine.xxx 的权威取值，§5.1）。
#pragma once

#include <string>
#include <vector>

namespace sm {

struct EngineDescriptor {
  const char* id;      // 总线地址，例 engine.media
  const char* dll;     // Windows 插件文件名，例 MediaEngine.dll
  const char* module;  // 模块代号 / 中文名
  const char* phase;   // 落地阶段（规格 §3.2 表格列）
};

// 注册表全集（顺序与规格 §3.2 表格一致）
const std::vector<EngineDescriptor>& engine_registry();

// id 是否在注册表中（用于信封 dst 校验与心跳监控登记）
bool is_known_engine(const std::string& id);

}  // namespace sm

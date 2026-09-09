// 引擎注册表实现（规格 §3.2）
// 同时引入 engine_api.h 以在宿主侧编译校验 ABI 头。
#include "engines/engine_registry.h"

#include <algorithm>

#include "engines/api/engine_api.h"

namespace sm {

// 规格 §3.2 引擎注册表：7 项（Phase 1 内核 2 项 + Phase 2 真实引擎 5 项）
const std::vector<EngineDescriptor>& engine_registry() {
  static const std::vector<EngineDescriptor> kEngines = {
      {"engine.media",    "MediaEngine.dll",    "M2 MediaEngine",  "Phase 1 实现"},
      {"engine.timeline", "TimelineEngine.dll", "M5 时间线引擎",    "Phase 1 实现"},
      {"engine.vjfx",     "VjfxEngine.dll",     "VJ 特效引擎",      "Phase 2 实现"},
      {"engine.led",      "LedEngine.dll",      "LED 播控引擎",     "Phase 2 实现"},
      {"engine.light",    "LightEngine.dll",    "灯光引擎",         "Phase 2 实现"},
      {"engine.pixel",    "PixelEngine.dll",    "像素灯带引擎",     "Phase 2 实现"},
      {"engine.device",   "DeviceEngine.dll",   "硬件中控引擎",     "Phase 2 实现"},
  };
  return kEngines;
}

bool is_known_engine(const std::string& id) {
  const auto& all = engine_registry();
  return std::any_of(all.begin(), all.end(),
                     [&](const EngineDescriptor& d) { return id == d.id; });
}

}  // namespace sm

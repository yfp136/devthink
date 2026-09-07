// 引擎注册表单元测试（规格 §3.2 / §3.3；链接 sm_engines 以编译校验 ABI 头）
#include <string>

#include "engines/api/engine_api.h"
#include "engines/engine_registry.h"
#include "test_common.h"

namespace {

void test_registry_contents() {
  const auto& reg = sm::engine_registry();
  SM_CHECK_EQ(reg.size(), std::size_t(7));  // §3.2 表格：2 实现 + 5 空壳

  bool has_media = false, has_timeline = false, has_device = false;
  for (const auto& d : reg) {
    if (std::string(d.id) == "engine.media") has_media = true;
    if (std::string(d.id) == "engine.timeline") has_timeline = true;
    if (std::string(d.id) == "engine.device") has_device = true;
    SM_CHECK(d.id != nullptr && d.dll != nullptr && d.phase != nullptr);
    SM_CHECK(std::string(d.dll).size() > 4);
  }
  SM_CHECK(has_media && has_timeline && has_device);
}

void test_known_engine() {
  SM_CHECK(sm::is_known_engine("engine.media"));
  SM_CHECK(sm::is_known_engine("engine.timeline"));
  SM_CHECK(sm::is_known_engine("engine.vjfx"));
  SM_CHECK(sm::is_known_engine("engine.light"));
  SM_CHECK(!sm::is_known_engine("engine.ghost"));
  SM_CHECK(!sm::is_known_engine(""));
}

void test_abi_header_compiles() {
  // 编译期校验 §3.3 ABI 头可用（不实例化引擎）
  SM_CHECK(sizeof(IEngine) > 0);
  SM_CHECK(sizeof(EngineConfig) > 0);
  SM_CHECK(sizeof(IMsgSink) > 0);
}

}  // namespace

int main() {
  test_registry_contents();
  test_known_engine();
  test_abi_header_compiles();
  return smtest::finish("test_engine_registry");
}

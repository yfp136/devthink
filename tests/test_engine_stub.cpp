// 引擎空壳 ABI 冒烟测试（规格 §3.2/§3.3）
// 宿主侧直接实例化 NoopEngine，验证 IEngine 头在本机可编译、生命周期与
// 总线投递语义安全。真正的插件 DLL 工厂（engine_plugin.cpp）由 Windows
// 构建/CI 编译（见 docs/BUILD_WINDOWS.md）。
#include <cstdio>
#include <string>

#include "engines/plugins/engine_noop.h"
#include "test_common.h"

namespace {

// 计数 sink：验证空壳在 Phase 1 不主动向总线回灌事件
// 注意：IMsgSink/IEngine 由 engine_api.h 声明于全局命名空间（C 兼容 ABI）。
class CountingSink final : public IMsgSink {
 public:
  void post(const char* /*json_envelope*/) override { ++posts_; }
  int posts() const { return posts_; }

 private:
  int posts_ = 0;
};

void test_lifecycle() {
  sm::stub::NoopEngine media("engine.media");
  SM_CHECK_EQ(std::string(media.id()), std::string("engine.media"));

  EngineConfig cfg;  // 全 nullptr：空壳不应触碰配置内容
  SM_CHECK(media.init(cfg));
  SM_CHECK(media.start());
  media.stop();   // 空操作
  media.stop();   // 幂等
  SM_CHECK_EQ(std::string(media.id()), std::string("engine.media"));
}

void test_identity_other_engine() {
  // 同一模板不同 id（registry 内 7 项都由该模板实例化）
  sm::stub::NoopEngine led("engine.led");
  SM_CHECK_EQ(std::string(led.id()), std::string("engine.led"));

  sm::stub::NoopEngine media("engine.media");
  SM_CHECK(std::string(media.id()) != std::string("engine.led"));
}

void test_command_is_noop_safe() {
  CountingSink sink;
  sm::stub::NoopEngine dev("engine.device");
  dev.setSink(&sink);

  // Phase 1 空壳：命令不执行业务、不产生事件
  dev.handleCommand(nullptr);
  dev.handleCommand("{\"op\":\"media.preview\"}");
  dev.handleCommand("");
  SM_CHECK_EQ(sink.posts(), 0);
}

void test_set_sink_null_safe() {
  sm::stub::NoopEngine light("engine.light");
  light.setSink(nullptr);  // 允许解绑
  light.handleCommand("{}");
  SM_CHECK(true);
}

}  // namespace

int main() {
  test_lifecycle();
  test_identity_other_engine();
  test_command_is_noop_safe();
  test_set_sink_null_safe();
  return smtest::finish("test_engine_stub");
}

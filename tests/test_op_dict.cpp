// 指令/事件字典单元测试（规格 §5.3 / §5.4）
#include <map>
#include <string>

#include "core/op_dict.h"
#include "test_common.h"

namespace {

using sm::cmd_ops;
using sm::evt_ops;

void test_counts() {
  SM_CHECK_EQ(cmd_ops().size(), std::size_t(33));  // Phase 1 全集
  SM_CHECK_EQ(evt_ops().size(), std::size_t(17));
}

void test_namespace_grouping() {
  std::map<std::string, int> hist;
  for (const auto& e : cmd_ops()) {
    ++hist[e.ns];
    const std::string prefix = std::string(e.ns) + ".";
    SM_CHECK(std::string(e.op).compare(0, prefix.size(), prefix) == 0);
    SM_CHECK(sm::op_namespace(e.op) != nullptr);
  }
  SM_CHECK_EQ(hist["sys"], 4);
  SM_CHECK_EQ(hist["media"], 7);
  SM_CHECK_EQ(hist["transport"], 5);
  SM_CHECK_EQ(hist["timeline"], 5);
  SM_CHECK_EQ(hist["scene"], 4);
  SM_CHECK_EQ(hist["playlist"], 7);
  SM_CHECK_EQ(hist["remote"], 1);
}

void test_lookup() {
  SM_CHECK(sm::is_cmd_op("sys.ping"));
  SM_CHECK(sm::is_cmd_op("media.preview"));
  SM_CHECK(sm::is_cmd_op("timeline.load"));
  SM_CHECK(sm::is_cmd_op("remote.hello"));
  SM_CHECK(!sm::is_cmd_op("evt.engine.up"));
  SM_CHECK(!sm::is_cmd_op("no.such.op"));

  SM_CHECK(sm::is_evt_op("evt.state.snapshot"));
  SM_CHECK(sm::is_evt_op("evt.transport.clock"));
  SM_CHECK(sm::is_evt_op("evt.engine.down"));
  SM_CHECK(!sm::is_evt_op("media.preview"));

  SM_CHECK(sm::is_known_op("sys.shutdown"));
  SM_CHECK(sm::is_known_op("evt.error"));
  SM_CHECK(!sm::is_known_op("random.op"));

  SM_CHECK(sm::op_namespace("media.preview") != nullptr);
  SM_CHECK_EQ(std::string(sm::op_namespace("playlist.next")), "playlist");
  SM_CHECK(sm::op_namespace("evt.engine.up") == nullptr);  // 事件不算指令命名空间
  SM_CHECK(sm::op_namespace("no.such") == nullptr);
}

void test_evt_prefix() {
  for (const char* op : evt_ops()) {
    SM_CHECK_MSG(std::string(op).rfind("evt.", 0) == 0, std::string("事件 op 应以 evt. 开头: ") + op);
  }
}

}  // namespace

int main() {
  test_counts();
  test_namespace_grouping();
  test_lookup();
  test_evt_prefix();
  return smtest::finish("test_op_dict");
}

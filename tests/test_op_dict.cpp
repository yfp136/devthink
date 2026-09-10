// 指令/事件字典单元测试（规格 §5.3 / §5.4）
#include <map>
#include <string>

#include "core/op_dict.h"
#include "test_common.h"

namespace {

using sm::cmd_ops;
using sm::evt_ops;
using sm::evt_ops_spec;

void test_counts() {
  SM_CHECK_EQ(cmd_ops().size(), std::size_t(33));      // Phase 1 全集
  SM_CHECK_EQ(evt_ops_spec().size(), std::size_t(17));  // §5.4 表 16 行、up/down 拆条
  SM_CHECK_EQ(evt_ops().size(), std::size_t(22));       // 核心集 17 + 实现增补集 5
}

// §11.4.1：实现增补集必须严格包含规格核心集，且不得与之重复。
void test_evt_superset() {
  for (const char* op : evt_ops_spec()) {
    SM_CHECK_MSG(sm::is_evt_op(op), std::string("规格事件应可识别: ") + op);
  }
  SM_CHECK_EQ(evt_ops().size() - evt_ops_spec().size(), std::size_t(5));
  // 增补集逐条点名，且必须不在核心集内（与 op_dict.cpp 注释同源）
  const char* const kExtra[] = {"evt.playlist.loaded", "evt.playlist.item_started",
                                "evt.playlist.item_ended", "evt.playlist.waiting_go",
                                "evt.scene.saved"};
  for (const char* op : kExtra) {
    SM_CHECK_MSG(sm::is_evt_op(op), std::string("增补事件应可识别: ") + op);
    bool in_spec = false;
    for (const char* s : evt_ops_spec()) in_spec = in_spec || std::string(s) == op;
    SM_CHECK_MSG(!in_spec, std::string("增补事件不得重复出现在核心集: ") + op);
  }
  // 全集内不得有重复项（计数断言的前提）
  for (std::size_t i = 0; i < evt_ops().size(); ++i) {
    for (std::size_t j = i + 1; j < evt_ops().size(); ++j) {
      SM_CHECK_MSG(std::string(evt_ops()[i]) != evt_ops()[j], "事件字典存在重复项");
    }
  }
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

// §9.5 [1015] 遥控控制面白名单：transport.* / playlist.* 前缀族 +
// scene.recall / sys.set_volume / sys.ping 单例放行，其余一律拒绝（1002）。
void test_remote_whitelist() {
  // 放行：前缀族（规格原文的 * 通配语义）
  for (const char* op : {"transport.play", "transport.pause", "transport.stop",
                         "transport.seek", "transport.goto_show",
                         "playlist.load", "playlist.start", "playlist.go",
                         "playlist.pause", "playlist.resume", "playlist.stop",
                         "playlist.next"}) {
    SM_CHECK_MSG(sm::is_remote_control_op(op), std::string("白名单应放行: ") + op);
  }
  // 放行：单例
  SM_CHECK(sm::is_remote_control_op("scene.recall"));
  SM_CHECK(sm::is_remote_control_op("sys.set_volume"));
  SM_CHECK(sm::is_remote_control_op("sys.ping"));
  // 拒绝：命名空间内的其余指令（同前缀族不等于全放行）
  SM_CHECK(!sm::is_remote_control_op("sys.shutdown"));
  SM_CHECK(!sm::is_remote_control_op("sys.get_state"));
  SM_CHECK(!sm::is_remote_control_op("media.import"));
  SM_CHECK(!sm::is_remote_control_op("media.preview"));
  SM_CHECK(!sm::is_remote_control_op("timeline.load"));
  SM_CHECK(!sm::is_remote_control_op("scene.save"));
  SM_CHECK(!sm::is_remote_control_op("scene.delete"));
  SM_CHECK(!sm::is_remote_control_op("scene.list"));
  SM_CHECK(!sm::is_remote_control_op("remote.hello"));
  // 拒绝：事件 op 不属控制面
  SM_CHECK(!sm::is_remote_control_op("evt.transport.state"));
  SM_CHECK(!sm::is_remote_control_op("evt.playlist.playing"));
  // 边界：仅前缀本身（无后续段）不得命中，避免 "" / "transport." 之类空操作放行
  SM_CHECK(!sm::is_remote_control_op("transport."));
  SM_CHECK(!sm::is_remote_control_op("playlist."));
  SM_CHECK(!sm::is_remote_control_op("transport"));
  SM_CHECK(!sm::is_remote_control_op(""));
  SM_CHECK(!sm::is_remote_control_op("no.such.op"));
}

}  // namespace

int main() {
  test_counts();
  test_evt_superset();
  test_namespace_grouping();
  test_lookup();
  test_evt_prefix();
  test_remote_whitelist();
  return smtest::finish("test_op_dict");
}

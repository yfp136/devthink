#include "core/op_dict.h"

#include <algorithm>

namespace sm {

// 规格 §5.3 指令字典（Phase 1 全集，共 33 条）
const std::vector<OpEntry>& cmd_ops() {
  static const std::vector<OpEntry> kOps = {
    // sys（4）
    {"sys", "sys.ping"},
    {"sys", "sys.shutdown"},
    {"sys", "sys.set_volume"},
    {"sys", "sys.get_state"},
    // media（7）
    {"media", "media.import"},
    {"media", "media.remove"},
    {"media", "media.restore"},
    {"media", "media.purge"},
    {"media", "media.update_tags"},
    {"media", "media.preview"},
    {"media", "media.query"},
    // transport（5）
    {"transport", "transport.play"},
    {"transport", "transport.pause"},
    {"transport", "transport.stop"},
    {"transport", "transport.seek"},
    {"transport", "transport.goto_show"},
    // timeline（5）
    {"timeline", "timeline.load"},
    {"timeline", "timeline.item_insert"},
    {"timeline", "timeline.item_update"},
    {"timeline", "timeline.item_remove"},
    {"timeline", "timeline.track_configure"},
    // scene（4）
    {"scene", "scene.save"},
    {"scene", "scene.recall"},
    {"scene", "scene.delete"},
    {"scene", "scene.list"},
    // playlist（7）
    {"playlist", "playlist.load"},
    {"playlist", "playlist.start"},
    {"playlist", "playlist.go"},
    {"playlist", "playlist.pause"},
    {"playlist", "playlist.resume"},
    {"playlist", "playlist.stop"},
    {"playlist", "playlist.next"},
    // remote（1）
    {"remote", "remote.hello"},
  };
  return kOps;
}

// 规格 §5.4 事件字典（Phase 1 核心集，16 行表；engine.up / engine.down 拆条 ⇒ 17）
const std::vector<const char*>& evt_ops_spec() {
  static const std::vector<const char*> kEvts = {
    "evt.state.snapshot",
    "evt.transport.state",
    "evt.transport.clock",
    "evt.transport.bpm",
    "evt.timeline.item_started",
    "evt.timeline.item_ended",
    "evt.media.import_progress",
    "evt.media.import_done",
    "evt.scene.recalled",
    "evt.playlist.playing",
    "evt.playlist.advance",
    "evt.playlist.ended",
    "evt.engine.up",
    "evt.engine.down",
    "evt.engine.heartbeat",
    "evt.log",
    "evt.error",
  };
  return kEvts;
}

// 实现事件全集 = §5.4 核心集 + 实现增补集。
// 增补集来源（§11.4.1「op/evt 变更须文档同源更新」）：
//   引擎已实际广播、且被 UI（InspectorPanel.qml）消费，但 §5.4 表未列名的事件，
//   登记入字典以保证 is_evt_op 判定、观测面转发与文档同源：
//     evt.playlist.loaded       playlist_executor.cpp load()      （§10.5.2 idle → loaded）
//     evt.playlist.item_started playlist_executor.cpp execute_current()
//     evt.playlist.item_ended   playlist_executor.cpp advance()
//     evt.playlist.waiting_go   playlist_executor.cpp tick()      （§10.5.2 → waiting_go）
//     evt.scene.saved           kernel.cpp scene.save 处理分支   （§10.3.1 落库成功）
const std::vector<const char*>& evt_ops() {
  static const std::vector<const char*> kAll = [] {
    std::vector<const char*> v = evt_ops_spec();
    v.insert(v.end(), {
      "evt.playlist.loaded",
      "evt.playlist.item_started",
      "evt.playlist.item_ended",
      "evt.playlist.waiting_go",
      "evt.scene.saved",
    });
    return v;
  }();
  return kAll;
}

bool is_cmd_op(const std::string& op) {
  const auto& all = cmd_ops();
  return std::any_of(all.begin(), all.end(),
                     [&](const OpEntry& e) { return op == e.op; });
}

bool is_evt_op(const std::string& op) {
  const auto& all = evt_ops();
  return std::find(all.begin(), all.end(), op) != all.end();
}

bool is_known_op(const std::string& op) { return is_cmd_op(op) || is_evt_op(op); }

const char* op_namespace(const std::string& op) {
  for (const OpEntry& e : cmd_ops()) {
    if (op == e.op) return e.ns;
  }
  return nullptr;
}

// §9.5 [1015] 遥控控制面白名单。
// 规格原文：允许 transport.*、playlist.*、scene.recall、sys.set_volume、sys.ping；
// 其余 op 一律拒绝（错误码 1002）。故前缀族按通配语义判定，
// 单例 op 按全等判定；命中与否只决定「是否放行到引擎」，
// op 本身是否合法仍由内核按 §5.5 返回 1001。
bool is_remote_control_op(const std::string& op) {
  static const char* const kPrefixes[] = {"transport.", "playlist."};
  for (const char* p : kPrefixes) {
    const std::string prefix(p);
    if (op.size() > prefix.size() && op.compare(0, prefix.size(), prefix) == 0) {
      return true;
    }
  }
  return op == "scene.recall" || op == "sys.set_volume" || op == "sys.ping";
}

}  // namespace sm

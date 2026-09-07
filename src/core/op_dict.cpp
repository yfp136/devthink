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

// 规格 §5.4 事件字典
const std::vector<const char*>& evt_ops() {
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

}  // namespace sm

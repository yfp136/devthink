// SceneStore 实现（§10.3）
#include "engines/scene/scene_store.h"

#include <chrono>
#include <cstdio>

#include "core/util.h"

namespace sm {
namespace scene {

using json = nlohmann::json;

SceneStore::SceneStore() = default;

std::string SceneStore::gen_id() const {
  return "scn_" + sm::uuid_hex32().substr(0, 12);
}

// ---- scene.save ----
std::string SceneStore::save(const std::string& scene_name,
                              const std::string& folder,
                              int fade_ms,
                              const std::string& recall_mode) {
  if (scene_name.empty()) return "";

  auto snap = std::make_shared<Snapshot>();
  snap->scene_id = gen_id();
  snap->scene_name = scene_name;
  snap->folder = folder;
  snap->fade_ms = fade_ms;
  snap->recall_mode = recall_mode;

  // 获取当前运行态
  if (get_current_state_) {
    snap->state_json = get_current_state_();
  } else {
    // 默认空状态
    snap->state_json = R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  }

  // 抓取缩略图
  if (capture_thumb_)
    snap->thumb_b64 = capture_thumb_();

  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  snap->create_ms = now;
  snap->update_ms = now;

  snapshots_[snap->scene_id] = snap;
  return snap->scene_id;
}

// ---- scene.recall ----
bool SceneStore::recall(const std::string& scene_id, int fade_ms_override,
                         const std::string& recall_mode_override) {
  auto it = snapshots_.find(scene_id);
  if (it == snapshots_.end()) return false;

  auto& snap = it->second;

  // fade_ms 优先级：参数 > 快照 > 默认 0
  int fade = (fade_ms_override >= 0) ? fade_ms_override : snap->fade_ms;
  std::string mode = recall_mode_override.empty() ? snap->recall_mode
                                                    : recall_mode_override;

  // overlay 在 P1 视作 cut 并记 warn（§10.3.2）
  if (mode == "overlay") {
    std::fprintf(stdout, "[scene] WARN: overlay not supported in P1, using cut\n");
    mode = "cut";
  }

  if (apply_state_)
    apply_state_(snap->state_json, fade, mode);

  return true;
}

// ---- scene.delete ----
bool SceneStore::remove(const std::string& scene_id,
                          const std::vector<std::string>& referencing_ids) {
  auto it = snapshots_.find(scene_id);
  if (it == snapshots_.end()) return false;

  // 引用检查
  if (!referencing_ids.empty())
    return false;  // 被引用，不能删

  snapshots_.erase(it);
  return true;
}

// ---- scene.list ----
json SceneStore::list(const std::string& folder) const {
  json arr = json::array();
  for (const auto& [id, snap] : snapshots_) {
    if (!folder.empty() && snap->folder != folder) continue;
    arr.push_back({
      {"scene_id", snap->scene_id},
      {"scene_name", snap->scene_name},
      {"folder", snap->folder},
      {"fade_ms", snap->fade_ms},
      {"recall_mode", snap->recall_mode},
      {"create_ms", snap->create_ms},
      {"has_thumb", !snap->thumb_b64.empty()},
    });
  }
  return json({{"items", arr}, {"total", arr.size()}});
}

std::shared_ptr<Snapshot> SceneStore::find(const std::string& scene_id) const {
  auto it = snapshots_.find(scene_id);
  if (it == snapshots_.end()) return nullptr;
  return it->second;
}

}  // namespace scene
}  // namespace sm

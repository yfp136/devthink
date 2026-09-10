// M3 场景快照引擎（§10.3）
// 职责：保存当前演出状态为快照、召回快照、淡变切换、列表管理。
// state_json 结构已在 src/project/state_json.* 实现（§10.4），本模块负责引擎层逻辑。
//
// 总线指令：scene.save / scene.recall / scene.delete / scene.list
// 事件：evt.scene.saved / evt.scene.recalled
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace scene {

// 快照记录（内存态，对应 scene_snapshot 表）
struct Snapshot {
  std::string scene_id;
  std::string scene_name;
  std::string folder;        // 一级分组
  std::string state_json;    // §10.4 状态结构
  int fade_ms = 0;           // 默认淡变时长
  std::string recall_mode = "cut";  // cut / fade / overlay
  std::string thumb_b64;     // 320px 缩略图 base64（可空）
  int64_t create_ms = 0;
  int64_t update_ms = 0;
};

// 召回回调（由 M2 MediaEngine 注入）
// 参数：state_json 文本、fade_ms、recall_mode
using ApplyStateFn = std::function<void(const std::string& state_json,
                                        int fade_ms,
                                        const std::string& recall_mode)>;
// 抓取当前 PGM 帧（返回 JPEG base64，可空）
using CaptureThumbFn = std::function<std::string()>;
// 获取当前运行态（返回 state_json 文本）
using GetCurrentStateFn = std::function<std::string()>;
// 召回完成回调（§10.3.2：状态应用完毕后发 evt.scene.recalled，携 applied_at_ms）
using RecalledFn = std::function<void(const std::string& scene_id, int fade_ms,
                                      int64_t applied_at_ms)>;

class SceneStore {
 public:
  SceneStore();

  // ---- 回调注入 ----
  void set_apply_state_cb(ApplyStateFn cb) { apply_state_ = std::move(cb); }
  void set_capture_thumb_cb(CaptureThumbFn cb) { capture_thumb_ = std::move(cb); }
  void set_get_current_state_cb(GetCurrentStateFn cb) { get_current_state_ = std::move(cb); }
  void set_recalled_cb(RecalledFn cb) { recalled_ = std::move(cb); }

  // ---- scene.save ----
  // 保存当前状态为快照。返回 scene_id（失败返回空）。
  std::string save(const std::string& scene_name, const std::string& folder = "",
                   int fade_ms = 0, const std::string& recall_mode = "fade");

  // ---- scene.recall ----
  // 召回指定场景。fade_ms 优先级：参数 > 快照 > 默认。
  // 返回 true=成功，false=场景不存在。
  bool recall(const std::string& scene_id, int fade_ms_override = -1,
              const std::string& recall_mode_override = "");

  // ---- scene.delete ----
  // 删除前检查引用（返回 false 表示被引用，不能删）。
  bool remove(const std::string& scene_id,
              const std::vector<std::string>& referencing_ids = {});

  // ---- scene.list ----
  nlohmann::json list(const std::string& folder = "") const;

  // 查询
  std::shared_ptr<Snapshot> find(const std::string& scene_id) const;
  size_t count() const { return snapshots_.size(); }

  // 清空（测试用）
  void clear() { snapshots_.clear(); }

 private:
  std::map<std::string, std::shared_ptr<Snapshot>> snapshots_;
  ApplyStateFn apply_state_;
  CaptureThumbFn capture_thumb_;
  GetCurrentStateFn get_current_state_;
  RecalledFn recalled_;

  std::string gen_id() const;
};

}  // namespace scene
}  // namespace sm

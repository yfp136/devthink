// state_json 统一状态结构 v1（规格 §10.4，scene_snapshot.state_json 契约）
// 顶层为版本化对象；解析规则：
//   * 未知字段一律忽略（向前兼容，Phase 3 设备层扩展不改根结构）
//   * sv > 当前支持版本时召回拒绝（调用方回 5001 并提示升级软件）
//   * scope 为本次快照覆盖的子系统；P1 ∈ {media, master}
//   * master{ gain_db, mute } 必填；media{ source, pos_ms, playing } 必填
// 本模块仅做语义校验与取值辅助，不持有场景数据。
#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace sm {

inline constexpr int kStateJsonVersion = 1;  // 当前支持的结构版本

// 校验结果
struct StateJsonResult {
  bool ok = false;
  std::string message;    // 失败原因（中文，供 err 信封与日志）
  int supported_sv = kStateJsonVersion;
};

// 校验一份 state_json 文本；语义：JSON 可解析、顶层为对象、sv 为整数且
// <= 当前支持版本、scope 数组非空且仅含 P1 允许的子系统、必填子树齐全。
StateJsonResult validate_state_json(const std::string& text);

// 从已解析 json 校验（供场景行集内部批量校验复用）
StateJsonResult validate_state_json(const nlohmann::json& state);

}  // namespace sm

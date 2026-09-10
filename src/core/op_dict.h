// 指令字典与事件字典（规格 §5.3 / §5.4，权威表）
// 指令共 33 条（Phase 1 全集）。
// 事件分两层：evt_ops_spec() = §5.4 规格核心集（16 行，engine.up/down 拆为
// 两个 op ⇒ 17 条）；evt_ops() = 规格核心集 + 实现增补集（引擎实际广播、
// 且被 UI 消费的事件，§11.4.1「op/evt 变更须文档同源更新」）。
#pragma once

#include <string>
#include <vector>

namespace sm {

struct OpEntry {
  const char* ns;   // 命名空间：sys / media / transport / timeline / scene / playlist / remote
  const char* op;   // 完整 op 名
};

// Phase 1 全部指令（33 条），命名空间分组排序
const std::vector<OpEntry>& cmd_ops();

// §5.4 规格事件核心集（16 行；engine.up / engine.down 为独立 op ⇒ 17 条）
const std::vector<const char*>& evt_ops_spec();

// 实现事件全集 = 规格核心集 + 实现增补集（superset，见 .cpp 注释）
const std::vector<const char*>& evt_ops();

bool is_cmd_op(const std::string& op);
bool is_evt_op(const std::string& op);
bool is_known_op(const std::string& op);   // cmd 或 evt

// 返回 op 所在命名空间（sys/media/...），未知返回 nullptr
const char* op_namespace(const std::string& op);

// §9.5 [1015] 遥控控制面白名单：仅允许 transport.*、playlist.*、
// scene.recall、sys.set_volume、sys.ping；其余 op 一律拒绝（错误码 1002）。
bool is_remote_control_op(const std::string& op);

}  // namespace sm

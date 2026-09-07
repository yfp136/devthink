// 指令字典与事件字典（规格 §5.3 / §5.4，权威表）
// 指令共 33 条（Phase 1 全集）；事件覆盖 §5.4 全部 op。
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

// 事件 op 全集（§5.4；engine.up 与 engine.down 为独立 op）
const std::vector<const char*>& evt_ops();

bool is_cmd_op(const std::string& op);
bool is_evt_op(const std::string& op);
bool is_known_op(const std::string& op);   // cmd 或 evt

// 返回 op 所在命名空间（sys/media/...），未知返回 nullptr
const char* op_namespace(const std::string& op);

}  // namespace sm

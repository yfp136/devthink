// 消息信封（规格 §5.1）
// 总线只传一种报文：JSON 信封。cmd / rsp / evt / err 共用同一结构。
// 字段：v、id、monotonic_ms、ts?、src、dst、type、op、params?、ref_id?、code(rsp/err 必填)
#pragma once

#include <cstdint>
#include <string>

#include "nlohmann/json.hpp"

namespace sm {

inline constexpr int kEnvelopeVersion = 1;

struct Envelope {
  int v = kEnvelopeVersion;
  std::string id;                 // 消息唯一 ID（发送方生成）
  std::int64_t monotonic_ms = 0;  // 宿主单调时钟毫秒
  std::string ts;                 // 系统时间 ISO8601（可选）
  std::string src;                // engine.xxx / engine.ui / remote.tcp:<peer>
  std::string dst;                // engine.xxx / *（广播）/ engine.ui
  std::string type;               // cmd / rsp / evt / err
  std::string op;                 // 指令名（§5.3 / §5.4 字典）
  nlohmann::json params = nlohmann::json::object();  // 参数对象（可选）
  std::string ref_id;             // rsp/err 回填所响应命令的 id
  int code = 0;                   // 错误码（rsp/err 必填，0=成功，见 §5.5）
  bool has_code = false;
};

struct ParseResult {
  bool ok = false;
  int code = 0;           // 失败时为错误码（默认 1002 参数非法）
  std::string message;    // 失败原因
};

// 解析信封；结构不合法返回 ParseResult{ok=false}，任何失败均映射 1002 语义
ParseResult parse_envelope(const std::string& json_text, Envelope& out);

// 序列化（rsp/err 自动携带 code）
std::string envelope_to_json(const Envelope& e);

// 便捷构造 ---------------------------------------------------------------
// 命令：从 UI/遥控向某引擎或 core 发指令
Envelope make_cmd(const std::string& src, const std::string& dst, const std::string& op,
                  const nlohmann::json& params = nlohmann::json::object());

// 响应 / 错误：回填所响应命令的 id 与 op；code 见 §5.5，0 表示成功
Envelope make_reply(const Envelope& request, int code,
                    const nlohmann::json& params = nlohmann::json::object());

// 事件：引擎/内核向订阅方广播（dst 通常为 * 或 engine.ui）
Envelope make_event(const std::string& src, const std::string& op,
                    const nlohmann::json& params = nlohmann::json::object());

}  // namespace sm

#include "core/envelope.h"

#include "core/error_codes.h"
#include "core/util.h"

namespace sm {

namespace {

bool is_type(const std::string& t) {
  return t == "cmd" || t == "rsp" || t == "evt" || t == "err";
}

}  // namespace

ParseResult parse_envelope(const std::string& json_text, Envelope& out) {
  ParseResult r;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_text);
  } catch (const nlohmann::json::exception& e) {
    r.code = ec::BAD_PARAM;
    r.message = std::string("信封不是合法 JSON: ") + e.what();
    return r;
  }
  if (!j.is_object()) {
    r.code = ec::BAD_PARAM;
    r.message = "信封根节点必须是 JSON 对象";
    return r;
  }

  // v：int 且当前为 1
  if (!j.contains("v") || !j["v"].is_number_integer() || j["v"].get<int>() != kEnvelopeVersion) {
    r.code = ec::BAD_PARAM;
    r.message = "v 必须为整数且等于协议版本 1";
    return r;
  }
  out.v = j["v"].get<int>();

  // id：必填非空字符串
  if (!j.contains("id") || !j["id"].is_string() || j["id"].get_ref<const std::string&>().empty()) {
    r.code = ec::BAD_PARAM;
    r.message = "id 为必填非空字符串";
    return r;
  }
  out.id = j["id"].get<std::string>();

  // monotonic_ms：必填 int64
  if (!j.contains("monotonic_ms") || !j["monotonic_ms"].is_number_integer()) {
    r.code = ec::BAD_PARAM;
    r.message = "monotonic_ms 为必填整数（毫秒单调时钟）";
    return r;
  }
  out.monotonic_ms = j["monotonic_ms"].get<std::int64_t>();

  // ts：可选字符串
  if (j.contains("ts")) {
    if (!j["ts"].is_string()) { r.code = ec::BAD_PARAM; r.message = "ts 必须为字符串"; return r; }
    out.ts = j["ts"].get<std::string>();
  }

  // src / dst / type / op：必填字符串
  for (const char* k : {"src", "dst", "type", "op"}) {
    if (!j.contains(k) || !j[k].is_string() || j[k].get_ref<const std::string&>().empty()) {
      r.code = ec::BAD_PARAM;
      r.message = std::string(k) + " 为必填非空字符串";
      return r;
    }
  }
  out.src = j["src"].get<std::string>();
  out.dst = j["dst"].get<std::string>();
  out.type = j["type"].get<std::string>();
  out.op = j["op"].get<std::string>();

  if (!is_type(out.type)) {
    r.code = ec::BAD_PARAM;
    r.message = "type 只能是 cmd / rsp / evt / err 之一";
    return r;
  }

  // params：可选对象
  if (j.contains("params")) {
    if (!j["params"].is_object()) { r.code = ec::BAD_PARAM; r.message = "params 必须为 JSON 对象"; return r; }
    out.params = j["params"];
  }

  // ref_id：可选字符串
  if (j.contains("ref_id")) {
    if (!j["ref_id"].is_string()) { r.code = ec::BAD_PARAM; r.message = "ref_id 必须为字符串"; return r; }
    out.ref_id = j["ref_id"].get<std::string>();
  }

  // code：rsp/err 必填整数；cmd/evt 不允许携带
  if (out.type == "rsp" || out.type == "err") {
    if (!j.contains("code") || !j["code"].is_number_integer()) {
      r.code = ec::BAD_PARAM;
      r.message = "rsp/err 信封必须携带整数 code（0=成功）";
      return r;
    }
    out.code = j["code"].get<int>();
    out.has_code = true;
  } else if (j.contains("code")) {
    r.code = ec::BAD_PARAM;
    r.message = "cmd/evt 信封不得携带 code";
    return r;
  }

  r.ok = true;
  return r;
}

std::string envelope_to_json(const Envelope& e) {
  nlohmann::json j;
  j["v"] = e.v;
  j["id"] = e.id;
  j["monotonic_ms"] = e.monotonic_ms;
  if (!e.ts.empty()) j["ts"] = e.ts;
  j["src"] = e.src;
  j["dst"] = e.dst;
  j["type"] = e.type;
  j["op"] = e.op;
  if (!e.params.is_null() && !e.params.empty()) j["params"] = e.params;
  if (!e.ref_id.empty()) j["ref_id"] = e.ref_id;
  if (e.type == "rsp" || e.type == "err") j["code"] = e.code;
  return j.dump();
}

Envelope make_cmd(const std::string& src, const std::string& dst, const std::string& op,
                  const nlohmann::json& params) {
  Envelope e;
  e.id = uuid_hex32();
  e.monotonic_ms = now_monotonic_ms();
  e.ts = iso8601_now();
  e.src = src;
  e.dst = dst;
  e.type = "cmd";
  e.op = op;
  e.params = params;
  return e;
}

Envelope make_reply(const Envelope& request, int code, const nlohmann::json& params) {
  Envelope e;
  e.id = uuid_hex32();
  e.monotonic_ms = now_monotonic_ms();
  e.ts = iso8601_now();
  e.src = request.dst;   // 应答方 = 请求的目标
  e.dst = request.src;   // 回给请求方
  e.type = (code == ec::OK) ? "rsp" : "err";
  e.op = request.op;
  e.ref_id = request.id;
  e.code = code;
  e.has_code = true;
  e.params = params;
  return e;
}

Envelope make_event(const std::string& src, const std::string& op, const nlohmann::json& params) {
  Envelope e;
  e.id = uuid_hex32();
  e.monotonic_ms = now_monotonic_ms();
  e.ts = iso8601_now();
  e.src = src;
  e.dst = "*";
  e.type = "evt";
  e.op = op;
  e.params = params;
  return e;
}

}  // namespace sm

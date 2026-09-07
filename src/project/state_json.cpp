#include "project/state_json.h"

namespace sm {

namespace {

StateJsonResult fail(const std::string& msg) { return {false, msg, kStateJsonVersion}; }

// scope 白名单：P1 仅 media/master（§10.4）；未知子系统不算错误但记入 message 由调用方决定
bool p1_scope_ok(const nlohmann::json& scope) {
  if (!scope.is_array() || scope.empty()) return false;
  for (const auto& s : scope) {
    if (!s.is_string()) return false;
    const std::string v = s.get<std::string>();
    if (v != "media" && v != "master") return false;
  }
  return true;
}

}  // namespace

StateJsonResult validate_state_json(const nlohmann::json& st) {
  if (!st.is_object()) return fail("state_json 顶层必须是对象");

  // sv：必填、整数、不高于当前支持版本（高于则召回方拒绝并提示升级）
  if (!st.contains("sv")) return fail("缺少必填字段 sv");
  if (!st["sv"].is_number_integer()) return fail("sv 必须是整数");
  if (st["sv"].get<int>() > kStateJsonVersion)
    return fail("sv 高于当前支持版本，需升级软件（拒绝召回）");

  // scope：P1 ∈ {media, master}
  if (!st.contains("scope")) return fail("缺少必填字段 scope");
  if (!p1_scope_ok(st["scope"]))
    return fail("scope 必须为非空数组且 P1 仅允许 media/master");

  // master{ gain_db, mute }：必填
  if (!st.contains("master") || !st["master"].is_object())
    return fail("缺少必填对象 master");
  const auto& master = st["master"];
  if (!master.contains("gain_db") || !master["gain_db"].is_number())
    return fail("master.gain_db 必填且为数值（-60..6 dB）");
  if (!master.contains("mute") || !master["mute"].is_boolean())
    return fail("master.mute 必填且为布尔");

  // media{ source, pos_ms, playing }：必填；source.type ∈ {none, media}
  if (!st.contains("media") || !st["media"].is_object())
    return fail("缺少必填对象 media");
  const auto& media = st["media"];
  if (!media.contains("source") || !media["source"].is_object())
    return fail("media.source 必填且为对象");
  const auto& src = media["source"];
  if (!src.contains("type") || !src["type"].is_string())
    return fail("media.source.type 必填");
  const std::string stype = src["type"].get<std::string>();
  if (stype != "none" && stype != "media")
    return fail("media.source.type 仅允许 none|media（P1）");
  if (stype == "media" && (!src.contains("media_id") || !src["media_id"].is_string()))
    return fail("media.source.type=media 时必须携带 media_id");
  if (!media.contains("pos_ms") || !media["pos_ms"].is_number_integer())
    return fail("media.pos_ms 必填且为整数（type=none 时固定 0）");
  if (!media.contains("playing") || !media["playing"].is_boolean())
    return fail("media.playing 必填且为布尔");

  // 可选数值字段：存在时须类型正确（gain_db/fade_in_ms/fade_out_ms/loop）
  for (const char* num : {"gain_db", "fade_in_ms", "fade_out_ms", "loop"}) {
    if (media.contains(num) && !media[num].is_number())
      return fail(std::string("media.") + num + " 必须为数值（可选）");
  }

  // devices：必填数组；P1 恒为 []（Phase 3 扩展），非空仅警告不拒绝（向前兼容）
  if (!st.contains("devices") || !st["devices"].is_array())
    return fail("devices 必填且为数组（P1 恒为 []）");

  return {true, "", kStateJsonVersion};
}

StateJsonResult validate_state_json(const std::string& text) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(text);
  } catch (const nlohmann::json::exception& e) {
    return fail(std::string("state_json JSON 非法: ") + e.what());
  }
  return validate_state_json(doc);
}

}  // namespace sm

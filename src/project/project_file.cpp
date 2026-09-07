#include "core/error_codes.h"
#include "project/project_file.h"

#include "nlohmann/json.hpp"

namespace sm {

namespace {

// 顶层字段类型断言（§8.2）。除 format/version/project 单独细查外，
// 行集与可选字段在此校验。kind：'s'=string 'o'=object 'a'=array
// "?"=允许缺失（存在时仍须类型正确）。
struct FieldSpec {
  const char* key;
  char kind;      // 's' / 'o' / 'a'
  bool required;  // false = 允许缺失
};

bool kind_ok(const nlohmann::json& v, char kind) {
  switch (kind) {
    case 's': return v.is_string();
    case 'o': return v.is_object();
    case 'a': return v.is_array();
    default: return false;
  }
}

constexpr FieldSpec kTopFields[] = {
    {"saved_at", 's', false},   {"saved_by", 's', false},
    {"tracks", 'a', true},      {"items", 'a', true},
    {"scenes", 'a', true},      {"playlists", 'a', true},
    {"devices", 'a', true},     {"settings", 'o', false},
};

ManifestResult fail(int code, const std::string& msg) {
  return {false, code, msg};
}

}  // namespace

ManifestResult validate_manifest(const std::string& manifest_text) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(manifest_text);
  } catch (const nlohmann::json::exception& e) {
    return fail(ec::PROJECT_CORRUPT, std::string("manifest JSON 非法: ") + e.what());
  }

  if (!doc.is_object())
    return fail(ec::PROJECT_CORRUPT, "manifest 顶层必须是 JSON 对象");

  // format / version / project：先查存在性（4001），再做语义检查（4002/4001）
  for (const char* key : {"format", "version", "project"}) {
    if (!doc.contains(key))
      return fail(ec::PROJECT_CORRUPT, std::string("缺少必填字段: ") + key);
  }
  for (const auto& f : kTopFields) {
    const auto it = doc.find(f.key);
    if (it == doc.end()) {
      if (f.required) return fail(ec::PROJECT_CORRUPT, std::string("缺少必填字段: ") + f.key);
      continue;
    }
    if (!kind_ok(*it, f.kind))
      return fail(ec::PROJECT_CORRUPT, std::string("字段类型错误: ") + f.key);
  }

  // format 不认识 / version 过新 → 4002（含"打开非本软件工程"）
  if (!doc["format"].is_string() || doc["format"] != kShowprojMagic)
    return fail(ec::PROJECT_VERSION_TOO_NEW,
                "format 非 \"showmaster.project\"（非本软件工程或格式过新）");
  if (!doc["version"].is_number_integer())
    return fail(ec::PROJECT_CORRUPT, "version 必须是整数");
  if (doc["version"].get<int>() > kShowprojFormat)
    return fail(ec::PROJECT_VERSION_TOO_NEW, "manifest 版本过新，请升级软件打开");

  // project 对象子字段（name 必填；save_mode ∈ ref|pack；media_root 必填）
  const auto& prj = doc["project"];
  if (!prj.is_object())
    return fail(ec::PROJECT_CORRUPT, "project 必须是对象");
  if (!prj.contains("name") || !prj["name"].is_string())
    return fail(ec::PROJECT_CORRUPT, "project.name 必填且为字符串");
  if (!prj.contains("save_mode") || !prj["save_mode"].is_string())
    return fail(ec::PROJECT_CORRUPT, "project.save_mode 必填");
  const std::string mode = prj["save_mode"].get<std::string>();
  if (mode != "ref" && mode != "pack")
    return fail(ec::PROJECT_CORRUPT, "project.save_mode 仅允许 ref|pack");
  if (!prj.contains("media_root") || !prj["media_root"].is_string())
    return fail(ec::PROJECT_CORRUPT, "project.media_root 必填且为字符串");

  // scenes 元素含 state_json 时由 state_json 模块另行校验（§10.4）
  if (doc.contains("devices") && !doc["devices"].empty())
    return fail(ec::PROJECT_CORRUPT, "devices 在 Phase 1 必须为空数组（结构预留）");

  return {true, 0, ""};
}

const nlohmann::json& rows_of(const nlohmann::json& doc, const char* key) {
  static const nlohmann::json kEmpty = nlohmann::json::array();
  const auto it = doc.find(key);
  if (it == doc.end() || !it->is_array()) return kEmpty;
  return *it;
}

nlohmann::json make_manifest_skeleton(const std::string& project_name,
                                      const std::string& save_mode,
                                      const std::string& media_root,
                                      const std::string& saved_by) {
  nlohmann::json m;
  m["format"] = kShowprojMagic;
  m["version"] = kShowprojFormat;
  m["saved_by"] = saved_by;
  m["project"] = {{"name", project_name}, {"save_mode", save_mode},
                  {"media_root", media_root}};
  m["tracks"] = nlohmann::json::array();
  m["items"] = nlohmann::json::array();
  m["scenes"] = nlohmann::json::array();
  m["playlists"] = nlohmann::json::array();
  m["devices"] = nlohmann::json::array();
  m["settings"] = nlohmann::json::object();
  return m;
}

}  // namespace sm

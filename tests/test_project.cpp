// 工程层单元测试：.showproj manifest.json 校验（规格 §8.2/§8.3，错误码 4xxx）
// 与 scene_snapshot.state_json v1 契约校验（规格 §10.4）。
#include <cstdio>
#include <string>

#include "core/error_codes.h"
#include "project/project_file.h"
#include "project/state_json.h"
#include "test_common.h"

namespace {

using nlohmann::json;
using sm::ManifestResult;
using sm::StateJsonResult;

// ---------------------------------------------------------------------------
// manifest.json 校验（§8.2）：结构错误 4001 / 版本过新 4002
// ---------------------------------------------------------------------------
void test_manifest_rejects_bad_json() {
  const ManifestResult r = sm::validate_manifest("{ not json !");
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);  // 4001
}

void test_manifest_top_level_must_be_object() {
  const ManifestResult r = sm::validate_manifest("[1,2,3]");
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
}

void test_manifest_missing_magic_fields() {
  // 缺 format / version / project 任一 → 4001
  json j = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  for (const char* key : {"format", "version", "project"}) {
    json broken = j;
    broken.erase(key);
    const ManifestResult r = sm::validate_manifest(broken.dump());
    SM_CHECK(!r.ok);
    SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
  }
}

void test_manifest_missing_required_rowsets() {
  // 缺 tracks/items/scenes/playlists/devices 之一 → 4001
  json j = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  for (const char* key : {"tracks", "items", "scenes", "playlists", "devices"}) {
    json broken = j;
    broken.erase(key);
    const ManifestResult r = sm::validate_manifest(broken.dump());
    SM_CHECK(!r.ok);
    SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
  }
}

void test_manifest_rowsets_must_be_arrays() {
  // 行集类型错误（对象冒充数组）→ 4001
  json j = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  j["tracks"] = json::object();
  const ManifestResult r = sm::validate_manifest(j.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
}

void test_manifest_bad_format_or_version() {
  // format 非 magic → 4002（非本软件工程/格式过新）
  json a = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  a["format"] = "other.app";
  ManifestResult r = sm::validate_manifest(a.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_VERSION_TOO_NEW);  // 4002

  // version 非整数 → 4001
  json b = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  b["version"] = "1";
  r = sm::validate_manifest(b.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);

  // version 过新（>1）→ 4002
  json c = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  c["version"] = 2;
  r = sm::validate_manifest(c.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_VERSION_TOO_NEW);
}

void test_manifest_project_fields() {
  // project.name 缺失/类型错误 → 4001
  json a = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  a["project"].erase("name");
  ManifestResult r = sm::validate_manifest(a.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);

  // project.save_mode 非法（非 ref|pack）→ 4001
  json b = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  b["project"]["save_mode"] = "hybrid";
  r = sm::validate_manifest(b.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);

  // project.media_root 缺失 → 4001
  json c = sm::make_manifest_skeleton("demo", "pack", "/media", "u1");
  c["project"].erase("media_root");
  r = sm::validate_manifest(c.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
}

void test_manifest_devices_must_be_empty_p1() {
  // P1 devices 恒为空数组；非空 → 4001（结构预留）
  json j = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  j["devices"].push_back({{"id", "dev1"}});
  const ManifestResult r = sm::validate_manifest(j.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
}

void test_manifest_optional_field_type() {
  // settings 可选；存在但类型错误 → 4001
  json j = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  j["settings"] = json::array();
  const ManifestResult r = sm::validate_manifest(j.dump());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);

  // saved_at 类型错误 → 4001；缺失仍合法
  json k = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  k["saved_at"] = 12345;
  ManifestResult r2 = sm::validate_manifest(k.dump());
  SM_CHECK(!r2.ok);
  SM_CHECK_EQ(r2.code, sm::ec::PROJECT_CORRUPT);

  json l = sm::make_manifest_skeleton("demo", "ref", "/media", "u1");
  l.erase("saved_at");
  l.erase("saved_by");
  l.erase("settings");
  r2 = sm::validate_manifest(l.dump());
  SM_CHECK(r2.ok);
}

void test_manifest_valid_roundtrip() {
  // make_manifest_skeleton 产出 → 直接校验通过，行集抽取齐全
  json j = sm::make_manifest_skeleton("巡演 A", "ref", "/Volumes/Media/ShowA", "designer@sm");
  j["scenes"].push_back({{"id", "scn1"}, {"name", "开场"}});
  const ManifestResult r = sm::validate_manifest(j.dump());
  SM_CHECK(r.ok);
  SM_CHECK_EQ(r.message, "");

  const json& tracks = sm::rows_of(j, "tracks");
  const json& missing = sm::rows_of(j, "nonexistent");
  SM_CHECK(tracks.is_array());
  SM_CHECK(missing.is_array());
  SM_CHECK(missing.empty());
  SM_CHECK_EQ(j["scenes"].size(), std::size_t(1));
}

// ---------------------------------------------------------------------------
// state_json v1（§10.4）：未知字段忽略、sv 过高拒绝、必填子树校验
// ---------------------------------------------------------------------------
json valid_state() {
  return json{{"sv", 1},
              {"scope", json::array({"media", "master"})},
              {"master", {{"gain_db", -6.0}, {"mute", false}}},
              {"media", {{"source", {{"type", "media"}, {"media_id", "md_1"}}},
                         {"pos_ms", 1200},
                         {"playing", true},
                         {"gain_db", 0.0}}},
              {"devices", json::array()}};
}

void test_state_valid_and_forward_compat() {
  json j = valid_state();
  StateJsonResult r = sm::validate_state_json(j.dump());
  SM_CHECK(r.ok);
  SM_CHECK_EQ(r.supported_sv, 1);

  // 未知字段一律忽略（向前兼容）：追加根级与子级字段仍通过
  j["future_zone"] = {{"x", 1}};
  j["media"]["future_flag"] = true;
  r = sm::validate_state_json(j.dump());
  SM_CHECK(r.ok);
}

void test_state_rejects_bad_json_or_root() {
  StateJsonResult r = sm::validate_state_json(std::string("{ nope"));
  SM_CHECK(!r.ok);
  r = sm::validate_state_json(std::string("[1]"));  // 顶层非对象
  SM_CHECK(!r.ok);
}

void test_state_sv_guards() {
  // sv 缺失 / 非整数 / 高于支持版本 → 拒绝（过高时由调用方回 5001 提示升级）
  json no_sv = valid_state();
  no_sv.erase("sv");
  SM_CHECK(!sm::validate_state_json(no_sv.dump()).ok);

  json str_sv = valid_state();
  str_sv["sv"] = "1";
  SM_CHECK(!sm::validate_state_json(str_sv.dump()).ok);

  json high_sv = valid_state();
  high_sv["sv"] = 2;
  StateJsonResult r = sm::validate_state_json(high_sv.dump());
  SM_CHECK(!r.ok);
}

void test_state_scope_guards() {
  json no_scope = valid_state();
  no_scope.erase("scope");
  SM_CHECK(!sm::validate_state_json(no_scope.dump()).ok);

  json empty_scope = valid_state();
  empty_scope["scope"] = json::array();
  SM_CHECK(!sm::validate_state_json(empty_scope.dump()).ok);

  json bad_scope = valid_state();
  bad_scope["scope"] = json::array({"video"});  // P1 仅 media/master
  SM_CHECK(!sm::validate_state_json(bad_scope.dump()).ok);

  json non_str_scope = valid_state();
  non_str_scope["scope"] = json::array({1});
  SM_CHECK(!sm::validate_state_json(non_str_scope.dump()).ok);
}

void test_state_master_guards() {
  json no_master = valid_state();
  no_master.erase("master");
  SM_CHECK(!sm::validate_state_json(no_master.dump()).ok);

  json no_gain = valid_state();
  no_gain["master"].erase("gain_db");
  SM_CHECK(!sm::validate_state_json(no_gain.dump()).ok);

  json bad_mute = valid_state();
  bad_mute["master"]["mute"] = "off";
  SM_CHECK(!sm::validate_state_json(bad_mute.dump()).ok);
}

void test_state_media_guards() {
  // source.type=media 缺 media_id → 拒绝
  json no_media_id = valid_state();
  no_media_id["media"]["source"].erase("media_id");
  SM_CHECK(!sm::validate_state_json(no_media_id.dump()).ok);

  // source.type 非法值 → 拒绝
  json bad_type = valid_state();
  bad_type["media"]["source"]["type"] = "video";
  SM_CHECK(!sm::validate_state_json(bad_type.dump()).ok);

  // pos_ms 缺失 / 非整数 → 拒绝
  json no_pos = valid_state();
  no_pos["media"].erase("pos_ms");
  SM_CHECK(!sm::validate_state_json(no_pos.dump()).ok);

  json str_pos = valid_state();
  str_pos["media"]["pos_ms"] = "1200";
  SM_CHECK(!sm::validate_state_json(str_pos.dump()).ok);

  // playing 缺失 → 拒绝
  json no_playing = valid_state();
  no_playing["media"].erase("playing");
  SM_CHECK(!sm::validate_state_json(no_playing.dump()).ok);

  // type=none 可缺 media_id；pos_ms=0 合法
  json none_src = valid_state();
  none_src["media"]["source"] = {{"type", "none"}};
  none_src["media"]["pos_ms"] = 0;
  SM_CHECK(sm::validate_state_json(none_src.dump()).ok);

  // 可选数值字段存在但类型错误 → 拒绝
  json bad_opt = valid_state();
  bad_opt["media"]["fade_in_ms"] = "300";
  SM_CHECK(!sm::validate_state_json(bad_opt.dump()).ok);
}

void test_state_devices_must_be_array() {
  json j = valid_state();
  j["devices"] = json::object();
  SM_CHECK(!sm::validate_state_json(j.dump()).ok);

  json ok = valid_state();
  SM_CHECK(sm::validate_state_json(ok.dump()).ok);
}

}  // namespace

int main() {
  test_manifest_rejects_bad_json();
  test_manifest_top_level_must_be_object();
  test_manifest_missing_magic_fields();
  test_manifest_missing_required_rowsets();
  test_manifest_rowsets_must_be_arrays();
  test_manifest_bad_format_or_version();
  test_manifest_project_fields();
  test_manifest_devices_must_be_empty_p1();
  test_manifest_optional_field_type();
  test_manifest_valid_roundtrip();

  test_state_valid_and_forward_compat();
  test_state_rejects_bad_json_or_root();
  test_state_sv_guards();
  test_state_scope_guards();
  test_state_master_guards();
  test_state_media_guards();
  test_state_devices_must_be_array();

  return smtest::finish("test_project");
}

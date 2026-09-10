// P1-5 工程文件（.showproj）容器层单元测试（规格 §8.3 保存 / §8.4 加载）
// 覆盖：zip 容器写出/读回与 CRC-32 完整性、save→load 往返（ref/pack 两种模式）、
// 4xxx 错误码（4001 损坏 / 4002 版本过新 / 4003 打包素材缺失）、
// 保存失败不破坏既有工程、backups/ 只保留最近 5 份。
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/error_codes.h"
#include "core/util.h"
#include "project/project_archive.h"
#include "project/project_file.h"
#include "project/project_zip.h"
#include "test_common.h"

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

// --------------------------------------------------------------------------
// 测试沙箱（沿用 test_util / test_db 的 temp_directory_path 约定）
// --------------------------------------------------------------------------
fs::path sandbox() { return fs::temp_directory_path() / "sm_test_project_zip"; }

void reset_sandbox() {
  std::error_code ec;
  fs::remove_all(sandbox(), ec);
  fs::create_directories(sandbox(), ec);
}

void write_text(const fs::path& p, const std::string& s) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary);
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

json good_manifest(const std::string& mode) {
  json m = sm::make_manifest_skeleton("演示工程", mode, "D:/media", "tester");
  m["items"] = json::array({{{"item_id", "i1"}, {"name", "片头"}}});
  return m;
}

// --------------------------------------------------------------------------
// 1. CRC-32 标准向量 + zip 容器往返
// --------------------------------------------------------------------------
void test_crc32_standard_vector() {
  // CRC-32("123456789") == 0xCBF43926（IEEE 802.3 标准测试向量，与 zip 规范一致）
  const std::string s = "123456789";
  SM_CHECK_EQ(sm::crc32_bytes(s.data(), s.size()), 0xCBF43926u);
  SM_CHECK_EQ(sm::crc32_bytes("", 0), 0u);
}

void test_zip_roundtrip() {
  const fs::path p = sandbox() / "rt.zip";
  const std::string bin("\x00\x01\x02\xFF\xFE", 5);
  std::vector<sm::ZipEntry> in = {
      {"manifest.json", "{\"a\":1}"},
      {"media/0123456789abcdef_clip.mp4", bin},
      {"empty.txt", ""},
  };
  std::string err;
  SM_CHECK(sm::zip_write_file(p.string(), in, err));
  SM_CHECK_MSG(err.empty(), ("写出失败: " + err).c_str());

  std::vector<sm::ZipReadEntry> out;
  SM_CHECK(sm::zip_read_file(p.string(), out, err));
  SM_CHECK_EQ(out.size(), static_cast<std::size_t>(3));
  SM_CHECK_EQ(out[0].name, std::string("manifest.json"));
  SM_CHECK_EQ(out[0].data, std::string("{\"a\":1}"));
  SM_CHECK_EQ(out[1].name, std::string("media/0123456789abcdef_clip.mp4"));
  SM_CHECK_EQ(out[1].data, bin);
  SM_CHECK_EQ(out[2].data, std::string(""));
  SM_CHECK_EQ(out[1].crc32, sm::crc32_bytes(bin.data(), bin.size()));
}

void test_zip_rejects_corrupt_packages() {
  const fs::path p = sandbox() / "corrupt.zip";
  std::vector<sm::ZipEntry> in = {{"a.txt", "hello world"}};
  std::string err;
  SM_CHECK(sm::zip_write_file(p.string(), in, err));
  const std::string bytes = read_text(p);

  std::vector<sm::ZipReadEntry> out;

  // 截断（EOCD 丢失）
  SM_CHECK(!sm::zip_read_bytes(bytes.substr(0, bytes.size() / 2), out, err));
  SM_CHECK_MSG(!err.empty(), "截断包必须给出原因");

  // 本地文件头签名被破坏
  std::string bad_sig = bytes;
  bad_sig[0] = '\x00';
  SM_CHECK(!sm::zip_read_bytes(bad_sig, out, err));

  // 数据字节被改写 → CRC-32 必须报错（"a.txt" 名字长 5：数据起始 = 30 + 5 + 0）
  std::string bad_crc = bytes;
  bad_crc[35] = static_cast<char>(bad_crc[35] ^ 0x01);
  SM_CHECK(!sm::zip_read_bytes(bad_crc, out, err));
  SM_CHECK_MSG(err.find("CRC-32") != std::string::npos, ("应报 CRC-32 失败，实际: " + err).c_str());

  // 完全不是 zip
  SM_CHECK(!sm::zip_read_bytes("this is not a zip at all...........", out, err));
}

// --------------------------------------------------------------------------
// 2. save → load 往返（ref 模式）
// --------------------------------------------------------------------------
void test_save_load_ref_roundtrip() {
  const fs::path p = sandbox() / "proj" / "demo.showproj";

  sm::SaveProjectOptions opt;
  opt.path = p.string();
  opt.manifest = good_manifest("ref");

  const sm::ProjectResult sr = sm::save_project(opt);
  SM_CHECK_MSG(sr.ok, ("保存失败: " + sr.message).c_str());
  SM_CHECK_EQ(sr.code, sm::ec::OK);
  SM_CHECK(fs::exists(p));
  SM_CHECK_MSG(!fs::exists(p.string() + ".tmp"), "落盘后不应残留 .tmp");

  const sm::ProjectLoadResult lr = sm::load_project(p.string());
  SM_CHECK_MSG(lr.ok, ("加载失败: " + lr.message).c_str());
  SM_CHECK_EQ(lr.code, sm::ec::OK);
  SM_CHECK_EQ(lr.manifest["project"]["name"], std::string("演示工程"));
  SM_CHECK_EQ(lr.manifest["project"]["save_mode"], std::string("ref"));
  SM_CHECK(lr.manifest["saved_at"].is_string());
  SM_CHECK(!lr.manifest["saved_at"].get<std::string>().empty());
  SM_CHECK_EQ(lr.manifest["items"].size(), static_cast<std::size_t>(1));
  SM_CHECK_EQ(lr.entry_names.size(), static_cast<std::size_t>(1));
  SM_CHECK_EQ(lr.entry_names[0], std::string("manifest.json"));
}

// --------------------------------------------------------------------------
// 3. save → load 往返（pack 模式：素材入库 + media_files 记录）
// --------------------------------------------------------------------------
void test_save_load_pack_roundtrip() {
  reset_sandbox();
  const fs::path media = sandbox() / "src" / "片头.mp4";
  const std::string payload = std::string("FAKE-MP4-PAYLOAD-") + std::string(512, 'x');
  write_text(media, payload);

  const std::string hex = sm::sha256_hex(payload);
  const std::string expect_entry = "media/" + sm::pack_media_name(hex, "片头.mp4");

  const fs::path p = sandbox() / "pack" / "demo.showproj";
  sm::SaveProjectOptions opt;
  opt.path = p.string();
  opt.manifest = good_manifest("pack");
  opt.pack_files = {media.string()};

  const sm::ProjectResult sr = sm::save_project(opt);
  SM_CHECK_MSG(sr.ok, ("pack 保存失败: " + sr.message).c_str());

  // 包内应有 manifest.json + 素材两件，且素材取自真实文件字节
  std::vector<sm::ZipReadEntry> entries;
  std::string err;
  SM_CHECK(sm::zip_read_file(p.string(), entries, err));
  SM_CHECK_EQ(entries.size(), static_cast<std::size_t>(2));
  SM_CHECK_EQ(entries[1].name, expect_entry);
  SM_CHECK_EQ(entries[1].data, payload);
  SM_CHECK_EQ(sm::sha256_hex(entries[1].data), hex);

  const sm::ProjectLoadResult lr = sm::load_project(p.string());
  SM_CHECK_MSG(lr.ok, ("pack 加载失败: " + lr.message).c_str());
  SM_CHECK(lr.manifest.contains("media_files"));
  SM_CHECK_EQ(lr.manifest["media_files"].size(), static_cast<std::size_t>(1));
  SM_CHECK_EQ(lr.manifest["media_files"][0]["packed"], expect_entry);
  SM_CHECK_EQ(lr.manifest["media_files"][0]["sha256"], hex);
  SM_CHECK_EQ(lr.entry_names.size(), static_cast<std::size_t>(2));

  // 幂等：同素材再存一次，条目不重复
  const sm::ProjectResult sr2 = sm::save_project(opt);
  SM_CHECK(sr2.ok);
  SM_CHECK(sm::zip_read_file(p.string(), entries, err));
  SM_CHECK_EQ(entries.size(), static_cast<std::size_t>(2));
  SM_CHECK(fs::exists(sandbox() / "pack" / "backups"));
}

// --------------------------------------------------------------------------
// 4. 加载错误码：4001 / 4002 / 4003
// --------------------------------------------------------------------------
void test_load_error_codes() {
  // 4001：不是 zip
  const fs::path notzip = sandbox() / "notzip.showproj";
  write_text(notzip, "plain text, definitely not a zip container");
  sm::ProjectLoadResult r = sm::load_project(notzip.string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
  SM_CHECK(r.manifest.is_object() && r.manifest.empty());  // 失败不产生半加载状态

  // 4001：包内没有 manifest.json
  const fs::path nomf = sandbox() / "nomanifest.showproj";
  std::string err;
  SM_CHECK(sm::zip_write_file(nomf.string(), {{"media/x.bin", "z"}}, err));
  r = sm::load_project(nomf.string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
  SM_CHECK_MSG(r.message.find("manifest.json") != std::string::npos, r.message.c_str());

  // 4001：manifest JSON 非法
  const fs::path badjson = sandbox() / "badjson.showproj";
  SM_CHECK(sm::zip_write_file(badjson.string(), {{"manifest.json", "{ oops"}}, err));
  r = sm::load_project(badjson.string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);

  // 4002：版本过新
  json tooNew = good_manifest("ref");
  tooNew["version"] = sm::kShowprojFormat + 1;
  const fs::path v2 = sandbox() / "toonew.showproj";
  SM_CHECK(sm::zip_write_file(v2.string(), {{"manifest.json", tooNew.dump()}}, err));
  r = sm::load_project(v2.string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_VERSION_TOO_NEW);

  // 4003：pack 模式引用的素材不在包内
  json missing = good_manifest("pack");
  missing["items"][0]["media_path"] = "media/deadbeef00000000_ghost.mp4";
  const fs::path miss = sandbox() / "missing.showproj";
  SM_CHECK(sm::zip_write_file(miss.string(), {{"manifest.json", missing.dump()}}, err));
  r = sm::load_project(miss.string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PACKED_MEDIA_MISSING);
  SM_CHECK_MSG(r.message.find("media/deadbeef00000000_ghost.mp4") != std::string::npos,
               r.message.c_str());

  // 4003：ref 模式不校验包内素材（外部引用），但 manifest 结构错误仍是 4001
  json refBadMode = good_manifest("ref");
  refBadMode["project"]["save_mode"] = "copy";
  const fs::path badmode = sandbox() / "badmode.showproj";
  SM_CHECK(sm::zip_write_file(badmode.string(), {{"manifest.json", refBadMode.dump()}}, err));
  r = sm::load_project(badmode.string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
}

void test_load_missing_file() {
  const sm::ProjectLoadResult r = sm::load_project((sandbox() / "nope.showproj").string());
  SM_CHECK(!r.ok);
  SM_CHECK_EQ(r.code, sm::ec::PROJECT_CORRUPT);
  SM_CHECK(r.entry_names.empty());
}

// --------------------------------------------------------------------------
// 5. 保存侧防御：非法 manifest 拒绝、失败不破坏既有工程、backups 轮转
// --------------------------------------------------------------------------
void test_save_rejects_bad_manifest() {
  const fs::path p = sandbox() / "guard" / "demo.showproj";
  sm::SaveProjectOptions opt;
  opt.path = p.string();
  opt.manifest = good_manifest("ref");
  SM_CHECK(sm::save_project(opt).ok);
  const std::string first = read_text(p);

  // save_mode 非法 → 4001，且原工程与 .tmp 均不受影响
  opt.manifest = good_manifest("ref");
  opt.manifest["project"]["save_mode"] = "copy";
  const sm::ProjectResult bad = sm::save_project(opt);
  SM_CHECK(!bad.ok);
  SM_CHECK_EQ(bad.code, sm::ec::PROJECT_CORRUPT);
  SM_CHECK_EQ(read_text(p), first);                              // 原文件逐字节未变
  SM_CHECK_MSG(!fs::exists(p.string() + ".tmp"), "失败路径不得残留 .tmp");
  SM_CHECK(!fs::exists(fs::path(p).parent_path() / "backups"));  // 未走到备份阶段

  // manifest 不是对象 → 4001
  opt.manifest = json::array({1, 2});
  SM_CHECK_EQ(sm::save_project(opt).code, sm::ec::PROJECT_CORRUPT);

  // pack 模式素材不存在 → 4003
  opt.manifest = good_manifest("pack");
  opt.pack_files = {(sandbox() / "ghost.mp4").string()};
  SM_CHECK_EQ(sm::save_project(opt).code, sm::ec::PACKED_MEDIA_MISSING);
}

void test_backup_rotation_keeps_five() {
  const fs::path p = sandbox() / "rotate" / "demo.showproj";
  sm::SaveProjectOptions opt;
  opt.path = p.string();

  for (int i = 0; i < 7; ++i) {
    opt.manifest = good_manifest("ref");
    opt.manifest["items"] = json::array({{{"item_id", "i" + std::to_string(i)}}});
    const sm::ProjectResult r = sm::save_project(opt);
    SM_CHECK_MSG(r.ok, ("第 " + std::to_string(i + 1) + " 次保存失败: " + r.message).c_str());
  }

  const fs::path bdir = sandbox() / "rotate" / "backups";
  SM_CHECK(fs::exists(bdir));
  std::size_t count = 0;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(bdir, ec)) {
    if (e.path().extension() == ".showproj") ++count;
  }
  SM_CHECK_EQ(count, static_cast<std::size_t>(5));  // §8.3 保留最近 5 份

  // 最新工程仍可加载
  const sm::ProjectLoadResult lr = sm::load_project(p.string());
  SM_CHECK(lr.ok);
  SM_CHECK_EQ(lr.manifest["items"][0]["item_id"], std::string("i6"));
}

// --------------------------------------------------------------------------
// 6. 素材引用收集（嵌套 + 去重 + sha256 优先）
// --------------------------------------------------------------------------
void test_collect_media_refs() {
  json doc = good_manifest("pack");
  doc["items"] = json::array({
      {{"item_id", "i1"}, {"media_path", "media/aaa_1.mp4"}},
      {{"item_id", "i2"}, {"nested", {{"deep", "media/bbb_2.mp4"}}}},
  });
  doc["scenes"] = json::array({{{"state_json", {{"layers", json::array({{{"src", "media/aaa_1.mp4"}}})}}}}});
  doc["media_files"] = json::array({
      {{"packed", "media/aaa_1.mp4"}, {"sha256", std::string(64, 'a')}},
  });
  // 非 media/ 前缀的字符串不应被收集
  doc["settings"] = {{"note", "D:/media/aaa_1.mp4"}, {"other", "/abs/media/x.mp4"}};

  const std::vector<sm::MediaRef> refs = sm::collect_media_refs(doc);
  SM_CHECK_EQ(refs.size(), static_cast<std::size_t>(2));  // 去重后 2 条
  SM_CHECK_EQ(refs[0].path, std::string("media/aaa_1.mp4"));
  SM_CHECK_EQ(refs[0].sha256.size(), static_cast<std::size_t>(64));  // 记录里的 sha256 优先
  SM_CHECK_EQ(refs[1].path, std::string("media/bbb_2.mp4"));
  SM_CHECK(refs[1].sha256.empty());
}

// --------------------------------------------------------------------------
// 7. 打包素材内容被篡改 → 4001（SHA-256 不匹配）
// --------------------------------------------------------------------------
void test_pack_media_hash_mismatch() {
  reset_sandbox();
  const fs::path src = sandbox() / "clip.mp4";
  write_text(src, "ORIGINAL-CONTENT");

  const fs::path p = sandbox() / "tamper" / "t.showproj";
  sm::SaveProjectOptions opt;
  opt.path = p.string();
  opt.manifest = good_manifest("pack");
  opt.pack_files = {src.string()};
  const sm::ProjectResult sr = sm::save_project(opt);
  SM_CHECK_MSG(sr.ok, ("保存失败: " + sr.message).c_str());

  // 读出现有包，把素材条目内容换掉但保留中央目录里的旧 sha256 记录 → 加载必须报 4001
  std::vector<sm::ZipReadEntry> entries;
  std::string err;
  SM_CHECK(sm::zip_read_file(p.string(), entries, err));
  SM_CHECK_EQ(entries.size(), static_cast<std::size_t>(2));

  std::vector<sm::ZipEntry> tampered;
  tampered.push_back({"manifest.json", entries[0].data});
  tampered.push_back({entries[1].name, "TAMPERED-CONTENT"});
  const fs::path p2 = sandbox() / "tamper" / "t2.showproj";
  SM_CHECK(sm::zip_write_file(p2.string(), tampered, err));

  const sm::ProjectLoadResult lr = sm::load_project(p2.string());
  SM_CHECK(!lr.ok);
  SM_CHECK_EQ(lr.code, sm::ec::PROJECT_CORRUPT);
  SM_CHECK_MSG(lr.message.find("SHA-256") != std::string::npos, lr.message.c_str());
}

}  // namespace

int main() {
  reset_sandbox();

  test_crc32_standard_vector();
  test_zip_roundtrip();
  test_zip_rejects_corrupt_packages();
  test_save_load_ref_roundtrip();
  test_save_load_pack_roundtrip();
  test_load_error_codes();
  test_load_missing_file();
  test_save_rejects_bad_manifest();
  test_backup_rotation_keeps_five();
  test_collect_media_refs();
  test_pack_media_hash_mismatch();

  std::error_code ec;
  fs::remove_all(sandbox(), ec);
  return smtest::finish("test_project_zip");
}

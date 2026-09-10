#include "project/project_archive.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/error_codes.h"
#include "core/util.h"
#include "project/project_file.h"
#include "project/project_zip.h"

namespace fs = std::filesystem;

namespace sm {

namespace {

constexpr const char* kManifestEntry = "manifest.json";
constexpr const char* kMediaPrefix = "media/";
constexpr const char* kBackupDir = "backups";

ProjectResult fail(int code, std::string msg) { return {false, code, std::move(msg)}; }

bool starts_with(const std::string& s, const std::string& pre) {
  return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}

bool ends_with(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// ISO8601 → 纯数字串，用作备份文件名后缀（Windows 文件名不允许 ':'）
std::string compact_stamp(const std::string& iso) {
  std::string out;
  for (char c : iso) {
    if (c >= '0' && c <= '9') out.push_back(c);
  }
  if (out.empty()) out = "0";
  return out;
}

std::string file_name_of(const std::string& p) {
  const std::string name = fs::path(p).filename().string();
  return name.empty() ? p : name;
}

// ---- manifest → 包内素材引用（递归）----
void walk_refs(const nlohmann::json& v, std::vector<MediaRef>& out) {
  if (v.is_string()) {
    const std::string s = v.get<std::string>();
    if (starts_with(s, kMediaPrefix)) out.push_back(MediaRef{s, ""});
    return;
  }
  if (v.is_object()) {
    std::string packed;
    for (const char* key : {"packed", "packed_path"}) {
      const auto it = v.find(key);
      if (it != v.end() && it->is_string()) {
        packed = it->get<std::string>();
        break;
      }
    }
    std::string sha;
    const auto sit = v.find("sha256");
    if (sit != v.end() && sit->is_string()) sha = sit->get<std::string>();
    if (!packed.empty() && starts_with(packed, kMediaPrefix)) out.push_back(MediaRef{packed, sha});
    for (auto it = v.begin(); it != v.end(); ++it) walk_refs(it.value(), out);
    return;
  }
  if (v.is_array()) {
    for (const auto& e : v) walk_refs(e, out);
  }
}

// pack 模式校验：引用素材必须在包内（4003）+ SHA-256 必须吻合（4001）
ProjectResult verify_pack_refs(const nlohmann::json& doc,
                               const std::vector<ZipReadEntry>& entries) {
  for (const auto& r : collect_media_refs(doc)) {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&r](const ZipReadEntry& e) { return e.name == r.path; });
    if (it == entries.end()) return fail(ec::PACKED_MEDIA_MISSING, "打包素材缺失: " + r.path);
    if (!r.sha256.empty() && sha256_hex(it->data) != r.sha256) {
      return fail(ec::PROJECT_CORRUPT, "打包素材 SHA-256 不匹配: " + r.path);
    }
  }
  return {true, ec::OK, ""};
}

// backups/ 轮转：按修改时间保留最新 keep 份，其余删除
void rotate_backups(const fs::path& bdir, const std::string& stem, int keep) {
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> items;
  fs::directory_iterator it(bdir, ec);
  const fs::directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code fec;
    if (!it->is_regular_file(fec)) continue;
    const std::string name = it->path().filename().string();
    if (!starts_with(name, stem + ".") || !ends_with(name, ".showproj")) continue;
    items.emplace_back(it->last_write_time(fec), it->path());
  }
  if (items.size() <= static_cast<std::size_t>(keep)) return;
  std::sort(items.begin(), items.end(),
            [](const std::pair<fs::file_time_type, fs::path>& a,
               const std::pair<fs::file_time_type, fs::path>& b) { return a.first > b.first; });
  for (std::size_t i = static_cast<std::size_t>(keep); i < items.size(); ++i) {
    std::error_code ign;
    fs::remove(items[i].second, ign);
  }
}

}  // namespace

std::vector<MediaRef> collect_media_refs(const nlohmann::json& manifest) {
  std::vector<MediaRef> raw;
  walk_refs(manifest, raw);

  std::vector<MediaRef> out;
  for (const auto& r : raw) {
    auto it = std::find_if(out.begin(), out.end(),
                           [&r](const MediaRef& x) { return x.path == r.path; });
    if (it == out.end()) {
      out.push_back(r);
      continue;
    }
    if (it->sha256.empty() && !r.sha256.empty()) it->sha256 = r.sha256;
  }
  std::sort(out.begin(), out.end(),
            [](const MediaRef& a, const MediaRef& b) { return a.path < b.path; });
  return out;
}

ProjectResult save_project(const SaveProjectOptions& opt) {
  if (opt.path.empty()) return fail(ec::BAD_PARAM, "保存路径为空");
  if (!opt.manifest.is_object()) return fail(ec::PROJECT_CORRUPT, "manifest 必须是 JSON 对象");

  const fs::path target(opt.path);
  const fs::path parent = target.parent_path();
  std::error_code ec;
  if (!parent.empty()) {
    fs::create_directories(parent, ec);
    if (ec) return fail(ec::BAD_PARAM, "无法创建目录: " + parent.string() + " (" + ec.message() + ")");
  }

  nlohmann::json m = opt.manifest;
  if (!m.contains("project") || !m["project"].is_object())
    return fail(ec::PROJECT_CORRUPT, "manifest 缺少 project 对象");
  const std::string mode = m["project"].value("save_mode", std::string());
  if (mode != "ref" && mode != "pack")
    return fail(ec::PROJECT_CORRUPT, "project.save_mode 仅允许 ref|pack");

  std::vector<ZipEntry> entries;

  // ---- 1) pack 模式：源素材入库 media/<sha256前16位>_<原名> ----
  if (mode == "pack") {
    std::map<std::string, nlohmann::json> records;  // packed 路径 → 记录
    if (m.contains("media_files") && m["media_files"].is_array()) {
      for (const auto& r : m["media_files"]) {
        if (r.is_object() && r.contains("packed") && r["packed"].is_string()) {
          records[r["packed"].get<std::string>()] = r;
        }
      }
    }

    for (const auto& src : opt.pack_files) {
      std::string hex;
      std::string err;
      if (!sha256_file_hex(src, hex, err))
        return fail(ec::PACKED_MEDIA_MISSING, "素材不可读，无法打包: " + src + " (" + err + ")");

      std::string bytes;
      if (!read_file_bytes(src, bytes, err))
        return fail(ec::PACKED_MEDIA_MISSING, "素材不可读，无法打包: " + src + " (" + err + ")");

      const std::string orig = file_name_of(src);
      const std::string packed = std::string(kMediaPrefix) + pack_media_name(hex, orig);

      const auto dup = std::find_if(entries.begin(), entries.end(),
                                    [&packed](const ZipEntry& e) { return e.name == packed; });
      if (dup != entries.end()) {
        if (crc32_bytes(dup->data.data(), dup->data.size()) !=
            crc32_bytes(bytes.data(), bytes.size())) {
          return fail(ec::PROJECT_CORRUPT, "包内命名冲突（哈希前 16 位相同但内容不同）: " + packed);
        }
        continue;  // 同内容重复打包，幂等跳过
      }

      entries.push_back(ZipEntry{packed, bytes});
      records[packed] = nlohmann::json{{"packed", packed},
                                       {"sha256", hex},
                                       {"original_name", orig},
                                       {"size", static_cast<std::uint64_t>(bytes.size())}};
    }

    nlohmann::json mf = nlohmann::json::array();
    for (const auto& kv : records) mf.push_back(kv.second);  // std::map ⇒ 按路径有序
    m["media_files"] = std::move(mf);
  }

  // ---- 2) 行集序列化 + 语义校验 ----
  m["saved_at"] = iso8601_now();
  const std::string manifest_text = m.dump(2);
  const ManifestResult vr = validate_manifest(manifest_text);
  if (!vr.ok) return fail(vr.code, vr.message);

  entries.insert(entries.begin(), ZipEntry{kManifestEntry, manifest_text});

  // ---- 3) 写 .tmp ----
  const std::string tmp = opt.path + ".tmp";
  std::string err;
  if (!zip_write_file(tmp, entries, err)) {
    std::error_code ign;
    fs::remove(tmp, ign);
    return fail(ec::BAD_PARAM, "写临时包失败: " + err);
  }

  // ---- 4) 复读校验包完整性（zip 层逐条 CRC + manifest 再校验 + pack 引用核对）----
  std::vector<ZipReadEntry> back;
  if (!zip_read_file(tmp, back, err) || back.size() != entries.size()) {
    std::error_code ign;
    fs::remove(tmp, ign);
    return fail(ec::PROJECT_CORRUPT,
                "包完整性复校失败: " + (err.empty() ? std::string("条目数不一致") : err));
  }
  bool manifest_seen = false;
  for (const auto& e : back) {
    if (e.name != kManifestEntry) continue;
    manifest_seen = true;
    const ManifestResult r2 = validate_manifest(e.data);
    if (!r2.ok) {
      std::error_code ign;
      fs::remove(tmp, ign);
      return fail(r2.code, "写入后 manifest 复校失败: " + r2.message);
    }
  }
  if (!manifest_seen) {
    std::error_code ign;
    fs::remove(tmp, ign);
    return fail(ec::PROJECT_CORRUPT, "包内缺少 manifest.json");
  }
  if (mode == "pack") {
    const ProjectResult pr = verify_pack_refs(m, back);
    if (!pr.ok) {
      std::error_code ign;
      fs::remove(tmp, ign);
      return pr;
    }
  }

  // ---- 5) 旧文件轮转 + 原子改名 ----
  if (fs::exists(target, ec)) {
    if (opt.backup_keep > 0) {
      const fs::path bdir = (parent.empty() ? fs::path(".") : parent) / kBackupDir;
      fs::create_directories(bdir, ec);
      if (ec) {
        std::error_code ign;
        fs::remove(tmp, ign);
        return fail(ec::BAD_PARAM, "无法创建备份目录: " + bdir.string() + " (" + ec.message() + ")");
      }
      const std::string stem = target.stem().string();
      const std::string stamp = compact_stamp(iso8601_now());
      fs::path bpath = bdir / (stem + "." + stamp + ".showproj");
      for (int n = 1; fs::exists(bpath, ec) && n <= 1000; ++n) {
        bpath = bdir / (stem + "." + stamp + "-" + std::to_string(n) + ".showproj");
      }
      fs::rename(target, bpath, ec);
      if (ec) {
        std::error_code ign;
        fs::remove(tmp, ign);
        return fail(ec::BAD_PARAM, "旧工程备份失败: " + ec.message());
      }
      rotate_backups(bdir, stem, opt.backup_keep);
    } else {
      fs::remove(target, ec);
    }
  }

  fs::rename(tmp, target, ec);
  if (ec) {
    // 跨卷等不能原子改名的场景：退化为复制 + 删除（仍在 tmp 校验通过之后）
    std::error_code ec2;
    fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec2);
    std::error_code ign;
    fs::remove(tmp, ign);
    if (ec2) return fail(ec::BAD_PARAM, "落盘失败: " + ec2.message());
  }

  return {true, ec::OK, ""};
}

ProjectLoadResult load_project(const std::string& path) {
  ProjectLoadResult res;
  if (path.empty()) {
    res.code = ec::BAD_PARAM;
    res.message = "路径为空";
    return res;
  }

  std::vector<ZipReadEntry> entries;
  std::string err;
  if (!zip_read_file(path, entries, err)) {
    res.code = ec::PROJECT_CORRUPT;
    res.message = "工程损坏（zip 容器校验失败）: " + err;
    return res;
  }
  res.entry_names.reserve(entries.size());
  for (const auto& e : entries) res.entry_names.push_back(e.name);

  const ZipReadEntry* mf = nullptr;
  for (const auto& e : entries) {
    if (e.name == kManifestEntry) {
      mf = &e;
      break;
    }
  }
  if (mf == nullptr) {
    res.code = ec::PROJECT_CORRUPT;
    res.message = "工程损坏（包内缺少 manifest.json）";
    return res;
  }

  const ManifestResult vr = validate_manifest(mf->data);
  if (!vr.ok) {
    res.code = vr.code;
    res.message = vr.message;
    return res;
  }

  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(mf->data);
  } catch (const nlohmann::json::exception& ex) {
    res.code = ec::PROJECT_CORRUPT;
    res.message = std::string("工程损坏: ") + ex.what();
    return res;
  }

  if (doc["project"].value("save_mode", std::string()) == "pack") {
    const ProjectResult pr = verify_pack_refs(doc, entries);
    if (!pr.ok) {
      res.code = pr.code;
      res.message = pr.message;
      return res;
    }
  }

  // 全有或全无：只有走到这里才交付 manifest，失败路径不产生半加载状态
  res.ok = true;
  res.code = ec::OK;
  res.manifest = std::move(doc);
  return res;
}

}  // namespace sm

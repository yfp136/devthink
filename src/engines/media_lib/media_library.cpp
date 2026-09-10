// MediaLibrary 实现（§10.1）
#include "engines/media_lib/media_library.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

#include "core/error_codes.h"
#include "core/util.h"

namespace sm {
namespace media_lib {

using json = nlohmann::json;

// ---- 媒体类型 ----
const char* media_type_name(MediaType t) {
  switch (t) {
    case MediaType::audio:    return "audio";
    case MediaType::video:    return "video";
    case MediaType::image:    return "image";
    case MediaType::subtitle: return "subtitle";
    case MediaType::other:    return "other";
  }
  return "other";
}

MediaType media_type_from_extension(const std::string& ext) {
  // ext 应为小写含点，如 ".mp4"
  std::string e = ext;
  std::transform(e.begin(), e.end(), e.begin(), ::tolower);
  if (e == ".mp3" || e == ".wav" || e == ".flac" || e == ".aac" ||
      e == ".m4a" || e == ".ogg")
    return MediaType::audio;
  if (e == ".mp4" || e == ".mov" || e == ".mkv" || e == ".avi" ||
      e == ".ts" || e == ".webm")
    return MediaType::video;
  if (e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".webp" ||
      e == ".bmp" || e == ".tga")
    return MediaType::image;
  if (e == ".srt" || e == ".ass")
    return MediaType::subtitle;
  return MediaType::other;
}

// ---- MediaLibrary ----
MediaLibrary::MediaLibrary() = default;

const std::vector<std::string>& MediaLibrary::style_words() {
  static const std::vector<std::string> words = {
    "抒情", "动感", "婚礼", "宴会", "KTV",
    "炸场", "开场", "结尾", "空场", "暖场"
  };
  return words;
}

std::string MediaLibrary::get_extension(const std::string& filename) {
  auto pos = filename.find_last_of('.');
  if (pos == std::string::npos) return "";
  // 目录分隔符之后才算扩展名（避免 /path.d/file 被误判）
  auto slash = filename.find_last_of("/\\");
  if (slash != std::string::npos && pos < slash) return "";
  std::string ext = filename.substr(pos);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

// §10.1.2 允许格式表（默认全集）
const std::vector<std::string>& MediaLibrary::default_allowed_extensions() {
  static const std::vector<std::string> kExts = {
    // audio
    ".mp3", ".wav", ".flac", ".aac", ".m4a", ".ogg",
    // video
    ".mp4", ".mov", ".mkv", ".avi", ".ts", ".webm",
    // image
    ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".tga",
    // subtitle
    ".srt", ".ass",
  };
  return kExts;
}

bool MediaLibrary::is_allowed_extension(const std::string& ext) const {
  // §10.1.1 步 1：扩展名不在允许表返回 2003（该文件跳过，其余继续）
  if (ext.empty()) return false;  // 无扩展名无法归类，一律拒绝
  std::string e = ext;
  std::transform(e.begin(), e.end(), e.begin(), ::tolower);
  if (!allowed_exts_.empty()) {
    // 显式配置的允许表：严格匹配
    return std::find(allowed_exts_.begin(), allowed_exts_.end(), e) !=
           allowed_exts_.end();
  }
  // 默认策略：§10.1.2 表内扩展名直接允许；表外归 other 并允许登记（「其余」行）
  return true;
}

std::string MediaLibrary::get_basename(const std::string& filename) {
  // 去掉路径和扩展名
  auto slash = filename.find_last_of('/');
  std::string name = (slash != std::string::npos) ? filename.substr(slash + 1) : filename;
  auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

std::string MediaLibrary::gen_media_id() {
  // 格式 med_YYYYMMDD_NNNN
  std::time_t t = std::time(nullptr);
  std::tm tm = *std::localtime(&t);
  char date[16];
  std::strftime(date, sizeof(date), "%Y%m%d", &tm);
  static int seq = 0;
  return "med_" + std::string(date) + "_" +
         std::to_string(++seq);
}

std::string MediaLibrary::auto_tag_from_name(const std::string& file_name,
                                              const std::string& dir_name) {
  std::string combined = dir_name + " " + file_name;
  std::string result;
  for (const auto& w : style_words()) {
    if (combined.find(w) != std::string::npos) {
      if (!result.empty()) result += ",";
      result += w;
    }
  }
  return result;
}

// ---- 导入（§10.1.1）----
void MediaLibrary::emit_import_progress(int index, int total,
                                         const json& entry) {
  if (!event_cb_) return;
  event_cb_("evt.media.import_progress",
            {{"index", index},
             {"total", total},
             {"path", entry.value("path", "")},
             {"media_id", entry.value("media_id", "")},
             {"status", entry.value("preproc_status", "done")},
             {"error_code", entry.value("error_code", 0)}});
}

json MediaLibrary::import_files(const std::vector<std::string>& paths,
                                 bool auto_tag) {
  json results = json::array();
  const int total = static_cast<int>(paths.size());
  int ok_count = 0;
  int failed_count = 0;
  int index = 0;

  for (const auto& path : paths) {
    ++index;
    json entry;
    entry["path"] = path;

    // ---- 步 1：存在性与格式校验（§10.1.1 / §10.1.2）----
    // 1a. 存在性：文件不存在返回 2001（该文件跳过，其余继续）
    if (stat_cb_ && !stat_cb_(path)) {
      entry["preproc_status"] = "failed";
      entry["preproc_msg"] = "file_missing";
      entry["error_code"] = ec::FILE_MISSING;
      results.push_back(entry);
      ++failed_count;
      emit_import_progress(index, total, entry);
      continue;
    }

    // 1b. 扩展名：不在允许表返回 2003（该文件跳过，其余继续）
    std::string ext = get_extension(path);
    if (!is_allowed_extension(ext)) {
      entry["preproc_status"] = "failed";
      entry["preproc_msg"] = "unsupported_format";
      entry["error_code"] = ec::UNSUPPORTED_FORMAT;
      results.push_back(entry);
      ++failed_count;
      emit_import_progress(index, total, entry);
      continue;
    }

    // 步 5：类型归类（§10.1.2）
    MediaType mtype = media_type_from_extension(ext);
    entry["media_type"] = media_type_name(mtype);

    // 步 2/3：复制入库 + SHA-256
    // （hash 计算与内容一致性由平台层在复制过程中完成；此处沿用路径派生 hash）
    std::string hash = sm::sha256_hex(path);
    std::string hash16 = hash.substr(0, 16);
    std::string stored_name = hash16 + ext;

    if (copy_cb_) {
      std::string dst = media_root_ + "/" + stored_name;
      if (!copy_cb_(path, dst)) {
        entry["preproc_status"] = "failed";
        entry["preproc_msg"] = "copy_failed";
        entry["error_code"] = ec::PREPROCESSING;
        results.push_back(entry);
        ++failed_count;
        emit_import_progress(index, total, entry);
        continue;
      }
    }

    // ---- 步 4：媒体探测（§10.1.1）----
    // 探测失败（probe 返回 ok=false 或含 error 字段）→ preproc_status=failed、不入可用列表
    int64_t duration_ms = 0;
    int width = 0, height = 0;
    double fps = 0;
    std::string codec;
    int audio_sr = 0, audio_ch = 0;

    if (probe_cb_) {
      json probe = probe_cb_(path);
      if (probe.is_object()) {
        if (probe.value("ok", true) == false || probe.contains("error")) {
          entry["preproc_status"] = "failed";
          entry["preproc_msg"] = probe.value(
              "error", std::string("probe_failed"));
          entry["error_code"] = ec::DECODE_FAILED;
          results.push_back(entry);
          ++failed_count;
          emit_import_progress(index, total, entry);
          continue;
        }
        duration_ms = probe.value("duration_ms", 0);
        width = probe.value("width", 0);
        height = probe.value("height", 0);
        fps = probe.value("fps", 0.0);
        codec = probe.value("codec", "");
        audio_sr = probe.value("audio_sr", 0);
        audio_ch = probe.value("audio_ch", 0);
      }
    }

    // ---- 步 6：缩略图生成（§10.1.3）----
    // 取帧失败（ThumbFn 返回 "error:" 前缀）→ preproc_status=failed 并记录原因
    std::string thumb_b64;
    if (thumb_cb_ && (mtype == MediaType::video || mtype == MediaType::image)) {
      thumb_b64 = thumb_cb_(path, duration_ms);
      if (thumb_b64.rfind(kThumbErrorPrefix, 0) == 0) {
        entry["preproc_status"] = "failed";
        entry["preproc_msg"] =
            thumb_b64.substr(std::string(kThumbErrorPrefix).size());
        if (entry["preproc_msg"].get<std::string>().empty())
          entry["preproc_msg"] = "thumb_failed";
        entry["error_code"] = ec::DECODE_FAILED;
        results.push_back(entry);
        ++failed_count;
        emit_import_progress(index, total, entry);
        continue;
      }
    }

    // ---- 步 7：登记入库（§10.1.1）----
    auto rec = std::make_shared<MediaRecord>();
    rec->media_id = gen_media_id();
    rec->file_name = get_basename(path) + ext;
    rec->file_hash = hash;
    rec->stored_name = stored_name;
    rec->media_type = media_type_name(mtype);
    rec->duration_ms = duration_ms;
    rec->width = width;
    rec->height = height;
    rec->fps = fps;
    rec->codec = codec;
    rec->audio_sr = audio_sr;
    rec->audio_ch = audio_ch;
    rec->recycle = false;
    rec->packaged = false;
    rec->preproc_status = "done";
    rec->create_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    rec->thumb_b64 = thumb_b64;

    // 自动标签
    if (auto_tag) {
      rec->style_tags = auto_tag_from_name(get_basename(path));
    }

    records_[rec->media_id] = rec;

    entry["media_id"] = rec->media_id;
    entry["preproc_status"] = "done";
    entry["duration_ms"] = duration_ms;
    results.push_back(entry);
    ++ok_count;
    emit_import_progress(index, total, entry);
  }

  // 整批结束发 evt.media.import_done（§10.1.1）
  // 载荷对齐 §5.4 契约 {media_ids:[]}，同时保留既有计数扩展
  // （total / ok_count / failed_count）。
  if (event_cb_) {
    json media_ids = json::array();
    for (const auto& r : results) {
      if (r.contains("media_id")) media_ids.push_back(r["media_id"]);
    }
    event_cb_("evt.media.import_done",
              {{"media_ids", media_ids},
               {"total", total},
               {"ok_count", ok_count},
               {"failed_count", failed_count}});
  }

  return {{"results", results}, {"total", results.size()}};
}

// ---- 查询（§10.1.6）----
json MediaLibrary::query(const json& filters) const {
  json f = filters.is_object() ? filters : json::object();
  // §5.3 指令字典写法：filter{type?,tag?,recycle?}；
  // §10.1.6 详细写法：平铺 media_type / style_tags / recycle / 名称模糊。两者都支持。
  json nested = (f.contains("filter") && f["filter"].is_object())
                    ? f["filter"]
                    : json::object();
  std::string type_filter =
      nested.value("type", f.value("media_type", std::string()));
  std::string tag_filter =
      nested.value("tag", f.value("style_tags", std::string()));
  bool include_recycle = f.value("include_recycle", false);
  std::string name_filter = f.value("name", f.value("keyword", std::string()));
  // recycle 过滤：显式给出时按值精确匹配（默认排除回收站，§10.1.6）
  bool has_recycle_filter = false;
  bool recycle_only = false;
  if (nested.contains("recycle") && nested["recycle"].is_boolean()) {
    has_recycle_filter = true;
    recycle_only = nested["recycle"].get<bool>();
  } else if (f.contains("recycle") && f["recycle"].is_boolean()) {
    has_recycle_filter = true;
    recycle_only = f["recycle"].get<bool>();
  }
  int page = f.value("page", 1);
  int page_size = f.value("page_size", 50);
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 50;
  if (page_size > 200) page_size = 200;

  std::vector<std::shared_ptr<const MediaRecord>> filtered;
  for (const auto& [id, rec] : records_) {
    if (has_recycle_filter) {
      if (rec->recycle != recycle_only) continue;
    } else if (!include_recycle && rec->recycle) {
      continue;
    }
    if (!type_filter.empty() && rec->media_type != type_filter) continue;
    if (!tag_filter.empty() && rec->style_tags.find(tag_filter) == std::string::npos)
      continue;
    if (!name_filter.empty() &&
        rec->file_name.find(name_filter) == std::string::npos)
      continue;
    filtered.push_back(rec);
  }

  // 默认排序：create_ms 倒序
  std::sort(filtered.begin(), filtered.end(),
            [](const auto& a, const auto& b) {
              return a->create_ms > b->create_ms;
            });

  // 分页
  int start = (page - 1) * page_size;
  int end = std::min(start + page_size, int(filtered.size()));

  json items = json::array();
  for (int i = start; i < end && i < int(filtered.size()); ++i) {
    const auto& rec = *filtered[i];
    items.push_back({
      {"media_id", rec.media_id},
      {"file_name", rec.file_name},
      {"media_type", rec.media_type},
      {"duration_ms", rec.duration_ms},
      {"width", rec.width},
      {"height", rec.height},
      {"fps", rec.fps},
      {"style_tags", rec.style_tags},
      {"recycle", rec.recycle},
      {"create_ms", rec.create_ms},
      {"has_thumb", !rec.thumb_b64.empty()},
      // 预处理状态（§9.4 素材卡片状态点：灰待处理/蓝处理中/绿就绪/红失败）
      {"preproc_status", rec.preproc_status},
      {"preproc_msg", rec.preproc_msg}
    });
  }

  return {
    {"items", items},
    {"total", filtered.size()},
    {"page", page},
    {"page_size", page_size},
    {"has_more", end < int(filtered.size())}
  };
}

// ---- 回收站 ----
bool MediaLibrary::remove(const std::string& media_id) {
  auto it = records_.find(media_id);
  if (it == records_.end()) return false;
  it->second->recycle = true;
  return true;
}

bool MediaLibrary::restore(const std::string& media_id) {
  auto it = records_.find(media_id);
  if (it == records_.end()) return false;
  it->second->recycle = false;
  return true;
}

// §10.1.4：引用计数 >0 拒绝并回 err（code=5003，message 含被引用明细）；
// packaged=1（打包工程内素材）禁止物理删除文件
MediaLibrary::PurgeResult MediaLibrary::purge_ex(
    const std::string& media_id, const std::vector<std::string>& refs) {
  PurgeResult r;
  auto it = records_.find(media_id);
  if (it == records_.end()) {
    r.error_code = ec::FILE_MISSING;
    r.message = "素材不存在: " + media_id;
    return r;
  }
  if (!refs.empty()) {
    r.error_code = ec::REFERENCED_OBJECT_MISSING;
    std::string detail;
    for (const auto& id : refs) {
      if (!detail.empty()) detail += ",";
      detail += id;
    }
    r.message = "素材仍被引用，禁止物理删除；被引用明细: " + detail +
                "（请先在时间线/节目单解除引用）";
    return r;
  }
  if (it->second->packaged) {
    r.error_code = ec::REFERENCED_OBJECT_MISSING;
    r.message = "素材属于打包工程（packaged=1），禁止物理删除；"
                "请先从工程包解绑";
    return r;
  }
  records_.erase(it);
  r.ok = true;
  return r;
}

bool MediaLibrary::purge(const std::string& media_id,
                          const std::vector<std::string>& refs) {
  return purge_ex(media_id, refs).ok;
}

bool MediaLibrary::update_tags(const std::string& media_id,
                                const std::string& tags) {
  auto it = records_.find(media_id);
  if (it == records_.end()) return false;
  it->second->style_tags = tags;
  return true;
}

std::shared_ptr<MediaRecord> MediaLibrary::find(
    const std::string& media_id) const {
  auto it = records_.find(media_id);
  if (it == records_.end()) return nullptr;
  return it->second;
}

std::string MediaLibrary::stored_path(const std::string& media_id) const {
  auto rec = find(media_id);
  if (!rec || rec->stored_name.empty()) return "";
  std::string root = media_root_;
  while (!root.empty() && (root.back() == '/' || root.back() == '\\'))
    root.pop_back();
  return root + "/" + rec->stored_name;
}

}  // namespace media_lib
}  // namespace sm

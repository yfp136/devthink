// MediaLibrary 实现（§10.1）
#include "engines/media_lib/media_library.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

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
  return filename.substr(pos);
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
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
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
json MediaLibrary::import_files(const std::vector<std::string>& paths,
                                 bool auto_tag) {
  json results = json::array();

  for (const auto& path : paths) {
    json entry;
    entry["path"] = path;

    // 1. 存在性校验
    // （实际由 copy_cb_ 或文件系统检查，这里简化）

    // 2. 扩展名校验
    std::string ext = get_extension(path);
    MediaType mtype = media_type_from_extension(ext);
    entry["media_type"] = media_type_name(mtype);

    // 3. 计算 SHA-256（这里用文件路径作为输入，实际应读文件）
    std::string hash = sm::sha256_hex(path);
    std::string hash16 = hash.substr(0, 16);
    std::string stored_name = hash16 + ext;

    // 4. 复制入库
    if (copy_cb_) {
      std::string dst = media_root_ + "/" + stored_name;
      if (!copy_cb_(path, dst)) {
        entry["preproc_status"] = "failed";
        entry["preproc_msg"] = "copy_failed";
        results.push_back(entry);
        continue;
      }
    }

    // 5. 媒体探测
    int64_t duration_ms = 0;
    int width = 0, height = 0;
    double fps = 0;
    std::string codec;
    int audio_sr = 0, audio_ch = 0;

    if (probe_cb_) {
      json probe = probe_cb_(path);
      if (probe.is_object()) {
        duration_ms = probe.value("duration_ms", 0);
        width = probe.value("width", 0);
        height = probe.value("height", 0);
        fps = probe.value("fps", 0.0);
        codec = probe.value("codec", "");
        audio_sr = probe.value("audio_sr", 0);
        audio_ch = probe.value("audio_ch", 0);
      }
    }


    // 6. 缩略图
    std::string thumb_b64;
    if (thumb_cb_ && (mtype == MediaType::video || mtype == MediaType::image)) {
      thumb_b64 = thumb_cb_(path, duration_ms);
    }

    // 7. 登记入库
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
  }

  return {{"results", results}, {"total", results.size()}};
}

// ---- 查询（§10.1.6）----
json MediaLibrary::query(const json& filters) const {
  json f = filters.is_object() ? filters : json::object();
  std::string type_filter = f.value("media_type", "");
  std::string tag_filter = f.value("style_tags", "");
  bool include_recycle = f.value("include_recycle", false);
  std::string name_filter = f.value("name", "");
  int page = f.value("page", 1);
  int page_size = f.value("page_size", 50);
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 50;
  if (page_size > 200) page_size = 200;

  std::vector<std::shared_ptr<const MediaRecord>> filtered;
  for (const auto& [id, rec] : records_) {
    if (!include_recycle && rec->recycle) continue;
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
      {"has_thumb", !rec.thumb_b64.empty()}
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

bool MediaLibrary::purge(const std::string& media_id,
                          const std::vector<std::string>& refs) {
  auto it = records_.find(media_id);
  if (it == records_.end()) return false;
  if (!refs.empty()) return false;  // 被引用不能物理删
  records_.erase(it);
  return true;
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

}  // namespace media_lib
}  // namespace sm

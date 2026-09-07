// M1 素材库导入管线（§10.1）
// 职责：把外部媒体文件变为可被时间线引用的媒体库记录。
// 流程：存在性校验 → 复制入库(命名file_hash前16位) → SHA-256 → 类型归类 → 缩略图 → 登记入库
// 回收站：remove=软删(recycle=1)、restore=恢复、purge=物理删(引用保护)
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {
namespace media_lib {

// 媒体类型
enum class MediaType { audio, video, image, subtitle, other };
const char* media_type_name(MediaType t);
MediaType media_type_from_extension(const std::string& ext);

// 媒体记录（对应 media_library 表）
struct MediaRecord {
  std::string media_id;        // med_YYYYMMDD_NNNN
  std::string file_name;       // 原始文件名
  std::string file_hash;      // SHA-256 全文
  std::string stored_name;    // 入库后文件名(hash前16位.ext)
  std::string media_type;     // audio/video/image/subtitle/other
  int64_t duration_ms = 0;
  int width = 0;
  int height = 0;
  double fps = 0;
  std::string codec;
  int audio_sr = 0;
  int audio_ch = 0;
  std::string style_tags;     // 逗号分隔
  bool recycle = false;
  bool packaged = false;
  std::string preproc_status = "done";  // done / failed
  std::string preproc_msg;
  int64_t create_ms = 0;
  std::string thumb_b64;       // 缩略图 JPEG base64（可空）
};

// 平台回调
// 探测媒体信息（duration/width/height/fps/codec/audio_sr/audio_ch）
// 返回 JSON 文本
using ProbeFn = std::function<nlohmann::json(const std::string& file_path)>;
// 生成缩略图（返回 JPEG base64，可空）
using ThumbFn = std::function<std::string(const std::string& file_path,
                                           int64_t duration_ms)>;
// 复制文件回调（返回是否成功）
using CopyFn = std::function<bool(const std::string& src, const std::string& dst)>;

class MediaLibrary {
 public:
  MediaLibrary();

  // 设置回调
  void set_probe_cb(ProbeFn cb) { probe_cb_ = std::move(cb); }
  void set_thumb_cb(ThumbFn cb) { thumb_cb_ = std::move(cb); }
  void set_copy_cb(CopyFn cb) { copy_cb_ = std::move(cb); }
  void set_media_root(const std::string& root) { media_root_ = root; }

  // ---- 导入（§10.1.1 media.import）----
  // 逐文件处理；返回导入结果数组
  nlohmann::json import_files(const std::vector<std::string>& paths,
                               bool auto_tag = false);

  // ---- 查询（§10.1.6 media.query）----
  nlohmann::json query(const nlohmann::json& filters) const;

  // ---- 回收站（§10.1.4）----
  bool remove(const std::string& media_id);   // 软删 recycle=1
  bool restore(const std::string& media_id);  // 恢复 recycle=0
  bool purge(const std::string& media_id,
             const std::vector<std::string>& referencing_ids = {});

  // ---- 标签（§10.1.5）----
  bool update_tags(const std::string& media_id, const std::string& tags);

  // 查询
  std::shared_ptr<MediaRecord> find(const std::string& media_id) const;
  size_t count() const { return records_.size(); }

  // 自动标签匹配（§10.1.5）
  static std::string auto_tag_from_name(const std::string& file_name,
                                         const std::string& dir_name = "");

  // 生成 media_id
  static std::string gen_media_id();

 private:
  std::map<std::string, std::shared_ptr<MediaRecord>> records_;
  ProbeFn probe_cb_;
  ThumbFn thumb_cb_;
  CopyFn copy_cb_;
  std::string media_root_ = "/tmp/showmaster/media";

  // 风格词表
  static const std::vector<std::string>& style_words();

  // 计算文件扩展名（小写，含点）
  static std::string get_extension(const std::string& filename);

  // 从文件名提取不含扩展名的部分
  static std::string get_basename(const std::string& filename);
};

}  // namespace media_lib
}  // namespace sm

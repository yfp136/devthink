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
// 存在性检查回调（返回 true=文件存在）；未设置时跳过该步（便于纯逻辑单测）
using StatFn = std::function<bool(const std::string& file_path)>;
// 导入事件回调（evt 名 + 载荷），用于发 evt.media.import_progress / evt.media.import_done
using EventFn = std::function<void(const std::string& evt,
                                   const nlohmann::json& payload)>;

// 缩略图失败约定：ThumbFn 返回以该前缀开头的字符串表示取帧失败（§10.1.3）
inline constexpr const char* kThumbErrorPrefix = "error:";

class MediaLibrary {
 public:
  MediaLibrary();

  // 设置回调
  void set_probe_cb(ProbeFn cb) { probe_cb_ = std::move(cb); }
  void set_thumb_cb(ThumbFn cb) { thumb_cb_ = std::move(cb); }
  void set_copy_cb(CopyFn cb) { copy_cb_ = std::move(cb); }
  void set_stat_cb(StatFn cb) { stat_cb_ = std::move(cb); }
  void set_event_cb(EventFn cb) { event_cb_ = std::move(cb); }
  void set_media_root(const std::string& root) { media_root_ = root; }
  // 覆写允许扩展名表（小写含点）。为空表示按 §10.1.2 默认策略：
  // 表内扩展名按类型归类；表外归 other 并允许登记（§10.1.2「其余」）
  void set_allowed_extensions(std::vector<std::string> exts) {
    allowed_exts_ = std::move(exts);
  }

  // ---- 导入（§10.1.1 media.import）----
  // 逐文件处理；返回导入结果数组
  nlohmann::json import_files(const std::vector<std::string>& paths,
                               bool auto_tag = false);

  // ---- 查询（§10.1.6 media.query）----
  nlohmann::json query(const nlohmann::json& filters) const;

  // ---- 回收站（§10.1.4）----
  bool remove(const std::string& media_id);   // 软删 recycle=1
  bool restore(const std::string& media_id);  // 恢复 recycle=0

  // purge 结果：ok=true 成功；否则给出错误码与说明（供 err 信封 message）
  struct PurgeResult {
    bool ok = false;
    int error_code = 0;        // 0=成功；5003=被引用/打包素材禁止删除；2001=素材不存在
    std::string message;
  };
  bool purge(const std::string& media_id,
             const std::vector<std::string>& referencing_ids = {});
  // 带明细的物理删除（§10.1.4：引用计数 >0 拒绝并回 err code=5003，message 含被引用明细；
  // packaged=1 禁止物理删除）
  PurgeResult purge_ex(const std::string& media_id,
                       const std::vector<std::string>& referencing_ids = {});

  // ---- 标签（§10.1.5）----
  bool update_tags(const std::string& media_id, const std::string& tags);

  // 查询
  std::shared_ptr<MediaRecord> find(const std::string& media_id) const;
  size_t count() const { return records_.size(); }

  // 素材在库内的绝对路径（media_root + stored_name）；
  // 未入库返回空串。供场景召回恢复媒体源（§10.3.2）使用。
  std::string stored_path(const std::string& media_id) const;
  // 库根目录（§10.1.1：默认 %APPDATA%/ShowMaster/media/）
  const std::string& media_root() const { return media_root_; }

  // 自动标签匹配（§10.1.5）
  static std::string auto_tag_from_name(const std::string& file_name,
                                         const std::string& dir_name = "");

  // 生成 media_id
  static std::string gen_media_id();

  // §10.1.2 允许格式表（默认全集，小写含点）
  static const std::vector<std::string>& default_allowed_extensions();
  // 扩展名是否在允许表内（空扩展名一律不允许 → 2003）
  bool is_allowed_extension(const std::string& ext) const;

 private:
  std::map<std::string, std::shared_ptr<MediaRecord>> records_;
  ProbeFn probe_cb_;
  ThumbFn thumb_cb_;
  CopyFn copy_cb_;
  StatFn stat_cb_;
  EventFn event_cb_;
  std::vector<std::string> allowed_exts_;  // 空=按 §10.1.2 默认策略
  std::string media_root_ = "/tmp/showmaster/media";

  // 发 evt.media.import_progress（§10.1.1：逐文件完成后发）
  void emit_import_progress(int index, int total, const nlohmann::json& entry);

  // 风格词表
  static const std::vector<std::string>& style_words();

  // 计算文件扩展名（小写，含点）
  static std::string get_extension(const std::string& filename);

  // 从文件名提取不含扩展名的部分
  static std::string get_basename(const std::string& filename);
};

}  // namespace media_lib
}  // namespace sm

// 工程文件格式 .showproj v1（规格 §8）
// 工程 = 标准 zip 容器 + manifest.json 主清单 + 可选打包资源。
// 里程碑说明：zip 容器读写（minizip-ng）属平台里程碑，本模块为纯逻辑内核版，
// 只负责 manifest.json 的结构校验与行集抽取/装配；zip 层拿到 manifest 文本后
// 调用本模块完成语义校验，错误码对齐 §5.5 的 4xxx 区间：
//   4001 工程损坏（JSON 非法 / 缺少必填字段 / 结构错误）
//   4002 版本过新（format 或 version 超当前支持）
//   4003 打包素材缺失（pack 模式引用文件不在包内，由 zip/加载层报告）
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {

inline constexpr int kShowprojFormat = 1;                  // manifest version，Phase 1 为 1
inline constexpr const char* kShowprojMagic = "showmaster.project";

// manifest 校验结果
struct ManifestResult {
  bool ok = false;
  int code = 0;          // 失败时为 4001 / 4002（对齐 error_codes.h）
  std::string message;   // 人类可读原因
};

// 校验 manifest.json 文本；通过后调用方如需读取行集可自行 json::parse 一次。
// 顶层字段：format/version/saved_at/saved_by/project/tracks/items/scenes/
//           playlists/devices/settings（§8.2，devices Phase 1 恒为空数组）
ManifestResult validate_manifest(const std::string& manifest_text);

// 行集抽取（校验通过后调用；任一不存在返回空数组）：
//   rows_of(doc, "tracks") / "items" / "scenes" / "playlists" / "devices"
const nlohmann::json& rows_of(const nlohmann::json& doc, const char* key);

// 构造 manifest 顶层骨架：填入 format/version/project 基础字段后返回对象，
// 由保存方继续挂 tracks/items/... 行集并序列化（§8.3 保存流程=行集序列化）
nlohmann::json make_manifest_skeleton(const std::string& project_name,
                                      const std::string& save_mode,  // "ref"|"pack"
                                      const std::string& media_root,
                                      const std::string& saved_by);

}  // namespace sm

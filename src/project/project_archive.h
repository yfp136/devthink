// .showproj 工程包装配与加载（规格 §8.3 保存流程 / §8.4 加载流程）
//
// 职责边界：本模块是「zip 容器 + 保存/加载编排」，语义校验仍由 project_file
// 的 validate_manifest 负责（manifest.json 结构 → 4001/4002）。
//
// 保存流程（§8.3，与规格一一对应）：
//   1) manifest 行集序列化（调用方给全量 manifest；本模块补 saved_at）
//   2) pack 模式：把源素材读入包内 media/<sha256前16位>_<原名>，并回写 media_files 记录
//   3) 写 .tmp —— zip 容器 + 逐条 CRC-32
//   4) **复读 .tmp 校验包完整性**（EOCD/中央目录/本地头/CRC 全过 + manifest 再校验）
//   5) 原子改名 .tmp → 目标；旧文件轮转进 backups/（保留最近 N 份，默认 5）
//
// 加载流程（§8.4）：读包 → manifest.json 存在性（4001）→ validate_manifest
//   （4001/4002）→ pack 模式引用素材存在性（4003）与 SHA-256 核对（4001）。
//   全过程为纯函数：失败时返回空的 manifest，**不产生半加载状态**（无副作用可残留）。
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace sm {

// 保存选项
struct SaveProjectOptions {
  std::string path;                     // 目标 .showproj
  nlohmann::json manifest;              // 完整 manifest（由 make_manifest_skeleton 起手）
  std::vector<std::string> pack_files;  // pack 模式：待打入包的源文件路径
  int backup_keep = 5;                  // backups/ 保留份数；<=0 表示不做旧文件轮转
};

struct ProjectResult {
  bool ok = false;
  int code = 0;  // 失败时为 4xxx
  std::string message;
};

struct ProjectLoadResult {
  bool ok = false;
  int code = 0;
  std::string message;
  nlohmann::json manifest = nlohmann::json::object();  // 失败时为空对象
  std::vector<std::string> entry_names;                // 包内条目清单（诊断/预览）
};

// 包内素材引用（用于 pack 模式存在性与哈希核对）
struct MediaRef {
  std::string path;    // 包内路径，形如 media/<sha256前16位>_<原名>
  std::string sha256;  // 期望的完整 SHA-256（记录缺失时为空，表示无法核对）
};

// 递归收集 manifest 中的包内素材引用：字符串前缀 "media/" 的值，
// 以及 {packed|path, sha256} 形式的记录对象；按 path 去重，优先保留带 sha256 的。
std::vector<MediaRef> collect_media_refs(const nlohmann::json& manifest);

// §8.3：整个保存流程；返回 4xxx 错误码
ProjectResult save_project(const SaveProjectOptions& opt);

// §8.4：整个加载流程；返回 4xxx 错误码
ProjectLoadResult load_project(const std::string& path);

}  // namespace sm

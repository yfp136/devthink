// ShowMaster 错误码常量（规格 §5.5）
// 区间约定：1xxx 总线 / 2xxx 媒体 / 3xxx 时间线 / 4xxx 工程 / 5xxx 场景与节目单
#pragma once

#include <string>

namespace sm {
namespace ec {
// ---- 0 成功 ----
inline constexpr int OK = 0;

// ---- 1xxx 总线（信封层）----
inline constexpr int UNKNOWN_OP = 1001;           // 未知 op
inline constexpr int BAD_PARAM = 1002;            // 参数非法
inline constexpr int ENGINE_OFFLINE = 1003;       // 目标引擎失联

// ---- 2xxx 媒体链路 ----
inline constexpr int FILE_MISSING = 2001;         // 文件不存在
inline constexpr int DECODE_FAILED = 2002;        // 解码失败
inline constexpr int UNSUPPORTED_FORMAT = 2003;   // 格式不支持
inline constexpr int PREPROCESSING = 2004;        // 预处理中

// ---- 3xxx 时间线 ----
inline constexpr int TRACK_MISSING = 3001;        // 轨道不存在
inline constexpr int ITEM_CONFLICT = 3002;        // 条目冲突
inline constexpr int PLAYHEAD_OUT_OF_RANGE = 3003;// 播放头越界
inline constexpr int CLOCK_UNAVAILABLE = 3004;    // 时钟源不可用

// ---- 4xxx 工程文件 ----
inline constexpr int PROJECT_CORRUPT = 4001;      // 工程损坏
inline constexpr int PROJECT_VERSION_TOO_NEW = 4002; // 版本过新
inline constexpr int PACKED_MEDIA_MISSING = 4003; // 打包素材缺失

// ---- 5xxx 场景与节目单 ----
inline constexpr int SCENE_NOT_FOUND = 5001;      // 场景不存在
inline constexpr int PLAYLIST_EMPTY = 5002;       // 节目单为空
inline constexpr int REFERENCED_OBJECT_MISSING = 5003; // 引用对象缺失
}  // namespace ec

// 返回错误码的中文说明（用于 err 信封 message 与日志），未知码返回通用文案
const char* error_text(int code);
}  // namespace sm

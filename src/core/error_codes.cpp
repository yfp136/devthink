#include "core/error_codes.h"

namespace sm {

const char* error_text(int code) {
  switch (code) {
    case ec::OK: return "成功";
    case ec::UNKNOWN_OP: return "未知指令";
    case ec::BAD_PARAM: return "参数非法";
    case ec::ENGINE_OFFLINE: return "目标引擎失联";
    case ec::FILE_MISSING: return "文件不存在";
    case ec::DECODE_FAILED: return "解码失败";
    case ec::UNSUPPORTED_FORMAT: return "格式不支持";
    case ec::PREPROCESSING: return "素材仍在预处理中";
    case ec::TRACK_MISSING: return "轨道不存在";
    case ec::ITEM_CONFLICT: return "条目冲突";
    case ec::PLAYHEAD_OUT_OF_RANGE: return "播放头越界";
    case ec::CLOCK_UNAVAILABLE: return "时钟源不可用";
    case ec::PROJECT_CORRUPT: return "工程损坏";
    case ec::PROJECT_VERSION_TOO_NEW: return "工程版本过新，请升级软件";
    case ec::PACKED_MEDIA_MISSING: return "打包素材缺失";
    case ec::SCENE_NOT_FOUND: return "场景不存在";
    case ec::PLAYLIST_EMPTY: return "节目单为空";
    case ec::REFERENCED_OBJECT_MISSING: return "引用对象缺失";
    default: return "未知错误";
  }
}

}  // namespace sm

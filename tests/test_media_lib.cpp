// MediaLibrary 单元测试（§10.1）
// 验证：导入流程/类型分类/SHA-256命名/查询分页/回收站/引用保护/自动标签
#include <string>
#include <vector>

#include "core/error_codes.h"
#include "engines/media_lib/media_library.h"
#include "test_common.h"

namespace {

using namespace sm::media_lib;
using nlohmann::json;

// ---- 导入基本流程 ----
void test_import_basic() {
  MediaLibrary lib;
  int copy_calls = 0;
  int probe_calls = 0;
  int thumb_calls = 0;

  lib.set_copy_cb([&](const auto&, const auto&) { ++copy_calls; return true; });
  lib.set_probe_cb([&](const auto&) {
    ++probe_calls;
    return json{{"duration_ms", 60000}, {"width", 1920}, {"height", 1080},
                {"fps", 25.0}, {"codec", "h264"}, {"audio_sr", 48000},
                {"audio_ch", 2}};
  });
  lib.set_thumb_cb([&](const auto&, int64_t) {
    ++thumb_calls;
    return "thumb_jpeg_b64";
  });

  auto result = lib.import_files({"/path/to/video.mp4"}, false);
  SM_CHECK_EQ(result["total"], 1);
  SM_CHECK_EQ(lib.count(), std::size_t(1));
  SM_CHECK_EQ(copy_calls, 1);
  SM_CHECK_EQ(probe_calls, 1);
  SM_CHECK_EQ(thumb_calls, 1);

  // 验证记录
  auto rec = lib.find(result["results"][0]["media_id"].get<std::string>());
  SM_CHECK(rec != nullptr);
  SM_CHECK_EQ(rec->media_type, std::string("video"));
  SM_CHECK_EQ(rec->duration_ms, int64_t(60000));
  SM_CHECK_EQ(rec->width, 1920);
  SM_CHECK_EQ(rec->height, 1080);
  SM_CHECK(!rec->file_hash.empty());
  SM_CHECK(!rec->stored_name.empty());
}

// ---- 类型分类 ----
void test_type_classification() {
  SM_CHECK(media_type_from_extension(".mp3") == MediaType::audio);
  SM_CHECK(media_type_from_extension(".wav") == MediaType::audio);
  SM_CHECK(media_type_from_extension(".MP4") == MediaType::video);
  SM_CHECK(media_type_from_extension(".mov") == MediaType::video);
  SM_CHECK(media_type_from_extension(".jpg") == MediaType::image);
  SM_CHECK(media_type_from_extension(".PNG") == MediaType::image);
  SM_CHECK(media_type_from_extension(".srt") == MediaType::subtitle);
  SM_CHECK(media_type_from_extension(".xyz") == MediaType::other);
}

// ---- 多文件导入 ----
void test_import_multiple() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  auto result = lib.import_files({
    "/path/a.mp3",
    "/path/b.mp4",
    "/path/c.jpg",
    "/path/d.srt"
  });
  SM_CHECK_EQ(result["total"], 4);
  SM_CHECK_EQ(lib.count(), std::size_t(4));
}

// ---- 查询 + 分页 ----
void test_query_pagination() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  // 导入 5 个文件
  std::vector<std::string> paths;
  for (int i = 0; i < 5; ++i)
    paths.push_back("/path/video" + std::to_string(i) + ".mp4");
  lib.import_files(paths, false);

  // 全部查询
  auto all = lib.query({});
  SM_CHECK_EQ(all["total"], 5);

  // 分页：page=1, page_size=2
  auto p1 = lib.query({{"page", 1}, {"page_size", 2}});
  SM_CHECK_EQ(p1["total"], 5);
  SM_CHECK_EQ(p1["items"].size(), std::size_t(2));
  SM_CHECK(p1["has_more"] == true);

  // page=3, page_size=2 → 1 项
  auto p3 = lib.query({{"page", 3}, {"page_size", 2}});
  SM_CHECK_EQ(p3["items"].size(), std::size_t(1));
  SM_CHECK(p3["has_more"] == false);
}

// ---- 类型过滤 ----
void test_query_type_filter() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  lib.import_files({"/a.mp3", "/b.mp4", "/c.jpg", "/d.wav"});
  auto videos = lib.query({{"media_type", "video"}});
  SM_CHECK_EQ(videos["total"], 1);

  auto audios = lib.query({{"media_type", "audio"}});
  SM_CHECK_EQ(audios["total"], 2);
}

// ---- 回收站 ----
void test_recycle() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  auto res = lib.import_files({"/a.mp4"}, false);
  std::string id = res["results"][0]["media_id"];

  // 软删
  SM_CHECK(lib.remove(id));
  auto rec = lib.find(id);
  SM_CHECK(rec->recycle);

  // 查询默认排除回收站
  auto q = lib.query({});
  SM_CHECK_EQ(q["total"], 0);

  // 恢复
  SM_CHECK(lib.restore(id));
  rec = lib.find(id);
  SM_CHECK(!rec->recycle);

  // 恢复后查询可见
  q = lib.query({});
  SM_CHECK_EQ(q["total"], 1);
}

// ---- 引用保护物理删除 ----
void test_purge_protection() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  auto res = lib.import_files({"/a.mp4"}, false);
  std::string id = res["results"][0]["media_id"];

  // 有引用 → 不能删
  SM_CHECK(!lib.purge(id, {"itm_001"}));
  SM_CHECK_EQ(lib.count(), std::size_t(1));

  // 无引用 → 可物理删
  SM_CHECK(lib.purge(id));
  SM_CHECK_EQ(lib.count(), std::size_t(0));
}

// ---- 自动标签 ----
void test_auto_tag() {
  // 命中词表
  SM_CHECK(MediaLibrary::auto_tag_from_name("婚礼背景音乐", "开场") == "婚礼,开场");

  // 不命中
  SM_CHECK(MediaLibrary::auto_tag_from_name("random_file", "misc").empty());

  // 部分命中
  SM_CHECK(MediaLibrary::auto_tag_from_name("炸场视频", "") == "炸场");
}

// ---- 标签更新 ----
void test_update_tags() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  auto res = lib.import_files({"/a.mp4"}, true);  // auto_tag=true
  std::string id = res["results"][0]["media_id"];

  // 手动覆盖标签
  SM_CHECK(lib.update_tags(id, "动感,KTV"));
  auto rec = lib.find(id);
  SM_CHECK_EQ(rec->style_tags, std::string("动感,KTV"));
}

// ---- 导入失败（复制失败）----
void test_import_copy_failure() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return false; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  auto res = lib.import_files({"/a.mp4"}, false);
  SM_CHECK_EQ(res["total"], 1);
  SM_CHECK_EQ(res["results"][0]["preproc_status"], std::string("failed"));
  SM_CHECK_EQ(lib.count(), std::size_t(0));  // 复制失败不入库
}

// ---- 名称模糊搜索 ----
void test_query_name_filter() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  lib.import_files({"/path/wedding_song.mp3", "/path/dance_mix.mp3"}, false);

  auto q = lib.query({{"name", "wedding"}});
  SM_CHECK_EQ(q["total"], 1);
}

// ---- §10.1.4 purge_ex 错误码明细（2001 / 5003）----
// 素材不存在 → 2001；被引用 → 5003 且 message 含被引用明细；
// packaged=1（打包工程内素材）→ 5003，均不得真正删除记录。
void test_purge_ex_error_codes() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  // 1) 素材不存在 → 2001「素材不存在」
  auto missing = lib.purge_ex("med_not_exist");
  SM_CHECK(!missing.ok);
  SM_CHECK_EQ(missing.error_code, sm::ec::FILE_MISSING);
  SM_CHECK(missing.message.find("素材不存在") != std::string::npos);

  auto res = lib.import_files({"/a.mp4"}, false);
  std::string id = res["results"][0]["media_id"];

  // 2) 有引用明细 → 5003，message 必须列出被引用条目
  auto refd = lib.purge_ex(id, {"itm_001", "playlist/pit_002"});
  SM_CHECK(!refd.ok);
  SM_CHECK_EQ(refd.error_code, sm::ec::REFERENCED_OBJECT_MISSING);
  SM_CHECK(refd.message.find("被引用明细") != std::string::npos);
  SM_CHECK(refd.message.find("itm_001") != std::string::npos);
  SM_CHECK(refd.message.find("playlist/pit_002") != std::string::npos);
  SM_CHECK_EQ(lib.count(), std::size_t(1));  // 被拒时记录仍在

  // 3) packaged=1 → 5003，且不删除记录
  lib.find(id)->packaged = true;
  auto packed = lib.purge_ex(id);
  SM_CHECK(!packed.ok);
  SM_CHECK_EQ(packed.error_code, sm::ec::REFERENCED_OBJECT_MISSING);
  SM_CHECK(packed.message.find("packaged") != std::string::npos);
  SM_CHECK_EQ(lib.count(), std::size_t(1));

  // 4) 解除打包标记且无引用 → 允许物理删除
  lib.find(id)->packaged = false;
  auto ok = lib.purge_ex(id);
  SM_CHECK(ok.ok);
  SM_CHECK_EQ(ok.error_code, 0);
  SM_CHECK_EQ(lib.count(), std::size_t(0));
}

// ---- §10.3.2 stored_path：入库素材的绝对路径 ----
void test_stored_path() {
  MediaLibrary lib;
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  // 未入库 → 空串
  SM_CHECK(lib.stored_path("med_none").empty());

  auto res = lib.import_files({"/a.mp4"}, false);
  std::string id = res["results"][0]["media_id"];
  auto rec = lib.find(id);
  SM_CHECK(rec != nullptr);
  SM_CHECK_EQ(lib.stored_path(id), lib.media_root() + "/" + rec->stored_name);

  // 库根目录被改名后，stored_path 随之改变（供场景召回恢复媒体源）
  lib.set_media_root("/tmp/showmaster/media2/");
  SM_CHECK_EQ(lib.stored_path(id),
              std::string("/tmp/showmaster/media2/") + rec->stored_name);
}

// ---- §10.1.1 步 1a：存在性校验失败 → 2001 且不入库 ----
void test_stat_cb_missing_file() {
  MediaLibrary lib;
  lib.set_stat_cb([](const auto& path) { return path != "/missing.mp4"; });
  lib.set_copy_cb([](const auto&, const auto&) { return true; });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  auto res = lib.import_files({"/missing.mp4", "/present.mp4"}, false);
  SM_CHECK_EQ(res["total"], 2);

  // 缺失文件：failed + 2001，且不占用 media_id
  const auto& bad = res["results"][0];
  SM_CHECK_EQ(bad["preproc_status"], std::string("failed"));
  SM_CHECK_EQ(bad["preproc_msg"], std::string("file_missing"));
  SM_CHECK_EQ(bad["error_code"], sm::ec::FILE_MISSING);
  SM_CHECK(!bad.contains("media_id"));

  // 存在文件正常入库
  SM_CHECK_EQ(res["results"][1]["preproc_status"], std::string("done"));
  SM_CHECK_EQ(lib.count(), std::size_t(1));
}

// ---- §10.1.1 事件：逐文件 evt.media.import_progress + 整批 evt.media.import_done ----
void test_import_events() {
  MediaLibrary lib;
  std::vector<std::pair<std::string, json>> events;
  lib.set_event_cb([&](const std::string& evt, const json& payload) {
    events.emplace_back(evt, payload);
  });
  lib.set_copy_cb([](const auto& src, const auto&) {
    return src.find("bad") == std::string::npos;
  });
  lib.set_probe_cb([](const auto&) { return json{}; });
  lib.set_thumb_cb([](const auto&, int64_t) { return ""; });

  lib.import_files({"/good.mp4", "/bad.mp4"}, false);

  SM_CHECK_EQ(events.size(), std::size_t(3));  // 2 条 progress + 1 条 done
  SM_CHECK_EQ(events[0].first, std::string("evt.media.import_progress"));
  SM_CHECK_EQ(events[0].second["index"], 1);
  SM_CHECK_EQ(events[0].second["total"], 2);
  SM_CHECK_EQ(events[0].second["status"], std::string("done"));

  SM_CHECK_EQ(events[1].first, std::string("evt.media.import_progress"));
  SM_CHECK_EQ(events[1].second["index"], 2);
  SM_CHECK_EQ(events[1].second["status"], std::string("failed"));
  SM_CHECK_EQ(events[1].second["error_code"], sm::ec::PREPROCESSING);  // 2004

  SM_CHECK_EQ(events[2].first, std::string("evt.media.import_done"));
  SM_CHECK_EQ(events[2].second["total"], 2);
  SM_CHECK_EQ(events[2].second["ok_count"], 1);
  SM_CHECK_EQ(events[2].second["failed_count"], 1);
  // §5.4 契约载荷 {media_ids:[]}：仅收录成功导入的条目
  SM_CHECK(events[2].second.contains("media_ids"));
  SM_CHECK_EQ(events[2].second["media_ids"].size(), std::size_t(1));
  SM_CHECK_EQ(events[2].second["media_ids"][0], events[0].second["media_id"]);
}

}  // namespace

int main() {
  test_import_basic();
  test_type_classification();
  test_import_multiple();
  test_query_pagination();
  test_query_type_filter();
  test_recycle();
  test_purge_protection();
  test_auto_tag();
  test_update_tags();
  test_import_copy_failure();
  test_query_name_filter();
  test_purge_ex_error_codes();
  test_stored_path();
  test_stat_cb_missing_file();
  test_import_events();
  return smtest::finish("test_media_lib");
}

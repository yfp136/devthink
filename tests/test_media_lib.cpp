// MediaLibrary 单元测试（§10.1）
// 验证：导入流程/类型分类/SHA-256命名/查询分页/回收站/引用保护/自动标签
#include <string>
#include <vector>

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
  return smtest::finish("test_media_lib");
}

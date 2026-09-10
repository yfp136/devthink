// SceneStore 单元测试（§10.3）
// 验证：保存/召回/淡变优先级/引用保护删除/列表/overlay→cut 降级
#include <string>
#include <vector>

#include "engines/scene/scene_store.h"
#include "test_common.h"

namespace {

using namespace sm::scene;
using nlohmann::json;

// ---- 保存基本流程 ----
void test_save_basic() {
  SceneStore store;
  std::string state_captured;
  std::string thumb_captured;

  store.set_get_current_state_cb([&]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":-3,"mute":false},"media":{"source":{"type":"media","media_id":"m1"},"pos_ms":500,"playing":true},"devices":[]})";
  });
  store.set_capture_thumb_cb([&]() { return "thumb_jpeg_b64_data"; });

  std::string id = store.save("开场场景", "开场", 500, "fade");
  SM_CHECK(!id.empty());
  SM_CHECK_EQ(store.count(), std::size_t(1));

  auto snap = store.find(id);
  SM_CHECK(snap != nullptr);
  SM_CHECK_EQ(snap->scene_name, std::string("开场场景"));
  SM_CHECK_EQ(snap->folder, std::string("开场"));
  SM_CHECK_EQ(snap->fade_ms, 500);
  SM_CHECK_EQ(snap->recall_mode, std::string("fade"));
  SM_CHECK(!snap->state_json.empty());
  SM_CHECK(!snap->thumb_b64.empty());
}

// ---- 召回 + 淡变优先级 ----
void test_recall_fade_priority() {
  SceneStore store;
  std::string last_state;
  int last_fade = -1;
  std::string last_mode;

  store.set_get_current_state_cb([&]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });
  store.set_apply_state_cb([&](const std::string& s, int f, const std::string& m) {
    last_state = s;
    last_fade = f;
    last_mode = m;
  });

  // 保存快照，fade_ms=500
  std::string id = store.save("场景A", "", 500, "fade");

  // 召回：不覆盖参数 → 用快照的 fade_ms=500
  SM_CHECK(store.recall(id));
  SM_CHECK_EQ(last_fade, 500);
  SM_CHECK_EQ(last_mode, std::string("fade"));

  // 召回：覆盖 fade_ms=1000
  SM_CHECK(store.recall(id, 1000));
  SM_CHECK_EQ(last_fade, 1000);

  // 召回：覆盖 recall_mode=cut
  SM_CHECK(store.recall(id, -1, "cut"));
  SM_CHECK_EQ(last_mode, std::string("cut"));
}

// ---- overlay 在 P1 降级为 cut ----
void test_overlay_degrades_to_cut() {
  SceneStore store;
  std::string last_mode;

  store.set_get_current_state_cb([]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });
  store.set_apply_state_cb([&](const std::string&, int, const std::string& m) {
    last_mode = m;
  });

  std::string id = store.save("场景B", "", 0, "overlay");
  SM_CHECK(store.recall(id));
  SM_CHECK_EQ(last_mode, std::string("cut"));  // overlay→cut 降级
}

// ---- 召回不存在的场景 ----
void test_recall_nonexistent() {
  SceneStore store;
  store.set_apply_state_cb([](const auto&, int, const auto&) {});
  SM_CHECK(!store.recall("nonexistent_id"));
}

// ---- 引用保护删除 ----
void test_delete_with_reference() {
  SceneStore store;
  store.set_get_current_state_cb([]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });

  std::string id = store.save("场景C", "", 0, "fade");
  SM_CHECK_EQ(store.count(), std::size_t(1));

  // 无引用 → 可删
  SM_CHECK(store.remove(id));
  SM_CHECK_EQ(store.count(), std::size_t(0));
}

void test_delete_blocked_by_reference() {
  SceneStore store;
  store.set_get_current_state_cb([]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });

  std::string id = store.save("场景D", "", 0, "fade");
  // 有引用 → 不能删
  SM_CHECK(!store.remove(id, {"itm_001"}));
  SM_CHECK_EQ(store.count(), std::size_t(1));
}

// ---- 列表 ----
void test_list() {
  SceneStore store;
  store.set_get_current_state_cb([]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });

  store.save("场景1", "开场", 500, "fade");
  store.save("场景2", "开场", 300, "cut");
  store.save("场景3", "中场", 800, "fade");

  // 全部
  auto all = store.list();
  SM_CHECK_EQ(all["total"], 3);

  // 按 folder 过滤
  auto opening = store.list("开场");
  SM_CHECK_EQ(opening["total"], 2);

  auto mid = store.list("中场");
  SM_CHECK_EQ(mid["total"], 1);
}

// ---- 多次保存不同快照 ----
void test_multiple_saves() {
  SceneStore store;
  store.set_get_current_state_cb([]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });

  for (int i = 0; i < 5; ++i)
    store.save("场景" + std::to_string(i), "", 0, "fade");

  SM_CHECK_EQ(store.count(), std::size_t(5));
  auto list = store.list();
  SM_CHECK_EQ(list["total"], 5);
}

// ---- 空名拒绝保存 ----
void test_save_empty_name_rejected() {
  SceneStore store;
  std::string id = store.save("", "", 0, "fade");
  SM_CHECK(id.empty());
  SM_CHECK_EQ(store.count(), std::size_t(0));
}

// ---- §10.3 [1173]：state_json 语义非法 → 召回拒绝 ----
// sv 高于当前支持版本、必填子树缺失、顶层非对象，均须拒绝召回并提示升级软件。
void test_recall_rejects_unsupported_state() {
  SceneStore store;
  int applied = 0;
  store.set_apply_state_cb([&](const auto&, int, const auto&) { ++applied; });
  store.set_recalled_cb([&](const auto&, int, int64_t) {});

  const char* valid =
      R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";

  // 1) sv=999（高于支持版本 1）→ 拒绝；场景本身仍保留在库内
  store.set_get_current_state_cb([]() {
    return R"({"sv":999,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });
  std::string future = store.save("未来版本场景", "", 0, "cut");
  SM_CHECK(!future.empty());
  SM_CHECK(!store.recall(future));
  SM_CHECK_EQ(applied, 0);
  SM_CHECK_EQ(store.count(), std::size_t(1));

  // 2) 缺 sv → 拒绝
  store.set_get_current_state_cb([]() { return std::string(R"({"scope":[]})"); });
  std::string no_sv = store.save("缺版本场景", "", 0, "cut");
  SM_CHECK(!no_sv.empty());
  SM_CHECK(!store.recall(no_sv));
  SM_CHECK_EQ(applied, 0);

  // 3) 顶层非对象 → 拒绝
  store.set_get_current_state_cb([]() { return std::string("[1,2,3]"); });
  std::string arr = store.save("非对象场景", "", 0, "cut");
  SM_CHECK(!arr.empty());
  SM_CHECK(!store.recall(arr));
  SM_CHECK_EQ(applied, 0);

  // 4) 对照组：合法 sv=1 → 正常召回并应用
  store.set_get_current_state_cb([valid]() { return std::string(valid); });
  std::string ok = store.save("正常场景", "", 0, "cut");
  SM_CHECK(store.recall(ok));
  SM_CHECK_EQ(applied, 1);
}

// ---- §10.3.2：状态应用完成后回调携 {scene_id, fade_ms, applied_at_ms} ----
void test_recalled_callback_payload() {
  SceneStore store;
  std::string cb_scene;
  int cb_fade = -1;
  int64_t cb_at = 0;
  int cb_count = 0;

  store.set_get_current_state_cb([]() {
    return R"({"sv":1,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });
  store.set_apply_state_cb([](const auto&, int, const auto&) {});
  store.set_recalled_cb([&](const std::string& id, int fade, int64_t at) {
    ++cb_count;
    cb_scene = id;
    cb_fade = fade;
    cb_at = at;
  });

  // fade_ms 覆盖生效并写入事件载荷
  std::string id = store.save("事件场景", "", 300, "fade");
  SM_CHECK(!id.empty());
  SM_CHECK(store.recall(id, 800));
  SM_CHECK_EQ(cb_count, 1);
  SM_CHECK_EQ(cb_scene, id);
  SM_CHECK_EQ(cb_fade, 800);
  SM_CHECK(cb_at > 0);  // applied_at_ms 必须为真实时间戳

  // 失败路径（场景不存在）不得发事件
  cb_count = 0;
  SM_CHECK(!store.recall("nonexistent_id"));
  SM_CHECK_EQ(cb_count, 0);

  // 被拒绝的 state_json 同样不得发事件
  store.set_get_current_state_cb([]() {
    return R"({"sv":999,"scope":["media","master"],"master":{"gain_db":0,"mute":false},"media":{"source":{"type":"none"},"pos_ms":0,"playing":false},"devices":[]})";
  });
  std::string bad = store.save("坏版本场景", "", 0, "cut");
  SM_CHECK(!bad.empty());
  cb_count = 0;
  SM_CHECK(!store.recall(bad));
  SM_CHECK_EQ(cb_count, 0);
}

}  // namespace

int main() {
  test_save_basic();
  test_recall_fade_priority();
  test_overlay_degrades_to_cut();
  test_recall_nonexistent();
  test_delete_with_reference();
  test_delete_blocked_by_reference();
  test_list();
  test_multiple_saves();
  test_save_empty_name_rejected();
  test_recall_rejects_unsupported_state();
  test_recalled_callback_payload();
  return smtest::finish("test_scene");
}

// SQLite 存储层单元测试（规格 §6.1 连接参数 / §6.2 schema / 迁移约定）
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/util.h"
#include "db/db_store.h"
#include "test_common.h"

#ifndef SM_SCHEMA_SQL
#error "缺少编译期宏 SM_SCHEMA_SQL"
#endif
#ifndef SM_MIGRATIONS_DIR
#error "缺少编译期宏 SM_MIGRATIONS_DIR"
#endif

namespace {

using sm::DbStore;

void test_memory_schema() {
  std::string err;
  DbStore db;
  SM_CHECK(db.open(":memory:", err));
  SM_CHECK_EQ(db.schema_version(), -1);  // 未建 schema：表缺失 → -1

  SM_CHECK(db.apply_schema_file(SM_SCHEMA_SQL, err));
  SM_CHECK_EQ(db.schema_version(), 2);
}

void test_table_inventory() {
  std::string err;
  DbStore db;
  SM_CHECK(db.open(":memory:", err));
  SM_CHECK(db.apply_schema_file(SM_SCHEMA_SQL, err));

  // 规格 §6.2 DDL：17 张表（schema_version + 16 业务表）
  SM_CHECK_EQ(db.table_names().size(), std::size_t(17));

  const char* required[] = {
      "schema_version", "app_settings", "projects",       "media_library",
      "clip_virtual",   "timeline_track", "timeline_item", "scene_snapshot",
      "show_playlist",  "playlist_item",  "fixture_lib",   "project_fixture",
      "cue_light",      "pixel_program",  "device_list",   "device_action",
      "system_log",
  };
  for (const char* t : required) SM_CHECK_MSG(db.table_exists(t), std::string("缺表: ") + t);
}

void test_migrations_empty_noop() {
  std::string err;
  DbStore db;
  SM_CHECK(db.open(":memory:", err));
  SM_CHECK(db.apply_schema_file(SM_SCHEMA_SQL, err));
  SM_CHECK(db.run_migrations(SM_MIGRATIONS_DIR, err));  // 目录为空 → 成功且无副作用
  SM_CHECK_EQ(db.schema_version(), 2);
}

void test_closed_rejects_exec() {
  std::string err;
  DbStore db;
  SM_CHECK(db.open(":memory:", err));
  SM_CHECK(db.exec("SELECT 1;", err));
  db.close();
  SM_CHECK(!db.is_open());
  SM_CHECK(!db.exec("SELECT 1;", err));  // 关闭后拒绝
}

void test_wal_and_persistence() {
  std::string err;
  const std::string dir = (std::filesystem::temp_directory_path() / "sm_test_db").string();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = dir + "/" + sm::uuid_hex32() + ".db";

  {
    DbStore db;
    SM_CHECK(db.open(path, err));
    SM_CHECK(db.apply_schema_file(SM_SCHEMA_SQL, err));
    // §6.1：文件库必须 journal_mode=WAL；写入后存在 -wal 伴随文件
    SM_CHECK_MSG(std::filesystem::exists(path + "-wal", ec), "WAL 未生效");
  }  // close → checkpoint

  {
    DbStore db2;
    SM_CHECK(db2.open(path, err));       // 重开同一文件
    SM_CHECK_EQ(db2.schema_version(), 2);  // 结构持久化
  }

  std::filesystem::remove(path, ec);
  std::filesystem::remove(path + "-wal", ec);
  std::filesystem::remove(path + "-shm", ec);
  std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main() {
  test_memory_schema();
  test_table_inventory();
  test_migrations_empty_noop();
  test_closed_rejects_exec();
  test_wal_and_persistence();
  return smtest::finish("test_db");
}

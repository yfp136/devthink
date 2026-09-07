// SQLite 存储层（规格 §6.1）
// 连接 PRAGMA：journal_mode=WAL（文件库）；foreign_keys=ON；
//             synchronous=NORMAL；busy_timeout=3000。
// 迁移约定：库内置 schema_version 表；按 tools/migrations/ 目录内
//           NNN_*.sql 文件名前缀数字 > 当前版本时按序执行，并同步 schema_version。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace sm {

class DbStore {
 public:
  DbStore();
  ~DbStore();

  DbStore(const DbStore&) = delete;
  DbStore& operator=(const DbStore&) = delete;

  // path 为 ":memory:" 时用内存库（WAL 自动跳过）；否则为 db 文件路径
  bool open(const std::string& path, std::string& err);

  void close();
  bool is_open() const { return db_ != nullptr; }

  // 执行一条/一段 SQL（多语句用 sqlite3_exec 语义），失败置 err
  bool exec(const std::string& sql, std::string& err);

  // 读取 schema_version 表首行版本；表缺失返回 -1
  int schema_version() const;

  // 以文件内容整体执行（用于装入 src/db/schema_v2.sql）
  bool apply_schema_file(const std::string& path, std::string& err);

  // 顺序执行迁移目录（migrations_dir）中版本高于当前 schema_version 的 NNN_*.sql
  bool run_migrations(const std::string& migrations_dir, std::string& err);

  // 全部业务表名（剔除 sqlite_* 系统表）
  std::vector<std::string> table_names() const;
  bool table_exists(const std::string& name) const;

  sqlite3* raw() const { return db_; }
  const std::string& path() const { return path_; }

 private:
  bool set_schema_version(int version, std::string& err);
  sqlite3* db_ = nullptr;
  std::string path_;
};

}  // namespace sm

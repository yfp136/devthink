#include "db/db_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sm {

namespace {
bool read_text_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}
}  // namespace

DbStore::DbStore() = default;
DbStore::~DbStore() { close(); }

bool DbStore::open(const std::string& path, std::string& err) {
  close();
  const int rc = sqlite3_open(path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    err = std::string("sqlite3_open 失败: ") + (db_ ? sqlite3_errmsg(db_) : "未知");
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
    return false;
  }
  path_ = path;

  // 连接参数（§6.1）
  const bool memory = (path == ":memory:");
  std::string pragmas = "PRAGMA foreign_keys=ON;"
                        "PRAGMA synchronous=NORMAL;"
                        "PRAGMA busy_timeout=3000;";
  if (!memory) pragmas += "PRAGMA journal_mode=WAL;";
  if (!exec(pragmas, err)) {
    close();
    return false;
  }
  return true;
}

void DbStore::close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  path_.clear();
}

bool DbStore::exec(const std::string& sql, std::string& err) {
  if (!db_) { err = "数据库未打开"; return false; }
  char* zErr = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &zErr);
  if (rc != SQLITE_OK) {
    err = zErr ? zErr : sqlite3_errmsg(db_);
    sqlite3_free(zErr);
    return false;
  }
  return true;
}

int DbStore::schema_version() const {
  if (!db_) return -1;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT version FROM schema_version LIMIT 1", -1, &st, nullptr) != SQLITE_OK)
    return -1;
  int ver = -1;
  if (sqlite3_step(st) == SQLITE_ROW) ver = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return ver;
}

bool DbStore::set_schema_version(int version, std::string& err) {
  std::string sql = "DELETE FROM schema_version; INSERT INTO schema_version(version) VALUES(" +
                    std::to_string(version) + ");";
  return exec(sql, err);
}

bool DbStore::apply_schema_file(const std::string& path, std::string& err) {
  std::string sql;
  if (!read_text_file(path, sql)) {
    err = "无法读取 schema 文件: " + path;
    return false;
  }
  if (!exec(sql, err)) {
    err = "schema 执行失败(" + path + "): " + err;
    return false;
  }
  return true;
}

bool DbStore::run_migrations(const std::string& migrations_dir, std::string& err) {
  std::error_code ec;
  if (!std::filesystem::exists(migrations_dir, ec)) return true;  // 尚无迁移目录视为无迁移
  if (ec) { err = "访问迁移目录失败: " + migrations_dir; return false; }

  const int current = schema_version();
  if (current < 0) { err = "schema_version 表缺失，先执行 schema_v2.sql"; return false; }

  std::vector<std::string> files;
  for (const auto& entry : std::filesystem::directory_iterator(migrations_dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".sql") files.push_back(name);
  }
  std::sort(files.begin(), files.end());

  for (const auto& name : files) {
    // 文件名约定：NNN_name.sql，NNN 为迁移后 schema_version
    int target = -1;
    const std::size_t us = name.find('_');
    if (us != std::string::npos) {
      try {
        target = std::stoi(name.substr(0, us));
      } catch (...) { target = -1; }
    }
    if (target <= current) continue;
    if (target < 0) { err = "迁移文件名不合法(需 NNN_*.sql): " + name; return false; }

    std::string sql;
    if (!read_text_file((std::filesystem::path(migrations_dir) / name).string(), sql)) {
      err = "无法读取迁移脚本: " + name;
      return false;
    }
    if (!exec("BEGIN;", err)) return false;
    if (!exec(sql, err)) {
      exec("ROLLBACK;", err);
      return false;
    }
    if (!set_schema_version(target, err)) {
      exec("ROLLBACK;", err);
      return false;
    }
    if (!exec("COMMIT;", err)) return false;
  }
  return true;
}

std::vector<std::string> DbStore::table_names() const {
  std::vector<std::string> out;
  if (!db_) return out;
  sqlite3_stmt* st = nullptr;
  const char* sql =
      "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name";
  if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
  }
  sqlite3_finalize(st);
  return out;
}

bool DbStore::table_exists(const std::string& name) const {
  const auto names = table_names();
  return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace sm

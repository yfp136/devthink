# 数据库迁移脚本目录（规格 §6.1 迁移约定）

内核在 `DbStore` 打开库后按 **文件名升序** 执行本目录内的 `.sql` 脚本，
并把最新执行到的版本写入 `schema_version` 表（`db_store.cpp` 维护）。

- `schema_v2.sql` 基线建表脚本位于 `src/db/`（非本目录；由编译期宏
  `SM_SCHEMA_SQL` 指向），本目录存放其后续增量迁移。
- 增量迁移命名约定：`m00X_描述.sql`（如 `m001_add_playlist_notes.sql`），
  三位序号保证字典序即执行序，禁止跳号或改写已发布脚本。
- 每个迁移脚本内只写 DDL/DML，不写 `PRAGMA user_version`；版本推进由
  `DbStore::migrate` 依据文件名解析的序号完成。

当前无增量迁移；首个增量脚本随首个 schema 演进任务一并提交。

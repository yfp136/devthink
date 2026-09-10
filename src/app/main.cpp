// sm_kernel_smoke —— 内核冒烟程序（Phase 1 里程碑验收入口，规格 4.2）
// 依次自检：指令/事件字典与错误码 → 信封往返与合法性 → 消息总线与心跳 →
// SQLite schema 与迁移 → 引擎注册表。任一失败打印 [FAIL] 并以非 0 退出。
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/envelope.h"
#include "core/error_codes.h"
#include "core/msg_bus.h"
#include "core/op_dict.h"
#include "db/db_store.h"
#include "engines/api/engine_api.h"
#include "engines/engine_registry.h"

#ifndef SM_SCHEMA_SQL
#error "缺少编译期宏 SM_SCHEMA_SQL（顶层 CMakeLists 已注入 schema_v2.sql 路径）"
#endif
#ifndef SM_MIGRATIONS_DIR
#error "缺少编译期宏 SM_MIGRATIONS_DIR（顶层 CMakeLists 已注入 tools/migrations 路径）"
#endif

namespace {

int g_checks = 0;
int g_fail = 0;

void expect(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    std::fprintf(stderr, "  [FAIL] %s\n", what);
    ++g_fail;
  }
}

// ---- [1/5] 指令/事件字典与错误码（§5.3 / §5.4 / §5.5）----
void check_dict_and_codes() {
  using namespace sm;
  std::printf("[1/5] 指令/事件字典与错误码\n");
  expect(cmd_ops().size() == 33, "指令字典共 33 条（§5.3）");
  // 事件分两层：evt_ops_spec() = §5.4 表核心集（16 行，engine.up/down 拆条 ⇒ 17）；
  // evt_ops() = 核心集 + 实现增补集 5 条（§11.4.1 文档同源登记）⇒ 22。
  expect(evt_ops_spec().size() == 17, "规格事件核心集共 17 条（§5.4）");
  expect(evt_ops().size() == 22, "实现事件全集共 22 条（§5.4 + 实现增补）");
  expect(is_evt_op("evt.playlist.playing") && is_evt_op("evt.transport.bpm") &&
             is_evt_op("evt.engine.heartbeat") && is_evt_op("evt.log"),
         "§5.4 新增事件 op 可识别");
  // §9.5 [1015] 白名单：放行遥控控制面，拒绝其余
  expect(is_remote_control_op("transport.play") && is_remote_control_op("playlist.next") &&
             is_remote_control_op("scene.recall") && is_remote_control_op("sys.ping"),
         "遥控白名单放行控制面 op");
  expect(!is_remote_control_op("sys.shutdown") && !is_remote_control_op("media.import") &&
             !is_remote_control_op("scene.save"),
         "遥控白名单拒绝非控制面 op");
  expect(is_cmd_op("media.play") && is_cmd_op("sys.ping"), "典型指令 op 可识别");
  expect(is_evt_op("evt.engine.up") && is_evt_op("evt.error"), "典型事件 op 可识别");
  expect(op_namespace("timeline.load") != nullptr, "op 命名空间可反查");
  expect(error_text(ec::OK) != nullptr && error_text(ec::OK)[0] != '\0', "错误码 0 有文案");
  expect(std::string(error_text(9999)) == "未知错误", "未知错误码回落“未知错误”");
}

// ---- [2/5] 信封往返与合法性（§5.1）----
void check_envelope() {
  using namespace sm;
  std::printf("[2/5] 信封往返与合法性\n");
  const Envelope cmd = make_cmd("engine.ui", "engine.media", "media.play",
                                nlohmann::json{{"media_id", "m-abc"}});
  const std::string js = envelope_to_json(cmd);
  Envelope out;
  const ParseResult r = parse_envelope(js, out);
  expect(r.ok, "合法 cmd 信封可解析");
  expect(out.id == cmd.id && out.dst == "engine.media" && out.op == "media.play",
         "往返字段一致");
  expect(out.params.at("media_id") == "m-abc", "params 对象往返一致");

  const Envelope ok_rsp = make_reply(cmd, ec::OK);
  expect(ok_rsp.type == "rsp" && ok_rsp.code == ec::OK && ok_rsp.ref_id == cmd.id &&
             ok_rsp.dst == cmd.src,
         "make_reply(0) = rsp，回填 ref_id 且应答方向反转");
  const Envelope err_rsp = make_reply(cmd, ec::BAD_PARAM);
  expect(err_rsp.type == "err" && err_rsp.has_code, "make_reply(非0) = err 并带 code");

  // 非法输入
  expect(!parse_envelope("{not json", out).ok, "非法 JSON 拒绝");
  expect(!parse_envelope("[]", out).ok, "非对象根节点拒绝");
  const std::string bad_v =
      R"({"v":2,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping"})";
  expect(!parse_envelope(bad_v, out).ok, "协议版本必须为 1");
  const std::string no_id =
      R"({"v":1,"monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping"})";
  expect(!parse_envelope(no_id, out).ok, "缺 id 拒绝");
  const std::string cmd_with_code =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping","code":0})";
  expect(!parse_envelope(cmd_with_code, out).ok, "cmd 携带 code 拒绝");
  const std::string rsp_no_code =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"rsp","op":"sys.ping"})";
  expect(!parse_envelope(rsp_no_code, out).ok, "rsp 缺 code 拒绝");
  const std::string bad_type =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"ping","op":"sys.ping"})";
  expect(!parse_envelope(bad_type, out).ok, "type 枚举校验");
  const std::string bad_params =
      R"({"v":1,"id":"x","monotonic_ms":1,"src":"a","dst":"b","type":"cmd","op":"sys.ping","params":[1]})";
  expect(!parse_envelope(bad_params, out).ok, "params 必须为对象");
}

// ---- [3/5] 消息总线与心跳（§5.2）----
void check_bus_and_heartbeat() {
  using namespace sm;
  std::printf("[3/5] 消息总线与心跳\n");

  MsgBus bus;
  int got_media = 0, got_timeline = 0, got_default = 0;
  bus.register_sink("engine.media", [&](const Envelope&) { ++got_media; });
  bus.register_sink("engine.timeline", [&](const Envelope&) { ++got_timeline; });
  bus.set_default_sink([&](const Envelope&) { ++got_default; });

  bus.post(make_cmd("engine.ui", "engine.media", "media.play"));      // 精确命中
  bus.post(make_cmd("engine.ui", "engine.ghost", "sys.ping"));        // 未注册 → 默认
  const Envelope evt = make_event("engine.media", "evt.engine.heartbeat");
  expect(evt.dst == "*", "事件默认广播 dst=*");
  bus.post(evt);                                                      // 广播给全体
  expect(got_media == 2 && got_timeline == 1 && got_default == 2,
         "精确路由 / 未注册回落默认 / 广播三者计数正确");
  expect(bus.posted_count() == 3, "投递总量计数");
  expect(bus.recent(2).size() == 2 && bus.recent(99).size() == 3, "环形记录 recent(n) 语义");
  bus.clear();
  expect(bus.posted_count() == 0 && bus.recent(3).empty(), "clear 后清空");

  HeartbeatMonitor hb;
  hb.note_heartbeat("engine.media", 0);
  expect(hb.online("engine.media"), "首次心跳即在线");
  hb.note_heartbeat("engine.media", 3000);   // 间隔 >2s → missed=1
  expect(hb.online("engine.media"), "丢失 1 次仍在线");
  hb.note_heartbeat("engine.media", 6000);   // missed=2
  expect(hb.online("engine.media"), "丢失 2 次仍在线");
  hb.note_heartbeat("engine.media", 9000);   // missed=3 → 离线
  expect(!hb.online("engine.media"), "连续丢失 3 次判离线（§5.2）");
  hb.note_heartbeat("engine.media", 9500);   // 心跳恢复 → 重新上线
  expect(hb.online("engine.media"), "心跳恢复自动上线");

  HeartbeatMonitor hb2;
  hb2.note_heartbeat("engine.timeline", 0);
  hb2.tick(7000);                            // 距上次 > 2s*3
  expect(!hb2.online("engine.timeline"), "tick 超时判离线");
}

// ---- [4/5] SQLite schema 与迁移（§6.1 / §6.2）----
void check_db() {
  using namespace sm;
  std::printf("[4/5] SQLite schema 与迁移\n");
  std::string err;

  DbStore db;
  expect(db.open(":memory:", err), "打开内存库");
  expect(db.schema_version() == -1, "空库无 schema_version 表（返回 -1）");
  expect(db.apply_schema_file(SM_SCHEMA_SQL, err), "执行 schema_v2.sql 成功");
  expect(db.schema_version() == 2, "schema_version == 2");
  expect(db.table_names().size() == 17, "共 17 张表（含 schema_version，规格 §6.2 DDL）");
  expect(db.table_exists("projects") && db.table_exists("media_library") &&
             db.table_exists("timeline_track") && db.table_exists("timeline_item") &&
             db.table_exists("scene_snapshot") && db.table_exists("show_playlist") &&
             db.table_exists("playlist_item") && db.table_exists("system_log"),
         "关键业务表齐备");
  expect(db.run_migrations(SM_MIGRATIONS_DIR, err), "迁移目录空跑成功");
  expect(db.schema_version() == 2, "无待执行迁移时版本保持 2");
  db.close();
  expect(!db.exec("SELECT 1;", err), "关闭后 exec 拒绝执行");

  // WAL：文件库建库并写入后应存在 -wal 伴随文件（§6.1 journal_mode=WAL）
  const std::string dir =
      (std::filesystem::temp_directory_path() / "sm_kernel_smoke").string();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::string path = dir + "/t.db";
  DbStore fdb;
  expect(fdb.open(path, err) && fdb.apply_schema_file(SM_SCHEMA_SQL, err), "文件库建库");
  expect(std::filesystem::exists(path + "-wal", ec), "WAL 生效（存在 -wal 伴随文件）");
  fdb.close();
  DbStore fdb2;
  expect(fdb2.open(path, err) && fdb2.schema_version() == 2, "文件库重开 schema 持久化");
  fdb2.close();
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path + "-wal", ec);
  std::filesystem::remove(path + "-shm", ec);
  std::filesystem::remove_all(dir, ec);
}

// ---- [5/5] 引擎注册表与 ABI（§3.2 / §3.3）----
void check_engines() {
  using namespace sm;
  std::printf("[5/5] 引擎注册表与 ABI\n");
  expect(engine_registry().size() == 7, "注册表共 7 个引擎（§3.2 表格）");
  expect(is_known_engine("engine.media") && is_known_engine("engine.timeline") &&
             is_known_engine("engine.device"),
         "已注册引擎 id 可识别");
  expect(!is_known_engine("engine.ghost"), "未注册 id 拒绝");
  (void)sizeof(IEngine);   // 编译期校验 ABI 头可用
  (void)sizeof(EngineConfig);
}

}  // namespace

int main() {
  std::printf("=== ShowMaster 内核冒烟自检（Phase 1 里程碑）===\n");
  check_dict_and_codes();
  check_envelope();
  check_bus_and_heartbeat();
  check_db();
  check_engines();

  if (g_fail == 0) {
    std::printf("=== 全部通过：%d 项检查 ===\n", g_checks);
    return 0;
  }
  std::printf("=== 失败 %d/%d 项，请检查上方 [FAIL] 输出 ===\n", g_fail, g_checks);
  return 1;
}

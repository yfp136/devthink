-- ============================================================================
-- ShowMaster 数据库 Schema V2（规格 §6.2，直接执行）
-- 存储约定：所有时间统一 INTEGER 毫秒（列名 *_ms）；主键 TEXT UUID；
--           软删除用 deleted/recycle 标志列；复杂结构用 TEXT 存 JSON。
-- 数据库为编辑态；工程持久态以 manifest 序列化进 .showproj（§8）。
-- 结构变更禁止手工改库：在 tools/migrations/ 追加 NNN_*.sql 增量脚本。
-- ============================================================================

CREATE TABLE schema_version (version INTEGER NOT NULL);
INSERT INTO schema_version (version) VALUES (2);

CREATE TABLE app_settings (
  key TEXT PRIMARY KEY, value TEXT NOT NULL);

CREATE TABLE projects (
  project_id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  save_mode TEXT NOT NULL DEFAULT 'ref' CHECK (save_mode IN ('ref','pack')),
  last_path TEXT,
  media_root TEXT,
  created_ms INTEGER NOT NULL,
  updated_ms INTEGER NOT NULL,
  thumb BLOB);

CREATE TABLE media_library (
  media_id TEXT PRIMARY KEY,
  media_type TEXT NOT NULL CHECK (media_type IN ('audio','video','image','subtitle','virtual','other')),
  file_path TEXT,
  file_hash TEXT,                       -- SHA-256 十六进制
  duration_ms INTEGER,                  -- 音频/视频时长；图片为 0
  bpm REAL,                             -- 音频 BPM，未知为 NULL
  width INTEGER, height INTEGER, fps REAL,
  codec TEXT, audio_sr INTEGER, audio_ch INTEGER,
  style_tags TEXT,                      -- 逗号分隔：抒情,动感,KTV,宴会,炸场
  preproc_status TEXT NOT NULL DEFAULT 'pending' CHECK (preproc_status IN ('pending','done','failed')),
  preproc_msg TEXT,
  recycle INTEGER NOT NULL DEFAULT 0,   -- 0 正常 1 回收站
  thumbnail BLOB,
  packaged INTEGER NOT NULL DEFAULT 0,  -- 是否已随当前工程打包
  packed_rel TEXT,                      -- 打包后包内相对路径
  create_ms INTEGER NOT NULL);
CREATE INDEX idx_media_type ON media_library(media_type);
CREATE INDEX idx_media_recycle ON media_library(recycle);

CREATE TABLE clip_virtual (             -- 无损虚拟剪辑素材(Phase 2 启用，先建表)
  clip_id TEXT PRIMARY KEY,
  media_id TEXT NOT NULL REFERENCES media_library(media_id),
  clip_name TEXT NOT NULL,
  in_ms INTEGER NOT NULL DEFAULT 0,
  out_ms INTEGER NOT NULL DEFAULT 0,
  audio_gain_db REAL NOT NULL DEFAULT 0,
  audio_fade_in_ms INTEGER NOT NULL DEFAULT 0,
  audio_fade_out_ms INTEGER NOT NULL DEFAULT 0,
  video_fade_in_ms INTEGER NOT NULL DEFAULT 0,
  video_fade_out_ms INTEGER NOT NULL DEFAULT 0,
  thumbnail BLOB,
  deleted INTEGER NOT NULL DEFAULT 0,
  create_ms INTEGER NOT NULL);

CREATE TABLE timeline_track (
  track_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  track_index INTEGER NOT NULL,
  track_type TEXT NOT NULL CHECK (track_type IN ('audio','video','vj','light','pixel','device','scene','command')),
  name TEXT NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1,
  meta TEXT,                             -- JSON 扩展(如颜色/高度)
  UNIQUE (project_id, track_index));

CREATE TABLE timeline_item (
  item_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  track_id TEXT NOT NULL REFERENCES timeline_track(track_id),
  track_index INTEGER NOT NULL,
  start_ms INTEGER NOT NULL,
  duration_ms INTEGER NOT NULL DEFAULT 0,
  ref_type TEXT NOT NULL CHECK (ref_type IN ('media','clip','scene','command','cue')),
  ref_uuid TEXT NOT NULL,                -- media_id / clip_id / scene_id / command 名
  loop INTEGER NOT NULL DEFAULT 0,
  meta TEXT,                             -- JSON: 素材内入出点 trim_ms/音量/淡入淡出等
  UNIQUE (track_id, start_ms));
CREATE INDEX idx_ti_track ON timeline_item(project_id, track_index, start_ms);

CREATE TABLE scene_snapshot (
  scene_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  scene_name TEXT NOT NULL,
  folder TEXT DEFAULT '',
  fade_ms INTEGER NOT NULL DEFAULT 0,
  recall_mode TEXT NOT NULL DEFAULT 'fade' CHECK (recall_mode IN ('cut','fade','overlay')),
  save_scope TEXT,                       -- JSON: 本次保存涉及哪些子系统
  thumb BLOB,
  state_json TEXT NOT NULL,              -- JSON: 全设备实时状态(结构见 10.4)
  create_ms INTEGER NOT NULL);

CREATE TABLE show_playlist (
  playlist_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  playlist_name TEXT NOT NULL,
  description TEXT,
  loop_mode TEXT NOT NULL DEFAULT 'none' CHECK (loop_mode IN ('none','all','current')),
  create_ms INTEGER NOT NULL);

CREATE TABLE playlist_item (
  item_id TEXT PRIMARY KEY,
  playlist_id TEXT NOT NULL REFERENCES show_playlist(playlist_id) ON DELETE CASCADE,
  sort_index INTEGER NOT NULL,
  ref_type TEXT NOT NULL CHECK (ref_type IN ('scene','timeline_segment','media','delay','command')),
  ref_uuid TEXT NOT NULL,
  trigger_mode TEXT NOT NULL DEFAULT 'go' CHECK (trigger_mode IN ('auto','go','delay','timecode')),
  delay_ms INTEGER NOT NULL DEFAULT 0,   -- trigger_mode=delay 时生效
  fade_ms INTEGER NOT NULL DEFAULT 0,
  loop INTEGER NOT NULL DEFAULT 0,
  note TEXT);

CREATE TABLE fixture_lib (              -- 灯具库(Phase 3 启用，先建表)
  lib_id TEXT PRIMARY KEY,
  brand TEXT NOT NULL, model TEXT NOT NULL, mode_name TEXT,
  channel_count INTEGER NOT NULL DEFAULT 0,
  format TEXT NOT NULL CHECK (format IN ('gdtf','xml','custom')),
  source_file TEXT,
  json_data TEXT,                        -- 解析后的通道/能力 JSON
  create_ms INTEGER NOT NULL);

CREATE TABLE project_fixture (          -- 工程内灯具实例(Phase 3 启用)
  fix_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  lib_id TEXT NOT NULL REFERENCES fixture_lib(lib_id),
  name TEXT NOT NULL,
  dmx_address INTEGER NOT NULL DEFAULT 1,
  dmx_universe INTEGER NOT NULL DEFAULT 1,
  pos_x REAL, pos_y REAL, pos_z REAL,
  group_ids TEXT,                        -- 逗号分隔分组
  create_ms INTEGER NOT NULL);

CREATE TABLE cue_light (                -- 灯光 Cue(Phase 3 启用)
  cue_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  name TEXT NOT NULL,
  style_tags TEXT,
  json_data TEXT NOT NULL,
  create_ms INTEGER NOT NULL);

CREATE TABLE pixel_program (            -- 像素灯带节目(Phase 3 启用)
  prog_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  name TEXT NOT NULL,
  json_data TEXT NOT NULL,
  controller_cfg TEXT,
  create_ms INTEGER NOT NULL);

CREATE TABLE device_list (              -- 中控设备(Phase 3 启用，先建表)
  dev_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  dev_name TEXT NOT NULL,
  dev_type TEXT NOT NULL CHECK (dev_type IN ('pdu','amp','dsp','matrix','projector','ac','led_sender','relay')),
  conn_type TEXT NOT NULL CHECK (conn_type IN ('tcp','rs232','relay')),
  ip_addr TEXT, port INTEGER,
  params TEXT,                           -- JSON: 串口参数/命令模板
  cmd_on TEXT, cmd_off TEXT, status_query TEXT,
  group_id TEXT,
  create_ms INTEGER NOT NULL);

CREATE TABLE device_action (            -- 时序动作脚本(Phase 3 启用)
  act_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL REFERENCES projects(project_id),
  name TEXT NOT NULL,
  actions TEXT NOT NULL,                 -- JSON: [{delay_ms, dev_id, cmd}]
  create_ms INTEGER NOT NULL);

CREATE TABLE system_log (
  log_id INTEGER PRIMARY KEY AUTOINCREMENT,
  log_time_ms INTEGER NOT NULL,
  log_level TEXT NOT NULL,
  source TEXT,
  message TEXT NOT NULL);
CREATE INDEX idx_log_time ON system_log(log_time_ms);

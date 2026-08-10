# DevThink 功能测试报告（打包前置）

> 生成时间：2026-08-10 · 目标：打包前把所有可测功能跑通，确认无回归后再出 dmg。
> 测试环境：WorkBuddy 沙箱（headless，无 macOS GUI 会话），因此 **GUI 交互类功能无法在此自动验证**，已改为「沙箱自动测 + 本机人工冒烟」两段式。

## 一、结论速览

| 维度 | 结果 | 说明 |
|------|------|------|
| 前端生产构建（`vite build`） | ✅ 通过 | 3.99s 编译完成，无错误/未解析模块 |
| 纯逻辑单元测试 | ✅ 20/20 通过 | codegen / editAgent / runtime / prompts |
| IPC 通道一致性 | ✅ 一致 | 26 个 invoke 全部有 handle；preview:log 配对 send |
| 配置加载（.devthinkrc → 豆包） | ✅ 正确 | baseUrl 填到 `/v3`，main 自动拼 `/chat/completions` |
| 免登录访客模式 | ✅ 代码确认 | `store.user` 缺省 `local`，`saveProject` 容错 |
| 截图 / 预览 / 头像 / AI 实测 | ⏳ 待本机 | 需在 Mac 桌面端人工冒烟（见第二节） |
| **是否可打包** | **暂缓** | 等本机冒烟全绿后，再执行 `electron:build` |

## 二、沙箱内已自动验证（A 组）

### A1. 前端生产构建
- 命令：`vite build --outDir /tmp/devthink_build --emptyOutDir`
- 结果：`✓ built in 3.99s`，产出 index + 全部 chunk（含 mermaid/pdf/katex）。
- 含义：当前 `src/**` 所有组件、store、runtime、codegen 均可编译，无 import 解析错误、无模板语法错误；`ai-avatar.png` 资源引用可被 Vite 解析（构建已包含）。

### A2. 纯逻辑单元测试（`_selftest.mjs`，20 项全过）
| 模块 | 用例 | 结果 |
|------|------|------|
| codegen | `parseDbTables` 解析 2 实体 / 无表回退 `item` | ✅ |
| codegen | `generateProjectFiles` 产出 backend/web/desktop/mobile/README | ✅ |
| codegen | **web vite 强制 `127.0.0.1:5180`（预览 IPv4 修复回归守卫）** | ✅ |
| codegen | backend `package.json` 合法 JSON + `node:sqlite` 启动 + 建表语句含 user/order | ✅ |
| codegen | 非法 name 兜底 `MyApp` | ✅ |
| codegen | `buildTree` 嵌套且目录在前 | ✅ |
| editAgent | `parseEditReply` 单对象 / 代码围栏剥离 / 非法 JSON / 缺字段 | ✅ |
| editAgent | `applyEdits` 命中替换 + 未命中新建 | ✅ |
| editAgent | `diffLines` 产生 same/add/del | ✅ |
| editAgent | `buildEditContext` 注入文档/文件列表/选定源码 | ✅ |
| runtime | 导出可用，Node 下 `PLATFORM==='browser'` | ✅ |
| runtime | `config.get` 无 localStorage 回退默认配置 | ✅ |
| runtime | 无 key 走 mock 回复（离线闭环可用） | ✅ |
| runtime | `plain()` 已定义且用于 ai.chat/projects.save（防 "An object could not be cloned"） | ✅ |
| runtime | 免登录 `store.user`/`saveProject` 容错 | ✅ |
| prompts | 8 个导出符号齐全（供 runtime/main 引用） | ✅ |

### A3. IPC 通道一致性（preload ↔ main）
- preload 暴露 26 个 `invoke` 通道，main 全部注册对应 `ipcMain.handle` —— **无缺漏、无孤立 handle**。
- `preview:log` 为 main→renderer 推送事件，已 `webContents.send('preview:log')` 配对 ✅。
- 含义：渲染进程调用的每个原生能力（账号/项目/AI/截图/预览/SSH/打包）在主进程都有实现，不会出现「调用即崩」。

### A4. 配置加载路径
- `electron/main.cjs` 的 `defaultConfig()`：优先读 `Resources/devthinkrc`（打包后）或根 `.devthinkrc`（开发），再与默认合并。
- 当前 `.devthinkrc`：`provider=doubao`、`baseUrl=https://ark.cn-beijing.volces.com/api/v3`、`model=doubao-seed-2-0-lite-260428`。
- `callAI` 自动拼接 `${baseUrl}/chat/completions` → 最终 `…/api/v3/chat/completions`，**无双重拼接**。✅
- `package.json` `build.extraResources` 已把 `.devthinkrc` 拷为 `devthinkrc` 进 `Resources/` ✅。

## 三、需本机 GUI 冒烟测试（B 组，请在你的 Mac 上跑）

> 沙箱无 GUI，以下必须在本机 `npm run dev` + `npm run electron:dev` 两终端（或装最新 dmg）验证。约 5 分钟。

| # | 测试项 | 操作步骤 | 预期 | 通过？ |
|---|--------|----------|------|--------|
| B1 | 免登录进入 | 打开 App，不登录 | 标题显示「本地用户」，左窗有「主方案 1」分支，无「账号不存在」报错 | ☐ |
| B2 | AI 对话（真实豆包） | 左窗输入一段业务想法 → 发送 | 出现微信头像 + 「对方正在输入…」动画，随后返回对齐式回复（不甩完整方案） | ☐ |
| B3 | 确认并开发 | 回复「确认」，或点「✅ 确认并开发」 | 自动生成设计文档并推送到右窗，「改文档/改代码」模式出现 | ☐ |
| B4 | 生成同源工程 | 右窗「生成同源全栈工程」 | 代码树出现 backend/web/desktop/mobile；提示 N 个文件 | ☐ |
| B5 | 实时预览 5180 | 右窗「启动预览」 | 约 5–30s 依赖安装后，黑框日志出现 `Local:`，iframe 打开 `http://127.0.0.1:5180` 且**不白屏** | ☐ |
| B6 | 系统截图 | 左窗「📷 截图」→ 拖选区域 | 截图自动以缩略图进输入框（Esc 取消则无操作） | ☐ |
| B7 | 图片上传 | 左窗「🖼️ 图片」选一张图 | 缩略图进输入框，可随消息发送 | ☐ |
| B8 | 头像显示 | 观察任意 AI 回复 | 每条 AI 消息顶部有微信头像 + 「AI」字样 | ☐ |
| B9 | 模型测试 | 设置里「测试连接」 | 返回成功（401=Key 问题；fetch failed=代理/网络） | ☐ |
| B10 | 改代码 | 右窗选中文件 → 左窗「🛠️ 改代码」描述改动 | 代码被改写、可「撤销上次改动」回退 | ☐ |
| B11 | 保存项目 | 点保存 | 提示「项目已保存到本地」，无 clone 报错 | ☐ |

> ⚠️ 注意：当前默认模型 `doubao-seed-2-0-lite` 为**纯文本**，截图/图片能展示但 AI 看不到图内容（B6/B7 发送时左窗会提示「纯文本模型无法识别图片」）。如需看图请换多模态模型。

## 四、已知约束与待办

1. **Windows 包**：本沙箱无 wine、且 Electron Windows 二进制下载被护栏拦截，**无法在此出 exe**。方案：推 GitHub 触发 `.github/workflows/build.yml` 三平台 CI，或在 Windows 机器跑 `npm run electron:build`。
2. **根目录 `main.cjs` 是冗余副本**：与 `electron/main.cjs` 内容完全相同（疑似误拷贝），`package.json` 实际加载的是 `electron/main.cjs`。建议删除根 `main.cjs` 以免混淆（删除前可 `git clean -n` 确认未跟踪）。
3. **豆包 Key 已随源码/配置**：公司内部分发无碍；若对外分发需先重置 Key。
4. **测试脚本留存**：`_selftest.mjs` 与本次构建验证可作为回归测试，下次改代码后 `node _selftest.mjs` 即可快速复跑。

## 五、打包指引（B 组全绿后执行）

```bash
# 1) 先确认两终端在跑或在最新 dmg 上验证完 B 组
# 2) 出 Mac dmg（沙箱内曾成功，152–153M；若 vite build 清 dist 被护栏拦，先 mv dist dist_bak_xxx）
npm run electron:build
# 产出 release/DevThink-0.1.0-arm64.dmg

# 3) 装到应用程序：xattr -cr DevThink-0.1.0-arm64.dmg 解隔离后拖入应用程序覆盖旧版
```

**下一步**：请你在本机跑完第二节 B1–B11（重点 B2/B5/B8/B9），全绿后回复「打包」，我即执行 `electron:build` 产出最终 dmg。

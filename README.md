# DevThink · AI 全栈研发工作台（一期 MVP 骨架）

按 PRD 落地的 **双窗口 AI 全链路开发工具** 一期可运行骨架：

- 左：逻辑思考窗口（AI 多轮对话 / 模式切换 / Mermaid / 内容标记 / 需求自检 / 快照 / 定稿推送）
- 右：成品开发窗口（代码工程 / 本地打包 / 服务器部署 三标签）
- 对话式改代码（二期）：生成工程后，左侧「🛠️ 改代码」用自然语言让 AI 读懂意图并改写文件，支持选中文件定向改、多文件改动、行级 diff 预览与一键撤销
- 生成前端实时预览（HMR，二期）：右侧「👁 实时预览」在桌面端启动 Vite dev server，内置窗口边改边看
- 本地账号（注册 / 登录 / 数据隔离）、可配置 AI 接口 + 离线 Mock 兜底
- 真实同源全栈代码生成（后端 Express + node:sqlite 持久化 / Web Vue3 / 桌面 Tauri / 移动 Android，23 个文件）
- 真实 SSH 连接测试 + 部署环境预检 + 真实一键部署（Electron 桌面端）：**静态站点** 与 **全栈（前端 + Express 后端 · systemd 自启 + Nginx 反代 /api）** 两种形态
- 拖拽分割比例的双栏固定布局

> 一期聚焦「主闭环可跑通」。真实代码生成、真实 SSH 预检已落地；**二期已落地「真实打包出包」「真实静态站点 + 全栈部署」「服务器监控」「对话式改代码（含行级 diff 与实时预览）」**（详见 [DEPLOY.md](DEPLOY.md)）。云端编译引擎、Docker 部署、手机伴侣、团队协同为后续规划。

## 运行

```bash
npm install            # 安装依赖（含 Electron 二进制，首次较慢）
npm run dev            # 浏览器原型：Vite 开发服务器 http://localhost:5174
npm run electron:dev   # 桌面端：先 npm run dev 起服务，再另开终端运行此命令
npm run build          # 仅构建前端（验证用）
npm run electron:build # 打包桌面安装包（dmg / nsis / AppImage）
```

> 注：若 `npm install` 后发现 `node_modules/electron/dist` 为空，执行 `node node_modules/electron/install.js`（或 `npm rebuild electron`）手动下载 Electron 二进制。桌面端需在有 GUI 的 macOS/Windows/Linux 会话运行；headless/无显示器环境无法启动窗口。

## AI 后端

本项目已**默认接入 DeepSeek**（`.devthinkrc` 预填了 Base URL 与 Key，已加入 `.gitignore` 防止泄露）：

- 打开即用真实模型：左侧对话、生成设计文档均走 DeepSeek（`deepseek-chat`，服务端现映射 `deepseek-v4-flash`）；
- 留空 Key → 回落内置 Mock，离线可演示完整闭环；
- 右上角「⚙️ AI 设置」可修改配置并点「测试连接」即时验证，Key 仅存于 Electron 主进程（不暴露给渲染页）。

> 安全提示：`.devthinkrc` 含明文 Key，请勿提交进仓库；若已在对话中泄露，建议在对应服务商后台轮换。

### 支持的 AI 服务商

「⚙️ AI 设置」里新增**服务商下拉**，选中即自动填入接口地址与推荐模型（密钥需自备）：

| 服务商 | 接口地址 | 推荐模型 | 思考链 |
|--------|----------|----------|--------|
| DeepSeek（默认） | `https://api.deepseek.com/v1` | `deepseek-chat` / `deepseek-reasoner` | `reasoner` 原生思考 |
| 豆包（火山方舟） | `https://ark.cn-beijing.volces.com/api/v3` | 模型 ID（如 `doubao-seed-2-0-lite-260428`）或端点 ID（`ep-xxxx`） | `doubao-thinking-*` |
| 通义千问（DashScope） | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `qwen-plus` / `qwen-max` / `qwen3-max` 等 | `qwen3` 系列原生思考 |
| 自定义 | 任意兼容 OpenAI 协议地址 | 按服务商填 | 取决于模型 |

所有服务商均走 OpenAI 兼容的 `/chat/completions`，原生思考模型（DeepSeek reasoner / 千问 qwen3 / 豆包 thinking）的 `reasoning_content` 会自动展示在「💭 思考过程」折叠块里。

## 代码生成

左侧「定稿推送」后，右侧「代码工程」一键生成**真实可运行**的同源全栈工程（从设计文档的库表自动解析实体）：

- 后端：`Express` + `node:sqlite` 真实持久化，自动生成 CRUD 接口与 `db/init.sql`；
- Web 前端：`Vue3 + Vite + axios`，含路由、Dashboard、实体列表页；
- 桌面端：`Tauri` 工程骨架；
- 移动端：`Android` 工程骨架。

生成的工程在 Electron 桌面端会自动写入 `userData/generated/<项目名>/`。

## 服务器部署

右侧「🚀 服务器部署」现已支持**真实一键部署**，提供两种形态：

- **静态站点**（仅前端静态托管）：环境预检 → 构建 → SFTP 上传 → Nginx 静态根 → 可选 SSL → 防火墙 → 日志轮转；
- **全栈**（默认，前端 + Express 后端）：在静态基础上额外「上传后端 → 服务器装依赖 → 注册 systemd 开机自启 → Nginx 反代 `/api` 到本地后端（端口 3001）→ 每日数据库备份」。

服务器表单的「部署形态」下拉切换即可；**全栈要求服务器 Node ≥ 22**（后端用内置 `node:sqlite`）。完整架构、步骤、前置条件与安全须知见 [DEPLOY.md](DEPLOY.md)。

- 添加 Linux 服务器（IP / SSH 端口 / 账号 / 密码或密钥；可选填域名 + 邮箱以启用 HTTPS）；
- 「测试连接」真实 SSH 登录并预检环境（uname / node / nginx / 磁盘 / 当前用户）；
- 「一键部署」全程真实执行：环境预检 → 本地构建静态产物 → 远程建目录 → SFTP 上传 → 部署 Nginx 并 reload → 可选 SSL（certbot）→ 防火墙放行 80/443 → 日志备份（logrotate）；
- 部署后可切到同一标签页底部的「📊 运维监控」：通过真实 SSH 采集 CPU / 内存 / 磁盘 / Nginx / 站点指标，命中阈值（CPU≥85% / 内存≥90% / 磁盘≥90% 等）告警并回流左侧窗口（详见 [DEPLOY.md](DEPLOY.md) 的「运维监控」章节）。

SSH 密钥与密码仅在 Electron 主进程中使用，不暴露给渲染页与网络。

## 对话式改代码（二期「边做边改 / 边改边做」）

右侧「代码工程」生成同源工程后，左侧会出现「🛠️ 改代码」模式：用自然语言描述改动，AI 会读懂你的意图、定位要改的文件，并把修改后的**完整内容**直接写回工程（内存 + 桌面端磁盘）。

- 在右侧「代码工程」点开某个文件，再回到左侧「🛠️ 改代码」描述需求，AI 会优先改你选中的文件；
- 不选文件时，AI 自行判断该改哪个文件（可一次改多个），并在对话里给出每条改动的中文说明；
- 右侧顶部会出现「最近一次对话式改动」卡片：**行级 diff 预览**（绿=新增 / 红=删除）让你一眼看清改了哪几行，点「↩️ 撤销上次改动」即可回退（新建文件也会一并删除）；
- 同一卡片还提供「📦 重新打包」「🚀 重新部署」按钮，以及「改完自动重新打包」开关（开启后每次改动自动触发真实打包）；
- 真实模型需在「设置」中填入 AI Key（桌面端已预置 DeepSeek Key）；未填 Key 时走离线 Mock，仅追加一行演示注释以验证闭环。

### 生成前端实时预览（HMR）

右侧新增「👁 实时预览」标签页：在桌面端自动对生成的 `web` 工程执行 `npm install` 并启动 Vite 开发服务器（端口 5180），在内置窗口实时查看 UI 改动效果——配合「🛠️ 改代码」做到**边改边看**。

- 需先在桌面端「生成同源全栈工程」（工程已写入本地磁盘 `userData/generated/<项目名>/web`）；
- 浏览器预览模式无本地运行环境，会提示「需在桌面端运行」；
- 生成前端的 `vite.config.js` 默认把 `/api` 代理到后端 `3001`；只看 UI 时可不改，要联调 CRUD 则需另启后端。

## 目录

```
DevThink/
├─ electron/main.js        # 主进程：窗口 / 存储 / 账号 / AI 调用 / SSH·SFTP
├─ electron/preload.js     # 安全桥接（contextIsolation）
├─ src/
│  ├─ App.vue              # 双窗口外壳 + 顶栏 + 分割条
│  ├─ store.js             # 响应式状态 + 业务动作
│  ├─ runtime.js           # 环境抽象（Electron IPC / 浏览器兜底）
│  ├─ codegen.js           # 设计文档 → 同源全栈源码生成器
│  ├─ packager.js          # 真实打包编排器（PRD 二期「真实打包出包」）
│  ├─ deploy.js            # 真实部署编排器（静态站点 + 全栈，PRD 二期「真实服务器部署」）
│  ├─ editAgent.js         # 对话式代码修改编排器（PRD 二期「边做边改」）
│  ├─ ai/{prompts,mock}.js # 提示词 + 离线 Mock
│  └─ components/          # 左/右面板、Markdown、Mermaid、账号、设置
├─ DEPLOY.md               # 真实服务器部署文档
├─ vite.config.js
└─ package.json
```

# DevThink 真实服务器部署（静态站点 / 全栈）

> 把左侧生成的设计文档 → 右侧「一键打包」产出的工程，通过 SSH 真实部署到一台 Linux 服务器。**两种形态**：静态站点（仅前端静态托管）与全栈（前端静态托管 + Express 后端 `systemd` 开机自启 + Nginx 反代 `/api`）。无需你手写任何命令。

> 在右侧「🚀 服务器部署」添加服务器时，用「部署形态」下拉选择 `全栈` 或 `静态`；默认为 `全栈`（更完整）。两种形态步骤的前 8 步一致，全栈额外多了「安装后端依赖 / 注册 systemd / /api 反代」等步骤。

适用版本：DevThink 0.1.0+（二期「真实服务器部署」）。本文档与代码同步维护；若行为与文档不符，以 `src/deploy.js` 为准。

---

## 你最终得到什么

点完「一键部署」后，服务器上会出现：

- 站点根目录 `/var/www/<软件名>/`，里面是构建好的静态文件（`index.html` + 资源）；
- （全栈形态）后端服务目录 `/opt/<软件名>/`，已 `npm install --omit=dev` 安装 express / cors 依赖，并由 **systemd** 注册为开机自启服务（崩溃自动重启，端口 3001）；
- 一个启用的 Nginx 站点配置（`/etc/nginx/sites-available/<软件名>.conf` → `sites-enabled/` 软链），已 `nginx -t` 校验并 reload；全栈形态下该配置额外把 `/api/` 反向代理到 `http://127.0.0.1:3001`（即本地后端），实现前端与后端同源部署；
- （可选）HTTPS：若填写了合法域名 + 邮箱且服务器已装 `certbot`，自动申请证书并开启 80→443 重定向；
- 防火墙放行 80/443（仅当服务器 `ufw` 处于 active）；
- 日志轮转配置 `/etc/logrotate.d/<软件名>`。

访问方式：`http://<服务器IP>/`（或 `https://<域名>/`，若启用了 SSL）。

---

## 工作原理（一图概览）

```
DevThink 桌面端（Electron）
  │  1. 本地构建：npm install + vite build → web/dist
  │  2. SFTP 上传 web/dist ──────────────┐
  │  3. SSH 执行：建目录 / 写 Nginx / reload / ufw / logrotate
  ▼                                      ▼
你的 Linux 服务器（IP + SSH）
  /var/www/<name>/  ←──  Nginx 站点  ←──  浏览器访问
```

所有远程操作都通过 **Electron 主进程里的 `ssh2`** 真实执行；SSH 密码 / 密钥只留在主进程，不进入渲染页，也不经网络外传。

### 全栈形态额外拓扑

```
浏览器 ──http──▶ Nginx (80/443)
                  ├─ /          → 静态文件  /var/www/<name>/
                  └─ /api/      → 反代     http://127.0.0.1:3001
                                       │
                                  Express 后端（systemd 托管）
                                       ├─ 端口 3001（Restart=always 开机自启）
                                       └─ node:sqlite 数据库 /opt/<name>/<name>.db
```

前端构建产物仍托管在 `/var/www/<name>`；后端代码在 `/opt/<name>`，由 systemd 守护常驻；Nginx 把 `/api` 转发到本机后端端口，对访问者而言前端与后端同源同域名。

---

## 前置条件

**服务器端（目标 Linux 主机）**

- 开通 SSH（默认 22，可改端口），你能用密码或密钥登录（建议 `root` 或具备 `sudo` 的账号）；
- 已安装 **Nginx**（`apt install nginx`）。部署脚本会检测，缺失则在「部署 Nginx 配置」步骤如实报错中止；
- **（全栈形态必需）已安装 Node.js ≥ 22**：生成的 Express 后端使用内置 `node:sqlite`（需 Node 22.5+ 且带 `--experimental-sqlite` 启动参数）。脚本在预检步骤会检测 `node --version`，低于 22 直接报错中止并提示升级（如 `nvm install 22`）；
- 磁盘有剩余空间（`df -h /` 预检可见）；
- （可选）若想用 HTTPS：域名已解析到该服务器 IP，且已装 `certbot`（`apt install certbot python3-certbot-nginx`）；
- （可选）若用云厂商安全组而非 `ufw`，请自行在控制台放行 80/443——脚本检测到 `ufw` 未启用时会跳过，不影响站点本身。

**DevThink 端**

- 必须在 **桌面端（Electron）** 中操作。浏览器预览环境没有 SSH/SFTP 能力，只能看到流程，不会真正部署；
- 已生成同源工程（右侧「代码工程」点过「生成同源全栈工程」）；若没生成，点「一键部署」会自动先生成再部署。

---

## 操作步骤

### 1. 添加服务器

右侧「🚀 服务器部署」→「添加服务器」填写：

| 字段 | 说明 | 示例 |
|------|------|------|
| 名称 | 便于识别 | 生产环境 |
| IP / 域名 | 服务器公网 IP | `118.24.52.156` |
| SSH 端口 | 默认 22 | `22` |
| SSH 账号 | 登录用户 | `root` |
| 认证方式 | 密码 / 密钥 | 密码 |
| 密码/密钥 | 对应凭证 | `********` |
| 域名（可选） | 填了且填邮箱可启用 HTTPS | `app.example.com` |
| SSL 邮箱（可选） | Let's Encrypt 证书注册邮箱 | `admin@example.com` |
| 部署形态 | `全栈`（默认，前端 + 后端）或 `静态`（仅前端） | `全栈` |

### 2. 测试连接

点「测试连接」→ 真实 SSH 登录并预检（输出 `uname / node / nginx / 磁盘 / 当前用户`）。连接成功状态变绿，失败会显示原因。

### 3. 一键部署

点「一键部署」→ 弹出二次确认（写目录 / 上传 / 改 Nginx，建议在测试服务器先验证）→ 确认后逐步真实执行。每一步的实时输出会显示在「部署进度」里，失败时标红并可展开详情。

---

## 真实执行的 8 个步骤

| # | 步骤 | 真实动作 | 失败处理 |
|---|------|----------|----------|
| 1 | 环境预检 | `ssh.exec`：uname / node / nginx -v / df / whoami | 连不上或 nginx 缺失→报错并中止后续 |
| 2 | 本地构建静态产物 | 在 DevThink 机器写盘 + `npm install` + `vite build --base ./` | 构建失败→中止 |
| 3 | 创建远程目录 | `ssh.exec`：`rm -rf /var/www/<name> && mkdir -p ...` | 失败→中止 |
| 4 | 上传静态文件 | `ssh.sftp` 递归上传 `web/dist` → `/var/www/<name>/` | 失败→中止 |
| 5 | 部署 Nginx 配置 | SFTP 上传 conf → `ln -s` → `nginx -t` → `reload` | 失败→中止（关键步） |
| 6 | 申请 SSL（可选） | 仅当填了合法域名+邮箱：`certbot --nginx` | 缺 certbot 或失败→标记「跳过」，保留 HTTP |
| 7 | 防火墙放行 | `ufw allow 80/tcp 443/tcp`（仅 ufw active 时） | ufw 未启用→标记「跳过」 |
| 8 | 日志与备份 | SFTP 上传 `logrotate.d/<name>`（保留 14 天压缩日志） | 失败→报错（不影响站点） |

第 1–5 步为关键步，全部成功才判定部署成功；第 6–8 步为增强项，跳过不影响站点可访问。

---

## 安全须知

- **凭证不落地前端**：SSH 密码 / 密钥仅在 Electron 主进程内存中使用，渲染页与网络均拿不到；
- **仅桌面端可执行**：浏览器预览模式下 `runtime.ssh` 为空，部署会提示「需桌面端」，不会误触发；
- **目录隔离**：站点固定写到 `/var/www/<软件名>/`（`<软件名>` 由打包名净化得到，仅含 `[A-Za-z0-9_-]`），不会触碰服务器上其他站点；
- **写前清空**：第 3 步会 `rm -rf /var/www/<name>` 再重建，确保无残留旧文件——请确认该目录专用于本项目；
- **二次确认**：真正触碰服务器前弹 `confirm`，建议先用测试机跑通；
- **生产服务器操作**：对 `118.24.52.156` 这类生产机，请先用测试账号/测试目录验证，或手动在服务器建好 `/var/www/<name>` 后再部署。

---

## 故障排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 测试连接失败 | IP/端口/账号/密码错；服务器 SSH 未开；网络不通 | 核对凭证；`ssh -p <port> <user>@<ip>` 手动验证 |
| 预检报 `NO_NGINX` | 服务器未装 Nginx | `apt install nginx` 后重试 |
| 本地构建失败 | DevThink 机器无 node_modules / 无网络 | 桌面端先 `npm install`；查看步骤 2 详情里的 npm 报错 |
| 上传失败 | 磁盘满 / 权限不足 / 路径含特殊字符 | 清磁盘；用 root 或 sudo 账号；软件名只用字母数字 |
| Nginx 步骤报错 | `nginx -t` 配置语法错 / reload 失败 | 展开步骤 5 详情，按报错修（多为域名/路径问题） |
| SSL 标记「跳过」 | 未填域名+邮箱 / 未装 certbot | 填合法域名邮箱并重跑；或手动 `certbot --nginx -d <域名>` |
| 站点能访问但样式错乱 | 未用相对 base 构建 | 部署固定用 `vite build --base ./`，确保刷新后仍正常 |
| 预检报 `Node < 22` | 服务器 Node 版本过低，后端用 node:sqlite 需 ≥22 | 升级 Node（如 `nvm install 22 && nvm alias default 22`），或手动装 Node 22 后重跑 |
| 后端服务启动失败（UNIT_NOT_ACTIVE） | 依赖缺失 / 端口冲突 / 代码错误 | 展开步骤 6 详情看 `journalctl` 日志；常见为 `npm install` 未装好或 3001 端口被占 |
| 前端能开但接口 502 / 404 | Nginx 未反代 `/api` 或后端未起 | 确认部署形态为全栈；`systemctl status <name>` 看后端；`nginx -t` 看配置 |
| 接口偶发连不上 | 后端崩溃未重启 | 确认 systemd `Restart=always` 已生效（`systemctl is-enabled <name>`） |

---

## 运维监控（真实 SSH 采集）

部署完成后，在「🚀 服务器部署」标签页底部的「📊 运维监控」卡片里，可以实时查看这台服务器的健康状态。采集通过 `runtime.ssh.exec` 在服务器本地真实执行，不是估算值。

### 采集的指标

| 指标 | 来源命令 | 说明 |
|------|----------|------|
| CPU 使用率 | `top -bn1 \| grep %Cpu` | 100 − idle；含最近 N 次趋势折线 |
| 内存 | `free -m` | 总量 / 已用 / 使用率% |
| 磁盘 | `df -h /`（及 `/var/www`） | 根分区使用率（站点目录若有则一并展示） |
| 负载 | `uptime` | 1 分钟 load average |
| Nginx | `systemctl is-active nginx` + `ss -s` | active/inactive 状态与 TCP 连接数 |
| 站点 HTTP | `curl -w '%{http_code}'` | 有域名测 `https://域名`，否则测本地 `http://127.0.0.1` |

### 怎么用

1. 在监控卡片的「服务器」下拉里选择一台已添加且填了密码/密钥的服务器；
2. 点「🔄 刷新」拉取一次；或勾选「自动（10s）」每 10 秒轮询；
3. CPU/内存/磁盘以进度条 + 颜色分级展示（绿=正常，黄=达阈值 75%，红=达阈值）；
4. 切到其它标签页会自动停止轮询，避免无谓的 SSH 连接。

### 告警与回流

命中以下阈值会产生告警（60 秒内同类型去重），并显示在监控卡片的告警列表里：

- CPU ≥ 85%
- 内存 ≥ 90%
- 磁盘（根分区）≥ 90%
- Nginx 状态非 active（且非 unknown）
- 站点返回非 2xx/3xx（仅当填写了域名）

告警会**回流左侧「逻辑思考窗口」**——以一条 AI 气泡形式插入当前活跃方案分支，方便你在和 AI 讨论需求时顺带看到线上异常。

> 同样，真实监控只在桌面端（Electron）执行；浏览器预览会提示「需桌面端」，不会真正连服务器。

---

## 全栈部署（前端 + Express 后端）

当服务器表单的「部署形态」选为 `全栈`（默认）时，「一键部署」会在静态部署的基础上多出以下真实步骤，最终形成一个「前端静态托管 + 后端 API 常驻」的完整站点。

### 全栈专属步骤（在静态 8 步之外）

| # | 步骤 | 真实动作 | 失败处理 |
|---|------|----------|----------|
| 4（扩展） | 上传后端源码 | `ssh.sftp` 递归上传 `backend/`（不含 node_modules）→ `/opt/<name>/` | 失败→中止 |
| 5 | 安装后端依赖 | `ssh.exec`：`cd /opt/<name> && npm install --omit=dev --registry=npmmirror` | 失败→中止 |
| 6 | 注册 systemd 并启动 | 上传 `/etc/systemd/system/<name>.service` → `daemon-reload`/`enable`/`restart` → 校验 `is-active`（失败则回显 `journalctl` 日志） | 服务未起→中止并给出诊断日志 |
| 7（扩展） | Nginx 反代 `/api` | 配置新增 `location /api/ { proxy_pass http://127.0.0.1:3001; ... }` | 失败→中止（关键步） |
| 10（扩展） | 日志轮转 | 部署 `/etc/logrotate.d/<name>`（保留 14 天压缩日志） | 失败→报错（不影响站点） |

> 静态形态的前 8 步（预检 / 构建 / 建目录 / 上传前端 / Nginx 静态根 / SSL / 防火墙 / 日志轮转）全栈形态同样执行，只是第 7 步的 Nginx 配置多了 `/api` 反代块。

### 生成的 systemd 单元（示例）

```ini
[Unit]
Description=myapp backend (DevThink auto-deploy)
After=network.target

[Service]
Type=simple
WorkingDirectory=/opt/myapp
ExecStart=/usr/bin/node --experimental-sqlite /opt/myapp/src/index.js
Environment=PORT=3001
Environment=NODE_ENV=production
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

- `Restart=always` 保证进程崩溃或被 kill 后自动拉起，且开机自启（`systemctl enable`）；
- `ExecStart` 使用预检阶段探测到的 `node` 绝对路径（避免 systemd 环境无 PATH）；
- 后端数据库文件 `<name>.db` 落在工作目录 `/opt/<name>/`，**未启用自动备份**（按需求不配置；如需数据保护请自行定期拷贝该文件）。

### 生成的 Nginx 反代（全栈形态多出的关键块）

```nginx
location /api/ {
    proxy_pass http://127.0.0.1:3001;
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
}
```

`proxy_pass` 不带 URI 后缀，因此请求 `/api/xxx` 会原样转发到后端 `/api/xxx`（与生成后端 `app.use('/api', router)` 一致）。前端构建时使用 `baseURL: '/api'`，生产环境所有接口请求经 Nginx 反代到本地后端，无需跨域。

---

## 当前范围与后续

**已落地（静态站点 + 全栈 + 监控）**：

- 静态站点：前端构建产物 → Nginx 静态托管，含可选 HTTPS、防火墙、日志轮转；
- 全栈：在静态基础上额外部署 Express 后端（systemd 开机自启 + Nginx 反代 `/api`）；
- 运维监控：基于真实 SSH 的 CPU/内存/磁盘/Nginx/站点指标采集 + 阈值告警回流左侧窗口（见上）。

**规划中（非本次范围）**：

- Docker 化：前后端打进 `docker-compose`，含开机自启与端口放行；
- 日志在线查看：在监控卡片内直接 tail 服务器日志（当前为指标 + 告警，日志查看待接入）。

> 范围说明：静态与全栈两种形态均已落地，在右侧服务器表单的「部署形态」下拉切换即可；底层统一复用 `runtime.ssh.exec` / `runtime.ssh.sftp`。如需 Docker 形态，在 `src/deploy.js` 中新增对应步骤即可。

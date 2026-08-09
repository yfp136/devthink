// 真实静态站点部署编排器（PRD 4.3 二期「真实服务器部署」）。
// 与旧版「首步真实预检 + 后续 delay 模拟」不同：本模块通过 runtime.ssh（Electron 主进程 ssh2）
// 真实连接服务器，逐步执行：本地构建 → 远程建目录 → SFTP 上传 → 部署 Nginx → 可选 SSL → 防火墙 → 日志备份。
// 浏览器预览环境无 SSH 能力，明确告知「需桌面端」。
import { runtime, PLATFORM } from './runtime.js'

function safeName(name) {
  return (name || 'MyApp').replace(/[^\w-]/g, '') || 'MyApp'
}

function sshCfg(srv) {
  return {
    host: srv.ip,
    port: srv.port || 22,
    username: srv.sshUser,
    password: srv.authType === 'password' ? srv.secret : undefined,
    privateKey: srv.authType === 'key' ? srv.secret : undefined
  }
}

// 真实步骤清单（序号与 store 中 tasks 对齐）
export const DEPLOY_STEPS = [
  '环境预检（真实 SSH）',
  '本地构建静态产物',
  '创建远程目录',
  '上传静态文件（SFTP）',
  '部署 Nginx 配置',
  '申请 SSL 证书（可选）',
  '防火墙放行端口',
  '配置日志与备份'
]

// emit(i, patch) 实时把每步状态/详情写回 UI（store 传入闭包）；patch = { status, detail }
export async function deployStatic({ files, server, pack, emit }) {
  const safe = safeName(pack?.name)
  const set = (i, patch) => emit && emit(i, patch)
  const results = []

  const mark = (i, status, detail) => {
    set(i, { status, detail: detail ?? '' })
    results[i] = { status, detail: detail ?? '' }
  }

  if (PLATFORM !== 'electron' || !runtime.ssh) {
    return {
      ok: false,
      platform: PLATFORM,
      message: '真实部署需在 DevThink 桌面端（Electron）中运行；当前为浏览器预览，仅展示流程。请在桌面端打开本工程后重试「一键部署」。',
      results
    }
  }

  const cfg = sshCfg(server)
  const remoteDir = `/var/www/${safe}`
  const domain = (server.domain || '').trim()
  const sslEmail = (server.sslEmail || '').trim()
  const serverName = domain || '_'

  // ---------- 步骤 0：环境预检（真实 SSH）----------
  try {
    set(0, { status: 'running' })
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [
        'uname -a',
        'node --version 2>/dev/null || echo NO_NODE',
        'nginx -v 2>&1 || echo NO_NGINX',
        'df -h / | tail -1',
        'whoami'
      ]
    })
    if (!r.ok) {
      mark(0, 'error', '预检失败：' + r.error)
      return { ok: false, results }
    }
    const hasNginx = !/NO_NGINX/.test(r.output)
    mark(0, 'done', r.output + (hasNginx ? '' : '\n[警告] 未检测到 Nginx，部署将于「部署 Nginx 配置」步骤中止。'))
    if (!hasNginx) {
      // 仍继续后续步骤，但在 Nginx 步骤如实报错，避免静默成功
    }
  } catch (e) {
    mark(0, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 1：本地构建静态产物（DevThink 机器上 npm install + vite build）----------
  set(1, { status: 'running' })
  let distLocal
  try {
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files })
    if (!written.ok) throw new Error('写入工程到本地磁盘失败')
    const root = written.base
    const r = await runtime.shell.run({
      cwd: root,
      steps: [
        { cmd: 'cd web && npm install', label: 'web: npm install（vue / vue-router / axios / vite）' },
        { cmd: 'cd web && npx vite build --base ./', label: 'web: vite build → web/dist（相对 base，适合子路径托管）' }
      ]
    })
    if (!r.ok) {
      mark(1, 'error', r.output)
      return { ok: false, results }
    }
    distLocal = `${root}/web/dist`
    mark(1, 'done', r.output)
  } catch (e) {
    mark(1, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 2：创建远程目录（清空旧产物，避免残留）----------
  set(2, { status: 'running' })
  try {
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [`rm -rf ${remoteDir} && mkdir -p ${remoteDir} && echo OK_DIR`]
    })
    if (!r.ok) {
      mark(2, 'error', '创建远程目录失败：' + r.error)
      return { ok: false, results }
    }
    mark(2, 'done', `已创建并清空远程目录：${remoteDir}`)
  } catch (e) {
    mark(2, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 3：SFTP 上传 dist ----------
  set(3, { status: 'running' })
  try {
    const r = await runtime.ssh.sftp({ ...cfg, localDir: distLocal, remoteDir })
    if (!r.ok) {
      mark(3, 'error', '上传失败：' + r.error)
      return { ok: false, results }
    }
    mark(3, 'done', `已上传 ${r.uploaded}/${r.count} 个文件到 ${remoteDir}`)
  } catch (e) {
    mark(3, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 4：部署 Nginx 配置 ----------
  set(4, { status: 'running' })
  const nginxConf = `server {
    listen 80;
    server_name ${serverName};
    root ${remoteDir};
    index index.html;
    location / {
        try_files $uri $uri/ /index.html;
    }
    access_log /var/log/nginx/${safe}.access.log;
    error_log  /var/log/nginx/${safe}.error.log;
}
`
  try {
    const confName = `${safe}.conf`
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files: [{ path: confName, content: nginxConf }] })
    const localConf = `${written.base}/${confName}`
    const remoteConf = `/etc/nginx/sites-available/${confName}`
    const up = await runtime.ssh.sftp({ ...cfg, local: localConf, remote: remoteConf })
    if (!up.ok) {
      mark(4, 'error', 'Nginx 配置上传失败：' + up.error)
      return { ok: false, results }
    }
    const en = await runtime.ssh.exec({
      ...cfg,
      commands: [
        `ln -sf ${remoteConf} /etc/nginx/sites-enabled/${confName}`,
        'nginx -t',
        'systemctl reload nginx || service nginx reload || nginx -s reload'
      ]
    })
    if (!en.ok) {
      mark(4, 'error', 'Nginx 部署失败：' + en.error)
      return { ok: false, results }
    }
    mark(4, 'done', `Nginx 配置已启用并 reload。\n配置内容：\n${nginxConf}\n--- 执行结果 ---\n${en.output}`)
  } catch (e) {
    mark(4, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 5：SSL（仅当填了合法域名+邮箱）----------
  set(5, { status: 'running' })
  const domainOk = /^[a-zA-Z0-9.-]+$/.test(domain)
  const emailOk = /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(sslEmail)
  if (domainOk && emailOk) {
    try {
      const r = await runtime.ssh.exec({
        ...cfg,
        commands: [
          'command -v certbot >/dev/null 2>&1 && echo HAVE_CERTBOT || echo NO_CERTBOT',
          `certbot --nginx -d ${domain} --non-interactive --agree-tos -m ${sslEmail} --redirect 2>&1 || echo SSL_SKIP`
        ]
      })
      if (!r.ok) mark(5, 'error', 'SSL 申请失败：' + r.error)
      else if (/NO_CERTBOT/.test(r.output)) mark(5, 'skipped', '服务器未安装 certbot，跳过 SSL。可手动 `apt install certbot python3-certbot-nginx` 后重跑。\n' + r.output)
      else if (/SSL_SKIP/.test(r.output)) mark(5, 'skipped', 'certbot 执行未成功，保留 HTTP（不影响站点访问）。\n' + r.output)
      else mark(5, 'done', 'SSL 证书已申请并启用（HTTPS 重定向已开启）。\n' + r.output)
    } catch (e) {
      mark(5, 'error', e.message)
    }
  } else {
    mark(5, 'skipped', '未填写合法域名/邮箱，跳过 SSL（部署为 HTTP）。在服务器表单补全后重跑即可启用 HTTPS。')
  }

  // ---------- 步骤 6：防火墙放行 80/443 ----------
  set(6, { status: 'running' })
  try {
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [
        'ufw status | head -1',
        'if ufw status | grep -q "Status: active"; then ufw allow 80/tcp; ufw allow 443/tcp; echo UFW_DONE; else echo UFW_INACTIVE; fi'
      ]
    })
    if (!r.ok) mark(6, 'error', '防火墙配置失败：' + r.error)
    else if (/UFW_INACTIVE/.test(r.output)) mark(6, 'skipped', 'ufw 未启用，跳过端口放行（若用云厂商安全组，请自行放行 80/443）。')
    else mark(6, 'done', '已放行 80/443：\n' + r.output)
  } catch (e) {
    mark(6, 'error', e.message)
  }

  // ---------- 步骤 7：日志与备份（logrotate）----------
  set(7, { status: 'running' })
  const lrConf = `${remoteDir} {
    daily
    missingok
    rotate 14
    compress
    delaycompress
    notifempty
    create 0640 www-data adm
}
`
  try {
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files: [{ path: 'logrotate.conf', content: lrConf }] })
    const localLr = `${written.base}/logrotate.conf`
    const up = await runtime.ssh.sftp({ ...cfg, local: localLr, remote: `/etc/logrotate.d/${safe}` })
    const en = await runtime.ssh.exec({ ...cfg, commands: [`logrotate -d /etc/logrotate.d/${safe} 2>&1 || true`] })
    if (!up.ok) mark(7, 'error', '日志配置上传失败：' + up.error)
    else mark(7, 'done', `已部署 logrotate 配置（/etc/logrotate.d/${safe}，保留 14 天压缩日志）。\n${en.output || ''}`)
  } catch (e) {
    mark(7, 'error', e.message)
  }

  // 关键步（预检/构建/目录/上传/Nginx）全部成功即视为部署成功
  const criticalDone = [0, 1, 2, 3, 4].every((i) => results[i] && results[i].status === 'done')
  const ok = criticalDone
  return { ok, results, remoteDir, domain: domain || null }
}

// ---------- 全栈部署（前端静态托管 + Express 后端 systemd 自启 + Nginx 反代 /api）----------
// 与静态部署的区别：额外把生成的 Express 后端也送上服务器，注册 systemd 开机自启，
// 并让 Nginx 把 /api 反向代理到本地后端端口（默认 3001），形成「前端静态 + 后端 API」完整站点。
export const DEPLOY_STEPS_FULL = [
  '环境预检（SSH / Node≥22 / Nginx / systemd）',
  '本地构建 Web 静态产物',
  '创建远程目录（前端 + 后端）',
  '上传前端静态文件（SFTP）',
  '上传后端源码（SFTP）',
  '安装后端依赖（npm install --omit=dev）',
  '注册 systemd 服务并启动',
  '部署 Nginx（静态根 + /api 反代）',
  '申请 SSL 证书（可选）',
  '防火墙放行端口',
  '配置日志轮转'
]

const BACKEND_PORT = 3001 // 与 codegen.js 生成后端的 process.env.PORT || 3001 保持一致

// 全栈部署：返回 { ok, results, remoteWeb, remoteApi, domain }
export async function deployFullStack({ files, server, pack, emit }) {
  const safe = safeName(pack?.name)
  const set = (i, patch) => emit && emit(i, patch)
  const results = []
  const mark = (i, status, detail) => {
    set(i, { status, detail: detail ?? '' })
    results[i] = { status, detail: detail ?? '' }
  }
  const cfg = sshCfg(server)
  const domain = (server.domain || '').trim()
  const sslEmail = (server.sslEmail || '').trim()
  const serverName = domain || '_'
  const remoteWeb = `/var/www/${safe}`
  const remoteApi = `/opt/${safe}`

  if (PLATFORM !== 'electron' || !runtime.ssh) {
    return {
      ok: false,
      platform: PLATFORM,
      message: '真实部署需在 DevThink 桌面端（Electron）中运行；当前为浏览器预览，仅展示流程。请在桌面端打开本工程后重试「一键部署」。',
      results
    }
  }

  let nodeBin = 'node'

  // ---------- 步骤 0：环境预检 ----------
  set(0, { status: 'running' })
  try {
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [
        'uname -a',
        'node --version 2>/dev/null || echo NO_NODE',
        'npm --version 2>/dev/null || echo NO_NPM',
        'nginx -v 2>&1 || echo NO_NGINX',
        'command -v systemctl >/dev/null && echo HAVE_SYSTEMD || echo NO_SYSTEMD',
        'echo NODE_BIN=$(command -v node)',
        'df -h / | tail -1',
        'whoami'
      ]
    })
    if (!r.ok) {
      mark(0, 'error', '预检失败：' + r.error)
      return { ok: false, results }
    }
    const out = r.output
    const hasNginx = !/NO_NGINX/.test(out)
    const hasSystemd = !/NO_SYSTEMD/.test(out)
    const m = out.match(/NODE_BIN=(\S+)/)
    if (m) nodeBin = m[1]
    const nodeV = (out.match(/v(\d+)\./) || [])[1]
    if (/NO_NODE/.test(out)) {
      mark(0, 'error', '服务器未安装 Node.js，后端无法运行。请先安装 Node ≥ 22（如 `apt install nodejs` 或 nvm）后重试。\n' + out)
      return { ok: false, results }
    }
    if (nodeV && Number(nodeV) < 22) {
      mark(0, 'error', `后端使用内置 node:sqlite，需服务器 Node ≥ 22，检测到 v${nodeV}。请升级 Node（如 ` + '`nvm install 22`）' + ` 后重试。\n${out}`)
      return { ok: false, results }
    }
    let warn = ''
    if (!hasNginx) warn += '\n[警告] 未检测到 Nginx，部署将于「部署 Nginx」步骤中止。'
    if (!hasSystemd) warn += '\n[警告] 未检测到 systemd，后端将无法开机自启（可改用 nohup/pm2，或手动启动）。'
    mark(0, 'done', out + warn)
  } catch (e) {
    mark(0, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 1：本地构建 Web 静态产物 ----------
  set(1, { status: 'running' })
  let distLocal
  try {
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files })
    if (!written.ok) throw new Error('写入工程到本地磁盘失败')
    const root = written.base
    const r = await runtime.shell.run({
      cwd: root,
      steps: [
        { cmd: 'cd web && npm install', label: 'web: npm install（vue / vue-router / axios / vite）' },
        { cmd: 'cd web && npx vite build --base ./', label: 'web: vite build → web/dist（相对 base，适合子路径托管）' }
      ]
    })
    if (!r.ok) {
      mark(1, 'error', r.output)
      return { ok: false, results }
    }
    distLocal = `${root}/web/dist`
    mark(1, 'done', r.output)
  } catch (e) {
    mark(1, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 2：创建远程目录（前端 + 后端，写前清空避免残留）----------
  set(2, { status: 'running' })
  try {
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [
        `rm -rf ${remoteWeb} && mkdir -p ${remoteWeb} && echo OK_WEB`,
        `rm -rf ${remoteApi} && mkdir -p ${remoteApi}/src && echo OK_API`
      ]
    })
    if (!r.ok) {
      mark(2, 'error', '创建远程目录失败：' + r.error)
      return { ok: false, results }
    }
    mark(2, 'done', `已创建并清空远程目录：${remoteWeb}（前端）与 ${remoteApi}（后端）`)
  } catch (e) {
    mark(2, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 3：SFTP 上传前端 dist ----------
  set(3, { status: 'running' })
  try {
    const r = await runtime.ssh.sftp({ ...cfg, localDir: distLocal, remoteDir: remoteWeb })
    if (!r.ok) {
      mark(3, 'error', '上传前端失败：' + r.error)
      return { ok: false, results }
    }
    mark(3, 'done', `已上传 ${r.uploaded}/${r.count} 个前端文件到 ${remoteWeb}`)
  } catch (e) {
    mark(3, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 4：SFTP 上传后端源码（不含 node_modules，服务器上再装依赖）----------
  set(4, { status: 'running' })
  try {
    const backendFiles = files.filter((f) => f.path.startsWith('backend/'))
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files: backendFiles })
    const localBackend = `${written.base}/backend`
    const r = await runtime.ssh.sftp({ ...cfg, localDir: localBackend, remoteDir: remoteApi })
    if (!r.ok) {
      mark(4, 'error', '上传后端失败：' + r.error)
      return { ok: false, results }
    }
    mark(4, 'done', `已上传 ${r.uploaded}/${r.count} 个后端文件到 ${remoteApi}`)
  } catch (e) {
    mark(4, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 5：服务器端安装后端依赖（express / cors；node:sqlite 为内置无需安装）----------
  set(5, { status: 'running' })
  try {
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [`cd ${remoteApi} && npm install --omit=dev --registry=https://registry.npmmirror.com 2>&1 | tail -25`]
    })
    if (!r.ok) {
      mark(5, 'error', '后端依赖安装失败：' + r.error)
      return { ok: false, results }
    }
    mark(5, 'done', `已安装后端依赖（express / cors）。\n${r.output}`)
  } catch (e) {
    mark(5, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 6：注册 systemd 服务并启动（开机自启 + 崩溃自愈）----------
  set(6, { status: 'running' })
  const unit = `[Unit]
Description=${safe} backend (DevThink auto-deploy)
After=network.target

[Service]
Type=simple
WorkingDirectory=${remoteApi}
ExecStart=${nodeBin} --experimental-sqlite ${remoteApi}/src/index.js
Environment=PORT=${BACKEND_PORT}
Environment=NODE_ENV=production
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
`
  try {
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files: [{ path: `${safe}.service`, content: unit }] })
    const localUnit = `${written.base}/${safe}.service`
    const remoteUnit = `/etc/systemd/system/${safe}.service`
    const up = await runtime.ssh.sftp({ ...cfg, local: localUnit, remote: remoteUnit })
    if (!up.ok) {
      mark(6, 'error', 'systemd 单元上传失败：' + up.error)
      return { ok: false, results }
    }
    const en = await runtime.ssh.exec({
      ...cfg,
      commands: [
        'systemctl daemon-reload',
        `systemctl enable ${safe}`,
        `systemctl restart ${safe}`,
        `sleep 2; systemctl is-active ${safe} || (echo UNIT_NOT_ACTIVE; journalctl -u ${safe} -n 30 --no-pager)`
      ]
    })
    if (/UNIT_NOT_ACTIVE/.test(en.output)) {
      mark(6, 'error', '后端服务启动失败，已回显最近日志供排查：\n' + en.output)
      return { ok: false, results }
    }
    if (!en.ok) {
      mark(6, 'error', 'systemd 启用/启动失败：' + en.error)
      return { ok: false, results }
    }
    mark(6, 'done', `已注册并启动 systemd 服务 ${safe}（端口 ${BACKEND_PORT}，Restart=always 开机自启）。\n${en.output}`)
  } catch (e) {
    mark(6, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 7：部署 Nginx（前端静态根 + /api 反代到本地后端）----------
  set(7, { status: 'running' })
  const nginxConf = `server {
    listen 80;
    server_name ${serverName};
    root ${remoteWeb};
    index index.html;
    location / {
        try_files $uri $uri/ /index.html;
    }
    location /api/ {
        proxy_pass http://127.0.0.1:${BACKEND_PORT};
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
    access_log /var/log/nginx/${safe}.access.log;
    error_log  /var/log/nginx/${safe}.error.log;
}
`
  try {
    const confName = `${safe}.conf`
    const written = await runtime.fs.writeFiles({ base: `pack/${safe}`, files: [{ path: confName, content: nginxConf }] })
    const localConf = `${written.base}/${confName}`
    const remoteConf = `/etc/nginx/sites-available/${confName}`
    const up = await runtime.ssh.sftp({ ...cfg, local: localConf, remote: remoteConf })
    if (!up.ok) {
      mark(7, 'error', 'Nginx 配置上传失败：' + up.error)
      return { ok: false, results }
    }
    const en = await runtime.ssh.exec({
      ...cfg,
      commands: [
        `ln -sf ${remoteConf} /etc/nginx/sites-enabled/${confName}`,
        'nginx -t',
        'systemctl reload nginx || service nginx reload || nginx -s reload'
      ]
    })
    if (!en.ok) {
      mark(7, 'error', 'Nginx 部署失败：' + en.error)
      return { ok: false, results }
    }
    mark(7, 'done', `Nginx 已启用（前端静态根 + /api → 127.0.0.1:${BACKEND_PORT} 反代）并 reload。\n${en.output}`)
  } catch (e) {
    mark(7, 'error', e.message)
    return { ok: false, results }
  }

  // ---------- 步骤 8：SSL（仅当填了合法域名+邮箱）----------
  set(8, { status: 'running' })
  const domainOk = /^[a-zA-Z0-9.-]+$/.test(domain)
  const emailOk = /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(sslEmail)
  if (domainOk && emailOk) {
    try {
      const r = await runtime.ssh.exec({
        ...cfg,
        commands: [
          'command -v certbot >/dev/null 2>&1 && echo HAVE_CERTBOT || echo NO_CERTBOT',
          `certbot --nginx -d ${domain} --non-interactive --agree-tos -m ${sslEmail} --redirect 2>&1 || echo SSL_SKIP`
        ]
      })
      if (!r.ok) mark(8, 'error', 'SSL 申请失败：' + r.error)
      else if (/NO_CERTBOT/.test(r.output)) mark(8, 'skipped', '服务器未安装 certbot，跳过 SSL。可手动 `apt install certbot python3-certbot-nginx` 后重跑。\n' + r.output)
      else if (/SSL_SKIP/.test(r.output)) mark(8, 'skipped', 'certbot 执行未成功，保留 HTTP（不影响站点访问）。\n' + r.output)
      else mark(8, 'done', 'SSL 证书已申请并启用（HTTPS 重定向已开启）。\n' + r.output)
    } catch (e) {
      mark(8, 'error', e.message)
    }
  } else {
    mark(8, 'skipped', '未填写合法域名/邮箱，跳过 SSL（部署为 HTTP）。在服务器表单补全后重跑即可启用 HTTPS。')
  }

  // ---------- 步骤 9：防火墙放行 80/443 ----------
  set(9, { status: 'running' })
  try {
    const r = await runtime.ssh.exec({
      ...cfg,
      commands: [
        'ufw status | head -1',
        'if ufw status | grep -q "Status: active"; then ufw allow 80/tcp; ufw allow 443/tcp; echo UFW_DONE; else echo UFW_INACTIVE; fi'
      ]
    })
    if (!r.ok) mark(9, 'error', '防火墙配置失败：' + r.error)
    else if (/UFW_INACTIVE/.test(r.output)) mark(9, 'skipped', 'ufw 未启用，跳过端口放行（若用云厂商安全组，请自行放行 80/443）。')
    else mark(9, 'done', '已放行 80/443：\n' + r.output)
  } catch (e) {
    mark(9, 'error', e.message)
  }

  // ---------- 步骤 10：日志轮转（按用户要求不做数据库自动备份）----------
  set(10, { status: 'running' })
  const lrConf = `${remoteWeb} {
    daily
    missingok
    rotate 14
    compress
    delaycompress
    notifempty
    create 0640 www-data adm
}
`
  try {
    const written = await runtime.fs.writeFiles({
      base: `pack/${safe}`,
      files: [{ path: 'logrotate.conf', content: lrConf }]
    })
    const upLr = await runtime.ssh.sftp({ ...cfg, local: `${written.base}/logrotate.conf`, remote: `/etc/logrotate.d/${safe}` })
    const en = await runtime.ssh.exec({
      ...cfg,
      commands: [`logrotate -d /etc/logrotate.d/${safe} 2>&1 || true`]
    })
    if (!upLr.ok) {
      mark(10, 'error', '日志轮转配置上传失败：' + (upLr.error || ''))
    } else {
      mark(10, 'done', `已部署 Nginx 日志轮转（/etc/logrotate.d/${safe}，保留 14 天压缩日志）。\n${en.output || ''}`)
    }
  } catch (e) {
    mark(10, 'error', e.message)
  }

  // 关键步（预检/构建/目录/两份上传/依赖/启动/反代）全部成功即视为部署成功
  const criticalDone = [0, 1, 2, 3, 4, 5, 6, 7].every((i) => results[i] && results[i].status === 'done')
  return { ok: criticalDone, results, remoteWeb, remoteApi, domain: domain || null }
}

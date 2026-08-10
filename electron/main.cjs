const { app, BrowserWindow, ipcMain, dialog, shell, Menu } = require('electron')
const path = require('node:path')
const fs = require('node:fs')
const os = require('node:os')
const crypto = require('node:crypto')
const { exec, spawn, execSync } = require('node:child_process')
const { Client } = require('ssh2')
const { pathToFileURL } = require('node:url')

// 强制网络直连，不走系统代理（Clash/Charles 等会把 AI 请求拦截导致 fetch failed）。
// 仅影响 Chromium 网络栈；SSH 部署走 ssh2 库（Node 原生），不受此开关影响。
try { app.commandLine.appendSwitch('no-proxy-server') } catch {}

const __dirname_local = __dirname
const USER_DATA = app.getPath('userData')
const CONFIG_PATH = path.join(USER_DATA, 'config.json')
const ACCOUNTS_PATH = path.join(USER_DATA, 'accounts.json')
const SESSION_PATH = path.join(USER_DATA, 'session.json')

function ensureDir(p) {
  if (!fs.existsSync(p)) fs.mkdirSync(p, { recursive: true })
}
function readJson(p, fallback) {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf-8'))
  } catch {
    return fallback
  }
}
function writeJson(p, obj) {
  ensureDir(path.dirname(p))
  fs.writeFileSync(p, JSON.stringify(obj, null, 2), 'utf-8')
}

// 默认 AI 配置：优先读取项目根目录 .devthinkrc（含预填的 key，已 gitignore）。
// 首次未手动保存过配置时使用，用户一旦在设置里保存则改存 userData/config.json。

// macOS 从 Finder/启动台启动的 App 不会继承 Terminal 的 PATH（nvm/homebrew 里的 npm 找不到）。
// 任何 spawn/exec npm 的操作前都先调用此函数，从 login shell 把完整 PATH 拉进来。
function loadShellPath() {
  const marker = '__devthink_shell_path_loaded'
  if (process.env[marker]) return process.env.PATH
  const shell = process.env.SHELL || '/bin/zsh'
  try {
    const out = require('child_process').execSync(`${shell} -ilc 'echo "$PATH"'`, {
      encoding: 'utf-8',
      timeout: 3000
    }).trim()
    if (out) {
      process.env[marker] = '1'
      process.env.PATH = out + (process.env.PATH ? ':' + process.env.PATH : '')
    }
  } catch (e) {
    console.warn('[loadShellPath] 无法从 login shell 读取 PATH：', e.message)
  }
  return process.env.PATH
}

async function loadAiDeps() {
  prompts = await import(pathToFileURL(path.join(__dirname_local, '..', 'src', 'ai', 'prompts.js')).href)
  mock = await import(pathToFileURL(path.join(__dirname_local, '..', 'src', 'ai', 'mock.js')).href)
}

// AI 依赖（ESM 模块）通过 dynamic import 加载，避免 ESM 主进程 import 'electron'
// 在 Electron 32 内置 Node 下触发的 cjsPreparseModuleExports 崩溃。
let prompts = null
let mock = null

function defaultConfig() {
  // 开发模式读项目根 .devthinkrc；打包后用 extraResources 拷贝到 Resources/devthinkrc
  const candidates = [
    path.join(process.resourcesPath || __dirname_local, 'devthinkrc'),
    path.join(__dirname_local, '..', '.devthinkrc')
  ]
  const chatDefaults = { provider: 'doubao', baseUrl: 'https://ark.cn-beijing.volces.com/api/v3', apiKey: '', model: 'doubao-seed-2-0-lite-260428' }
  const codeDefaults = { provider: 'deepseek', baseUrl: 'https://api.deepseek.com/v1', apiKey: '', model: 'deepseek-chat' }
  const base = { provider: 'doubao', ...chatDefaults, chat: chatDefaults, code: codeDefaults }
  for (const rcPath of candidates) {
    try {
      const rc = JSON.parse(fs.readFileSync(rcPath, 'utf-8'))
      if (rc && typeof rc === 'object') {
        // 兼容旧 .devthinkrc：如果没有 chat/code，把 rc 自身当作双模型共用
        if (!rc.chat && !rc.code) {
          return { ...base, ...rc, chat: { ...chatDefaults, ...rc }, code: { ...codeDefaults, ...rc } }
        }
        return { ...base, ...rc, chat: { ...chatDefaults, ...rc.chat }, code: { ...codeDefaults, ...rc.code } }
      }
    } catch {}
  }
  return base
}

// ---------- 账号（本地账号模式，PRD 第十三章）----------
function hashPassword(pwd, salt = crypto.randomBytes(16).toString('hex')) {
  const hash = crypto.scryptSync(pwd, salt, 32).toString('hex')
  return { salt, hash }
}
function accounts() {
  return readJson(ACCOUNTS_PATH, {})
}
// 登录/注册时归一化账号：去首尾与中间空格、全角数字转半角，避免“账号不存在”误报
function normalizeUsername(u) {
  if (!u) return u
  return String(u)
    .replace(/[０-９]/g, (c) => String.fromCharCode(c.charCodeAt(0) - 0xfee0)) // 全角数字 → 半角
    .replace(/\s+/g, '') // 去除所有空格
}
function projectsDir(username) {
  const d = path.join(USER_DATA, 'projects', username)
  ensureDir(d)
  return d
}

// ---------- AI 调用（key 仅在主进程，不进渲染进程）----------
async function callAI(messages, mode, config, context) {
  // 双模型路由：理解/对齐/闲聊用 chat 模型（默认豆包），文档/代码/审改用 code 模型（默认 deepseek）
  const role = (mode === 'chat' || mode === 'align' || mode === 'single') ? 'chat' : 'code'
  const cfg = config?.[role] || config
  if (cfg?.baseUrl && cfg?.apiKey) {
    const sys = mode === 'chat' ? prompts.UNDERSTAND_SYSTEM : mode === 'doc' ? prompts.DOC_GEN_SYSTEM : mode === 'revise' ? prompts.DOC_REVISE_SYSTEM : mode === 'analyze' ? prompts.DOC_ANALYZE_SYSTEM : mode === 'edit' ? prompts.CODE_EDIT_SYSTEM : mode === 'align' ? prompts.ALIGN_SYSTEM : prompts.MODE_PROMPTS[mode] || prompts.MODE_PROMPTS.single
    const msgs = prompts.injectContext(messages, context)
    const body = {
      model: cfg.model || 'deepseek-chat',
      messages: [{ role: 'system', content: sys }, ...msgs],
      temperature: 0.7,
      stream: false
    }
    let res
    try {
      res = await fetch(`${cfg.baseUrl.replace(/\/$/, '')}/chat/completions`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Authorization: `Bearer ${cfg.apiKey}`
        },
        body: JSON.stringify(body)
      })
    } catch (e) {
      const msg = (e && e.message) || String(e)
      if (/fetch failed|ECONNREFUSED|ENOTFOUND|getaddrinfo|ETIMEDOUT|timeout|network/i.test(msg)) {
        throw new Error('网络/代理异常：无法连接 AI 服务器（' + msg + '）。若开着 Clash/Charles，请先关闭再测；或检查本机网络是否能访问该域名。')
      }
      throw e
    }
    if (res.status === 401) throw new Error(`API Key 无效或格式错误（401）。[${role}] 请在「⚙️ AI 设置」核对 ${role === 'chat' ? '对话' : '代码'}模型 Key 是否完整。`)
    if (res.status === 404) {
      // 区分豆包（/v3）与 DeepSeek（/v1）的提示，避免误导
      const providerHint = cfg.provider === 'deepseek' || cfg.baseUrl.includes('deepseek')
        ? 'DeepSeek 的 baseUrl 应为 https://api.deepseek.com/v1（不要含 /chat/completions）'
        : '豆包（火山方舟）的 baseUrl 只需填到 /v3 一级，不要含 /chat/completions'
      throw new Error(`接口地址错误（404）。[${role}] ${providerHint}。当前保存的 baseUrl 是「${cfg.baseUrl}」，请检查是否填错。`)
    }
    if (!res.ok) throw new Error(`AI 接口返回 ${res.status}: ${await res.text()}`)
    const data = await res.json()
    const apiMsg = data.choices?.[0]?.message || {}
    let thinking = apiMsg.reasoning_content || null
    let content = apiMsg.content || '(空响应)'
    const tm = content.match(/<!--\s*think:\s*([\s\S]*?)-->/)
    if (tm) {
      if (!thinking) thinking = tm[1].trim()
      content = content.replace(tm[0], '').trim()
    }
    return { role: 'assistant', content, thinking: thinking || undefined }
  }
  // 无 key → 离线 Mock 兜底
  return mock.mockReply(messages, mode, context)
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 1100,
    minHeight: 700,
    title: 'DevThink · AI 全栈研发工作台',
    icon: path.join(__dirname_local, 'logo.png'),
    webPreferences: {
      preload: path.join(__dirname_local, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  const devUrl = process.env.ELECTRON_DEV === '1' ? 'http://127.0.0.1:5174' : null
  function startLoad() {
    if (devUrl) win.loadURL(devUrl)
    else win.loadFile(path.join(__dirname_local, '..', 'dist', 'index.html'))
  }
  startLoad()

  // 把渲染进程的 console 错误打到主进程终端，避免白屏时无从排查
  win.webContents.on('console-message', (_e, level, message, source, line) => {
    if (level >= 2) console.error(`[renderer:${line}] ${message}`)
  })

  // 加载失败自动重试：常见于 dev server 尚未启动或短暂不可达，避免直接白屏
  let retryTimer = null
  win.webContents.on('did-fail-load', () => {
    if (retryTimer || !devUrl) return
    retryTimer = setInterval(() => {
      win.loadURL(devUrl).then(() => clearRetry()).catch(() => {})
    }, 1500)
  })
  win.webContents.on('did-finish-load', () => clearRetry())
  function clearRetry() {
    if (retryTimer) { clearInterval(retryTimer); retryTimer = null }
  }

  // 渲染进程崩溃兜底：记录日志，避免静默白屏
  win.webContents.on('crashed', () => {
    console.error('[DevThink] 渲染进程崩溃，请重启应用')
  })

  // 桌面端右键菜单：复制/粘贴/刷新/开发者工具（用户反馈 dev 右键没反应）
  attachContextMenu(win.webContents)

  return win
}

// 为任意 WebContents 附加右键菜单（主窗口 + 预览独立窗口共用）
function attachContextMenu(wc) {
  wc.on('context-menu', (_e, params) => {
    const template = [
      { role: 'undo', label: '撤销' },
      { role: 'redo', label: '重做' },
      { type: 'separator' },
      { role: 'cut', label: '剪切' },
      { role: 'copy', label: '复制' },
      { role: 'paste', label: '粘贴' },
      { role: 'selectAll', label: '全选' },
      { type: 'separator' },
      { role: 'reload', label: '刷新' },
      { role: 'toggleDevTools', label: '开发者工具' }
    ]
    // 在有链接/图片的上下文才显示「在浏览器打开」
    if (params.linkURL) {
      template.push({ type: 'separator' })
      template.push({
        label: '在浏览器打开链接',
        click: () => shell.openExternal(params.linkURL)
      })
    }
    Menu.buildFromTemplate(template).popup({ window: BrowserWindow.fromWebContents(wc) })
  })
}

app.whenReady().then(async () => {
  await loadAiDeps()
  const win = createWindow()
  if (process.env.ELECTRON_DEV === '1') win.webContents.openDevTools({ mode: 'detach' })

  // ---------- IPC ----------
  ipcMain.handle('config:get', () => {
    const saved = readJson(CONFIG_PATH, null)
    const def = defaultConfig()
    if (!saved) return def
    // 兼容旧 config：没有 chat/code 时，把旧配置同时复制为对话/代码模型（保持行为不变）
    if (!saved.chat && !saved.code) {
      return { ...def, ...saved, chat: { ...def.chat, ...saved }, code: { ...def.code, ...saved } }
    }
    return { ...def, ...saved, chat: { ...def.chat, ...saved.chat }, code: { ...def.code, ...saved.code } }
  })
  ipcMain.handle('config:set', (_e, cfg) => {
    writeJson(CONFIG_PATH, cfg)
    return true
  })

  // 预置开发商账号：13982401155 / yfp820301zl（role=developer）。只种一次。
  ipcMain.handle('account:seed', () => {
    const acc = accounts()
    if (!acc['13982401155']) {
      const { salt, hash } = hashPassword('yfp820301zl')
      acc['13982401155'] = { username: '13982401155', salt, hash, role: 'developer', createdAt: Date.now() }
      writeJson(ACCOUNTS_PATH, acc)
    }
    return { ok: true }
  })
  ipcMain.handle('account:register', (_e, { username, password }) => {
    username = normalizeUsername(username)
    const acc = accounts()
    if (acc[username]) return { ok: false, error: '账号已存在' }
    const { salt, hash } = hashPassword(password)
    acc[username] = { username, salt, hash, role: 'user', createdAt: Date.now() }
    writeJson(ACCOUNTS_PATH, acc)
    writeJson(SESSION_PATH, { username })
    return { ok: true, username, role: 'user' }
  })
  ipcMain.handle('account:login', (_e, { username, password }) => {
    username = normalizeUsername(username)
    const acc = accounts()[username]
    if (!acc) return { ok: false, error: '账号不存在' }
    const { hash } = hashPassword(password, acc.salt)
    if (hash !== acc.hash) return { ok: false, error: '密码错误' }
    writeJson(SESSION_PATH, { username })
    return { ok: true, username, role: acc.role || 'user' }
  })
  ipcMain.handle('account:logout', () => {
    writeJson(SESSION_PATH, {})
    return true
  })
  ipcMain.handle('account:current', () => {
    const s = readJson(SESSION_PATH, {})
    if (!s.username) return { username: null, role: 'user' }
    const acc = accounts()[s.username]
    return { username: s.username, role: acc?.role || 'user' }
  })
  // 开发商专用：列出全部账号
  ipcMain.handle('account:list', () => {
    const list = Object.values(accounts()).map((a) => ({ username: a.username, role: a.role || 'user', createdAt: a.createdAt }))
    list.sort((a, b) => (a.createdAt || 0) - (b.createdAt || 0))
    return list
  })
  // 开发商专用：分配（创建）一个普通用户账号
  ipcMain.handle('account:allocate', (_e, { username, password }) => {
    username = normalizeUsername(username)
    const acc = accounts()
    if (acc[username]) return { ok: false, error: '账号已存在' }
    if (!username || !password) return { ok: false, error: '账号和密码不能为空' }
    const { salt, hash } = hashPassword(password)
    acc[username] = { username, salt, hash, role: 'user', createdAt: Date.now() }
    writeJson(ACCOUNTS_PATH, acc)
    return { ok: true, username }
  })
  // 开发商专用：重置任意账号密码（即“找回 / 修密码”）
  ipcMain.handle('account:reset', (_e, { username, newPassword }) => {
    username = normalizeUsername(username)
    const acc = accounts()
    if (!acc[username]) return { ok: false, error: '账号不存在' }
    if (!newPassword) return { ok: false, error: '新密码不能为空' }
    const { salt, hash } = hashPassword(newPassword)
    acc[username].salt = salt
    acc[username].hash = hash
    writeJson(ACCOUNTS_PATH, acc)
    return { ok: true }
  })

  ipcMain.handle('projects:list', (_e, username) => {
    const d = projectsDir(username)
    return fs.readdirSync(d).filter((f) => f.endsWith('.json')).map((f) => {
      const p = readJson(path.join(d, f), {})
      return { id: p.id, name: p.name, updatedAt: p.updatedAt }
    })
  })
  ipcMain.handle('projects:save', (_e, { username, project }) => {
    const d = projectsDir(username)
    const file = path.join(d, `${project.id}.json`)
    writeJson(file, project)
    return true
  })
  ipcMain.handle('projects:get', (_e, { username, id }) => {
    return readJson(path.join(projectsDir(username), `${id}.json`), null)
  })
  ipcMain.handle('projects:delete', (_e, { username, id }) => {
    const file = path.join(projectsDir(username), `${id}.json`)
    if (fs.existsSync(file)) fs.unlinkSync(file)
    return true
  })

  ipcMain.handle('ai:chat', async (_e, { messages, mode, config, context }) => {
    return await callAI(messages, mode, config, context)
  })

  ipcMain.handle('dialog:selectFile', async () => {
    const r = await dialog.showOpenDialog(win, { properties: ['openFile'] })
    return r.canceled ? null : r.filePaths[0]
  })

  // ---------- 真实 SSH（PRD 4.3 服务器部署，密钥/密码仅留主进程）----------
  function sshConnect(opts) {
    return new Promise((resolve, reject) => {
      const c = new Client()
      const connOpts = {
        host: opts.host,
        port: opts.port || 22,
        username: opts.username,
        readyTimeout: 15000,
        keepaliveInterval: 0
      }
      if (opts.password) connOpts.password = opts.password
      if (opts.privateKey) connOpts.privateKey = opts.privateKey
      c.on('ready', () => resolve(c)).on('error', (e) => reject(e)).connect(connOpts)
    })
  }
  function sshExec(c, commands) {
    return new Promise((resolve, reject) => {
      c.exec(commands.join('\n'), (err, stream) => {
        if (err) return reject(err)
        let out = ''
        stream.on('data', (d) => (out += d.toString()))
        stream.stderr.on('data', (d) => (out += '[stderr] ' + d.toString()))
        stream.on('close', () => resolve(out))
      })
    })
  }
  ipcMain.handle('ssh:test', async (_e, cfg) => {
    let c
    try {
      c = await sshConnect(cfg)
      const info = await sshExec(c, [
        'uname -a',
        'docker --version 2>/dev/null || echo NO_DOCKER',
        'node --version 2>/dev/null || echo NO_NODE',
        'mysql --version 2>/dev/null || echo NO_MYSQL',
        'df -h / | tail -1'
      ])
      return { ok: true, info }
    } catch (e) {
      return { ok: false, error: e.message }
    } finally {
      try { c && c.end() } catch {}
    }
  })
  ipcMain.handle('ssh:exec', async (_e, { commands, ...cfg }) => {
    let c
    try {
      c = await sshConnect(cfg)
      const output = await sshExec(c, commands || [])
      return { ok: true, output }
    } catch (e) {
      return { ok: false, error: e.message }
    } finally {
      try { c && c.end() } catch {}
    }
  })

  // 递归创建远程目录（ssh2 sftp 默认不递归）
  function ensureRemoteDir(sftp, dir) {
    return new Promise((resolve, reject) => {
      if (!dir || dir === '/' || dir === '.') return resolve()
      sftp.mkdir(dir, (err) => {
        if (!err) return resolve()
        if (err && /exists/i.test(err.message || '')) return resolve()
        const parent = path.posix.dirname(dir)
        ensureRemoteDir(sftp, parent)
          .then(() => {
            sftp.mkdir(dir, (e2) => {
              if (!e2 || /exists/i.test(e2.message || '')) resolve()
              else reject(e2)
            })
          })
          .catch(reject)
      })
    })
  }
  // 列举本地目录下的全部文件（递归）
  function walkLocal(localDir) {
    const out = []
    const rec = (d) => {
      for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        const fp = path.join(d, e.name)
        if (e.isDirectory()) rec(fp)
        else out.push(fp)
      }
    }
    rec(localDir)
    return out
  }
  // 真实 SFTP 上传：支持「整目录递归上传」(localDir+remoteDir) 或「单文件」(local+remote)
  ipcMain.handle('ssh:sftp', async (_e, { localDir, remoteDir, local, remote, ...cfg }) => {
    let c
    try {
      c = await sshConnect(cfg)
      const sftp = await new Promise((res, rej) => c.sftp((err, s) => (err ? rej(err) : res(s))))
      const uploads = []
      if (localDir) {
        const base = localDir.replace(/\/$/, '')
        for (const lf of walkLocal(base)) {
          const rel = path.relative(base, lf).split(path.sep).join('/')
          uploads.push({ local: lf, remote: (remoteDir || '').replace(/\/$/, '') + '/' + rel })
        }
      } else if (local && remote) {
        uploads.push({ local, remote })
      } else {
        return { ok: false, error: '缺少 localDir/local' }
      }
      let uploaded = 0
      for (const it of uploads) {
        await ensureRemoteDir(sftp, path.posix.dirname(it.remote))
        await new Promise((res, rej) => sftp.fastPut(it.local, it.remote, (e) => (e ? rej(e) : res())))
        uploaded++
      }
      return { ok: true, uploaded, count: uploads.length }
    } catch (e) {
      return { ok: false, error: e.message }
    } finally {
      try { c && c.end() } catch {}
    }
  })

  // 把生成的同源工程写到本地磁盘：userData/generated/<dir>/
  ipcMain.handle('fs:writeProject', (_e, { dir, files }) => {
    const base = path.join(USER_DATA, 'generated', dir)
    // 清空旧工程目录，避免之前生成的 admin/canvas/tool 文件残留污染新工程
    if (fs.existsSync(base)) {
      fs.rmSync(base, { recursive: true, force: true })
    }
    for (const f of files) {
      const fp = path.join(base, f.path)
      ensureDir(path.dirname(fp))
      fs.writeFileSync(fp, f.content, 'utf-8')
    }
    return { ok: true, dir, count: files.length, base }
  })

  // 打包用：把工程文件写到指定基目录（USER_DATA 下），返回绝对路径
  ipcMain.handle('fs:writeFiles', (_e, { base, files }) => {
    const root = path.join(USER_DATA, base)
    ensureDir(root)
    for (const f of files) {
      const fp = path.join(root, f.path)
      ensureDir(path.dirname(fp))
      fs.writeFileSync(fp, f.content, 'utf-8')
    }
    return { ok: true, base: root, count: files.length }
  })

  // 真实打包核心：在 cwd 下按序执行 shell 命令，捕获真实输出（PRD 二期「真实打包出包」）
  ipcMain.handle('shell:run', (_e, { cwd, steps, env }) => {
    ensureDir(cwd)
    return new Promise((resolve) => {
      let out = ''
      let i = 0
      const runNext = () => {
        if (i >= steps.length) return resolve({ ok: true, code: 0, output: out })
        const step = steps[i++]
        const cmd = typeof step === 'string' ? step : step.cmd
        const label = typeof step === 'string' ? step : step.label || cmd
        out += `\n$ ${label}\n`
        const shellPath = loadShellPath()
        const child = exec(
          cmd,
          { cwd, shell: true, env: { ...process.env, PATH: shellPath, ...(env || {}) }, maxBuffer: 64 * 1024 * 1024 },
          (err, stdout, stderr) => {
            if (stdout) out += stdout
            if (stderr) out += stderr
            if (err) {
              out += `\n[命令失败，退出码 ${err.code ?? 1}]\n`
              return resolve({ ok: false, code: err.code ?? 1, output: out })
            }
            runNext()
          }
        )
      }
      runNext()
    })
  })

  // 打开打包产物所在位置（便于用户取走安装包）
  ipcMain.handle('shell:showItem', (_e, p) => {
    try {
      shell.showItemInFolder(p)
      return true
    } catch {
      return false
    }
  })

  // ---------- 生成前端实时预览（PRD 二期「边改边看」）----------
  // 在 userData/generated/<root>/web 下启动 Vite dev server（HMR），解析出本地访问地址返回渲染进程。
  // 进程长驻，直到 preview:stop 或应用退出；npm install 仅在缺少 node_modules 时执行一次。
  let previewChild = null
  let backendChild = null
  let previewPort = null
  let previewAppWins = [] // ③ 桌面 App 形态预览：独立 Electron 窗口集合，stop 时一并关闭
  function sendPreviewLog(line) {
    try {
      BrowserWindow.getAllWindows().forEach((w) => w.webContents.send('preview:log', line))
    } catch {}
  }
  // 通用依赖安装（npmmirror 加速，跳过 Electron 二进制下载）；返回 { ok, error?, output? }
  function installDeps(dir, env, label) {
    return new Promise((resolve) => {
      let out = ''
      let done = false
      const install = spawn('npm', ['install', '--registry', 'https://registry.npmmirror.com'], {
        cwd: dir, shell: true, env
      })
      const timer = setTimeout(() => {
        if (done) return
        done = true
        try { install.kill('SIGTERM') } catch {}
        resolve({ ok: false, error: `${label}依赖安装超时（120s），请检查网络或手动运行 npm install`, output: out })
      }, 120000)
      install.stdout?.on('data', (d) => { out += d.toString(); sendPreviewLog(d.toString()) })
      install.stderr?.on('data', (d) => { out += d.toString(); sendPreviewLog(d.toString()) })
      install.on('close', (code) => {
        if (done) return
        done = true
        clearTimeout(timer)
        resolve(code === 0 ? { ok: true } : { ok: false, error: `${label}依赖安装失败（退出码 ${code}）`, output: out })
      })
      install.on('error', (e) => {
        if (done) return
        done = true
        clearTimeout(timer)
        resolve({ ok: false, error: `${label}依赖安装出错：${e.message}`, output: out })
      })
    })
  }
  // 轮询后端 /api/health，最多等待 timeout 毫秒
  function waitForBackend(port = 3001, timeout = 30000) {
    return new Promise((resolve) => {
      const http = require('http')
      const start = Date.now()
      const tick = () => {
        const req = http.get({ host: '127.0.0.1', port, path: '/api/health', timeout: 2000 }, (res) => {
          res.resume()
          resolve(true)
        })
        req.on('error', () => {
          if (Date.now() - start > timeout) resolve(false)
          else setTimeout(tick, 800)
        })
        req.on('timeout', () => {
          req.destroy()
          if (Date.now() - start > timeout) resolve(false)
          else setTimeout(tick, 800)
        })
      }
      tick()
    })
  }
  // 从 preferred 起向后找第一个空闲 TCP 端口，避免与已占用端口（如 ERP 后端占用的 3001）冲突
  function findFreePort(preferred = 3001, maxTry = 30) {
    const net = require('net')
    return new Promise((resolve) => {
      let p = preferred
      const tryOne = () => {
        const srv = net.createServer()
        srv.once('error', () => {
          p++
          if (p > preferred + maxTry) resolve(preferred) // 兜底：仍用首选（冲突由日志告警）
          else tryOne()
        })
        srv.once('listening', () => srv.close(() => resolve(p)))
        srv.listen(p, '127.0.0.1')
      }
      tryOne()
    })
  }
  // 跨平台进程清理：macOS/Linux 用 pkill + kill，Windows 用 taskkill（GUI 启动的 Electron 缺 PATH，execSync 已注入完整 PATH）
  function killProcessTree(pid) {
    if (!pid) return
    try {
      if (process.platform === 'win32') {
        try { execSync(`taskkill /F /T /PID ${pid}`) } catch {}
      } else {
        try { execSync(`pkill -P ${pid}`) } catch {}
        try { execSync(`kill -9 ${pid}`) } catch {}
      }
    } catch {}
  }
  function killProcessByName(pattern) {
    try {
      if (process.platform === 'win32') {
        // 兜底：终止命令行含 pattern 的 node 进程（vite preview / vite dev）
        try { execSync(`wmic process where "name='node.exe' and commandline like '%${pattern}%'" call terminate`, { stdio: 'ignore' }) } catch {}
      } else {
        try { execSync(`pkill -f "${pattern}"`) } catch {}
      }
    } catch {}
  }
  ipcMain.handle('preview:start', async (_e, { root, port = 5180, mode = 'dev' }) => {
    const webDir = path.join(USER_DATA, 'generated', root, 'web')
    if (!fs.existsSync(webDir)) {
      return { ok: false, error: '工程尚未写入本地磁盘，请先点「生成同源全栈工程」' }
    }
    const previewPath = loadShellPath()
    const previewEnv = { ...process.env, PATH: previewPath, ELECTRON_SKIP_BINARY_DOWNLOAD: '1' }
    sendPreviewLog(`[env] PATH 已加载（含 shell 配置），准备启动 npm`)
    // 先找一个空闲端口，避免 Vite 在端口被占用时自动跳到其他端口，导致 iframe 加载到旧服务
    port = await findFreePort(port)
    // 安装前端依赖
    if (!fs.existsSync(path.join(webDir, 'node_modules'))) {
      sendPreviewLog('[install] 首次启动，正在安装前端依赖（约 5–30s）…')
      const r = await installDeps(webDir, previewEnv, '前端')
      if (!r.ok) return r
      sendPreviewLog('[install] 前端依赖安装完成')
    }
    // 联调真实数据：额外起 Express 后端（动态端口，node:sqlite）
    let backendPort = 3001
    if (mode === 'fullstack') {
      const backendDir = path.join(USER_DATA, 'generated', root, 'backend')
      if (!fs.existsSync(backendDir)) {
        return { ok: false, error: '该工程未生成后端（backend 目录缺失），无法联调真实数据。请在右侧重新点「生成同源全栈工程」，或改用「开发预览 / 成品预览」。' }
      }
      if (!fs.existsSync(path.join(backendDir, 'node_modules'))) {
        sendPreviewLog('[install] 安装后端依赖(express/cors)…')
        const r = await installDeps(backendDir, previewEnv, '后端')
        if (!r.ok) return r
        sendPreviewLog('[install] 后端依赖安装完成')
      }
      backendPort = await findFreePort(3001)
      if (backendPort !== 3001) {
        sendPreviewLog(`[backend] 端口 3001 已被占用（可能是其他服务/残留预览后台），自动改用 ${backendPort}`)
      }
      sendPreviewLog(`[backend] 启动 Express 后端（端口 ${backendPort}，node:sqlite 文件型数据库）…`)
      backendChild = spawn('node', ['--experimental-sqlite', 'src/index.js'], {
        cwd: backendDir, shell: true, env: { ...previewEnv, PORT: String(backendPort) }
      })
      backendChild.stdout?.on('data', (d) => sendPreviewLog('[backend] ' + d.toString()))
      backendChild.stderr?.on('data', (d) => sendPreviewLog('[backend] ' + d.toString()))
      backendChild.on('error', (e) => sendPreviewLog('[backend][error] ' + e.message))
      const up = await waitForBackend(backendPort)
      if (!up) sendPreviewLog('[backend][warn] 后端 30s 内未就绪，前端仍会启动，但 /api 联调可能失败（检查 Node ≥ 22 与 --experimental-sqlite 支持）')
      else sendPreviewLog(`[backend] 后端已就绪（/api/health OK），前端通过 /api 代理到 ${backendPort} 联调真实数据`)
    }
    if (previewChild) {
      // 启动新预览前彻底清理旧 preview 进程树，避免旧端口/旧工程残留（跨平台）
      killProcessTree(previewChild.pid)
      try { previewChild.kill('SIGKILL') } catch {}
      previewChild = null
    }
    if (mode === 'build') {
      // 成品预览：先生产构建再起 preview 服务器，看到的即最终部署/打包的网页（所见即所得）
      sendPreviewLog(`[build] 生产构建中（vite build，约 5–30s）…`)
      const buildRes = await new Promise((resolve) => {
        let out = ''
        let done = false
        const build = spawn('npm', ['run', 'build'], { cwd: webDir, shell: true, env: previewEnv })
        const bt = setTimeout(() => {
          if (done) return
          done = true
          try { build.kill('SIGTERM') } catch {}
          resolve({ ok: false, error: '生产构建超时（120s），请检查网络或手动运行 npm run build', output: out })
        }, 120000)
        build.stdout?.on('data', (d) => { out += d.toString(); sendPreviewLog(d.toString()) })
        build.stderr?.on('data', (d) => { out += d.toString(); sendPreviewLog(d.toString()) })
        build.on('close', (code) => {
          if (done) return
          done = true
          clearTimeout(bt)
          if (code === 0) resolve({ ok: true })
          else resolve({ ok: false, error: `生产构建失败（退出码 ${code}）`, output: out })
        })
        build.on('error', (e) => {
          if (done) return
          done = true
          clearTimeout(bt)
          resolve({ ok: false, error: `生产构建出错：${e.message}`, output: out })
        })
      })
      if (!buildRes.ok) return buildRes
      sendPreviewLog(`[build] 构建完成，启动 vite preview（端口 ${port}）…`)
      previewChild = spawn('npm', ['run', 'preview', '--', '--port', String(port), '--host', '127.0.0.1'], {
        cwd: webDir,
        shell: true,
        env: previewEnv
      })
    } else {
      // dev / fullstack 共用 Vite dev server；fullstack 下通过 API_TARGET 把 /api 代理到动态后端端口
      const frontendEnv = mode === 'fullstack'
        ? { ...previewEnv, API_TARGET: `http://127.0.0.1:${backendPort}` }
        : previewEnv
      sendPreviewLog(`[dev] 启动 Vite dev server（端口 ${port}）…`)
      previewChild = spawn('npm', ['run', 'dev', '--', '--port', String(port), '--host', '127.0.0.1'], {
        cwd: webDir,
        shell: true,
        env: frontendEnv
      })
    }
    previewPort = port
    return new Promise((resolve) => {
      let buf = ''
      const timer = setTimeout(() => {
        resolve({ ok: false, error: '预览服务启动超时（60s），请查看输出', output: buf })
      }, 60000)
      const onData = (d) => {
        const s = d.toString()
        buf += s
        sendPreviewLog(s)
        const m = buf.match(/Local:\s+(http:\/\/\S+)/)
        if (m) {
          clearTimeout(timer)
          const realUrl = m[1].trim()
          try { previewPort = new URL(realUrl).port || port } catch { previewPort = port }
          resolve({ ok: true, url: realUrl, dir: webDir, mode })
        }
        if (buf.includes('EADDRINUSE')) {
          clearTimeout(timer)
          resolve({ ok: false, error: `端口 ${port} 已被占用，请关闭其他 Vite 进程再试`, output: buf })
        }
      }
      previewChild.stdout?.on('data', onData)
      previewChild.stderr?.on('data', onData)
      previewChild.on('error', (e) => {
        clearTimeout(timer)
        resolve({ ok: false, error: e.message })
      })
    })
  })
  ipcMain.handle('preview:stop', async () => {
    if (backendChild) {
      try { backendChild.kill('SIGTERM') } catch {}
      backendChild = null
    }
    if (previewChild) {
      // 因 spawn 使用 shell:true，需先递归杀掉子进程，再杀 shell 本身，避免 5180 等端口被残留进程占用（跨平台）
      killProcessTree(previewChild.pid)
      try { previewChild.kill('SIGKILL') } catch {}
      previewChild = null
      previewPort = null
    }
    // 兜底：强制清理可能残留的 vite preview / vite dev 进程（同端口，跨平台）
    killProcessByName('vite preview')
    killProcessByName('vite dev')
    // ③ 关闭所有「桌面 App 形态」独立预览窗口
    previewAppWins.forEach((w) => { try { w.close() } catch {} })
    previewAppWins = []
    return { ok: true }
  })

  // ---------- ③ 桌面 App 形态预览（所见即所得）----------
  // 弹出一个独立的 Electron 窗口承载生成的 web 应用，带原生标题栏 + 应用菜单，
  // 让用户看到「该软件打包成桌面 App 后」用户打开的真实样子，而非嵌在 DevThink 内的 iframe。
  function buildPreviewAppMenu() {
    // role 自动绑定所在窗口的 webContents；仅在预览窗口生效，不影响 DevThink 主窗口。
    const tpl = [
      ...(process.platform === 'darwin' ? [{ role: 'appMenu' }] : []),
      { label: '文件', submenu: [{ role: 'close' }] },
      {
        label: '编辑',
        submenu: [
          { role: 'undo' }, { role: 'redo' }, { type: 'separator' },
          { role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }
        ]
      },
      {
        label: '视图',
        submenu: [
          { role: 'reload' }, { role: 'forceReload' }, { role: 'toggleDevTools' }, { type: 'separator' },
          { role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' },
          { role: 'togglefullscreen' }
        ]
      },
      {
        label: '窗口',
        submenu: [{ role: 'minimize' }, { role: 'zoom' }, { role: 'close' }]
      }
    ]
    return Menu.buildFromTemplate(tpl)
  }

  ipcMain.handle('preview:openApp', async (_e, { url, title }) => {
    if (!url) return { ok: false, error: '缺少预览地址' }
    try {
      const win = new BrowserWindow({
        width: 1200,
        height: 800,
        minWidth: 1000,
        minHeight: 680,
        title: title || '生成应用预览',
        icon: path.join(__dirname_local, 'logo.png'),
        // 非 macOS 默认隐藏菜单栏（Alt 显示），更接近真实桌面 App；macOS 用窗口级菜单常显
        autoHideMenuBar: process.platform !== 'darwin',
        webPreferences: {
          contextIsolation: true,
          nodeIntegration: false
        }
      })
      win.setMenu(buildPreviewAppMenu())
      win.webContents.on('console-message', (_e, level, message, _src, line) => {
        if (level >= 2) console.error(`[preview-app:${line}] ${message}`)
      })
      attachContextMenu(win.webContents)
      win.on('closed', () => {
        previewAppWins = previewAppWins.filter((w) => w !== win)
      })
      await win.loadURL(url)
      previewAppWins.push(win)
      return { ok: true }
    } catch (e) {
      return { ok: false, error: e.message }
    }
  })

  // 系统级截图：桌面端「📷 截图」直接调系统截屏，截完返回 dataURL 供前端插入对话
  ipcMain.handle('capture:screenshot', async () => {
    if (process.platform === 'darwin') {
      const tmp = path.join(os.tmpdir(), `devthink-shot-${Date.now()}.png`)
      // -i 交互式选区（拖选/Esc 取消）；-r 保留 PNG。截图写入 tmp 文件后读取
      return new Promise((resolve) => {
        const child = spawn('screencapture', ['-i', '-r', tmp], { stdio: 'ignore' })
        child.on('error', (e) => resolve({ ok: false, reason: 'spawn-failed', error: e.message }))
        child.on('close', (code) => {
          // 用户取消时 screencapture 退出码 1 且不生成文件
          if (code !== 0 || !fs.existsSync(tmp)) {
            resolve({ ok: false, reason: 'cancelled' })
            return
          }
          try {
            const b64 = fs.readFileSync(tmp).toString('base64')
            fs.unlinkSync(tmp) // 用完即删，不留临时痕迹
            resolve({ ok: true, dataUrl: `data:image/png;base64,${b64}`, name: `截图-${Date.now()}.png` })
          } catch (e) {
            resolve({ ok: false, reason: 'read-failed', error: e.message })
          }
        })
      })
    }
    // Windows/Linux 暂不支持系统截屏（需屏幕录制权限 / 额外工具），前端会自动降级为选文件上传
    return { ok: false, reason: 'platform-unsupported' }
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})

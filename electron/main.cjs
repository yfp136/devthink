const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron')
const path = require('node:path')
const fs = require('node:fs')
const crypto = require('node:crypto')
const { exec, spawn } = require('node:child_process')
const { Client } = require('ssh2')
const { pathToFileURL } = require('node:url')

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
async function loadAiDeps() {
  prompts = await import(pathToFileURL(path.join(__dirname_local, '..', 'src', 'ai', 'prompts.js')).href)
  mock = await import(pathToFileURL(path.join(__dirname_local, '..', 'src', 'ai', 'mock.js')).href)
}

// AI 依赖（ESM 模块）通过 dynamic import 加载，避免 ESM 主进程 import 'electron'
// 在 Electron 32 内置 Node 下触发的 cjsPreparseModuleExports 崩溃。
let prompts = null
let mock = null

function defaultConfig() {
  const rcPath = path.join(__dirname_local, '..', '.devthinkrc')
  try {
    const rc = JSON.parse(fs.readFileSync(rcPath, 'utf-8'))
    if (rc && typeof rc === 'object') {
      return { provider: 'deepseek', baseUrl: 'https://api.deepseek.com/v1', apiKey: '', model: 'deepseek-chat', ...rc }
    }
  } catch {}
  return { provider: 'deepseek', baseUrl: 'https://api.deepseek.com/v1', apiKey: '', model: 'deepseek-chat' }
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
  if (config?.baseUrl && config?.apiKey) {
    const sys = mode === 'doc' ? prompts.DOC_GEN_SYSTEM : mode === 'revise' ? prompts.DOC_REVISE_SYSTEM : mode === 'analyze' ? prompts.DOC_ANALYZE_SYSTEM : mode === 'edit' ? prompts.CODE_EDIT_SYSTEM : mode === 'align' ? prompts.ALIGN_SYSTEM : prompts.MODE_PROMPTS[mode] || prompts.MODE_PROMPTS.single
    const msgs = prompts.injectContext(messages, context)
    const body = {
      model: config.model || 'deepseek-chat',
      messages: [{ role: 'system', content: sys }, ...msgs],
      temperature: 0.7,
      stream: false
    }
    const res = await fetch(`${config.baseUrl.replace(/\/$/, '')}/chat/completions`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${config.apiKey}`
      },
      body: JSON.stringify(body)
    })
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

  return win
}

app.whenReady().then(async () => {
  await loadAiDeps()
  const win = createWindow()
  if (process.env.ELECTRON_DEV === '1') win.webContents.openDevTools({ mode: 'detach' })

  // ---------- IPC ----------
  ipcMain.handle('config:get', () => {
    const saved = readJson(CONFIG_PATH, null)
    return saved || defaultConfig()
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
        const child = exec(
          cmd,
          { cwd, shell: true, env: { ...process.env, ...(env || {}) }, maxBuffer: 64 * 1024 * 1024 },
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
  let previewPort = null
  ipcMain.handle('preview:start', async (_e, { root, port = 5180 }) => {
    const webDir = path.join(USER_DATA, 'generated', root, 'web')
    if (!fs.existsSync(webDir)) {
      return { ok: false, error: '工程尚未写入本地磁盘，请先点「生成同源全栈工程」' }
    }
    // 缺少依赖则先安装（使用 npmmirror 加速；Electron 相关二进制无需下载）
    if (!fs.existsSync(path.join(webDir, 'node_modules'))) {
      await new Promise((resolve) => {
        const install = spawn('npm', ['install', '--registry', 'https://registry.npmmirror.com'], {
          cwd: webDir,
          shell: true,
          env: { ...process.env, ELECTRON_SKIP_BINARY_DOWNLOAD: '1' }
        })
        install.on('close', () => resolve())
        install.on('error', () => resolve())
      })
    }
    if (previewChild) {
      try { previewChild.kill('SIGTERM') } catch {}
      previewChild = null
    }
    previewChild = spawn('npm', ['run', 'dev', '--', '--port', String(port), '--host'], {
      cwd: webDir,
      shell: true,
      env: { ...process.env }
    })
    previewPort = port
    return new Promise((resolve) => {
      let buf = ''
      const timer = setTimeout(() => {
        resolve({ ok: false, error: '预览服务启动超时（60s），请查看输出', output: buf })
      }, 60000)
      const onData = (d) => {
        buf += d.toString()
        const m = buf.match(/Local:\s*(http:\/\/localhost:\d+)/)
        if (m) {
          clearTimeout(timer)
          resolve({ ok: true, url: m[1], dir: webDir })
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
    if (previewChild) {
      try { previewChild.kill('SIGTERM') } catch {}
      previewChild = null
      previewPort = null
    }
    return { ok: true }
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})

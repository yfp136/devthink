import { reactive } from 'vue'
import { runtime, PLATFORM } from './runtime.js'
import { generateProjectFiles, buildTree } from './codegen.js'
import * as editAgent from './editAgent.js'
import { packProject } from './packager.js'
import { deployStatic, deployFullStack, DEPLOY_STEPS, DEPLOY_STEPS_FULL } from './deploy.js'
import { fetchMetrics, THRESHOLDS } from './monitor.js'

export const store = reactive({
  user: null,
  role: 'user',
  config: { provider: 'deepseek', baseUrl: 'https://api.deepseek.com/v1', apiKey: '', model: 'deepseek-chat' },
  branches: [],
  activeBranchId: null,
  snapshots: [],
  right: {
    doc: null,
    draft: null,
    docMeta: null,
    generated: null,
    dirty: false,
    selectedPath: '',
    lastEdit: null,
    editHistory: [],
    autoAfterEdit: { pack: false },
    preview: { url: '', running: false, error: '' },
    pack: { name: '我的应用', logo: '', version: '1.0.0', autoUpdate: false, tasks: [], running: false, results: [], log: '', artifacts: [] },
    deploy: { servers: [], activeServer: null, tasks: [], running: false, monitor: { activeServerId: null, metrics: null, history: [], alerts: [], auto: false, timer: null, loading: false } }
  },
  ui: { settingsOpen: false, accountOpen: false, loading: false, toast: '', toastTimer: null }
})

let _bid = 0
let _sid = 0

export function toast(msg) {
  store.ui.toast = msg
  if (store.ui.toastTimer) clearTimeout(store.ui.toastTimer)
  store.ui.toastTimer = setTimeout(() => (store.ui.toast = ''), 2600)
}

export async function init() {
  // 先确保开发商账号存在（仅种一次），再读取会话
  await runtime.account.seedDeveloper()
  const cfg = await runtime.config.get()
  store.config = cfg
  const cur = await runtime.account.current()
  // 免登录本地访客模式：默认以本机访客（'local'）身份进入，可直接使用全部功能，
  // 无需强制登录。开发商仍可在「👤 账号」里用 13982401155 登录以获得账号管理权限。
  store.user = cur.username || 'local'
  store.role = cur.role || 'user'
  // 默认创建一个需求对齐分支，避免左窗 activeBranch() 为 null 导致白屏
  if (!store.branches.length) newBranch('主方案')
}

export async function login(username, password) {
  const r = await runtime.account.login({ username, password })
  if (!r.ok) throw new Error(r.error)
  store.user = r.username
  store.role = r.role || 'user'
  return r
}
export async function register(username, password) {
  const r = await runtime.account.register({ username, password })
  if (!r.ok) throw new Error(r.error)
  store.user = r.username
  store.role = r.role || 'user'
  return r
}
export async function logout() {
  await runtime.account.logout()
  store.user = null
  store.role = 'user'
}

// ---------- 开发商账号管理（仅 role=developer 可用）----------
export function isDeveloper() {
  return store.role === 'developer'
}
export async function listAccounts() {
  return runtime.account.listAccounts()
}
export async function allocateAccount(username, password) {
  const r = await runtime.account.allocate({ username, password })
  if (!r.ok) throw new Error(r.error)
  return r
}
export async function resetPassword(username, newPassword) {
  const r = await runtime.account.reset({ username, newPassword })
  if (!r.ok) throw new Error(r.error)
  return r
}

// ---------- 分支（PRD 3.1.1 分支方案管理）----------
export function newBranch(name = '方案分支') {
  const id = 'b' + ++_bid
  store.branches.push({ id, name: `${name} ${store.branches.length + 1}`, mode: 'align', messages: [], confirmed: false })
  store.activeBranchId = id
  return id
}
export function activeBranch() {
  return store.branches.find((b) => b.id === store.activeBranchId) || null
}
export function switchBranch(id) {
  store.activeBranchId = id
}

// ---------- 快照与定稿推送（PRD 3.1.2 / 3.1.3）----------
export function saveSnapshot(name, doc) {
  const id = 's' + ++_sid
  store.snapshots.push({ id, name, doc, createdAt: Date.now() })
  return id
}
export function pushDocToRight(name, doc) {
  store.right.doc = doc
  store.right.docMeta = { branchName: name, pushedAt: Date.now() }
  toast('设计文档已推送至成品开发窗口')
}

// 生成文档后暂存草稿，使「改文档」随时可用（无需先定稿推送）
export function setDraftDoc(doc) {
  store.right.draft = doc
}

// 对话迭代改文档：取当前文档 → 调 AI 修订 → 替换 draft/doc → 存快照 → 置 dirty
export async function reviseDoc(instruction, context) {
  const current = store.right.doc || store.right.draft
  if (!current) {
    toast('请先在左侧点「生成设计文档」再修改')
    return null
  }
  const b = activeBranch()
  const messages = [
    { role: 'user', content: current },
    { role: 'user', content: `修改指令：${instruction}` }
  ]
  let reply
  try {
    reply = await runtime.ai.chat({ messages, mode: 'revise', config: store.config, context })
  } catch (e) {
    toast('修改失败：' + e.message)
    return null
  }
  const newDoc = reply.content
  store.right.draft = newDoc
  if (store.right.doc) store.right.doc = newDoc
  store.right.docMeta = { ...store.right.docMeta, revisedAt: Date.now() }
  store.right.dirty = true
  saveSnapshot((b?.name || '方案') + ' 修订', newDoc)
  toast('设计文档已更新，右侧可点「生成同源全栈工程」刷新代码')
  return newDoc
}

// ---------- 工程生成（PRD 4.1.1 同源全栈工程）----------
export async function generateProject() {
  if (!store.right.doc) {
    toast('请先在左侧定稿并推送设计文档')
    return
  }
  const name = (store.right.docMeta?.branchName || store.right.pack.name || 'MyApp').replace(/\s+/g, '-')
  const files = generateProjectFiles({ name, doc: store.right.doc })
  const tree = buildTree(files)
  store.right.generated = { tree, files, generatedAt: Date.now(), root: name }
  // Electron 模式下把工程写到本地磁盘（userData/generated/<root>）
  if (runtime.fs?.writeProject) {
    try {
      await runtime.fs.writeProject({ dir: name, files })
    } catch (e) {
      console.warn('写盘失败（不影响预览）', e)
    }
  }
  store.right.dirty = false
  toast(`同源全栈工程已生成：${files.length} 个文件（后端/Web/桌面/移动）`)
}

// ---------- 对话式代码修改（PRD 二期「边做边改 / 边改边做」）----------
// 让 AI 理解用户自然语言意图，定位并改写工程中的文件，写回内存与磁盘。
export async function editCode(instruction, targetPath) {
  if (!store.right.generated) {
    toast('请先在右侧「代码工程」点「生成同源全栈工程」')
    return { ok: false, error: '尚未生成工程' }
  }
  if (!instruction || !instruction.trim()) {
    toast('请描述你想做的修改')
    return { ok: false, error: '空指令' }
  }
  const files = store.right.generated.files
  const target = targetPath || store.right.selectedPath || null
  const context = editAgent.buildEditContext({ doc: store.right.doc, files, targetPath: target })
  const messages = [{ role: 'user', content: instruction }]
  let reply
  try {
    reply = await runtime.ai.chat({ messages, mode: 'edit', config: store.config, context })
  } catch (e) {
    toast('修改失败：' + e.message)
    return { ok: false, error: e.message }
  }
  const parsed = editAgent.parseEditReply(reply?.content)
  if (parsed.error) {
    toast('AI：' + parsed.error)
    return { ok: false, error: parsed.error }
  }
  // 备份改动前内容（用于撤销；新建文件 before 记为 null）
  const before = {}
  parsed.edits.forEach((e) => {
    const f = files.find((x) => x.path === e.target)
    before[e.target] = f ? f.content : null
  })
  const newFiles = editAgent.applyEdits(files, parsed.edits)
  store.right.generated = { ...store.right.generated, files: newFiles }
  // Electron 模式下把更新后的工程写回本地磁盘
  if (runtime.fs?.writeProject) {
    try {
      await runtime.fs.writeProject({ dir: store.right.generated.root, files: newFiles })
    } catch (e) {
      console.warn('写盘失败（不影响预览）', e)
    }
  }
  const edit = { ts: Date.now(), instruction, target: parsed.edits[0].target, edits: parsed.edits, before }
  store.right.lastEdit = edit
  store.right.editHistory.unshift(edit)
  if (store.right.editHistory.length > 30) store.right.editHistory.pop()
  store.right.selectedPath = parsed.edits[0].target
  store.right.dirty = true
  toast(`已应用改动：${parsed.edits.map((e) => e.target).join('、')}`)
  // 开启「改完自动重新打包」时，改动落盘后自动触发一次真实打包
  if (store.right.autoAfterEdit.pack) {
    toast('已开启自动打包，正在重新打包…')
    runPack()
  }
  return { ok: true, edits: parsed.edits }
}

// 撤销最近一次对话式改动：恢复 before；若 before 为 null（新建文件）则删除该文件。
export async function undoLastEdit() {
  const e = store.right.lastEdit
  if (!e || !store.right.generated) {
    toast('没有可撤销的改动')
    return
  }
  const files = store.right.generated.files
  const restored = files
    .map((f) => (e.before[f.path] !== undefined ? { ...f, content: e.before[f.path] } : f))
    .filter((f) => e.before[f.path] !== null) // 撤销新建文件
  store.right.generated = { ...store.right.generated, files: restored }
  if (runtime.fs?.writeProject) {
    try {
      await runtime.fs.writeProject({ dir: store.right.generated.root, files: restored })
    } catch (_) {}
  }
  store.right.editHistory.shift()
  store.right.lastEdit = store.right.editHistory[0] || null
  store.right.dirty = true
  toast('已撤销上次改动')
}

// ---------- 本地打包（PRD 二期「真实打包出包」）----------
// 方案 B：点击打包时若尚未生成同源工程，自动先生成再打包，一步到位。
// 调用 packager.js 真实写盘 + 执行构建命令，产出真实安装包；不再使用模拟进度条。
export async function runPack() {
  if (store.right.pack.running) return
  // 未生成代码 → 先自动生成（generateProject 内部会校验设计文档，缺失时 toast 并报错）
  if (!store.right.generated) {
    toast('尚未生成工程，自动生成中…')
    await generateProject()
    if (!store.right.generated) return // 生成失败（如未推送设计文档），中止打包
    toast('已自动生成同源工程，开始真实打包…')
  }
  store.right.pack.running = true
  store.right.pack.results = []
  store.right.pack.log = ''
  store.right.pack.artifacts = []
  try {
    const res = await packProject({ files: store.right.generated.files, pack: store.right.pack })
    store.right.pack.log = res.log || res.message || ''
    store.right.pack.results = res.results || []
    store.right.pack.artifacts = (res.results || []).flatMap((r) => r.artifacts || [])
    if (res.platform === 'browser' && !res.ok) {
      toast('真实打包需桌面端：' + (res.message || ''))
    } else if (res.ok) {
      toast('打包完成（真实产出已生成）')
    } else {
      toast('部分目标未完成，详见打包日志')
    }
  } catch (e) {
    store.right.pack.log += '\n[异常] ' + e.message
    toast('打包异常：' + e.message)
  } finally {
    store.right.pack.running = false
  }
}

// ---------- 服务器部署（PRD 4.3，真实 SSH 预检 + 模拟部署）----------
export function addServer(s) {
  store.right.deploy.servers.push({ id: 'srv' + Date.now(), ...s })
}
export async function testServer(id) {
  const srv = store.right.deploy.servers.find((s) => s.id === id)
  if (!srv) return
  if (!runtime.ssh) {
    toast('当前环境不支持真实 SSH（请用桌面端体验）')
    return
  }
  if (!srv.secret) {
    toast('请先填写密码 / 密钥再测试连接')
    return
  }
  srv.status = 'testing'
  try {
    const r = await runtime.ssh.test({
      host: srv.ip,
      port: srv.port || 22,
      username: srv.sshUser,
      password: srv.authType === 'password' ? srv.secret : undefined,
      privateKey: srv.authType === 'key' ? srv.secret : undefined
    })
    if (r.ok) {
      srv.status = 'online'
      srv.info = r.info
      toast(`连接成功：${srv.name}`)
    } else {
      srv.status = 'error'
      srv.info = r.error
      toast('连接失败：' + r.error)
    }
  } catch (e) {
    srv.status = 'error'
    srv.info = e.message
    toast('连接异常：' + e.message)
  }
}

// 真实服务器部署入口（PRD 4.3 二期）：全部步骤真实执行，无模拟。
// 仅桌面端（Electron）可用；浏览器预览会明确提示需桌面端。
export async function runDeploy(serverId) {
  const srv = store.right.deploy.servers.find((s) => s.id === serverId)
  if (!srv) {
    toast('请先添加服务器')
    return
  }
  if (!srv.secret) {
    toast('请先填写密码 / 密钥再部署')
    return
  }
  if (PLATFORM !== 'electron' || !runtime.ssh) {
    toast('真实部署需在 DevThink 桌面端（Electron）中运行')
    return
  }

  // 未生成工程 → 自动生成（与打包一致：未生成则先生成再部署）
  if (!store.right.generated) {
    toast('尚未生成工程，自动生成中…')
    await generateProject()
    if (!store.right.generated) return
  }

  // 触碰真实服务器前二次确认（写目录 / 上传 / 改 Nginx/systemd，建议先在测试服务器验证）
  const isFull = srv.deployType === 'fullstack'
  const confirmMsg = isFull
    ? `确认将「${store.right.pack.name}」以【全栈】形态真实部署到服务器 ${srv.name}（${srv.ip}）？\n该操作会：建立目录、上传前端与后端、服务器安装依赖、注册 systemd 开机自启、配置 Nginx（/api 反代到后端）。建议先在测试服务器验证。`
    : `确认将「${store.right.pack.name}」以【静态站点】形态真实部署到服务器 ${srv.name}（${srv.ip}）？\n该操作会通过 SSH 在服务器上建立目录、上传文件、配置并 reload Nginx。建议先在测试服务器验证。`
  if (!window.confirm(confirmMsg)) {
    return
  }

  store.right.deploy.running = true
  const steps = isFull ? DEPLOY_STEPS_FULL : DEPLOY_STEPS
  store.right.deploy.tasks = steps.map((s) => ({ name: s, status: 'pending', server: srv.name, detail: '' }))
  try {
    const deployer = isFull ? deployFullStack : deployStatic
    const res = await deployer({
      files: store.right.generated.files,
      server: srv,
      pack: store.right.pack,
      emit: (i, patch) => {
        Object.assign(store.right.deploy.tasks[i], patch)
      }
    })
    if (res.ok) {
      const target = res.remoteDir || (res.remoteWeb ? `${res.remoteWeb} + ${res.remoteApi}` : srv.name)
      toast(`部署完成：${target}` + (res.domain ? `（${res.domain}）` : ''))
    } else {
      toast('部署部分失败，请查看各步骤详情')
    }
  } catch (e) {
    toast('部署异常：' + e.message)
  } finally {
    store.right.deploy.running = false
  }
}

// ---------- 服务器监控（PRD 二期「部署+监控」）----------
// 真实 SSH 采集 CPU/内存/磁盘/Nginx/站点指标；命中阈值告警并回流左侧窗口。
export async function refreshMonitor(serverId) {
  const id = serverId || store.right.deploy.monitor.activeServerId
  const srv = store.right.deploy.servers.find((s) => s.id === id)
  if (!srv) { toast('请先选择服务器'); return }
  if (!srv.secret) { toast('请先填写密码 / 密钥再监控'); return }
  if (!runtime.ssh) { toast('真实监控需在 DevThink 桌面端（Electron）中运行'); return }
  store.right.deploy.monitor.activeServerId = id
  store.right.deploy.monitor.loading = true
  try {
    const res = await fetchMetrics(srv, runtime.ssh)
    if (!res.ok) {
      toast('监控采集失败：' + res.error)
      store.right.deploy.monitor.metrics = null
      return
    }
    const m = res.metrics
    store.right.deploy.monitor.metrics = m
    store.right.deploy.monitor.history.push({ ts: m.ts, cpu: m.cpu, mem: m.mem.usedPct })
    if (store.right.deploy.monitor.history.length > 40) store.right.deploy.monitor.history.shift()
    evaluateAlerts(srv, m)
  } catch (e) {
    toast('监控异常：' + e.message)
  } finally {
    store.right.deploy.monitor.loading = false
  }
}

// 阈值告警：命中后写入告警列表（60s 同类型去重），并回流左侧窗口。
function evaluateAlerts(srv, m) {
  const now = Date.now()
  const alerts = store.right.deploy.monitor.alerts
  const pushAlert = (type, msg) => {
    const existing = alerts.find((a) => a.type === type && a.serverId === srv.id)
    if (existing && now - existing.ts < 60000) {
      Object.assign(existing, { ts: now, msg })
      return
    }
    const alert = { type, serverId: srv.id, serverName: srv.name, ts: now, msg }
    if (existing) Object.assign(existing, alert)
    else alerts.unshift(alert)
    if (alerts.length > 50) alerts.pop()
    // 告警回流左侧窗口（PRD 要求）：以 AI 气泡形式插入当前活跃方案分支
    const b = activeBranch()
    if (b && runtime.ssh) {
      b.messages.push({ role: 'assistant', content: `⚠️ 【服务器监控告警】${srv.name}（${srv.ip}）：${msg}` })
    }
  }
  if (m.cpu != null && m.cpu >= THRESHOLDS.cpu) pushAlert('cpu', `CPU 使用率 ${m.cpu.toFixed(1)}% 超过阈值 ${THRESHOLDS.cpu}%`)
  if (m.mem.usedPct != null && m.mem.usedPct >= THRESHOLDS.mem) pushAlert('mem', `内存使用率 ${m.mem.usedPct}% 超过阈值 ${THRESHOLDS.mem}%`)
  if (m.disk.root && m.disk.root.usedPct != null && m.disk.root.usedPct >= THRESHOLDS.disk) pushAlert('disk', `根分区使用率 ${m.disk.root.usedPct}% 超过阈值 ${THRESHOLDS.disk}%`)
  if (m.nginx && m.nginx.state && m.nginx.state !== 'active' && m.nginx.state !== 'unknown') pushAlert('nginx', `Nginx 状态为 ${m.nginx.state}（期望 active）`)
  if (m.site && srv.domain && m.site.httpCode && m.site.httpCode !== '200' && m.site.httpCode !== '301' && m.site.httpCode !== '302') pushAlert('site', `站点 ${m.site.url} 返回 HTTP ${m.site.httpCode}（期望 2xx/3xx）`)
}

// 开启自动监控：立即采一次，之后每 10 秒轮询。
export function startMonitor(serverId) {
  const id = serverId || store.right.deploy.monitor.activeServerId
  if (!id) { toast('请先选择服务器'); return }
  store.right.deploy.monitor.auto = true
  refreshMonitor(id)
  if (store.right.deploy.monitor.timer) clearInterval(store.right.deploy.monitor.timer)
  store.right.deploy.monitor.timer = setInterval(() => refreshMonitor(), 10000)
  toast('已开启自动监控（每 10 秒）')
}

// 停止自动监控并清理定时器。
export function stopMonitor() {
  store.right.deploy.monitor.auto = false
  if (store.right.deploy.monitor.timer) {
    clearInterval(store.right.deploy.monitor.timer)
    store.right.deploy.monitor.timer = null
  }
}

// ---------- 生成前端实时预览（PRD 二期「边改边看」）----------
// 仅桌面端（Electron）可用：在本地启动生成前端的 Vite dev server（HMR），返回访问地址。
// 浏览器预览模式无 runtime.preview，明确提示需桌面端。
export async function startPreview() {
  const root = store.right.generated?.root
  if (!root) {
    toast('请先在右侧「代码工程」生成同源全栈工程')
    return
  }
  if (PLATFORM !== 'electron' || !runtime.preview) {
    toast('实时预览需在 DevThink 桌面端（Electron）中运行')
    return
  }
  store.right.preview.running = true
  store.right.preview.error = ''
  store.right.preview.url = ''
  try {
    const r = await runtime.preview.start({ root, port: 5180 })
    if (r.ok) store.right.preview.url = r.url
    else store.right.preview.error = r.error
  } catch (e) {
    store.right.preview.error = e.message
  } finally {
    store.right.preview.running = false
  }
}

// 停止预览 dev server 并清空地址。
export async function stopPreview() {
  try {
    if (runtime.preview) await runtime.preview.stop()
  } catch {}
  store.right.preview.url = ''
}

// ---------- 持久化（按账号隔离；未登录时落到本机访客命名空间）----------
export async function saveProject(name) {
  const username = store.user || 'local'
  const project = {
    id: 'proj_' + username + '_' + (store.activeBranchId || 'main'),
    name,
    updatedAt: Date.now(),
    branches: store.branches,
    snapshots: store.snapshots,
    right: store.right
  }
  await runtime.projects.save(username, project)
  toast('项目已保存到本地')
}

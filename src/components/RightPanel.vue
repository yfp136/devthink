<script setup>
import { ref, computed, watch } from 'vue'
import { store, generateProject, runPack, addServer, runDeploy, testServer, saveProject, toast, refreshMonitor, startMonitor, stopMonitor, undoLastEdit, startPreview, stopPreview } from '../store.js'
import { runtime } from '../runtime.js'
import { THRESHOLDS as TH } from '../monitor.js'
import { diffLines } from '../editAgent.js'
import TreeView from './TreeView.vue'

const r = store.right
const tab = computed({
  get: () => r.activeTab || 'code',
  set: (v) => { r.activeTab = v }
})

// 代码工程：点击文件预览内容（selectedPath 提升到 store，供左侧「改代码」共享目标文件）
const selectedContent = computed(() => {
  if (!r.selectedPath || !r.generated) return ''
  return r.generated.files.find((f) => f.path === r.selectedPath)?.content || ''
})
function onOpenFile(node) {
  r.selectedPath = node.path
}

// 生成前端实时预览（PRD 二期「边改边看」）：在桌面端启动 Vite dev server，iframe 内加载 HMR 页面
const pv = computed(() => r.preview)
// 预览模式：dev=开发态(HMR 热更新) / build=成品态(先 vite build 再 preview，所见即所得)
const previewMode = ref('dev')
const previewShell = ref('embed') // 'embed' 内嵌面板 | 'app' 桌面窗口(App 形态，所见即所得)
const switching = ref(false)
async function setPreviewMode(mode) {
  if (mode === previewMode.value) return
  if (pv.value.url) {
    switching.value = true
    await stopPreview()
    switching.value = false
  }
  previewMode.value = mode
}
async function setPreviewShell(shell) {
  if (shell === previewShell.value) return
  if (pv.value.url) {
    switching.value = true
    await stopPreview()
    switching.value = false
  }
  previewShell.value = shell
}
function togglePreview() {
  if (pv.value.url) stopPreview()
  else startPreview(previewMode.value, previewShell.value)
}

// 接收主进程实时推送的预览启动日志
if (runtime.preview?.onLog) {
  runtime.preview.onLog((line) => {
    r.preview.log += line
    // 日志自动滚动到底部
    setTimeout(() => {
      const el = document.getElementById('preview-log')
      if (el) el.scrollTop = el.scrollHeight
    }, 0)
  })
}

// 最近一次对话式改动的行级 diff：展示首个被改文件的增删行；其余文件在列表里列出
const lastEditDiff = computed(() => {
  const e = r.lastEdit
  if (!e || !e.edits.length) return null
  const first = e.edits[0]
  const oldText = e.before && e.before[first.target] != null ? e.before[first.target] : ''
  const added = e.edits.length > 1 ? e.edits.length - 1 : 0
  return {
    target: first.target,
    isNew: oldText === '',
    addedFiles: added,
    lines: diffLines(oldText, first.content)
  }
})

// 改完一键重新部署（需先在「服务器部署」配置好服务器）
async function redeploy() {
  const id = r.deploy.monitor.activeServerId || (r.deploy.servers[0] && r.deploy.servers[0].id)
  if (!id) {
    toast('请先在「服务器部署」添加并配置服务器')
    return
  }
  await runDeploy(id)
}

// 本地打包
const pack = r.pack
const logoInput = ref(null)
function pickLogo() {
  runtime.dialog.selectFile().then((p) => {
    if (p) {
      pack.logo = p
      return
    }
    // 浏览器回退：用原生文件选择
    if (logoInput.value) logoInput.value.click()
  })
}
function onLogoFileChange(e) {
  const f = e.target.files?.[0]
  if (!f) return
  // 浏览器拿不到真实磁盘路径，用文件名占位；Electron 走 dialog 返回真实路径
  pack.logo = f.name
  e.target.value = ''
}

// 服务器部署
const srvForm = ref({ name: '', ip: '', sshUser: 'root', authType: 'password', secret: '', port: 22, domain: '', sslEmail: '', deployType: 'fullstack' })
function addSrv() {
  if (!srvForm.value.name || !srvForm.value.ip) {
    toast('请填写服务器名称与 IP')
    return
  }
  addServer({ ...srvForm.value })
  srvForm.value = { name: '', ip: '', sshUser: 'root', authType: 'password', secret: '', port: 22, domain: '', sslEmail: '', deployType: 'fullstack' }
  toast('服务器已添加')
}

async function doSave() {
  await saveProject(r.docMeta?.branchName || '未命名项目')
}

// 打开打包产物所在位置（Electron 桌面端用系统文件管理器定位安装包）
function openArtifact(p) {
  runtime.shell.showItem(p).then((ok) => {
    if (!ok) toast('无法打开所在位置（桌面端支持）')
  })
}

// 清空当前标签页内容：出错时一键重置（代码工程 / 打包进度 / 部署进度分别处理）
function clearRight() {
  if (tab.value === 'code') {
    if (!r.generated) { toast('暂无生成内容可清空'); return }
    if (window.confirm('清空已生成的同源全栈工程？（可重新点「生成同源全栈工程」恢复）')) {
      r.generated = null
      r.selectedPath = ''
      toast('已清空生成工程')
    }
  } else if (tab.value === 'pack') {
    if (!r.pack.results.length && !r.pack.log && !r.pack.artifacts.length) { toast('暂无打包内容可清空'); return }
    if (window.confirm('清空打包结果与日志？（已生成的安装包文件仍保留在磁盘，仅清除本页记录）')) {
      r.pack.results = []
      r.pack.log = ''
      r.pack.artifacts = []
      toast('已清空打包记录')
    }
  } else if (tab.value === 'deploy') {
    stopMonitor()
    const hasError = r.deploy.servers.some((s) => s.status === 'error' || s.status === 'testing')
    if (!r.deploy.tasks.length && !hasError) { toast('暂无部署内容可清空'); return }
    r.deploy.tasks = []
    r.deploy.running = false
    r.deploy.servers.forEach((s) => { if (s.status === 'error' || s.status === 'testing') { s.status = 'pending'; s.info = '' } })
    toast('已清空部署进度与错误状态')
  } else if (tab.value === 'preview') {
    if (!pv.value.url) { toast('预览未启动'); return }
    stopPreview()
    toast('已停止预览')
  }
}

// ---------- 服务器监控（PRD 二期「部署+监控」）----------
const mon = computed(() => r.deploy.monitor)
const monitorServerId = ref(r.deploy.monitor.activeServerId || (r.deploy.servers[0] && r.deploy.servers[0].id) || '')
// 切换服务器时立即刷新一次
watch(monitorServerId, (id) => { if (id) refreshMonitor(id) })
// 切到 / 离开部署标签页：进入时首次拉一次指标；离开时停掉自动轮询
watch(tab, (t) => {
  if (t !== 'deploy' && mon.value.auto) stopMonitor()
  if (t === 'deploy' && monitorServerId.value && !mon.value.metrics && !mon.value.loading) refreshMonitor(monitorServerId.value)
})
function toggleAuto(e) {
  if (e.target.checked) startMonitor(monitorServerId.value || r.deploy.servers[0]?.id)
  else stopMonitor()
}
// 使用率颜色分级：ok（绿）/ warn（黄，达阈值 75%）/ danger（红，达阈值）
function usageClass(v, t) {
  if (v == null) return ''
  if (v >= t) return 'danger'
  if (v >= t * 0.75) return 'warn'
  return 'ok'
}
// CPU 趋势迷你折线（最近采集历史）
const cpuSparkline = computed(() => {
  const h = mon.value.history
  if (!h.length) return ''
  const w = 200, ht = 40
  return h.map((p, i) => {
    const x = h.length === 1 ? w / 2 : (i / (h.length - 1)) * w
    const y = ht - (Math.min(p.cpu || 0, 100) / 100) * ht
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).join(' ')
})
</script>

<template>
  <div class="panel right">
    <div class="tabs">
      <div class="tab" :class="{ active: tab === 'code' }" @click="tab = 'code'">💻 代码工程</div>
      <div class="tab" :class="{ active: tab === 'pack' }" @click="tab = 'pack'">📦 本地打包</div>
      <div class="tab" :class="{ active: tab === 'deploy' }" @click="tab = 'deploy'">🚀 服务器部署</div>
      <div class="tab" :class="{ active: tab === 'preview' }" @click="tab = 'preview'">👁 实时预览</div>
      <button class="tab-clear" @click="clearRight" title="清空当前标签页内容（出错时一键重置）">🗑️ 清空</button>
    </div>

    <!-- 代码工程：只展示生成的工程文件树，不再渲染中间设计文档 -->
    <div class="panel-body" v-show="tab === 'code'">
      <div v-if="!r.generated" class="muted" style="text-align:center;margin-top:40px">
        暂无生成工程。请到左侧输入需求并点击「✅ 确认并生成成品」。
      </div>
      <template v-else>
        <div v-if="r.dirty" class="dirty-banner" style="background:#fef3c7;color:#92400e;border:1px solid #f59e0b;padding:8px 12px;border-radius:8px;margin-bottom:10px;font-weight:600">📌 设计文档已修改，点击「⚙️ 重新生成同源全栈工程」刷新代码</div>
        <div class="card">
          <h4>🌳 同源全栈工程（{{ r.generated.files.length }} 个文件 · {{ new Date(r.generated.generatedAt).toLocaleString() }}）</h4>
          <div style="display:flex; gap:12px; align-items:flex-start">
            <div class="tree" style="flex:0 0 280px; max-height:360px; overflow:auto; border-right:1px solid var(--border); padding-right:10px">
              <TreeView v-for="n in r.generated.tree" :key="n.name" :node="n" @open="onOpenFile" />
            </div>
            <div style="flex:1; min-width:0">
              <div v-if="!r.selectedPath" class="muted">👈 点击左侧文件查看源码内容</div>
              <div v-else>
                <div class="pill" style="margin-bottom:6px">{{ r.selectedPath }}</div>
                <pre style="background:#0f172a;color:#e2e8f0;padding:12px;border-radius:8px;overflow:auto;max-height:320px;font-size:12px"><code>{{ selectedContent }}</code></pre>
              </div>
            </div>
          </div>
          <div class="panel-foot" style="position:static;border:none;padding:12px 0 0">
            <button class="primary" @click="generateProject">⚙️ 重新生成同源全栈工程</button>
            <button @click="doSave">💾 保存项目</button>
          </div>
          <p class="muted">含 服务端(Express + node:sqlite 真实持久化) / Web 前端(Vue3 已对接 CRUD) / 桌面端(Tauri) / 移动端(Android) 四套同源产出，由左侧设计文档的库表自动解析生成（已在 Electron 模式写入本地磁盘）。选中某个文件后，可在左侧「🛠️ 改代码」模式下对它做对话式修改。</p>
        </div>
        <div v-if="r.lastEdit" class="card" style="border-color:#2563eb">
          <h4>🛠️ 最近一次对话式改动</h4>
          <p class="muted" style="margin:0 0 6px">指令：{{ r.lastEdit.instruction }}</p>
          <ul style="margin:0 0 8px;padding-left:18px">
            <li v-for="(e, i) in r.lastEdit.edits" :key="i"><b>{{ e.target }}</b> — {{ e.explanation }}</li>
          </ul>

          <!-- 行级 diff 预览（首个被改文件） -->
          <div v-if="lastEditDiff" class="diff-box">
            <div class="diff-head">
              <span>📄 {{ lastEditDiff.target }}</span>
              <span v-if="lastEditDiff.isNew" class="badge new">新建文件</span>
              <span v-else class="badge">行级改动（绿=新增 / 红=删除）</span>
              <span v-if="lastEditDiff.addedFiles" class="muted">+ {{ lastEditDiff.addedFiles }} 个其他文件</span>
            </div>
            <pre class="diff-pre"><code><span v-for="(l, i) in lastEditDiff.lines" :key="i" :class="['dl', l.type]"><span class="dl-sign">{{ l.type === 'add' ? '+' : l.type === 'del' ? '-' : ' ' }}</span>{{ l.text }}</span></code></pre>
          </div>

          <label class="auto-toggle">
            <input type="checkbox" style="width:auto" v-model="r.autoAfterEdit.pack" />
            改完自动重新打包
          </label>

          <div class="panel-foot" style="position:static;border:none;padding:0;flex-wrap:wrap;gap:8px;margin-top:10px">
            <button @click="undoLastEdit">↩️ 撤销上次改动</button>
            <button @click="runPack" :disabled="r.pack.running">📦 重新打包</button>
            <button @click="redeploy" :disabled="r.deploy.running">🚀 重新部署</button>
            <span class="muted">累计 {{ r.editHistory.length }} 次改动</span>
          </div>
        </div>
      </template>
    </div>

    <!-- 本地打包 -->
    <div class="panel-body" v-show="tab === 'pack'">
      <div class="card">
        <h4>📦 打包配置（PRD 4.2）</h4>
        <div class="row"><label>软件名称</label><input v-model="pack.name" /></div>
        <div class="row"><label>版本号</label><input v-model="pack.version" style="width:160px" /></div>
        <div class="row logo-row">
          <label>软件图标</label>
          <input ref="logoInput" type="file" accept=".png,.jpg,.jpeg,.svg,.ico,.icns" style="display:none" @change="onLogoFileChange" />
          <input v-model="pack.logo" placeholder="选择图标文件" style="flex:1" />
          <button class="logo-btn" @click="pickLogo">浏览…</button>
        </div>
        <div class="row"><label>自动更新</label><input type="checkbox" v-model="pack.autoUpdate" style="width:auto" /></div>
        <button class="primary" @click="runPack" :disabled="pack.running">🚀 一键打包（真实构建 · 未生成自动生成）</button>
        <p class="muted" style="margin-top:8px">若右侧尚未生成同源工程，点击后将<b>自动生成再打包</b>。Web 前端将真实构建并打包 zip；桌面端/移动端若本机已装对应工具链则真出包，否则如实说明缺什么。<b>浏览器预览仅展示流程，真实出包请在桌面端（Electron）中操作。</b></p>
      </div>
      <div class="card" v-if="pack.results.length">
        <h4>📋 打包结果（真实构建日志）</h4>
        <div v-if="pack.running" class="muted">⏳ 正在真实构建中，请勿关闭窗口…</div>
        <div v-for="(r, i) in pack.results" :key="i" class="pack-target" :class="r.status">
          <div class="pack-target-head">
            <span class="dot" :class="r.status"></span>
            <b>{{ r.target }}</b>
            <span class="muted" style="margin-left:auto">{{ r.status === 'done' ? '✅ 成功' : r.status === 'error' ? '❌ 失败' : '⏭️ 跳过（缺工具链）' }}</span>
          </div>
          <div v-if="r.artifacts && r.artifacts.length" class="artifacts">
            <button v-for="a in r.artifacts" :key="a.path" class="artifact-btn" @click="openArtifact(a.path)">📂 打开：{{ a.name }}</button>
          </div>
          <pre v-if="r.log" class="pack-log">{{ r.log }}</pre>
        </div>
        <pre v-if="pack.log" class="pack-log">{{ pack.log }}</pre>
      </div>
      <div class="card" v-else-if="!pack.running">
        <p class="muted">点击「一键打包」开始。浏览器预览环境仅展示流程；<b>真实出包请在 DevThink 桌面端（Electron）中进行</b>。</p>
      </div>
    </div>

    <!-- 服务器部署 -->
    <div class="panel-body" v-show="tab === 'deploy'">
      <div class="card">
        <h4>🖥️ 添加服务器（PRD 4.3.1）</h4>
        <div class="row"><label>名称</label><input v-model="srvForm.name" placeholder="如：生产环境" /></div>
        <div class="row"><label>IP / 域名</label><input v-model="srvForm.ip" placeholder="118.24.52.156" /></div>
        <div class="row"><label>SSH 端口</label><input v-model.number="srvForm.port" style="width:120px" /></div>
        <div class="row"><label>SSH 账号</label><input v-model="srvForm.sshUser" placeholder="root" style="width:160px" /></div>
        <div class="row"><label>认证方式</label>
          <select v-model="srvForm.authType" style="width:auto">
            <option value="password">密码</option>
            <option value="key">密钥</option>
          </select>
        </div>
        <div class="row"><label>部署形态</label>
          <select v-model="srvForm.deployType" style="width:auto">
            <option value="fullstack">全栈（前端 + Express 后端 · systemd 自启 + Nginx 反代 /api）</option>
            <option value="static">静态站点（仅前端静态托管）</option>
          </select>
        </div>
        <div class="row"><label>密码/密钥</label><input v-model="srvForm.secret" type="password" /></div>
        <div class="row"><label>域名(可选)</label><input v-model="srvForm.domain" placeholder="example.com（填了且填邮箱可启用 HTTPS）" /></div>
        <div class="row"><label>SSL 邮箱(可选)</label><input v-model="srvForm.sslEmail" placeholder="admin@example.com" /></div>
        <button @click="addSrv">+ 添加</button>
      </div>
      <div class="card" v-if="r.deploy.servers.length">
        <h4>服务器列表</h4>
        <div v-for="s in r.deploy.servers" :key="s.id" class="row" style="flex-wrap:wrap;align-items:center">
          <span class="pill">{{ s.name }}</span>
          <span class="pill" style="background:#1e293b;color:#93c5fd">{{ s.deployType === 'fullstack' ? '全栈' : '静态' }}</span>
          <span class="muted">{{ s.ip }}:{{ s.port||22 }} · {{ s.sshUser }}</span>
          <span class="dot" :class="s.status || 'pending'" :title="s.info || ''"></span>
          <button style="margin-left:8px" :disabled="r.deploy.running || s.status==='testing'" @click="testServer(s.id)">测试连接</button>
          <button class="primary" style="margin-left:auto" :disabled="r.deploy.running" @click="runDeploy(s.id)">一键部署</button>
        </div>
        <p v-if="r.deploy.servers.some(s=>s.info)" class="muted" style="white-space:pre-wrap">{{ r.deploy.servers.filter(s=>s.info).map(s=>'['+s.name+'] '+s.info).join('\n\n') }}</p>
      </div>
      <div class="card" v-if="r.deploy.tasks.length">
        <h4>部署进度</h4>
        <div class="task" v-for="(t, i) in r.deploy.tasks" :key="i" style="flex-wrap:wrap">
          <span class="dot" :class="t.status"></span>{{ t.name }}
          <span class="muted" style="margin-left:auto">{{ t.status === 'done' ? '完成' : t.status === 'running' ? '进行中' : t.status === 'error' ? '失败' : '等待' }}</span>
          <pre v-if="t.detail" class="ssh-detail">{{ t.detail }}</pre>
        </div>
        <p class="muted">每一步均为<b>真实执行</b>。<b>全栈</b>形态：环境预检（含 Node≥22）→ 构建前端 → 建目录 → 上传前后端 → 服务器装依赖 → 注册 systemd 开机自启 → Nginx（静态根 + /api 反代后端）→ 可选 SSL → 防火墙 → 日志轮转 + 数据库每日备份。<b>静态</b>形态：仅前端静态托管（不含后端 /api）。部署前会二次确认，建议先在测试服务器验证。<b>真实部署请在 DevThink 桌面端（Electron）中操作。</b></p>
      </div>
      <div class="card">
        <h4>📊 运维监控（真实 SSH 采集）</h4>
        <div v-if="!r.deploy.servers.length" class="muted">请先在上方添加服务器并填写密码 / 密钥，才能进行监控。</div>
        <template v-else>
          <div class="row" style="flex-wrap:wrap;align-items:center">
            <label style="width:auto">服务器</label>
            <select v-model="monitorServerId" style="width:auto">
              <option v-for="s in r.deploy.servers" :key="s.id" :value="s.id">{{ s.name }}（{{ s.ip }}）</option>
            </select>
            <button style="margin-left:8px" :disabled="mon.loading" @click="refreshMonitor(monitorServerId)">🔄 刷新</button>
            <label style="margin-left:auto;display:flex;gap:6px;align-items:center;font-size:12px;color:var(--text-2)">
              <input type="checkbox" style="width:auto" :checked="mon.auto" @change="toggleAuto" /> 自动（10s）
            </label>
          </div>

          <div v-if="mon.loading && !mon.metrics" class="muted" style="margin-top:10px">⏳ 正在通过 SSH 采集服务器指标…</div>

          <div v-if="mon.metrics" class="monitor-grid">
            <div class="metric">
              <div class="metric-head"><span>CPU 使用率</span><b :class="usageClass(mon.metrics.cpu, TH.cpu)">{{ mon.metrics.cpu != null ? mon.metrics.cpu.toFixed(1) + '%' : '—' }}</b></div>
              <div class="bar"><div class="bar-fill" :class="usageClass(mon.metrics.cpu, TH.cpu)" :style="{ width: (mon.metrics.cpu || 0) + '%' }"></div></div>
            </div>
            <div class="metric">
              <div class="metric-head"><span>内存 {{ mon.metrics.mem.used }}/{{ mon.metrics.mem.total }} MB</span><b :class="usageClass(mon.metrics.mem.usedPct, TH.mem)">{{ mon.metrics.mem.usedPct != null ? mon.metrics.mem.usedPct + '%' : '—' }}</b></div>
              <div class="bar"><div class="bar-fill" :class="usageClass(mon.metrics.mem.usedPct, TH.mem)" :style="{ width: (mon.metrics.mem.usedPct || 0) + '%' }"></div></div>
            </div>
            <div class="metric" v-if="mon.metrics.disk.root">
              <div class="metric-head"><span>磁盘 / {{ mon.metrics.disk.root.used }}/{{ mon.metrics.disk.root.total }}</span><b :class="usageClass(mon.metrics.disk.root.usedPct, TH.disk)">{{ mon.metrics.disk.root.usedPct != null ? mon.metrics.disk.root.usedPct + '%' : '—' }}</b></div>
              <div class="bar"><div class="bar-fill" :class="usageClass(mon.metrics.disk.root.usedPct, TH.disk)" :style="{ width: (mon.metrics.disk.root.usedPct || 0) + '%' }"></div></div>
            </div>
            <div class="metric meta">
              <div><span class="muted">主机</span> {{ mon.metrics.host.name || '—' }} <span class="muted">{{ mon.metrics.host.kernel }}</span></div>
              <div><span class="muted">负载</span> {{ mon.metrics.load1 != null ? mon.metrics.load1 : '—' }} <span class="muted">· {{ mon.metrics.uptime }}</span></div>
              <div><span class="muted">Nginx</span> <span :class="mon.metrics.nginx.active ? 'ok' : 'bad'">{{ mon.metrics.nginx.state }}</span> <span class="muted" v-if="mon.metrics.nginx.tcpEstab != null">· TCP {{ mon.metrics.nginx.tcpEstab }}</span></div>
              <div><span class="muted">站点</span> <span :class="mon.metrics.site.ok ? 'ok' : 'bad'">{{ mon.metrics.site.httpCode || '—' }}</span> <span class="muted">{{ mon.metrics.site.url }}</span></div>
            </div>
            <div class="metric spark">
              <div class="muted" style="font-size:11px;margin-bottom:2px">CPU 趋势（最近 {{ mon.history.length }} 次采集）</div>
              <svg viewBox="0 0 200 40" width="100%" height="40" preserveAspectRatio="none">
                <polyline :points="cpuSparkline" fill="none" stroke="#2563eb" stroke-width="2" />
              </svg>
            </div>
          </div>

          <div v-if="mon.alerts.length" class="alerts">
            <div v-for="(a, i) in mon.alerts.slice(0, 8)" :key="i" class="alert">
              <span class="alert-ts">{{ new Date(a.ts).toLocaleTimeString() }}</span>
              <span class="alert-msg">{{ a.msg }}</span>
            </div>
          </div>
          <p class="muted" style="margin-top:8px">指标通过真实 SSH 在服务器本地采集（top / free / df / systemctl / curl），非估算。<b>真实监控请在 DevThink 桌面端（Electron）中操作</b>；阈值：CPU ≥ {{ TH.cpu }}% / 内存 ≥ {{ TH.mem }}% / 磁盘 ≥ {{ TH.disk }}% 触发告警并回流左侧窗口。</p>
        </template>
      </div>
    </div>
    <!-- 实时预览 -->
    <div class="panel-body" v-show="tab === 'preview'">
      <div class="card">
        <h4>👁 生成前端实时预览</h4>
        <p class="muted">在桌面端启动生成前端的本地服务，内置窗口实时查看效果。需先「生成同源全栈工程」并在 DevThink 桌面端运行。</p>
        <div class="pv-mode">
          <button :class="['pv-mode-btn', { active: previewMode === 'dev' }]" @click="setPreviewMode('dev')" :disabled="switching">🔄 开发预览</button>
          <button :class="['pv-mode-btn', { active: previewMode === 'build' }]" @click="setPreviewMode('build')" :disabled="switching">📦 成品预览</button>
          <button :class="['pv-mode-btn', { active: previewMode === 'fullstack' }]" @click="setPreviewMode('fullstack')" :disabled="switching">🔗 联调真实数据</button>
        </div>
        <div class="pv-shell">
          <span class="muted">预览形态：</span>
          <button :class="['pv-shell-btn', { active: previewShell === 'embed' }]" @click="setPreviewShell('embed')" :disabled="switching">🪟 内嵌面板</button>
          <button :class="['pv-shell-btn', { active: previewShell === 'app' }]" @click="setPreviewShell('app')" :disabled="switching">🖥 桌面窗口（App 形态）</button>
        </div>
        <div v-if="pv.url" class="pv-running-hint">
          <span class="dot running"></span>
          正在运行：<b>{{ pv.mode === 'fullstack' ? '联调真实数据' : (pv.mode === 'build' ? '成品预览' : '开发预览') }}</b>
          <span class="muted">（点击其他模式会自动停止并切换）</span>
        </div>
        <div class="panel-foot" style="position:static;border:none;padding:0 0 12px">
          <button class="primary" @click="togglePreview" :disabled="pv.running">{{ pv.url ? '⏹ 停止预览' : (pv.running ? '⏳ 启动中…' : (previewMode === 'fullstack' ? '▶️ 启动联调预览' : (previewMode === 'build' ? '▶️ 启动成品预览' : '▶️ 启动开发预览'))) }}</button>
          <span v-if="pv.error" class="muted" style="color:#dc2626">{{ pv.error }}</span>
        </div>
        <div v-if="pv.url" class="preview-wrap">
          <div class="preview-bar">
            <span class="pill">{{ pv.url }}</span>
            <span class="pill" style="background:#1e293b;color:#93c5fd">{{ pv.mode === 'fullstack' ? '联调真实数据' : (pv.mode === 'build' ? '成品预览' : '开发预览') }}</span>
            <span class="pill" style="background:#7c3aed;color:#ede9fe">{{ previewShell === 'app' ? '桌面窗口' : '内嵌面板' }}</span>
            <a :href="pv.url" target="_blank" rel="noopener" class="artifact-btn">↗ 新窗口打开</a>
          </div>
          <iframe v-if="previewShell === 'embed'" :src="pv.url" class="preview-frame" title="生成前端预览"></iframe>
          <div v-else class="app-shell-hint">
            ✅ 已在独立桌面窗口中打开（带原生标题栏与应用菜单）。<br />
            这就是该软件「打包成桌面 App」后用户看到的样子。若窗口已关闭，点「停止预览」后重新启动即可再次打开。
          </div>
        </div>
        <pre v-if="pv.running || pv.log" id="preview-log" class="preview-log">{{ pv.log || '启动中…' }}</pre>
        <p v-else-if="!pv.running" class="muted">
          <template v-if="previewShell === 'app'">「桌面窗口（App 形态）」会在独立 Electron 窗口中打开生成的软件，带原生菜单与标题栏——所见即所得地看到它作为桌面 App 分发后的样子。数据真实度仍由上方模式（开发 / 成品 / 联调）决定。</template>
          <template v-else-if="previewMode === 'fullstack'">「联调真实数据」会同时启动前端与 Express 后端（3001，node:sqlite），前端 <code>/api</code> 代理到后端，看到的是带真实增删改查数据的系统（后端首次启动会自动写入示例数据）。需工程包含 backend 目录。</template>
          <template v-else-if="previewMode === 'build'">「成品预览」会先 <code>vite build</code> 再起 preview 服务，看到的即最终部署/打包的网页（所见即所得）。改代码后需停止并重新启动才刷新。</template>
          <template v-else>「开发预览」带 HMR 热更新，改代码自动刷新。仅看 UI 可不改后端；想联调真实数据请选「联调真实数据」模式。</template>
        </p>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* 标签页右侧的清空按钮：出错/重来时一键重置 */
.tab-clear {
  margin-left: auto;
  align-self: center;
  font-size: 12px;
  padding: 4px 10px;
  border-radius: 6px;
  background: #fef2f2;
  border: 1px solid #fca5a5;
  color: #dc2626;
  cursor: pointer;
  transition: all 0.15s;
}
.tab-clear:hover {
  background: #fee2e2;
  border-color: #ef4444;
  color: #991b1b;
}

/* 软件图标行：输入框与浏览按钮横向排列 */
.logo-row {
  display: flex;
  gap: 8px;
  align-items: center;
  flex-wrap: nowrap;
}
.logo-row > label {
  width: 90px;
  flex-shrink: 0;
  color: var(--text-2);
}
.logo-row > input[type="text"] {
  flex: 1;
  min-width: 0;
}
.logo-btn {
  white-space: nowrap;
  flex-shrink: 0;
}

/* 真实打包结果：按目标展示日志与产物 */
.pack-target {
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 10px 12px;
  margin-bottom: 10px;
  background: var(--panel-2);
}
.pack-target.done { border-left: 3px solid #16a34a; }
.pack-target.error { border-left: 3px solid #dc2626; }
.pack-target.skipped { border-left: 3px solid #d97706; }
.pack-target-head {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
}
.artifacts {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin: 8px 0;
}
.artifact-btn {
  font-size: 12px;
  padding: 5px 10px;
  border-radius: 6px;
  background: #ecfdf5;
  border: 1px solid #6ee7b7;
  color: #065f46;
  cursor: pointer;
}
.artifact-btn:hover { background: #d1fae5; }
.pack-log {
  background: #0f172a;
  color: #e2e8f0;
  padding: 10px;
  border-radius: 6px;
  max-height: 260px;
  overflow: auto;
  font-size: 11px;
  white-space: pre-wrap;
  word-break: break-all;
  margin-top: 6px;
}

/* 监控面板 */
.monitor-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin-top: 10px;
}
.metric {
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 10px 12px;
  background: var(--panel-2);
}
.metric-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 12px;
  color: var(--text-2);
  margin-bottom: 6px;
}
.metric-head b { font-size: 15px; color: var(--text); }
.metric-head b.ok { color: #16a34a; }
.metric-head b.warn { color: #d97706; }
.metric-head b.danger { color: #dc2626; }
.bar {
  height: 8px;
  background: #e2e8f0;
  border-radius: 999px;
  overflow: hidden;
}
.bar-fill {
  height: 100%;
  background: #16a34a;
  transition: width 0.4s;
}
.bar-fill.warn { background: #d97706; }
.bar-fill.danger { background: #dc2626; }
.metric.meta {
  grid-column: 1 / -1;
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 12px;
}
.metric.meta .ok { color: #16a34a; font-weight: 600; }
.metric.meta .bad { color: #dc2626; font-weight: 600; }
.metric.spark { grid-column: 1 / -1; }
.alerts {
  margin-top: 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.alert {
  display: flex;
  gap: 8px;
  font-size: 12px;
  padding: 6px 10px;
  border-radius: 6px;
  background: #fef2f2;
  border: 1px solid #fca5a5;
  color: #991b1b;
}
.alert-ts { color: #b91c1c; flex-shrink: 0; }

/* 对话式改动的行级 diff 预览 */
.diff-box {
  border: 1px solid var(--border);
  border-radius: 8px;
  overflow: hidden;
  margin: 6px 0 10px;
  background: var(--panel-2);
}
.diff-head {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 10px;
  font-size: 12px;
  background: #f8fafc;
  border-bottom: 1px solid var(--border);
  flex-wrap: wrap;
}
.badge {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 999px;
  background: #e0e7ff;
  color: #3730a3;
}
.badge.new { background: #dcfce7; color: #166534; }
.diff-pre {
  margin: 0;
  max-height: 280px;
  overflow: auto;
  padding: 8px 0;
  font-size: 11px;
  line-height: 1.5;
  background: #0f172a;
  color: #e2e8f0;
}
.diff-pre code { display: block; white-space: pre; }
.dl { display: block; padding: 0 10px; }
.dl-sign { display: inline-block; width: 14px; color: #94a3b8; user-select: none; }
.dl.add { background: rgba(22,163,74,0.18); color: #bbf7d0; }
.dl.del { background: rgba(220,38,38,0.18); color: #fecaca; }
.dl.same { color: #cbd5e1; }
.auto-toggle {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-2);
  margin: 4px 0 2px;
  cursor: pointer;
}

/* 实时预览面板 */
.pv-mode {
  display: flex;
  gap: 8px;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.pv-mode-btn {
  flex: 1;
  min-width: 160px;
  font-size: 12px;
  padding: 8px 10px;
  border-radius: 8px;
  background: #fff;
  border: 1px solid #d1d5db;
  color: #374151;
  cursor: pointer;
  transition: all 0.15s;
}
.pv-mode-btn:hover:not(:disabled) { border-color: #2563eb; color: #2563eb; background: #eff6ff; }
.pv-mode-btn.active {
  background: #2563eb;
  border-color: #2563eb;
  color: #fff;
  font-weight: 600;
  box-shadow: 0 2px 4px rgba(37,99,235,0.18);
}
.pv-mode-btn:disabled { opacity: 0.5; cursor: not-allowed; }
.pv-shell {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  flex-wrap: wrap;
}
.pv-shell-btn {
  font-size: 12px;
  padding: 6px 12px;
  border-radius: 8px;
  background: #fff;
  border: 1px solid #d1d5db;
  color: #374151;
  cursor: pointer;
  transition: all 0.15s;
}
.pv-shell-btn:hover:not(:disabled) { border-color: #7c3aed; color: #7c3aed; background: #f5f3ff; }
.pv-shell-btn.active {
  background: #7c3aed;
  border-color: #7c3aed;
  color: #fff;
  font-weight: 600;
  box-shadow: 0 2px 4px rgba(124,58,237,0.18);
}
.pv-shell-btn:disabled { opacity: 0.5; cursor: not-allowed; }
.pv-running-hint {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  margin-bottom: 12px;
  background: #eff6ff;
  border: 1px solid #bfdbfe;
  border-radius: 8px;
  font-size: 12px;
  color: #1e40af;
}
.pv-running-hint .dot.running {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #22c55e;
  box-shadow: 0 0 0 3px rgba(34,197,94,0.25);
  animation: pulse-dot 1.4s infinite;
}
@keyframes pulse-dot {
  0% { box-shadow: 0 0 0 0 rgba(34,197,94,0.4); }
  70% { box-shadow: 0 0 0 6px rgba(34,197,94,0); }
  100% { box-shadow: 0 0 0 0 rgba(34,197,94,0); }
}
.preview-wrap {
  margin-top: 10px;
  border: 1px solid var(--border);
  border-radius: 8px;
  overflow: hidden;
}
.preview-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 10px;
  background: var(--panel-2);
  border-bottom: 1px solid var(--border);
  flex-wrap: wrap;
}
.preview-frame {
  width: 100%;
  height: 460px;
  border: none;
  background: #fff;
  display: block;
}
.app-shell-hint {
  padding: 28px 20px;
  background: #faf5ff;
  color: #6d28d9;
  font-size: 13px;
  line-height: 1.7;
}
.preview-log {
  margin-top: 10px;
  max-height: 220px;
  overflow: auto;
  background: #0f172a;
  color: #e2e8f0;
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  font-size: 12px;
  line-height: 1.5;
  padding: 10px 12px;
  border-radius: 8px;
  white-space: pre-wrap;
  word-break: break-all;
}
</style>

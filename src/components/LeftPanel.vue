<script setup>
import { ref, computed, watch, nextTick } from 'vue'
import { store, newBranch, switchBranch, activeBranch, saveSnapshot, pushDocToRight, setDraftDoc, reviseDoc, editCode, toast } from '../store.js'
import { runtime } from '../runtime.js'
import { MODE_LABELS } from '../ai/prompts.js'
import { readFileAsText, readImage, buildContext, formatSize } from '../ai/fileReader.js'
import MarkdownView from './MarkdownView.vue'

const TAGS = ['需求', '数据库', '接口', '部署', '风险']
const input = ref('')
const loading = ref(false)
const lastDoc = ref('')
const bodyEl = ref(null)
const branch = computed(() => activeBranch())
const composeMode = ref('chat')
const hasDoc = computed(() => !!(store.right.doc || store.right.draft))

// ---------- 本地文件上传（客户文档 → 大模型分析）----------
const attachments = ref([])
const fileInput = ref(null)
const dragOver = ref(false)
const reading = ref(false)

const contextText = computed(() => buildContext(attachments.value))

function pickFiles() {
  fileInput.value?.click()
}
async function onFilesPicked(e) {
  const files = e.target.files
  if (files && files.length) await addFiles(files)
  e.target.value = '' // 允许重复选同一文件
}
async function onDrop(e) {
  dragOver.value = false
  const files = e.dataTransfer?.files
  if (files && files.length) await addFiles(files)
}
async function addFiles(fileList) {
  reading.value = true
  try {
    for (const f of Array.from(fileList)) {
      const r = await readFileAsText(f)
      if (r.unsupported) {
        toast('⚠️ ' + (r.note || '该文件无法解析'))
        continue
      }
      if (r.image) {
        toast('ℹ️ ' + (r.note || '图片不支持文本分析'))
        continue
      }
      if (r.truncated) toast('📄 ' + r.name + ' 内容过长已截断')
      attachments.value.push({ name: r.name, size: r.size, type: r.type, text: r.text })
    }
  } finally {
    reading.value = false
  }
}
function removeAttachment(i) {
  attachments.value.splice(i, 1)
}

// ---------- 图片附件（微信式截图 / 发图）----------
const imageAttachments = ref([])
const imageInput = ref(null)

function pickImages() {
  imageInput.value?.click()
}
async function onImagesPicked(e) {
  const files = e.target.files
  if (files && files.length) await addImages(files)
  e.target.value = '' // 允许重复选同一文件
}
async function addImages(fileList) {
  for (const f of Array.from(fileList)) {
    const r = await readImage(f)
    if (r.unsupported) {
      toast('⚠️ ' + (r.note || '该图片无法读取'))
      continue
    }
    imageAttachments.value.push({ name: r.name, size: r.size, dataUrl: r.dataUrl })
  }
}
function removeImage(i) {
  imageAttachments.value.splice(i, 1)
}
// 微信式：在输入框直接 Ctrl+V 粘贴截图（浏览器从剪贴板读图）
async function onPaste(e) {
  const items = e.clipboardData?.items
  if (!items) return
  const files = []
  for (const it of items) {
    if (it.type && it.type.startsWith('image/')) {
      const f = it.getAsFile()
      if (f) files.push(f)
    }
  }
  if (files.length) {
    e.preventDefault()
    await addImages(files)
  }
}
function openImage(dataUrl) {
  window.open(dataUrl, '_blank')
}

function ensureBranch() {
  if (!store.branches.length) newBranch('主方案')
  return activeBranch()
}

// 清空当前方案的对话记录（出错或重来时一键重置）
function clearChat() {
  const b = activeBranch()
  if (!b) return
  if (!b.messages.length) {
    toast('对话已经是空的')
    return
  }
  if (window.confirm('确定清空当前方案的对话记录吗？此操作不可撤销。')) {
    b.messages = []
    lastDoc.value = ''
    imageAttachments.value = []
    toast('对话已清空')
    scrollDown()
  }
}

async function send(text) {
  if (composeMode.value === 'revise') {
    await sendRevise(text ?? input.value)
    return
  }
  if (composeMode.value === 'edit') {
    await sendEdit(text ?? input.value)
    return
  }
  const b = ensureBranch()
  const content = (text ?? input.value).trim()
  const imgs = imageAttachments.value
  // 微信式：允许「只发图」或「文字 + 图」；两者皆空则不发送
  if (!content && !imgs.length) return
  if (loading.value) return
  // 当前默认模型为纯文本，无法识别图片内容，给 AI 一句提示避免它误以为能看图
  // 需求对齐模式：用户回复确认词 → 直接进入开发（生成设计文档并推送右窗）
  if (b.mode === 'align' && !b.confirmed && content && isConfirmText(content)) {
    b.messages.push({ role: 'user', content, tags: [] })
    b.confirmed = true
    input.value = ''
    attachments.value = []
    imageAttachments.value = []
    scrollDown()
    await genDoc()
    finalize()
    return
  }
  let ctx = contextText.value
  if (imgs.length) {
    ctx = (ctx ? ctx + '\n\n' : '') +
      `[用户同时发送了 ${imgs.length} 张图片，但当前为纯文本模型，无法识别图片内容；如需要图中信息，请提示用户用文字补充描述。]`
  }
  input.value = ''
  attachments.value = []
  imageAttachments.value = []
  b.messages.push({
    role: 'user',
    content: content || '（仅发送了图片）',
    tags: [],
    images: imgs.length ? imgs.map((i) => ({ name: i.name, size: i.size, dataUrl: i.dataUrl })) : undefined
  })
  loading.value = true
  try {
    const reply = await runtime.ai.chat({ messages: b.messages, mode: b.mode, config: store.config, context: ctx })
    b.messages.push(reply)
  } catch (e) {
    b.messages.push({ role: 'assistant', content: '⚠️ 调用失败：' + e.message })
  } finally {
    loading.value = false
    scrollDown()
  }
}

// 上传文档分析：把客户文档作为上下文，让模型做结构化抽取
async function analyzeDoc() {
  if (!attachments.value.length) {
    toast('请先点 📎 上传客户文档')
    return
  }
  const b = ensureBranch()
  const ctx = contextText.value
  const content = (input.value.trim() || '请分析以上上传文档，并提炼可转化为软件系统的需求、实体、字段与业务流程。')
  input.value = ''
  attachments.value = []
  imageAttachments.value = []
  b.messages.push({ role: 'user', content: '📎 分析上传文档：' + content, tags: [] })
  loading.value = true
  try {
    const reply = await runtime.ai.chat({ messages: b.messages, mode: 'analyze', config: store.config, context: ctx })
    b.messages.push(reply)
  } catch (e) {
    b.messages.push({ role: 'assistant', content: '⚠️ 调用失败：' + e.message })
  } finally {
    loading.value = false
    scrollDown()
  }
}

async function genDoc() {
  const b = ensureBranch()
  if (!b.messages.length) {
    toast('请先输入需求再生成文档')
    return
  }
  const ctx = contextText.value
  loading.value = true
  try {
    const reply = await runtime.ai.chat({ messages: b.messages, mode: 'doc', config: store.config, context: ctx })
    b.messages.push(reply)
    lastDoc.value = reply.content
    setDraftDoc(reply.content)
    toast('设计文档已生成，可点「定稿推送」')
  } catch (e) {
    toast('生成失败：' + e.message)
  } finally {
    loading.value = false
    attachments.value = []
    imageAttachments.value = []
    scrollDown()
  }
}

function finalize() {
  const b = activeBranch()
  if (!b) return
  const doc = lastDoc.value || b.messages.find((m) => m.role === 'assistant')?.content || ''
  if (!doc) {
    toast('请先生成设计文档')
    return
  }
  saveSnapshot(b.name, doc)
  pushDocToRight(b.name, doc)
}

async function selfCheck() {
  await send('【需求自检】请检查以上需求，列出模糊点、逻辑冲突、缺失字段、权限漏洞与部署风险，并高亮提示。')
}

// 改文档模式：把修改指令交给 reviseDoc，AI 返回改后完整文档
async function sendRevise(reviseText) {
  const b = ensureBranch()
  const content = (reviseText ?? input.value).trim()
  if (!content || loading.value) return
  const ctx = contextText.value
  input.value = ''
  attachments.value = []
  imageAttachments.value = []
  b.messages.push({ role: 'user', content: '✏️ 修改文档：' + content })
  loading.value = true
  try {
    const newDoc = await reviseDoc(content, ctx)
    if (newDoc) {
      b.messages.push({ role: 'assistant', content: newDoc })
      lastDoc.value = newDoc
    }
  } catch (e) {
    b.messages.push({ role: 'assistant', content: '⚠️ 修改失败：' + e.message })
  } finally {
    loading.value = false
    scrollDown()
  }
}

// 对话式改代码：把自然语言指令交给 editCode，AI 理解意图并改写工程文件
async function sendEdit(instruction) {
  const b = ensureBranch()
  const content = (instruction ?? input.value).trim()
  if (!content || loading.value) return
  if (!store.right.generated) {
    toast('请先在右侧「代码工程」生成同源工程，再对话式改代码')
    return
  }
  input.value = ''
  attachments.value = []
  imageAttachments.value = []
  b.messages.push({
    role: 'user',
    content: '💻 改代码：' + content + (store.right.selectedPath ? `（目标文件：${store.right.selectedPath}）` : '（由 AI 自行判断要改的文件）')
  })
  loading.value = true
  try {
    const res = await editCode(content, store.right.selectedPath)
    if (res?.ok) {
      const summary = res.edits.map((e) => `• ${e.target}：${e.explanation}`).join('\n')
      b.messages.push({
        role: 'assistant',
        content: '✅ 已应用代码改动：\n' + summary + '\n\n可在右侧「代码工程」查看更新后的源码；若不满意，点「撤销上次改动」即可回退。'
      })
    } else {
      b.messages.push({ role: 'assistant', content: '⚠️ 未能应用改动：' + (res?.error || 'AI 未返回有效结果') })
    }
  } catch (e) {
    b.messages.push({ role: 'assistant', content: '⚠️ 修改失败：' + e.message })
  } finally {
    loading.value = false
    scrollDown()
  }
}

// 需求确认词识别：用户在对齐模式回复这些词即视为确认开发
function isConfirmText(t) {
  return ['确认', '可以', '开始', '没问题', '同意', 'go', 'yes', '开发吧', '做吧', '准', 'ok']
    .some((w) => t.toLowerCase().includes(w.toLowerCase()))
}

// 确认并开发：生成设计文档 + 推送右窗（开发窗口）
async function confirmAndDevelop() {
  const b = activeBranch()
  if (!b || b.confirmed) return
  b.confirmed = true
  await genDoc()
  finalize()
}

function toggleTag(msg, tag) {
  if (!msg.tags) msg.tags = []
  const i = msg.tags.indexOf(tag)
  if (i >= 0) msg.tags.splice(i, 1)
  else msg.tags.push(tag)
}

function scrollDown() {
  requestAnimationFrame(() => {
    const el = bodyEl.value
    if (!el) return
    el.scrollTop = el.scrollHeight
    // Markdown 渲染可能略晚于 DOM 更新，做一次兜底滚动
    setTimeout(() => {
      if (bodyEl.value) bodyEl.value.scrollTop = bodyEl.value.scrollHeight
    }, 80)
  })
}
// 切换分支或新增消息时自动滚到最新：发送对话后内容自动上滚显示
watch(() => store.activeBranchId, scrollDown)
watch(() => branch.value?.messages.length, scrollDown)
</script>

<template>
  <div class="panel left">
    <div class="panel-head">
      <span>🧠 逻辑思考窗口</span>
      <select :value="store.activeBranchId" @change="switchBranch($event.target.value)" style="width:auto">
        <option v-for="b in store.branches" :key="b.id" :value="b.id">{{ b.name }}</option>
      </select>
      <button @click="newBranch()">+ 分支</button>
      <select v-if="branch" v-model="branch.mode" style="width:auto">
        <option v-for="(label, key) in MODE_LABELS" :key="key" :value="key">{{ label }}</option>
      </select>
      <button class="head-btn" @click="clearChat" title="清空当前方案的对话记录">🗑️ 清空</button>
    </div>

    <div class="panel-body" ref="bodyEl">
      <div v-if="!store.branches.length" class="muted" style="text-align:center;margin-top:40px">
        还没有方案分支。在下方输入需求，或点击「+ 分支」开始。
      </div>
      <div v-for="(m, idx) in (branch?.messages || [])" :key="idx" class="msg" :class="m.role">
        <div class="role">{{ m.role === 'user' ? '你' : 'AI' }}</div>
        <div v-if="m.thinking" class="think">
          <details open>
            <summary>💭 思考过程</summary>
            <div class="think-body">{{ m.thinking }}</div>
          </details>
        </div>
        <MarkdownView v-if="m.role === 'assistant'" :source="m.content" />
        <div v-else style="white-space:pre-wrap">{{ m.content }}</div>
        <div v-if="m.images && m.images.length" class="msg-images">
          <img v-for="(img, i) in m.images" :key="i" :src="img.dataUrl" :alt="img.name" class="msg-img" @click="openImage(img.dataUrl)" title="点击放大" />
        </div>
        <div class="tags" v-if="m.role === 'user'">
          <span v-for="t in TAGS" :key="t" class="tag" :style="m.tags?.includes(t) ? 'background:#bfdbfe;color:#1d4ed8' : ''" @click="toggleTag(m, t)">{{ t }}</span>
        </div>
        <div v-if="m.role === 'assistant' && branch?.mode === 'align' && !branch?.confirmed" class="confirm-bar">
          <button class="btn-confirm primary" @click="confirmAndDevelop">✅ 确认并开发</button>
          <span class="muted">认可理解后点击，或回复「确认」</span>
        </div>
      </div>
      <div v-if="loading" class="muted" style="padding:6px 2px">AI 生成中…</div>
    </div>

    <div class="panel-foot">
      <button @click="genDoc" :disabled="loading">📄 生成设计文档</button>
      <button class="primary" @click="finalize">✅ 定稿推送开发窗口</button>
      <button @click="selfCheck" :disabled="loading">🔍 需求自检</button>
      <button @click="analyzeDoc" :disabled="loading || !attachments.length" :title="attachments.length ? '基于上传文档做分析' : '请先上传文档'">📎 分析文档</button>
    </div>
    <div
      class="composer"
      @dragover.prevent="dragOver=true"
      @dragenter.prevent="dragOver=true"
      @dragleave.prevent="dragOver=false"
      @drop.prevent="onDrop"
      @paste="onPaste"
    >
      <div v-if="dragOver" class="drop-hint">松开鼠标即可上传文档（txt / md / pdf / docx / csv / 代码…）</div>

      <div v-if="hasDoc || store.right.generated" class="mode-switch">
        <button
          class="mode-btn"
          :class="{ active: composeMode === 'chat' }"
          @click="composeMode = 'chat'"
        >💬 对话</button>
        <button
          v-if="hasDoc"
          class="mode-btn"
          :class="{ active: composeMode === 'revise' }"
          @click="composeMode = 'revise'"
        >✏️ 改文档</button>
        <button
          v-if="store.right.generated"
          class="mode-btn"
          :class="{ active: composeMode === 'edit' }"
          @click="composeMode = 'edit'"
        >🛠️ 改代码</button>
      </div>

      <div v-if="attachments.length" class="attachments">
        <div v-for="(a, i) in attachments" :key="i" class="att-chip" :title="a.name">
          <span class="att-icon">📄</span>
          <span class="att-name">{{ a.name }}</span>
          <span class="att-size">{{ formatSize(a.size) }}</span>
          <span class="att-del" @click="removeAttachment(i)">✕</span>
        </div>
      </div>

      <div v-if="imageAttachments.length" class="img-previews">
        <div v-for="(img, i) in imageAttachments" :key="i" class="img-thumb">
          <img :src="img.dataUrl" :alt="img.name" />
          <span class="img-name">{{ img.name }}</span>
          <span class="img-del" @click="removeImage(i)">✕</span>
        </div>
      </div>

      <textarea
        v-model="input"
        :placeholder="composeMode === 'edit'
          ? '描述你想对代码做的修改，如：给后端加按名称搜索的接口 / 把列表页改成卡片式 / 加一个登录页（可先在右侧选中目标文件）'
          : composeMode === 'revise'
          ? '描述要做的修改，如：给 user 表加 phone/email 字段'
          : branch?.mode === 'align'
          ? '用大白话说你的业务想法就行——AI 会先思考、复述理解并和你确认，确认后才进入开发'
          : '描述你的业务想法，或直接粘贴客户文档内容…\nEnter 发送，Shift+Enter 换行，支持拖拽上传文档'
        "
        @keydown.enter.exact.prevent="send()"
      ></textarea>

      <div class="composer-toolbar">
        <div class="toolbar-left">
          <button class="btn-upload" @click="pickFiles" :disabled="reading" title="上传 txt / md / pdf / docx / csv / 代码">
            {{ reading ? '读取中…' : '📎 上传文档' }}
          </button>
          <button class="btn-upload" @click="pickImages" title="截图 / 上传图片：可选图片文件，或直接 Ctrl+V 粘贴截图">📷 截图</button>
          <button v-if="attachments.length" class="btn-text" @click="attachments = []" title="清空已选附件">清空</button>
        </div>
        <div class="toolbar-center">
          <span v-if="attachments.length" class="att-status">
            已附 {{ attachments.length }} 个文档，将随本次消息一起发送
          </span>
          <span v-else class="att-status empty">可拖拽或点击 📎 上传客户文档</span>
        </div>
        <button class="btn-send primary" @click="send()" :disabled="loading">
          {{ composeMode === 'edit' ? '应用改动' : composeMode === 'revise' ? '应用修改' : '发送' }}
        </button>
      </div>

      <input ref="fileInput" type="file" multiple accept=".txt,.md,.markdown,.csv,.tsv,.json,.js,.ts,.jsx,.tsx,.vue,.py,.java,.go,.c,.cpp,.h,.sh,.yml,.yaml,.xml,.html,.css,.sql,.log,.pdf,.docx" style="display:none" @change="onFilesPicked" />
      <input ref="imageInput" type="file" accept="image/*" multiple style="display:none" @change="onImagesPicked" />
    </div>
  </div>
</template>

<style scoped>
/* 重写全局 .composer，让输入区垂直堆叠、更大气 */
.composer {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 14px;
  border-top: 1px solid var(--border);
  background: var(--panel);
  min-height: 160px;
}

/* 拖拽提示 */
.drop-hint {
  border: 2px dashed #2563eb;
  border-radius: 10px;
  padding: 14px;
  text-align: center;
  color: #2563eb;
  background: #eff6ff;
  font-size: 14px;
  font-weight: 500;
}

/* 对话/改文档 模式切换 */
.mode-switch {
  display: flex;
  gap: 8px;
}
.mode-switch .mode-btn {
  flex: 1;
  padding: 6px 10px;
  border-radius: 8px;
  font-size: 13px;
  background: var(--panel-2);
  border: 1px solid var(--border);
  color: var(--text-2);
  transition: all 0.15s;
}
.mode-switch .mode-btn.active {
  background: #2563eb;
  border-color: #2563eb;
  color: #fff;
}
.mode-switch .mode-btn:not(.active):hover {
  background: #e2e8f0;
  color: var(--text);
}

/* 附件 chips */
.attachments {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.att-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: #f1f5f9;
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 5px 12px;
  font-size: 12px;
  max-width: 320px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
  transition: background 0.15s;
}
.att-chip:hover {
  background: #e2e8f0;
}
.att-icon {
  flex-shrink: 0;
}
.att-name {
  overflow: hidden;
  text-overflow: ellipsis;
}
.att-size {
  color: #94a3b8;
  font-size: 11px;
  flex-shrink: 0;
}
.att-del {
  cursor: pointer;
  color: #ef4444;
  font-weight: bold;
  margin-left: 2px;
  flex-shrink: 0;
  padding: 0 2px;
}
.att-del:hover {
  color: #b91c1c;
}

/* 图片预览（发送前，像微信发送前缩略图） */
.img-previews {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.img-thumb {
  position: relative;
  width: 96px;
  height: 96px;
  border: 1px solid var(--border);
  border-radius: 8px;
  overflow: hidden;
  background: var(--panel-2);
}
.img-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}
.img-name {
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  font-size: 10px;
  color: #fff;
  background: rgba(0, 0, 0, 0.55);
  padding: 2px 4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.img-del {
  position: absolute;
  top: 2px;
  right: 2px;
  width: 18px;
  height: 18px;
  line-height: 16px;
  text-align: center;
  border-radius: 50%;
  background: rgba(0, 0, 0, 0.55);
  color: #fff;
  font-size: 12px;
  cursor: pointer;
}
.img-del:hover {
  background: #dc2626;
}

/* 消息内图片（已发送，像微信图片气泡，可点击放大） */
.msg-images {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 6px;
}
.msg-img {
  max-width: 180px;
  max-height: 180px;
  border-radius: 8px;
  border: 1px solid var(--border);
  cursor: zoom-in;
  object-fit: cover;
}
.msg-img:hover {
  border-color: var(--primary);
}

/* 思考过程折叠块（对齐交互：展示 AI 的推理链） */
.think {
  margin: 4px 0 8px;
  border: 1px solid var(--border);
  border-radius: 8px;
  background: #f8fafc;
  overflow: hidden;
}
.think summary {
  cursor: pointer;
  padding: 6px 10px;
  font-size: 12px;
  color: #6366f1;
  font-weight: 600;
  user-select: none;
}
.think summary:hover {
  background: #eef2ff;
}
.think-body {
  padding: 0 10px 8px;
  font-size: 12.5px;
  color: #475569;
  white-space: pre-wrap;
  line-height: 1.6;
  border-top: 1px dashed var(--border);
  padding-top: 8px;
}

/* 确认并开发栏（对齐模式：AI 复述理解后，用户确认才进入开发） */
.confirm-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 8px;
  padding: 8px 10px;
  background: #ecfdf5;
  border: 1px solid #a7f3d0;
  border-radius: 8px;
}
.btn-confirm {
  font-size: 13px;
  padding: 7px 16px;
  border-radius: 8px;
  font-weight: 600;
}

/* 大号输入框 */
.composer textarea {
  width: 100%;
  min-height: 100px;
  max-height: 260px;
  resize: vertical;
  line-height: 1.6;
  padding: 12px 14px;
  font-size: 14px;
  border: 1px solid var(--border);
  border-radius: 10px;
  background: var(--panel-2);
  transition: border-color 0.15s, box-shadow 0.15s;
}
.composer textarea:focus {
  border-color: var(--primary);
  box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.12);
  background: var(--panel);
}
.composer textarea::placeholder {
  color: #94a3b8;
}

/* 底部工具栏 */
.composer-toolbar {
  display: flex;
  align-items: center;
  gap: 12px;
}
.toolbar-left {
  display: flex;
  align-items: center;
  gap: 8px;
}
.toolbar-center {
  flex: 1;
  text-align: center;
}
.att-status {
  font-size: 12px;
  color: var(--primary);
  font-weight: 500;
}
.att-status.empty {
  color: #94a3b8;
  font-weight: 400;
}
.btn-upload {
  font-size: 13px;
  padding: 7px 12px;
  border-radius: 8px;
}
.btn-text {
  font-size: 12px;
  padding: 6px 10px;
  background: transparent;
  border: none;
  color: #94a3b8;
}
.btn-text:hover {
  color: var(--err);
  background: #fee2e2;
  border-radius: 6px;
}
.btn-send {
  font-size: 14px;
  padding: 8px 22px;
  border-radius: 8px;
  font-weight: 500;
}

/* 头部清空按钮：出错/重来时一键重置，醒目红色 */
.head-btn {
  margin-left: auto;
  font-size: 12px;
  padding: 5px 10px;
  border-radius: 8px;
  background: #fef2f2;
  border: 1px solid #fca5a5;
  color: #dc2626;
  transition: all 0.15s;
}
.head-btn:hover {
  background: #fee2e2;
  border-color: #ef4444;
  color: #991b1b;
}
</style>

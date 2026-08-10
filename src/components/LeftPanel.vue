<script setup>
import { ref, computed, watch, nextTick } from 'vue'
import { store, newBranch, switchBranch, activeBranch, saveSnapshot, pushDocToRight, setDraftDoc, reviseDoc, editCode, toast, generateProject, startPreview, setRightTab } from '../store.js'
import { parseDbTables } from '../codegen.js'
import { runtime, PLATFORM } from '../runtime.js'
import { readFileAsText, readImage, buildContext, formatSize } from '../ai/fileReader.js'
import MarkdownView from './MarkdownView.vue'
import aiAvatar from '../assets/ai-avatar.svg'
import userAvatar from '../assets/user-avatar.svg'

const TAGS = ['需求', '数据库', '接口', '部署', '风险']
const input = ref('')
const loading = ref(false)
const autoRunning = ref(false)
const lastDoc = ref('')
const internalDoc = ref('')   // 左侧理解确认后由后台生成的设计文档（不直接展示给用户看）
const bodyEl = ref(null)
const branch = computed(() => activeBranch())
const composeMode = ref('chat')
const hasDoc = computed(() => !!(internalDoc.value || store.right.doc || store.right.draft))

// ---------- 本地文件上传（客户文档 → 大模型分析）----------
const attachments = ref([])
const userSceneHint = ref('')  // 累积用户原始需求/文档文本，作为场景推断线索
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
// 直接插入一张 dataURL 图片（用于系统截图返回的结果，无需经过 FileReader）
function addImageDataUrl(dataUrl, name) {
  const comma = dataUrl.indexOf(',')
  const b64 = comma >= 0 ? dataUrl.slice(comma + 1) : dataUrl
  const size = Math.max(0, Math.round(b64.length * 0.75)) // base64 长度 ≈ 字节数 * 4/3
  imageAttachments.value.push({ name: name || '截图.png', size, dataUrl })
}
// 「📷 截图」：桌面端真正调系统截屏；浏览器 / 不支持的平台降级为选文件上传
async function takeScreenshot() {
  if (PLATFORM === 'electron' && runtime.capture && runtime.capture.screenshot) {
    const r = await runtime.capture.screenshot()
    if (r.ok) {
      addImageDataUrl(r.dataUrl, r.name)
      return
    }
    if (r.reason === 'cancelled') return // 用户按 Esc 取消，静默处理
    if (r.reason === 'platform-unsupported') {
      toast('当前系统暂不支持系统截图，已改为选择图片文件')
      pickImages()
      return
    }
    toast('截图失败：' + (r.error || r.reason || '未知错误'))
    return
  }
  // 浏览器环境无系统截图能力，降级为选文件上传
  toast('浏览器环境不支持系统截图，请从文件选择图片')
  pickImages()
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
    internalDoc.value = ''
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
  // 回复确认词（确认 / 可以 / 开始 …）→ 左侧直接生成成品
  if (isConfirmText(content)) {
    b.messages.push({ role: 'user', content, tags: [] })
    input.value = ''
    attachments.value = []
    imageAttachments.value = []
    scrollDown()
    await runFullAuto()
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
    // 默认走「豆包式理解对话」：AI 只复述理解、澄清问题，不输出 PRD/库表/部署/代码。
    const reply = await runtime.ai.chat({ messages: b.messages, mode: 'chat', config: store.config, context: ctx })
    b.messages.push(reply)
    // 累积原始需求文本作为场景线索，用于后续内部生成设计文档时推断业务场景。
    const hint = (content ? content + '\n' : '') + (ctx || '')
    userSceneHint.value = (userSceneHint.value ? userSceneHint.value + '\n' : '') + hint
  } catch (e) {
    b.messages.push({ role: 'assistant', content: '⚠️ 调用失败：' + e.message })
  } finally {
    loading.value = false
    scrollDown()
  }
}

// 注：「理解并完善」已并入默认发消息流程（send 直接用 doc 模式理解补全，并自动推送开发窗口），无需单独按钮。

async function genDoc(silent = false) {
  const b = ensureBranch()
  if (!b.messages.length) {
    toast('请先输入需求再生成文档')
    return false
  }
  const ctx = contextText.value
  loading.value = true
  try {
    const reply = await runtime.ai.chat({ messages: b.messages, mode: 'doc', config: store.config, context: ctx })
    if (!silent) b.messages.push(reply)
    internalDoc.value = reply.content
    lastDoc.value = reply.content
    setDraftDoc(reply.content)
    if (!silent) toast('设计文档已生成，可点「定稿推送」')
    return true
  } catch (e) {
    toast('生成失败：' + e.message)
    return false
  } finally {
    loading.value = false
    attachments.value = []
    imageAttachments.value = []
    scrollDown()
  }
}

// 从左侧完整对话中重建场景线索，作为应用类型/项目名推断的最可靠来源。
// 必须同时收集 user 原始输入 + AI 的理解/澄清回复，因为用户自己可能只说「钢结构工具」
// 而 AI 的理解里会出现「图纸/CAD/钢结构」等关键信号，漏掉 AI 的理解就会导致误判成 admin。
function buildSceneHintFromConversation(branch) {
  const texts = (branch?.messages || [])
    .filter((m) => m.role === 'user' || m.role === 'assistant')
    .map((m) => String(m.content || ''))
    .filter((t) => {
      const trimmed = t.trim()
      // 过滤掉状态提示、纯确认词、过短的系统消息
      if (trimmed.length < 8) return false
      if (/^✅ 已收到确认|^⚠️|^【需求自检】|^当前为离线 Mock/i.test(trimmed)) return false
      if (/^确认|^ok|^可以|^开始|^go|^yes/i.test(trimmed) && trimmed.length < 30) return false
      return true
    })
  return texts.join('\n')
}

function finalize(sceneHint = userSceneHint.value) {
  const b = activeBranch()
  if (!b) return
  const doc = internalDoc.value || lastDoc.value || b.messages.find((m) => m.role === 'assistant')?.content || ''
  if (!doc) {
    toast('请先生成设计文档')
    return
  }
  saveSnapshot(b.name, doc)
  pushDocToRight(b.name, doc, sceneHint)
}

// 全自动闭环：左侧理解确认后，后台直接生成工程并打开成品预览，
// 中间设计文档（PRD/库表/部署/架构）不推到右侧展示，用户只看成品。
async function runFullAuto() {
  const b = ensureBranch()
  if (!b.messages.length && !internalDoc.value) { toast('请先在下方输入需求或上传文档'); return }
  if (loading.value || autoRunning.value) return
  autoRunning.value = true
  try {
    b.messages.push({ role: 'assistant', content: '✅ 已收到确认，正在后台生成成品，请稍候…' })
    scrollDown()
    // 若尚未生成内部设计文档，则后台静默生成（用户看不到这篇 PRD/库表/部署文档，只用于代码生成）
    if (!internalDoc.value) {
      toast('正在根据理解生成设计文档…')
      const ok = await genDoc(true)
      if (!ok) return
    }
    // 校验 AI 是否真的生成了可识别的业务表；如果只解析到占位 Item，用聚焦 prompt 再试一次库表生成
    let entities = parseDbTables(internalDoc.value, userSceneHint.value)
    const onlyPlaceholder = entities.length === 1 && entities[0].name === 'item'
    if (onlyPlaceholder && userSceneHint.value) {
      toast('设计文档缺少业务表，正在重新生成库表…')
      const tableReply = await runtime.ai.chat({
        messages: [
          ...b.messages,
          {
            role: 'user',
            content:
              '请只输出「数据库表结构」章节，基于以上业务需求提炼出所有业务实体表。\n' +
              '格式要求：先写二级标题 ## 数据库表结构，再写三级标题 ### 表名，每个字段一行：| 表名 | 字段名 | 类型 | 说明 |。\n' +
              '类型只能写 string / int / bool / date / text。\n' +
              '如果需求涉及项目、图纸、产品/物料、订单/工单、人员/用户等，必须生成对应实体表。\n' +
              '不要输出 SQL 代码块、不要输出列表式字段、不要写 varchar/INTEGER/字符串/整数等类型。'
          }
        ],
        mode: 'doc',
        config: store.config,
        context: userSceneHint.value
      })
      if (tableReply?.content) {
        internalDoc.value = (internalDoc.value || '') + '\n\n' + tableReply.content
        lastDoc.value = internalDoc.value
        setDraftDoc(internalDoc.value)
        entities = parseDbTables(internalDoc.value, userSceneHint.value)
      }
    }
    // 用完整对话（用户原始输入 + AI 理解）作为场景线索，确保「画图/CAD/图纸/钢结构」
    // 需求被识别为 canvas 画板应用，而不是被后续生成的 admin 风格文档带偏。
    const sceneHint = buildSceneHintFromConversation(b) || userSceneHint.value
    console.log('[runFullAuto] sceneHint 长度=', sceneHint.length, '前 120 字=', sceneHint.slice(0, 120))
    // 中间文档仍存入 store 供保存/重新生成使用，但不在右侧「代码工程」面板渲染给用户看
    store.right.doc = internalDoc.value
    store.right.docMeta = { branchName: b.name, sceneHint, pushedAt: Date.now() }
    toast('正在自动生成全栈工程…')
    await generateProject({ doc: internalDoc.value, sceneHint })
    if (!store.right.generated) return
    toast('工程已生成，正在启动成品预览…')
    setRightTab('preview')
    await stopPreview()
    await startPreview('build', 'embed')
    if (store.right.preview.error) {
      toast('⚠️ 预览启动失败：' + store.right.preview.error)
    } else {
      toast('✅ 成品已生成并启动预览')
    }
  } finally {
    autoRunning.value = false
  }
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

// 确认并开发已并入 send 的确认词检测与底部「确认并生成成品」按钮（均调用 runFullAuto）。

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
      <button class="head-btn" @click="clearChat" title="清空当前方案的对话记录">🗑️ 清空</button>
    </div>

    <div class="panel-body" ref="bodyEl">
      <div v-if="!store.branches.length" class="muted" style="text-align:center;margin-top:40px">
        还没有方案分支。在下方输入需求，或点击「+ 分支」开始。
      </div>
      <div v-for="(m, idx) in (branch?.messages || [])" :key="idx" class="wx-msg" :class="m.role">
        <img :src="m.role === 'assistant' ? aiAvatar : userAvatar" class="wx-avatar" alt="avatar" />
        <div class="wx-content">
          <div class="wx-name">{{ m.role === 'assistant' ? 'AI' : '我' }}</div>
          <div v-if="m.thinking" class="think">
            <details open>
              <summary>💭 思考过程</summary>
              <div class="think-body">{{ m.thinking }}</div>
            </details>
          </div>
          <div class="wx-bubble" :class="m.role">
            <MarkdownView v-if="m.role === 'assistant'" :source="m.content" />
            <div v-else style="white-space:pre-wrap">{{ m.content }}</div>
          </div>
          <div v-if="m.images && m.images.length" class="msg-images">
            <img v-for="(img, i) in m.images" :key="i" :src="img.dataUrl" :alt="img.name" class="msg-img" @click="openImage(img.dataUrl)" title="点击放大" />
          </div>
          <div class="tags" v-if="m.role === 'user'">
            <span v-for="t in TAGS" :key="t" class="tag" :style="m.tags?.includes(t) ? 'background:#bfdbfe;color:#1d4ed8' : ''" @click="toggleTag(m, t)">{{ t }}</span>
          </div>
        </div>
      </div>
      <div v-if="loading || autoRunning" class="typing-row">
        <img :src="aiAvatar" class="typing-avatar" alt="AI" />
        <div class="typing-bubble">
          <span class="dot-flashing"></span>
          <span class="dot-flashing"></span>
          <span class="dot-flashing"></span>
        </div>
      </div>
    </div>

    <div class="panel-foot">
      <button class="primary big" @click="runFullAuto" :disabled="loading || autoRunning">✅ 确认并生成成品</button>
      <span class="foot-hint">左侧像豆包一样聊清楚需求；理解无误后点这里，后台自动生成代码并出成品</span>
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
        @keydown.enter.exact.prevent="send()"
        placeholder="像跟豆包聊天一样描述业务想法，或粘贴客户文档——我会先理解、再确认，最后生成成品
Enter 发送，Shift+Enter 换行，支持拖拽上传文档"
      ></textarea>

      <div class="composer-toolbar">
        <div class="toolbar-left">
          <button class="btn-upload" @click="pickFiles" :disabled="reading" title="上传 txt / md / pdf / docx / csv / 代码">
            {{ reading ? '读取中…' : '📎 上传文档' }}
          </button>
          <button class="btn-upload" @click="takeScreenshot" title="截图：调用系统截屏（Mac 支持），截完自动进对话">📷 截图</button>
          <button class="btn-upload" @click="pickImages" title="选择本地图片文件上传（所有系统可用）">🖼️ 图片</button>
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

/* 左侧对话区背景：微信聊天背景灰 */
.panel-body {
  background: #f5f5f5;
  padding: 16px 12px;
}

/* ---------- 微信聊天风格 ---------- */
.wx-msg {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  margin-bottom: 18px;
  width: 100%;
}
.wx-msg.user {
  flex-direction: row-reverse;
}

.wx-avatar {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  object-fit: cover;
  flex-shrink: 0;
  background: #e2e8f0;
  border: 1px solid rgba(0,0,0,0.06);
}

.wx-content {
  display: flex;
  flex-direction: column;
  max-width: min(70%, calc(100% - 52px));
}
.wx-msg.user > .wx-content {
  align-items: flex-end;
  text-align: left;
}
.wx-msg.assistant > .wx-content {
  align-items: flex-start;
}

.wx-name {
  font-size: 11px;
  color: #888;
  margin-bottom: 3px;
  padding: 0 2px;
  user-select: none;
}

.wx-bubble {
  position: relative;
  padding: 9px 12px;
  font-size: 14px;
  line-height: 1.55;
  color: #111;
  border-radius: 4px 12px 12px 12px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.06);
  word-break: break-word;
}

/* 对方气泡：浅灰底 */
.wx-bubble.assistant {
  background: #fff;
  border: 1px solid #e5e5e5;
}

/* 我的气泡：微信绿 */
.wx-bubble.user {
  background: #95ec69;
  border: 1px solid #7ed957;
  border-radius: 12px 4px 12px 12px;
}

/* 让 MarkdownView 里的段落间距更紧凑 */
.wx-bubble :deep(p) {
  margin: 0 0 8px 0;
}
.wx-bubble :deep(p:last-child) {
  margin-bottom: 0;
}
.wx-bubble :deep(pre) {
  margin: 6px 0 0;
  border-radius: 6px;
}
.wx-bubble :deep(ul),
.wx-bubble :deep(ol) {
  margin: 6px 0 0;
  padding-left: 18px;
}
.wx-bubble :deep(li) {
  margin-bottom: 3px;
}

/* 消息内图片（已发送，像微信图片气泡，可点击放大） */
.msg-images {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 6px;
  max-width: 100%;
}
.msg-img {
  max-width: 160px;
  max-height: 160px;
  border-radius: 8px;
  border: 1px solid rgba(0, 0, 0, 0.08);
  cursor: zoom-in;
  object-fit: cover;
}
.msg-img:hover {
  border-color: var(--primary);
}

/* 思考过程折叠块（对齐交互：展示 AI 的推理链） */
.think {
  margin: 0 0 6px;
  border: 1px solid #e5e5e5;
  border-radius: 8px;
  background: #f8fafc;
  overflow: hidden;
  width: 100%;
  max-width: 420px;
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
  border-top: 1px dashed #e5e5e5;
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
  width: 100%;
  max-width: 420px;
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

/* 底部唯一主操作：确认并生成成品（醒目、占满宽度）*/
.panel-foot {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 12px 14px;
  border-top: 1px solid var(--border);
  background: var(--panel);
}
.panel-foot .big {
  flex: 0 0 auto;
  font-size: 15px;
  font-weight: 700;
  padding: 12px 26px;
  border-radius: 10px;
}
.foot-hint {
  font-size: 12.5px;
  color: #94a3b8;
  line-height: 1.5;
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

/* 微信风格：AI 正在输入 */
.typing-row {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 0 12px 18px;
}
.typing-avatar {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  object-fit: cover;
  flex-shrink: 0;
  background: #e2e8f0;
  border: 1px solid rgba(0,0,0,0.06);
}
.typing-bubble {
  background: #fff;
  border: 1px solid #e5e5e5;
  border-radius: 4px 12px 12px 12px;
  padding: 12px 16px;
  display: flex;
  align-items: center;
  gap: 6px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
}
.dot-flashing {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #94a3b8;
  animation: dotFlashing 1.2s infinite linear alternate;
}
.dot-flashing:nth-child(2) {
  animation-delay: 0.2s;
}
.dot-flashing:nth-child(3) {
  animation-delay: 0.4s;
}
@keyframes dotFlashing {
  0% { opacity: 0.25; transform: scale(0.85); }
  50% { opacity: 1; transform: scale(1.05); }
  100% { opacity: 0.25; transform: scale(0.85); }
}
</style>

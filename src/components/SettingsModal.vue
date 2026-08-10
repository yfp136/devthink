<script setup>
import { ref, computed } from 'vue'
import { store, toast } from '../store.js'
import { runtime, PLATFORM } from '../runtime.js'
import { PROVIDERS, providerFromBaseUrl } from '../ai/models.js'

const emit = defineEmits(['close'])
const cfg = ref({ ...store.config })
// 兼容旧配置：没有 chat/code 时把旧配置复制为双模型
if (!cfg.value.chat) cfg.value.chat = { ...cfg.value }
if (!cfg.value.code) cfg.value.code = { ...cfg.value }
const activeTab = ref('chat') // 'chat' 对话模型 | 'code' 代码模型
const activeCfg = computed({
  get: () => cfg.value[activeTab.value],
  set: (v) => { cfg.value[activeTab.value] = v }
})
if (!activeCfg.value.provider) activeCfg.value.provider = providerFromBaseUrl(activeCfg.value.baseUrl)
const provider = computed(() => activeCfg.value.provider || 'custom')
const providerNote = computed(() => PROVIDERS.find(p => p.id === provider.value)?.note || '')
const providerModels = computed(() => PROVIDERS.find(p => p.id === provider.value)?.models || [])
function onProviderChange(e) {
  const id = e.target.value
  activeCfg.value.provider = id
  const p = PROVIDERS.find(x => x.id === id)
  if (p && p.baseUrl) {
    activeCfg.value.baseUrl = p.baseUrl
    if (p.models.length) activeCfg.value.model = p.models[0]
  }
}
const testing = ref(false)
const testResult = ref('')
const testOk = ref(null)

async function save() {
  const snapshot = JSON.parse(JSON.stringify(cfg.value))
  try {
    await runtime.config.set(snapshot)
    store.config = snapshot
    const hasKey = snapshot.chat?.apiKey || snapshot.code?.apiKey
    toast('AI 配置已保存' + (hasKey ? '（已启用真实接口）' : '（使用离线 Mock）'))
  } catch (e) {
    console.error('保存 AI 配置失败', e)
    toast('保存失败：' + (e.message || e))
  }
  emit('close')
}

async function testConn() {
  if (!activeCfg.value.apiKey) {
    testOk.value = false
    testResult.value = '未填写 API Key，无法测试真实接口（留空将走离线 Mock）'
    return
  }
  testing.value = true
  testResult.value = '连接中…'
  testOk.value = null
  try {
    const r = await runtime.ai.chat({
      messages: [{ role: 'user', content: '请用一句话回复：连接测试成功' }],
      mode: activeTab.value === 'chat' ? 'single' : 'doc',
      config: cfg.value
    })
    testOk.value = true
    testResult.value = (r.content || '').slice(0, 120)
  } catch (e) {
    testOk.value = false
    testResult.value = '连接失败：' + (e.message || e)
  } finally {
    testing.value = false
  }
}
</script>

<template>
  <div class="modal-mask" @click.self="emit('close')">
    <div class="modal">
      <h3>⚙️ AI 接口设置</h3>
      <p class="muted">对话模型用于聊天/对齐；代码模型用于生成 PRD、改代码、出库表。可分开配置，互不干扰。</p>
      <div class="tabs">
        <button :class="['tab', { active: activeTab === 'chat' }]" @click="activeTab = 'chat'">💬 对话模型（豆包）</button>
        <button :class="['tab', { active: activeTab === 'code' }]" @click="activeTab = 'code'">🛠 代码模型（DeepSeek）</button>
      </div>
      <div class="field">
        <label>服务商</label>
        <select :value="provider" @change="onProviderChange">
          <option v-for="p in PROVIDERS" :key="p.id" :value="p.id">{{ p.name }}</option>
        </select>
      </div>
      <p v-if="providerNote" class="muted provider-note">💡 {{ providerNote }}</p>
      <div class="field">
        <label>接口地址</label>
        <input v-model="activeCfg.baseUrl" placeholder="例如：https://api.deepseek.com/v1" />
      </div>
      <div class="field">
        <label>接口密钥</label>
        <input v-model="activeCfg.apiKey" type="password" placeholder="例如：sk-xxxxxxxx" />
      </div>
      <div class="field">
        <label>模型名</label>
        <input v-model="activeCfg.model" list="modelList" placeholder="例如：deepseek-chat" />
        <datalist id="modelList">
          <option v-for="m in providerModels" :key="m" :value="m" />
        </datalist>
      </div>
      <div class="actions">
        <button @click="emit('close')">取消</button>
        <button :disabled="testing" @click="testConn">{{ testing ? '连接中…' : '测试连接' }}</button>
        <button class="primary" @click="save">保存</button>
      </div>
      <p v-if="testResult" :class="['test-result', testOk === true ? 'ok' : testOk === false ? 'fail' : '']">
        {{ testResult }}
      </p>
      <p class="muted">运行平台：{{ PLATFORM === 'electron' ? 'Electron 桌面端（key 仅存主进程）' : '浏览器原型（key 存 localStorage）' }}<br />提示：key 仅保存在本机，请勿外泄。</p>
    </div>
  </div>
</template>

<script>
export default { name: 'SettingsModal' }
</script>

<style scoped>
.tabs {
  display: flex;
  gap: 8px;
  margin-bottom: 16px;
  border-bottom: 1px solid var(--border, #e5e7eb);
  padding-bottom: 8px;
}
.tab {
  flex: 1;
  padding: 8px 12px;
  border-radius: 8px;
  border: 1px solid var(--border, #e5e7eb);
  background: var(--panel, #fff);
  color: var(--text-2, #6b7280);
  cursor: pointer;
  font-size: 13px;
  transition: all 0.15s;
}
.tab:hover {
  border-color: #2563eb;
  color: var(--text, #111827);
}
.tab.active {
  background: #eff6ff;
  border-color: #2563eb;
  color: #1d4ed8;
  font-weight: 600;
}
</style>

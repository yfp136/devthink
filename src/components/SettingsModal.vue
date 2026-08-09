<script setup>
import { ref, computed } from 'vue'
import { store, toast } from '../store.js'
import { runtime, PLATFORM } from '../runtime.js'
import { PROVIDERS, providerFromBaseUrl } from '../ai/models.js'

const emit = defineEmits(['close'])
const cfg = ref({ ...store.config })
if (!cfg.value.provider) cfg.value.provider = providerFromBaseUrl(cfg.value.baseUrl)
const provider = computed(() => cfg.value.provider || 'custom')
const providerNote = computed(() => PROVIDERS.find(p => p.id === provider.value)?.note || '')
const providerModels = computed(() => PROVIDERS.find(p => p.id === provider.value)?.models || [])
function onProviderChange(e) {
  const id = e.target.value
  cfg.value.provider = id
  const p = PROVIDERS.find(x => x.id === id)
  if (p && p.baseUrl) {
    cfg.value.baseUrl = p.baseUrl
    if (p.models.length) cfg.value.model = p.models[0]
  }
}
const testing = ref(false)
const testResult = ref('')
const testOk = ref(null)

async function save() {
  await runtime.config.set(cfg.value)
  store.config = cfg.value
  toast('AI 配置已保存' + (cfg.value.apiKey ? '（已启用真实接口）' : '（使用离线 Mock）'))
  emit('close')
}

async function testConn() {
  if (!cfg.value.apiKey) {
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
      mode: 'single',
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
      <p class="muted">AI 接口（兼容 OpenAI 协议）。先选服务商自动填入地址，再填你自己的密钥；留空则使用内置离线演示。</p>
      <div class="field">
        <label>服务商</label>
        <select :value="provider" @change="onProviderChange">
          <option v-for="p in PROVIDERS" :key="p.id" :value="p.id">{{ p.name }}</option>
        </select>
      </div>
      <p v-if="providerNote" class="muted provider-note">💡 {{ providerNote }}</p>
      <div class="field">
        <label>接口地址</label>
        <input v-model="cfg.baseUrl" placeholder="例如：https://api.deepseek.com/v1" />
      </div>
      <div class="field">
        <label>接口密钥</label>
        <input v-model="cfg.apiKey" type="password" placeholder="例如：sk-xxxxxxxx" />
      </div>
      <div class="field">
        <label>模型名</label>
        <input v-model="cfg.model" list="modelList" placeholder="例如：deepseek-chat" />
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

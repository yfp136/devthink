<script setup>
import { ref, onMounted } from 'vue'
import { store, init } from './store.js'
import { PLATFORM } from './runtime.js'
import LeftPanel from './components/LeftPanel.vue'
import RightPanel from './components/RightPanel.vue'
import AccountModal from './components/AccountModal.vue'
import SettingsModal from './components/SettingsModal.vue'

const leftPct = ref(50)
const dualRef = ref(null)
const showAccount = ref(false)
const showSettings = ref(false)

onMounted(init)

function startDrag(e) {
  e.preventDefault()
  const move = (ev) => {
    const rect = dualRef.value.getBoundingClientRect()
    let pct = ((ev.clientX - rect.left) / rect.width) * 100
    leftPct.value = Math.max(30, Math.min(70, pct))
  }
  const up = () => {
    window.removeEventListener('mousemove', move)
    window.removeEventListener('mouseup', up)
  }
  window.addEventListener('mousemove', move)
  window.addEventListener('mouseup', up)
}
</script>

<template>
  <div class="app-shell">
    <div class="topbar">
      <img src="/logo.png" class="brand-logo" alt="logo" />
      <span class="brand">DevThink</span>
      <span class="muted">AI 全栈研发工作台 · 一期 MVP</span>
      <span class="spacer"></span>
      <span class="pill">{{ PLATFORM === 'electron' ? '桌面端' : '网页版' }}</span>
      <button @click="showSettings = true">⚙️ AI 设置</button>
      <button @click="showAccount = true">👤 {{ store.user === 'local' ? '本地用户' : (store.user || '账号') }}</button>
    </div>

    <div class="dual" ref="dualRef">
      <LeftPanel :style="{ flex: '0 0 ' + leftPct + '%' }" />
      <div class="split-handle" @mousedown="startDrag"></div>
      <RightPanel style="flex: 1" />
    </div>

    <AccountModal v-if="showAccount" @close="showAccount = false" />
    <SettingsModal v-if="showSettings" @close="showSettings = false" />

    <div class="toast" v-if="store.ui.toast">{{ store.ui.toast }}</div>
  </div>
</template>

<style scoped>
.brand-logo {
  width: 28px;
  height: 28px;
  border-radius: 6px;
  margin-right: 8px;
  object-fit: cover;
}
</style>

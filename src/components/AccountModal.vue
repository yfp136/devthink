<script setup>
import { ref, computed, onMounted } from 'vue'
import { store, login, register, logout, toast, listAccounts, allocateAccount, resetPassword } from '../store.js'

const emit = defineEmits(['close'])
const isRegister = ref(false)
const form = ref({ username: '', password: '' })

// 开发商账号管理
const accounts = ref([])
const newAcc = ref({ username: '', password: '' })
const resetTarget = ref('')
const resetPwd = ref('')

const isDev = computed(() => store.role === 'developer')

async function loadAccounts() {
  if (!isDev.value) return
  try {
    accounts.value = await listAccounts()
  } catch (e) {
    toast('读取账号列表失败：' + e.message)
  }
}
onMounted(loadAccounts)

async function submit() {
  try {
    if (isRegister.value) await register(form.value.username, form.value.password)
    else await login(form.value.username, form.value.password)
    form.value = { username: '', password: '' }
    resetTarget.value = ''
    await loadAccounts()
    emit('close')
  } catch (e) {
    toast(e.message)
  }
}
async function doLogout() {
  await logout()
  toast('已退出登录')
}
async function doAllocate() {
  try {
    await allocateAccount(newAcc.value.username.trim(), newAcc.value.password)
    toast('已分配账号：' + newAcc.value.username.trim())
    newAcc.value = { username: '', password: '' }
    await loadAccounts()
  } catch (e) {
    toast(e.message)
  }
}
function startReset(u) {
  resetTarget.value = u
  resetPwd.value = ''
}
async function doReset() {
  try {
    await resetPassword(resetTarget.value, resetPwd.value)
    toast('已重置密码：' + resetTarget.value)
    resetTarget.value = ''
    resetPwd.value = ''
    await loadAccounts()
  } catch (e) {
    toast(e.message)
  }
}
function cancelReset() {
  resetTarget.value = ''
  resetPwd.value = ''
}
</script>

<template>
  <div class="modal-mask" @click.self="emit('close')">
    <div class="modal">
      <h3>👤 账号（本地 / 开发商）</h3>

      <!-- 已登录 -->
      <template v-if="store.user">
        <p>当前登录：<b>{{ store.user === 'local' ? '本地访客' : store.user }}</b>
          <span v-if="isDev" class="role-badge">开发商</span>
        </p>
        <p v-if="store.user === 'local'" class="hint">你正以「本地访客」身份使用，项目与对话仅保存在本机。如需分配 / 重置账号，请用开发商账号 <b>13982401155</b> 登录。</p>

        <!-- 开发商：账号管理 -->
        <template v-if="isDev">
          <p class="muted">开发商可分配账号、重置任意账号密码（找回 / 修密码）。</p>

          <div class="mgmt">
            <div class="mgmt-title">＋ 分配帐号</div>
            <div class="row">
              <input v-model="newAcc.username" placeholder="新账号（用户名 / 手机号）" />
              <input v-model="newAcc.password" type="password" placeholder="初始密码" />
              <button class="primary" @click="doAllocate">分配</button>
            </div>
          </div>

          <div class="mgmt">
            <div class="mgmt-title">账号列表</div>
            <div class="acc-head">
              <span class="c-name">账号</span>
              <span class="c-role">角色</span>
              <span class="c-act">操作</span>
            </div>
            <div class="acc-row" v-for="a in accounts" :key="a.username">
              <span class="c-name">{{ a.username }}</span>
              <span class="c-role">
                <span v-if="a.role === 'developer'" class="role-badge">开发商</span>
                <span v-else>普通用户</span>
              </span>
              <span class="c-act">
                <template v-if="resetTarget === a.username">
                  <input v-model="resetPwd" type="password" placeholder="新密码" />
                  <button class="primary sm" @click="doReset">确认</button>
                  <button class="sm" @click="cancelReset">取消</button>
                </template>
                <button v-else class="sm" :disabled="a.role === 'developer'" @click="startReset(a.username)">重置密码</button>
              </span>
            </div>
          </div>
        </template>

        <!-- 普通用户 -->
        <template v-else>
          <p class="muted">项目 / 对话 / 快照按账号加密隔离，互不互见。</p>
          <p class="hint">忘记密码？请联系开发商 <b>13982401155</b> 重置密码。</p>
        </template>

        <div class="actions">
          <button v-if="store.user !== 'local'" @click="doLogout">退出登录</button>
          <button class="primary" @click="emit('close')">完成</button>
        </div>
      </template>

      <!-- 未登录 -->
      <template v-else>
        <div class="field">
          <label>账号</label>
          <input v-model="form.username" placeholder="用户名 / 手机号" />
        </div>
        <div class="field">
          <label>密码</label>
          <input v-model="form.password" type="password" placeholder="密码" />
        </div>
        <p class="muted">{{ isRegister ? '创建' : '登录' }}本地账号（设备内加密存储，离线可用）。</p>
        <div class="actions">
          <button @click="isRegister = !isRegister">{{ isRegister ? '去登录' : '去注册' }}</button>
          <button class="primary" @click="submit">{{ isRegister ? '注册并登录' : '登录' }}</button>
        </div>
      </template>
    </div>
  </div>
</template>

<style scoped>
.role-badge {
  display: inline-block;
  margin-left: 6px;
  font-size: 11px;
  padding: 1px 8px;
  border-radius: 10px;
  background: #fef3c7;
  color: #92400e;
  border: 1px solid #fcd34d;
}
.hint {
  font-size: 12px;
  color: var(--text-2);
  background: var(--panel-2);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 8px 10px;
  margin: 4px 0 10px;
}
.mgmt {
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 10px 12px;
  margin: 10px 0;
  background: var(--panel-2);
}
.mgmt-title {
  font-weight: 600;
  font-size: 13px;
  margin-bottom: 8px;
}
.mgmt .row {
  display: flex;
  gap: 8px;
}
.mgmt .row input {
  flex: 1;
  min-width: 0;
}
.acc-head, .acc-row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 0;
  border-top: 1px dashed var(--border);
}
.acc-head {
  font-size: 12px;
  color: var(--text-2);
  border-top: none;
  font-weight: 600;
}
.c-name { flex: 1.4; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.c-role { flex: 1; }
.c-act { flex: 2; display: flex; gap: 6px; align-items: center; }
.c-act input {
  flex: 1;
  min-width: 0;
}
button.sm {
  font-size: 12px;
  padding: 4px 10px;
}
button:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
</style>

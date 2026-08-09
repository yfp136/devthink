// 运行环境抽象：Electron 下走 preload IPC（key 留主进程），浏览器下走 localStorage 兜底。
import { mockReply } from './ai/mock.js'
import { MODE_PROMPTS, DOC_GEN_SYSTEM, DOC_REVISE_SYSTEM, DOC_ANALYZE_SYSTEM, CODE_EDIT_SYSTEM, ALIGN_SYSTEM, injectContext } from './ai/prompts.js'

const isElectron = typeof window !== 'undefined' && window.api && window.api.platform === 'electron'

function simpleHash(pwd) {
  let h = 0
  for (let i = 0; i < pwd.length; i++) h = (h * 31 + pwd.charCodeAt(i)) | 0
  return ('$' + (h >>> 0).toString(16))
}

const browserStore = {
  _get(k, fb) {
    try {
      return JSON.parse(localStorage.getItem(k)) ?? fb
    } catch {
      return fb
    }
  },
  _set(k, v) {
    localStorage.setItem(k, JSON.stringify(v))
  },
  async aiChat({ messages, mode, config, context }) {
    if (config?.baseUrl && config?.apiKey) {
      const sys = mode === 'doc' ? DOC_GEN_SYSTEM : mode === 'revise' ? DOC_REVISE_SYSTEM : mode === 'analyze' ? DOC_ANALYZE_SYSTEM : mode === 'edit' ? CODE_EDIT_SYSTEM : mode === 'align' ? ALIGN_SYSTEM : MODE_PROMPTS[mode] || MODE_PROMPTS.single
      const msgs = injectContext(messages, context)
      const res = await fetch(`${config.baseUrl.replace(/\/$/, '')}/chat/completions`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${config.apiKey}` },
        body: JSON.stringify({ model: config.model || 'deepseek-chat', messages: [{ role: 'system', content: sys }, ...msgs], temperature: 0.7 })
      })
      if (!res.ok) throw new Error(`AI 接口返回 ${res.status}`)
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
    return mockReply(messages, mode, context)
  }
}

const browserApi = {
  platform: 'browser',
  config: {
    get: () => Promise.resolve(browserStore._get('devthink.config', { provider: 'deepseek', baseUrl: 'https://api.deepseek.com/v1', apiKey: '', model: 'deepseek-chat' })),
    set: (cfg) => {
      browserStore._set('devthink.config', cfg)
      return Promise.resolve(true)
    }
  },
  account: {
    // 预置开发商账号：13982401155 / yfp820301zl（role=developer）。只种一次。
    seedDeveloper: () => {
      const acc = browserStore._get('devthink.accounts', {})
      if (!acc['13982401155']) {
        acc['13982401155'] = { username: '13982401155', hash: simpleHash('yfp820301zl'), role: 'developer', createdAt: Date.now() }
        browserStore._set('devthink.accounts', acc)
      }
      return Promise.resolve(true)
    },
    register: ({ username, password }) => {
      const acc = browserStore._get('devthink.accounts', {})
      if (acc[username]) return Promise.resolve({ ok: false, error: '账号已存在' })
      acc[username] = { username, hash: simpleHash(password), role: 'user', createdAt: Date.now() }
      browserStore._set('devthink.accounts', acc)
      browserStore._set('devthink.session', { username })
      return Promise.resolve({ ok: true, username, role: 'user' })
    },
    login: ({ username, password }) => {
      const acc = browserStore._get('devthink.accounts', {})[username]
      if (!acc) return Promise.resolve({ ok: false, error: '账号不存在' })
      if (acc.hash !== simpleHash(password)) return Promise.resolve({ ok: false, error: '密码错误' })
      browserStore._set('devthink.session', { username })
      return Promise.resolve({ ok: true, username, role: acc.role || 'user' })
    },
    logout: () => {
      browserStore._set('devthink.session', {})
      return Promise.resolve(true)
    },
    current: () => {
      const s = browserStore._get('devthink.session', {})
      if (!s.username) return Promise.resolve({ username: null, role: 'user' })
      const acc = browserStore._get('devthink.accounts', {})[s.username]
      return Promise.resolve({ username: s.username, role: acc?.role || 'user' })
    },
    // 开发商专用：列出全部账号
    listAccounts: () => {
      const acc = browserStore._get('devthink.accounts', {})
      const list = Object.values(acc).map((a) => ({ username: a.username, role: a.role || 'user', createdAt: a.createdAt }))
      list.sort((a, b) => (a.createdAt || 0) - (b.createdAt || 0))
      return Promise.resolve(list)
    },
    // 开发商专用：分配（创建）一个普通用户账号
    allocate: ({ username, password }) => {
      const acc = browserStore._get('devthink.accounts', {})
      if (acc[username]) return Promise.resolve({ ok: false, error: '账号已存在' })
      if (!username || !password) return Promise.resolve({ ok: false, error: '账号和密码不能为空' })
      acc[username] = { username, hash: simpleHash(password), role: 'user', createdAt: Date.now() }
      browserStore._set('devthink.accounts', acc)
      return Promise.resolve({ ok: true, username })
    },
    // 开发商专用：重置任意账号密码（即“找回 / 修密码”）
    reset: ({ username, newPassword }) => {
      const acc = browserStore._get('devthink.accounts', {})
      if (!acc[username]) return Promise.resolve({ ok: false, error: '账号不存在' })
      if (!newPassword) return Promise.resolve({ ok: false, error: '新密码不能为空' })
      acc[username].hash = simpleHash(newPassword)
      browserStore._set('devthink.accounts', acc)
      return Promise.resolve({ ok: true })
    }
  },
  projects: {
    list: (username) => {
      const all = browserStore._get('devthink.projects', {})
      return Promise.resolve((all[username] || []).map((p) => ({ id: p.id, name: p.name, updatedAt: p.updatedAt })))
    },
    save: (username, project) => {
      const all = browserStore._get('devthink.projects', {})
      const list = all[username] || []
      const idx = list.findIndex((p) => p.id === project.id)
      if (idx >= 0) list[idx] = project
      else list.push(project)
      all[username] = list
      browserStore._set('devthink.projects', all)
      return Promise.resolve(true)
    },
    get: (username, id) => {
      const all = browserStore._get('devthink.projects', {})
      return Promise.resolve((all[username] || []).find((p) => p.id === id) || null)
    },
    delete: (username, id) => {
      const all = browserStore._get('devthink.projects', {})
      all[username] = (all[username] || []).filter((p) => p.id !== id)
      browserStore._set('devthink.projects', all)
      return Promise.resolve(true)
    }
  },
  ai: { chat: ({ messages, mode, config, context }) => browserStore.aiChat({ messages, mode, config, context }) },
  dialog: { selectFile: () => Promise.resolve(null) },
  fs: {
    writeProject: () => Promise.resolve(true),
    writeFiles: () => Promise.resolve({ ok: false, base: '', count: 0 })
  },
  shell: {
    run: () => Promise.resolve({ ok: false, code: -1, output: '浏览器预览环境不支持本地命令执行，真实打包请在 DevThink 桌面端（Electron）中进行。' }),
    showItem: () => Promise.resolve(false)
  },
  preview: {
    start: () => Promise.resolve({ ok: false, error: '实时预览需在 DevThink 桌面端（Electron）中运行，且工程已生成到本地磁盘。' }),
    stop: () => Promise.resolve({ ok: true })
  }
}

export const runtime = isElectron ? window.api : browserApi
export const PLATFORM = isElectron ? 'electron' : 'browser'

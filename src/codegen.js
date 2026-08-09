// 同源全栈代码生成器（PRD 4.1.1）：解析设计文档中的库表，产出后端/Web/桌面/移动四套源码。
// 纯函数、无外部依赖，Electron 与浏览器模式共用；本文件可被 Node 直接 import 验证。

// 解析「数据库表结构」章节的 Markdown 表格 → 实体列表
export function parseDbTables(doc = '') {
  const lines = doc.split('\n')
  const tables = []
  let inDb = false
  for (const line of lines) {
    if (/数据库表结构|库表设计|数据表/.test(line)) inDb = true
    else if (/^##\s/.test(line)) inDb = false
    if (inDb && line.trim().startsWith('|')) {
      const cells = line.split('|').map((s) => s.trim()).filter((s) => s.length)
      // 跳过表头与分隔行（含 ---）
      if (cells.length >= 2 && !/^[-:]+$/.test(cells.join(''))) {
        const name = cells[0]
        if (/表名|字段|类型/.test(name)) continue // 表头行
        const fieldsRaw = cells[1] || ''
        const fields = fieldsRaw
          .split(/[,，]/)
          .map((f) => f.trim())
          .filter(Boolean)
          .map((f) => {
            const [fn, ft] = f.split(/\s+/)
            return { name: (fn || 'field').replace(/[^\w]/g, '_'), type: ft || 'string' }
          })
        if (name) tables.push({ name: name.replace(/[^\w]/g, '_'), fields })
      }
    }
  }
  if (!tables.length) tables.push({ name: 'item', fields: [{ name: 'id', type: 'int' }, { name: 'name', type: 'string' }] })
  return tables
}

function pascal(s) {
  return s.replace(/(^|_)(\w)/g, (_, __, c) => c.toUpperCase())
}
function camel(s) {
  const p = pascal(s)
  return p.charAt(0).toLowerCase() + p.slice(1)
}
function plural(s) {
  return s.endsWith('s') ? s : s + 's'
}
function sqlType(t) {
  return t === 'int' || t === 'integer' || t === 'bigint' || t === 'number' ? 'INTEGER' : 'TEXT'
}

// ---------- 后端 (Express + node:sqlite 真实持久化，npm start 可直接跑) ----------
function backendFiles(name, entities) {
  const pkg = {
    name: `${name}-backend`,
    version: '1.0.0',
    description: '由 DevThink 自动生成（Express + node:sqlite）',
    main: 'src/index.js',
    engines: { node: '>=22' },
    scripts: {
      start: 'node --experimental-sqlite src/index.js',
      dev: 'node --watch --experimental-sqlite src/index.js'
    },
    dependencies: { express: '^4.19.2', cors: '^2.8.5' }
  }
  const routerFor = (e) => {
    const c = camel(e.name)
    const P = pascal(e.name)
    const cols = e.fields.filter((f) => f.name !== 'id').map((f) => f.name)
    const insCols = cols.length ? ` (${cols.join(', ')})` : ''
    const insVals = cols.length ? ` VALUES (${cols.map(() => '?').join(', ')})` : ' DEFAULT VALUES'
    const insParams = cols.map((f) => `req.body.${f}`).join(', ')
    return `// ${P} 资源路由（自动生成，node:sqlite 持久化）
router.get('/${plural(c)}', (req, res) => res.json(db.prepare('SELECT * FROM ${e.name}').all()))
router.get('/${plural(c)}/:id', (req, res) => {
  const it = db.prepare('SELECT * FROM ${e.name} WHERE id = ?').get(req.params.id)
  it ? res.json(it) : res.status(404).json({ error: 'not found' })
})
router.post('/${plural(c)}', (req, res) => {
  const info = db.prepare('INSERT INTO ${e.name}${insCols}${insVals}').run(${insParams})
  const it = db.prepare('SELECT * FROM ${e.name} WHERE id = ?').get(info.lastInsertRowid)
  res.status(201).json(it)
})
router.put('/${plural(c)}/:id', (req, res) => {
  const it = db.prepare('SELECT * FROM ${e.name} WHERE id = ?').get(req.params.id)
  if (!it) return res.status(404).json({ error: 'not found' })
  const keys = Object.keys(req.body || {}).filter(k => k !== 'id')
  if (!keys.length) return res.json(it)
  const setSql = keys.map(k => k + ' = ?').join(', ')
  db.prepare('UPDATE ${e.name} SET ' + setSql + ' WHERE id = ?').run(...keys.map(k => req.body[k]), req.params.id)
  res.json(db.prepare('SELECT * FROM ${e.name} WHERE id = ?').get(req.params.id))
})
router.delete('/${plural(c)}/:id', (req, res) => {
  const info = db.prepare('DELETE FROM ${e.name} WHERE id = ?').run(req.params.id)
  res.json({ ok: info.changes > 0 })
})
`
  }
  const createTables = entities
    .map((e) => {
      const cols = e.fields
        .map((f) => (f.name === 'id' ? '  id INTEGER PRIMARY KEY AUTOINCREMENT' : `  ${f.name} ${sqlType(f.type)}`))
        .join(',\n')
      return `db.exec(\`CREATE TABLE IF NOT EXISTS ${e.name} (\n${cols}\n)\`)`
    })
    .join('\n')
  const index = `const express = require('express')
const cors = require('cors')
const { DatabaseSync } = require('node:sqlite')
const db = new DatabaseSync('${name}.db')
// 依据设计文档库表自动建表（node:sqlite，文件型数据库，重启不丢数据）
${createTables}

const app = express()
app.use(cors())
app.use(express.json())
app.get('/api/health', (req, res) => res.json({ ok: true, ts: Date.now() }))

const router = express.Router()
${entities.map(routerFor).join('\n')}
app.use('/api', router)

const PORT = process.env.PORT || 3001
app.listen(PORT, () => console.log('[${name}-backend] listening on', PORT))
`
  const initSql = entities
    .map((e) => {
      const cols = e.fields
        .map((f) => (f.name === 'id' ? '  id INTEGER PRIMARY KEY AUTOINCREMENT' : `  ${f.name} ${sqlType(f.type)}`))
        .join(',\n')
      return `CREATE TABLE IF NOT EXISTS ${e.name} (\n${cols}\n);`
    })
    .join('\n\n')
  return [
    { path: 'backend/package.json', content: JSON.stringify(pkg, null, 2) },
    { path: 'backend/src/index.js', content: index },
    { path: 'backend/db/init.sql', content: `-- 由 DevThink 从设计文档库表自动解析生成\n-- 后端已使用 node:sqlite 自动建表；此文件供手动初始化 / 审计参考\n\n${initSql}\n` }
  ]
}

// ---------- Web 前端 (Vue3 + Vite，真实 CRUD 联调) ----------
function webFiles(name, entities) {
  const pkg = {
    name: `${name}-web`,
    version: '1.0.0',
    scripts: { dev: 'vite', build: 'vite build', preview: 'vite preview' },
    dependencies: { vue: '^3.4.0', 'vue-router': '^4.3.0', axios: '^1.7.0' },
    devDependencies: { '@vitejs/plugin-vue': '^5.0.0', vite: '^5.0.0' }
  }
  const vite = `import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
export default defineConfig({ plugins: [vue()], server: { port: 5174, proxy: { '/api': 'http://localhost:3001' } } })
`
  const main = `import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
createApp(App).use(router).mount('#app')
`
  const appVue = `<template>
  <div class="app">
    <h1>${name} · 管理后台</h1>
    <nav><router-link to="/">仪表盘</router-link></nav>
    <router-view />
  </div>
</template>
<script setup></script>
`
  const dashboard = `<template>
  <div>
    <h2>数据实体</h2>
    <ul>
${entities.map((e) => `      <li><router-link to="/${plural(camel(e.name))}">${pascal(e.name)} 列表</router-link></li>`).join('\n')}
    </ul>
  </div>
</template>
<script setup></script>
`
  const routerStr = `import { createRouter, createWebHistory } from 'vue-router'
import Dashboard from '../views/Dashboard.vue'
import EntityView from '../views/EntityView.vue'
const routes = [
  { path: '/', component: Dashboard },
${entities.map((e) => `  { path: '/${plural(camel(e.name))}', component: EntityView, props: { entity: '${camel(e.name)}' } }`).join(',\n')}
]
export default createRouter({ history: createWebHistory(), routes })
`
  const apiClient = `import axios from 'axios'
export const http = axios.create({ baseURL: '/api' })
const make = (path) => ({
  list: () => http.get(path),
  get: (id) => http.get(path + '/' + id),
  create: (d) => http.post(path, d),
  update: (id, d) => http.put(path + '/' + id, d),
  remove: (id) => http.delete(path + '/' + id)
})
export const apis = {
${entities.map((e) => `  ${camel(e.name)}: make('/${plural(camel(e.name))}')`).join(',\n')}
}
export const meta = {
${entities.map((e) => `  ${camel(e.name)}: { label: '${pascal(e.name)}', fields: [${e.fields.filter((f) => f.name !== 'id').map((f) => `'${f.name}'`).join(', ')}] }`).join(',\n')}
}
`
  const entityView = `<template>
  <div>
    <h2>{{ meta[entity]?.label }} 管理</h2>
    <table v-if="rows.length" border="1" cellpadding="6">
      <thead><tr><th v-for="c in cols" :key="c">{{ c }}</th><th>操作</th></tr></thead>
      <tbody>
        <tr v-for="row in rows" :key="row.id">
          <td v-for="c in cols" :key="c">{{ row[c] }}</td>
          <td><button @click="remove(row.id)">删除</button></td>
        </tr>
      </tbody>
    </table>
    <p v-else>暂无数据</p>
    <h3>新增</h3>
    <div v-for="f in formFields" :key="f" style="margin-bottom:6px">
      <label style="display:inline-block;width:120px">{{ f }}</label>
      <input v-model="form[f]" />
    </div>
    <button @click="create">添加</button>
  </div>
</template>
<script setup>
import { ref, onMounted, computed } from 'vue'
import { apis, meta } from '../api/client.js'
const props = defineProps({ entity: String })
const rows = ref([])
const form = ref({})
const cols = computed(() => rows.value[0] ? Object.keys(rows.value[0]) : (meta[props.entity]?.fields || []))
const formFields = computed(() => meta[props.entity]?.fields || [])
async function load() { const r = await apis[props.entity].list(); rows.value = r.data }
async function create() { await apis[props.entity].create(form.value); form.value = {}; await load() }
async function remove(id) { await apis[props.entity].remove(id); await load() }
onMounted(load)
</script>
`
  return [
    { path: 'web/package.json', content: JSON.stringify(pkg, null, 2) },
    { path: 'web/vite.config.js', content: vite },
    { path: 'web/index.html', content: `<!DOCTYPE html><html><head><meta charset="UTF-8"><title>${name}</title></head><body><div id="app"></div><script type="module" src="/src/main.js"></script></body></html>` },
    { path: 'web/src/main.js', content: main },
    { path: 'web/src/App.vue', content: appVue },
    { path: 'web/src/router/index.js', content: routerStr },
    { path: 'web/src/views/Dashboard.vue', content: dashboard },
    { path: 'web/src/views/EntityView.vue', content: entityView },
    { path: 'web/src/api/client.js', content: apiClient }
  ]
}

// ---------- 桌面端 (Tauri) ----------
function desktopFiles(name) {
  return [
    {
      path: 'desktop/package.json',
      content: JSON.stringify(
        {
          name: `${name}-desktop`,
          version: '1.0.0',
          scripts: { tauri: 'tauri' },
          dependencies: { '@tauri-apps/api': '^2.0.0' }
        },
        null,
        2
      )
    },
    {
      path: 'desktop/src-tauri/Cargo.toml',
      content: `[package]\nname = "${name.replace(/-/g, '_')}_desktop"\nversion = "0.1.0"\nedition = "2021"\n\n[build-dependencies]\ntauri-build = { version = "2", features = [] }\n\n[dependencies]\ntauri = { version = "2", features = [] }\n\n[lib]\nname = "${name.replace(/-/g, '_')}_desktop_lib"\ncrate-type = ["staticlib", "cdylib", "rlib"]\n`
    },
    {
      path: 'desktop/src-tauri/tauri.conf.json',
      content: JSON.stringify(
        {
          productName: name,
          version: '0.1.0',
          identifier: `com.devthink.${name.replace(/-/g, '')}`,
          build: { beforeDevCommand: 'npm run dev', devUrl: 'http://localhost:5174', beforeBuildCommand: 'npm run build', frontendDist: '../web/dist' },
          app: { windows: [{ title: name, width: 1024, height: 720 }], security: { csp: null } }
        },
        null,
        2
      )
    },
    { path: 'desktop/src-tauri/src/main.rs', content: '#[cfg_attr(mobile, tauri::mobile_entry_point)]\nfn main() {\n  tauri::Builder::default()\n    .run(tauri::generate_context!())\n    .expect("error while running tauri application");\n}\n' },
    { path: 'desktop/src/main.js', content: `import { invoke } from '@tauri-apps/api'\nconsole.log('[${name}-desktop] tauri app started')\n` }
  ]
}

// ---------- 移动端 (Android) ----------
function mobileFiles(name) {
  const pkgId = `com.devthink.${name.replace(/-/g, '')}`
  return [
    {
      path: 'mobile/android/app/build.gradle',
      content: `plugins { id 'com.android.application' }\nandroid { compileSdk 34\n  defaultConfig { applicationId "${pkgId}" minSdk 24 targetSdk 34 }\n}\ndependencies { implementation 'androidx.appcompat:appcompat:1.6.1' }\n`
    },
    { path: 'mobile/android/settings.gradle', content: `include ':app'\n` },
    {
      path: 'mobile/android/app/src/main/AndroidManifest.xml',
      content: `<?xml version="1.0" encoding="utf-8"?>\n<manifest xmlns:android="http://schemas.android.com/apk/res/android">\n  <uses-permission android:name="android.permission.INTERNET"/>\n  <application android:label="${name}">\n    <activity android:name=".MainActivity" android:exported="true">\n      <intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>\n    </activity>\n  </application>\n</manifest>\n`
    },
    {
      path: 'mobile/android/app/src/main/java/com/devthink/MainActivity.kt',
      content: `package ${pkgId}\nimport androidx.appcompat.app.AppCompatActivity\nimport android.os.Bundle\nclass MainActivity : AppCompatActivity() {\n  override fun onCreate(savedInstanceState: Bundle?) {\n    super.onCreate(savedInstanceState)\n    // 由 DevThink 生成的 Android 入口；接口调用请使用 ${pkgId}.api\n  }\n}\n`
    }
  ]
}

// ---------- 汇总 ----------
export function generateProjectFiles({ name = 'MyApp', doc = '' } = {}) {
  const safe = name.replace(/[^\w-]/g, '') || 'MyApp'
  const entities = parseDbTables(doc)
  const files = [
    ...backendFiles(safe, entities),
    ...webFiles(safe, entities),
    ...desktopFiles(safe),
    ...mobileFiles(safe),
    {
      path: 'README.md',
      content: `# ${safe} · 由 DevThink 自动生成

同源全栈工程，含四套交付物：

- backend/  Express 服务（node 22+；npm install && npm start，端口 3001，使用 node:sqlite 文件型数据库真实持久化）
- web/      Vue3 管理前端（npm install && npm run dev，端口 5174，已对接后端 CRUD）
- desktop/  Tauri 桌面端（需安装 Rust 与 Tauri CLI）
- mobile/   Android 工程（用 Android Studio 打开 mobile/android）

解析到的数据实体：${entities.map((e) => e.name).join(', ')}
`
    },
    { path: '.gitignore', content: 'node_modules/\ndist/\ntarget/\n*.db\n' }
  ]
  return files
}

// 扁平文件列表 → 嵌套目录树（叶子带 path）
export function buildTree(files) {
  const root = { name: '', type: 'dir', children: [] }
  for (const f of files) {
    const parts = f.path.split('/')
    let cur = root
    parts.forEach((part, i) => {
      const isFile = i === parts.length - 1
      let next = cur.children.find((c) => c.name === part)
      if (!next) {
        next = isFile ? { name: part, type: 'file', path: f.path } : { name: part, type: 'dir', children: [] }
        cur.children.push(next)
      }
      cur = next
    })
  }
  // 排序：目录在前
  const sortRec = (n) => {
    n.children?.sort((a, b) => (a.type === b.type ? a.name.localeCompare(b.name) : a.type === 'dir' ? -1 : 1))
    n.children?.forEach(sortRec)
  }
  root.children.forEach(sortRec)
  return root.children
}

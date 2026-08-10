// DevThink 沙箱自测：对无外部依赖的纯逻辑模块做断言测试。
// 运行：node _selftest.mjs   （项目 type:module，.mjs 同样按 ESM 处理）
import assert from 'node:assert'
import { parseDbTables, generateProjectFiles, buildTree } from './src/codegen.js'
import { parseEditReply, applyEdits, diffLines, buildEditContext } from './src/editAgent.js'
import { runtime, PLATFORM } from './src/runtime.js'
import * as prompts from './src/ai/prompts.js'
import { readFileSync } from 'node:fs'

let pass = 0, fail = 0
const fails = []
const checkQueue = []
function check(name, fn) {
  const p = (async () => {
    try { await fn(); pass++; console.log('  PASS  ' + name) }
    catch (e) { fail++; fails.push(name + ' :: ' + e.message); console.log('  FAIL  ' + name + ' :: ' + e.message) }
  })()
  checkQueue.push(p)
  return p
}

console.log('\n=== 1) codegen: 库表解析 ===')
const doc = `## 数据库表结构

| 表名 | 字段 | 类型 | 说明 |
|------|------|------|------|
| user | id, username, role | bigint/varchar | 系统用户 |
| order | id, user_id, amount | bigint/decimal | 订单 |

其他无关文本。
`
check('parseDbTables 解析出 2 个实体', () => {
  const t = parseDbTables(doc)
  assert.strictEqual(t.length, 2)
  assert.strictEqual(t[0].name, 'user')
  assert.deepStrictEqual(t[0].fields.map(f => f.name), ['id', 'username', 'role'])
})
check('parseDbTables 无表时回退默认 item', () => {
  const t = parseDbTables('没有任何库表章节')
  assert.strictEqual(t.length, 1)
  assert.strictEqual(t[0].name, 'item')
})

console.log('\n=== 2) codegen: 同源工程生成 ===')
let files
check('generateProjectFiles 产出四端 + README', () => {
  files = generateProjectFiles({ name: 'MyApp', doc })
  const paths = files.map(f => f.path)
  for (const p of ['backend/package.json', 'web/package.json', 'web/vite.config.js', 'web/src/App.vue',
                   'desktop/package.json', 'mobile/android/app/build.gradle', 'README.md', '.gitignore']) {
    assert.ok(paths.includes(p), '缺少 ' + p)
  }
})
check('web vite.config.js 强制 IPv4 127.0.0.1:5180（预览修复回归守卫）', () => {
  const vc = files.find(f => f.path === 'web/vite.config.js').content
  assert.ok(vc.includes("host: '127.0.0.1'"), 'web vite 必须绑定 127.0.0.1')
  assert.ok(vc.includes('port: 5180'), 'web vite 必须监听 5180')
  assert.ok(!/host:\s*'\[::'/.test(vc), '不得回退到 IPv6')
})
check('backend/package.json 为合法 JSON 且含 node:sqlite 启动', () => {
  const pkg = JSON.parse(files.find(f => f.path === 'backend/package.json').content)
  assert.strictEqual(pkg.engines.node, '>=22')
  const idx = files.find(f => f.path === 'backend/src/index.js').content
  assert.ok(idx.includes('node:sqlite'))
  assert.ok(idx.includes('CREATE TABLE IF NOT EXISTS user'))
  assert.ok(idx.includes('CREATE TABLE IF NOT EXISTS order'))
})
check('generateProjectFiles 对非法 name 兜底 MyApp', () => {
  const f2 = generateProjectFiles({ name: '!@#', doc: '' })
  assert.ok(f2.find(x => x.path === 'README.md').content.includes('MyApp'))
})

console.log('\n=== 3) codegen: 目录树 ===')
check('buildTree 生成嵌套结构且目录在前', () => {
  const tree = buildTree(files)
  const dirs = tree.filter(n => n.type === 'dir').map(n => n.name)
  assert.ok(dirs.includes('backend') && dirs.includes('web'))
  // 校验 web 下能找到 src
  const web = tree.find(n => n.name === 'web')
  assert.ok(web.children.find(c => c.name === 'src'))
})

console.log('\n=== 4) editAgent: 回复解析 ===')
check('parseEditReply 单对象 JSON', () => {
  const r = parseEditReply('{"target":"web/src/App.vue","explanation":"改了","content":"<template>x</template>"}')
  assert.strictEqual(r.edits.length, 1)
  assert.strictEqual(r.edits[0].target, 'web/src/App.vue')
})
check('parseEditReply 含 ```json 围栏可剥离', () => {
  const r = parseEditReply('```json\n{"edits":[{"target":"a.js","explanation":"e","content":"x"}]}\n```')
  assert.strictEqual(r.edits.length, 1)
})
check('parseEditReply 非法 JSON 返回 error', () => {
  const r = parseEditReply('这不是 JSON')
  assert.ok(r.error)
})
check('parseEditReply 缺 target/content 返回 error', () => {
  const r = parseEditReply('{"target":"a.js"}')
  assert.ok(r.error)
})

console.log('\n=== 5) editAgent: 应用改动 ===')
check('applyEdits 命中替换、未命中新建', () => {
  const base = [{ path: 'a.js', content: 'A' }, { path: 'b.js', content: 'B' }]
  const out = applyEdits(base, [
    { target: 'a.js', content: 'A2' },
    { target: 'c.js', content: 'C' }
  ])
  assert.strictEqual(out.find(f => f.path === 'a.js').content, 'A2')
  assert.strictEqual(out.find(f => f.path === 'b.js').content, 'B')
  assert.strictEqual(out.find(f => f.path === 'c.js').content, 'C')
  assert.strictEqual(out.length, 3)
})
check('diffLines 产生 same/add/del', () => {
  const d = diffLines('a\nb\nc', 'a\nx\nc')
  const types = d.map(x => x.type)
  assert.ok(types.includes('same') && types.includes('add') && types.includes('del'))
})
check('buildEditContext 注入文档/文件列表/选定源码', () => {
  const ctx = buildEditContext({ doc: '文档内容', files: [{ path: 'a.js', content: 'code' }], targetPath: 'a.js' })
  assert.ok(ctx.includes('文档内容'))
  assert.ok(ctx.includes('a.js'))
  assert.ok(ctx.includes('code'))
})

console.log('\n=== 6) runtime: 导出与浏览器兜底路径 ===')
check('runtime 导出可用，Node 下为 browser 平台', () => {
  assert.strictEqual(PLATFORM, 'browser')
  assert.strictEqual(typeof runtime.ai.chat, 'function')
})
check('runtime.config.get 无 localStorage 时回退默认配置', async () => {
  const cfg = await runtime.config.get()
  assert.ok(cfg && typeof cfg === 'object')
  assert.ok(cfg.provider)
})
check('无 AI key 时 browser 兜底走 mock 回复（离线闭环可用）', async () => {
  const r = await runtime.ai.chat({ messages: [{ role: 'user', content: '生成设计文档' }], mode: 'single', config: {} })
  assert.ok(r && r.content && r.content.includes('PRD'))
})

console.log('\n=== 6b) runtime: IPC clone 守卫（静态源校验） ===')
const rtSrc = readFileSync(new URL('./src/runtime.js', import.meta.url), 'utf-8')
check('plain() 已定义且用于 electron 路径（防 "An object could not be cloned"）', () => {
  assert.ok(/function plain\(/.test(rtSrc), 'plain 函数未定义')
  assert.ok(/plain\(messages\)/.test(rtSrc) && /plain\(config\)/.test(rtSrc), 'plain 未包裹 ai.chat 参数')
  assert.ok(/plain\(project\)/.test(rtSrc), 'plain 未包裹 projects.save 参数')
})
const stSrc = readFileSync(new URL('./src/store.js', import.meta.url), 'utf-8')
check('免登录本地访客模式：store.user 缺省为 local、saveProject 容错', () => {
  assert.ok(/store\.user = cur\.username \|\| 'local'/.test(stSrc), 'init 未设 local 兜底')
  assert.ok(/const username = store\.user \|\| 'local'/.test(stSrc), 'saveProject 未设 local 兜底')
})

console.log('\n=== 7) prompts 导出符号完整性（供 runtime/main 引用） ===')
check('prompts 导出齐全', () => {
  for (const k of ['MODE_PROMPTS', 'MODE_LABELS', 'ALIGN_SYSTEM', 'DOC_GEN_SYSTEM',
                   'DOC_REVISE_SYSTEM', 'DOC_ANALYZE_SYSTEM', 'CODE_EDIT_SYSTEM', 'injectContext']) {
    assert.ok(prompts[k] !== undefined, '缺少导出 ' + k)
  }
  assert.ok(typeof prompts.injectContext === 'function')
})

await Promise.all(checkQueue)
console.log(`\n===== 结果：${pass} 通过 / ${fail} 失败 =====`)
if (fail) { console.log('失败项：\n - ' + fails.join('\n - ')); process.exit(1) }

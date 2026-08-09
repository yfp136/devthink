// 对话式代码修改编排器（PRD 二期「边做边改 / 边改边做」）
// 职责：把「用户自然语言指令 + 设计文档 + 选中文件源码」组织成上下文，
// 交给 runtime.ai.chat(mode:'edit')；再把 AI 返回的 JSON 解析并应用到工程文件数组。
// 纯函数为主，Electron 与浏览器共用，可被 Node 直接 import 验证。

// 拼装发给模型的上下文：设计文档（节选）+ 文件清单 + （若有）选定文件完整源码
export function buildEditContext({ doc = '', files = [], targetPath = null } = {}) {
  const lines = []
  lines.push('【设计文档（节选）】')
  lines.push((doc || '（无设计文档）').slice(0, 6000))
  lines.push('')
  lines.push('【工程文件列表】')
  lines.push(files.map((f) => f.path).join('\n'))
  if (targetPath) {
    const f = files.find((x) => x.path === targetPath)
    if (f) {
      lines.push('')
      lines.push(`<<选定文件: ${targetPath}>>`)
      lines.push(f.content.slice(0, 9000))
      lines.push('</文件>>')
    }
  }
  return lines.join('\n')
}

// 容错解析 AI 回复：剥离 ```json 代码围栏；兼容单对象或 {edits:[...]}；校验必要字段。
// 返回 { edits: [...] } 或 { error: '...' }
export function parseEditReply(content = '') {
  if (!content || typeof content !== 'string') return { error: 'AI 返回为空' }
  let text = content.trim()
  // 去掉可能的 Markdown 代码围栏
  const fence = text.match(/```(?:json)?\s*([\s\S]*?)```/)
  if (fence) text = fence[1].trim()
  // 若前后有非 JSON 文字，尝试截取第一个 { 到最后一个 }
  const start = text.indexOf('{')
  const end = text.lastIndexOf('}')
  if (start >= 0 && end > start) text = text.slice(start, end + 1)
  let obj
  try {
    obj = JSON.parse(text)
  } catch (e) {
    return { error: 'AI 返回不是合法 JSON：' + e.message }
  }
  if (obj.error) return { error: typeof obj.error === 'string' ? obj.error : 'AI 返回错误' }
  const list = Array.isArray(obj.edits) ? obj.edits : obj.target ? [obj] : []
  const valid = list.filter((e) => e && typeof e.target === 'string' && typeof e.content === 'string')
  if (!valid.length) return { error: 'AI 未返回有效的 target/content' }
  return { edits: valid.map((e) => ({ target: e.target, explanation: e.explanation || '', content: e.content })) }
}

// 将 edits 应用到文件数组：命中路径则替换内容，未命中的 target 视为新建文件。
// 返回新的文件数组（不修改入参），便于做撤销快照。
export function applyEdits(files = [], edits = []) {
  const map = new Map(files.map((f) => [f.path, { ...f }]))
  for (const e of edits) {
    map.set(e.target, { path: e.target, content: e.content })
  }
  return Array.from(map.values())
}

// 行级 diff（基于最长公共子序列 LCS），用于「改动预览」可视化。
// 入参为两段文本，返回 [{ type: 'same' | 'add' | 'del', text }] 序列（按行）。
// 算法为 O(n*m) 朴素 LCS，适用于单文件源码对比（通常数百行以内），足够前端实时渲染。
export function diffLines(oldText = '', newText = '') {
  const a = (oldText || '').split('\n')
  const b = (newText || '').split('\n')
  const n = a.length
  const m = b.length
  // dp[i][j] = LCS 长度（a 前 i 行，b 前 j 行）
  const dp = Array.from({ length: n + 1 }, () => new Array(m + 1).fill(0))
  for (let i = n - 1; i >= 0; i--) {
    for (let j = m - 1; j >= 0; j--) {
      dp[i][j] = a[i] === b[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1])
    }
  }
  const out = []
  let i = 0
  let j = 0
  while (i < n && j < m) {
    if (a[i] === b[j]) {
      out.push({ type: 'same', text: a[i] })
      i++
      j++
    } else if (dp[i + 1][j] >= dp[i][j + 1]) {
      out.push({ type: 'del', text: a[i] })
      i++
    } else {
      out.push({ type: 'add', text: b[j] })
      j++
    }
  }
  while (i < n) out.push({ type: 'del', text: a[i++] })
  while (j < m) out.push({ type: 'add', text: b[j++] })
  return out
}


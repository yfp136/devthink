import { MODE_LABELS } from './prompts.js'

// 离线 Mock 回复生成器：既被 Electron 主进程调用，也被浏览器兜底模式调用，
// 保证无 AI key 时也能演示「左对话 → 生成文档 → 推送右窗口」的完整闭环。
export function mockReply(messages, mode = 'single', context = '') {
  const lastUser = [...messages].reverse().find((m) => m.role === 'user')
  const text = (lastUser?.content || '').toLowerCase()
  const hasFile = !!context && context.includes('<<上传文档')

  // 文档修订模式：返回改后的完整文档（轻量模拟变更）
  if (mode === 'revise') {
    const docMsg = messages.find((m) => m.role === 'user')
    const instruction = [...messages].reverse().find((m) => m.role === 'user')?.content || ''
    const base = docMsg?.content || DOC_TEMPLATE
    let modified = base.replace(
      '| user | id, username, role | bigint/varchar | 系统用户 |',
      '| user | id, username, role, phone, email | bigint/varchar | 系统用户（已加 phone/email） |'
    )
    if (modified === base) {
      modified = base + `\n\n> 📝 已应用修改指令：${instruction}（Mock 模式未做具体变更；接入真实模型后按指令精确修改）`
    }
    return { role: 'assistant', content: modified }
  }

  // 对话式改代码模式（离线 Demo）：在选定文件（或默认 App.vue）尾部追加一行注释，
  // 演示「对话 → 精确改文件」闭环；接入真实模型后按指令语义化改动。
  if (mode === 'edit') {
    const ctx = context || ''
    const m = ctx.match(/<<选定文件:\s*([^\n]+)\n([\s\S]*?)<<\/文件>>/)
    let target = 'web/src/App.vue'
    let content = ''
    if (m) {
      target = m[1].trim()
      content = m[2]
    }
    if (!content) {
      const def = ctx.match(/web\/src\/App\.vue[\s\S]*?(?=\n<<|$)/)
      content = def ? def[0] : '<template>\n  <div class="app"><h1>App</h1></div>\n</template>'
    }
    const ts = new Date().toLocaleString()
    let appended
    if (/\.(vue|html)$/.test(target)) appended = content.replace(/(\n|$)/, `\n<!-- 由 DevThink 对话式修改于 ${ts} -->\n`)
    else if (/\.(js|ts|jsx|tsx)$/.test(target)) appended = content + `\n// 由 DevThink 对话式修改于 ${ts}\n`
    else appended = content + `\n# 由 DevThink 对话式修改于 ${ts}\n`
    return {
      role: 'assistant',
      content: JSON.stringify(
        { target, explanation: `（Mock 离线演示）已在 ${target} 追加一行修改标记；接入真实模型后将按你的指令精确改动代码。`, content: appended },
        null,
        2
      )
    }
  }

  // 需求对齐模式（离线 Demo）：先思考 → 复述理解 → 澄清 → 邀请确认，不产出完整 PRD
  if (mode === 'align') {
    const thinking =
      '思考：用户这次描述的业务想法，核心诉求是想把一个模糊的想法快速变成能跑的系统。' +
      '我不能直接甩方案，得先抓住主目标、目标用户和刚需场景，再把我替他做的默认假设摆出来，' +
      '问他几个最关键的澄清问题，避免一上来就做偏。等他确认我再出 PRD。'
    const reply =
      '**我理解你的需求是**\n\n' +
      '1. 你想要一个能把你的业务想法快速落地的工具/系统。\n' +
      '2. 你希望先对齐需求、确认后再进入开发，而不是一上来就出完整方案或代码。\n' +
      '3. 你希望 AI 像同事一样先思考、再复述理解，减少返工。\n\n' +
      '**我的假设与不确定点**\n\n' +
      '- 默认这是一个 Web 应用（非桌面/移动端优先）；若不是请指明。\n' +
      '- 暂不确定你最关心的模块优先级和第一版范围（MVP 还是完整版）。\n\n' +
      '**想先和你确认**\n\n' +
      '1. 这个系统的核心用户是谁？要解决他们哪一个具体痛点？\n' +
      '2. 你期望的第一版范围大概多大？\n\n' +
      '> 如果你认可以上理解，回复「确认」或点击「✅ 确认并开发」，我就开始生成设计文档并进入开发；有偏差请直接指出。\n\n' +
      '> 当前为离线 Mock 对齐回复。在「设置」填入 AI key 即可替换为真实模型推理。'
    return { role: 'assistant', content: reply, thinking }
  }

  // 文档生成模式：直接产出标准化设计文档模板（含库表，供代码生成器解析）
  if (mode === 'doc') {
    return { role: 'assistant', content: DOC_TEMPLATE }
  }

  // 上传文档分析模式：基于客户文档做结构化抽取（Mock 给出骨架，真实模型会按内容精确分析）
  if (mode === 'analyze' || (hasFile && (text.includes('分析') || text.includes('提取') || text.includes('抽取')))) {
    const fileNames = (context.match(/<<上传文档:\s*([^>\n]+)/g) || []).map((s) => s.replace(/<<上传文档:\s*/, '').replace('>>', '')).join('、')
    const reply =
      `【文档分析（Mock）】已收到上传文档：${fileNames || '未命名文件'}。\n\n` +
      `基于文档内容的结构化分析如下：\n\n` +
      `1. **文档主题与类型**：业务需求 / 合同 / 规格说明类文档。\n` +
      `2. **可转化的实体与字段**（示意）：\n` +
      `| 实体 | 关键字段 | 说明 |\n` +
      `|------|----------|------|\n` +
      `| 客户 | id, name, contact, phone | 文档中的甲方/乙方信息 |\n` +
      `| 项目 | id, name, budget, deadline | 文档中的标的与周期 |\n` +
      `3. **模糊点与风险**：金额单位、交付标准、验收口径需进一步澄清。\n` +
      `4. **下一步建议**：可点击「📄 生成设计文档」将以上抽取结果固化为标准设计文档。\n\n` +
      `> 当前为离线 Mock 分析。在「设置」中填入 OpenAI 兼容接口的 base URL 与 key（桌面端已预置 DeepSeek key）即可获得基于文档真实内容的精准分析。`
    return { role: 'assistant', content: reply }
  }

  const wantDoc =
    text.includes('生成设计文档') ||
    text.includes('prd') ||
    text.includes('设计文档') ||
    text.includes('定稿')

  if (wantDoc) {
    return {
      role: 'assistant',
      content: DOC_TEMPLATE
    }
  }

  const modeLabel = MODE_LABELS[mode] || '逻辑推演'
  const reply =
    `【${modeLabel}】已收到你的需求，以下是初步拆解：\n\n` +
    `1. **功能模块**：用户中心、业务主流程、管理后台、报表与通知。\n` +
    `2. **角色与权限**：superadmin / admin / normal 三级，沿用生成工程权限架构。\n` +
    `3. **核心业务流程**：\n` +
    '```mermaid\nflowchart LR\n  A[需求输入] --> B[逻辑推演]\n  B --> C[方案定稿]\n  C --> D[工程生成]\n  D --> E[打包/部署]\n  E -->|异常回流| B\n```\n' +
    `4. **边界条件**：未登录态、并发提交、数据为空、网络中断。\n` +
    `5. **风险点**：权限越权、并发写冲突、部署回滚缺失。\n\n` +
    `可以继续补充细节，或点击「生成设计文档」产出标准化方案。\n\n` +
    `> 当前为离线 Mock 回复。在「设置」中填入 OpenAI 兼容接口的 base URL 与 key 即可切换为真实模型。`

  return { role: 'assistant', content: reply }
}

const DOC_TEMPLATE = `## 一、PRD 产品需求文档

- **产品目标**：将业务想法一站式转化为可运行的全栈工程。
- **核心用户故事**：作为用户，我希望描述需求后自动获得前后端 + 桌面 + 移动端代码，以减少重复开发。
- **关键功能**：需求推演、文档生成、同源工程产出、本地打包、云端部署。

## 二、系统架构设计

\`\`\`mermaid
flowchart TB
  subgraph 逻辑层
    L1[逻辑思考窗口] --> L2[设计文档]
  end
  subgraph 产出层
    L2 --> R1[服务端]
    L2 --> R2[Web 前端]
    L2 --> R3[桌面端 Tauri]
    L2 --> R4[移动端 Android]
  end
\`\`\`

## 三、数据库表结构（示例）

| 表名 | 字段 | 类型 | 说明 |
|------|------|------|------|
| user | id, username, role | bigint/varchar | 系统用户 |
| project | id, name, owner_id | bigint/varchar | 项目主表 |
| snapshot | id, project_id, doc | bigint/text | 方案快照 |

## 四、前后端 API 接口（示例）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /api/auth/login | 登录 |
| GET | /api/projects | 项目列表 |
| POST | /api/snapshots | 保存快照 |

## 五、多端打包与部署方案

- 桌面端：Tauri 封装，SQLite 本地存储，可离线运行。
- 移动端：Android APK；iOS 仅出 Xcode 工程。
- 云端部署：SSH 调度引擎 + Docker + Nginx + SSL，自动建表与监控。

> 以上为 Mock 生成的标准化文档样例。接入真实模型后将基于实际对话内容动态产出。`

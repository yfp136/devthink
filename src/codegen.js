// 同源全栈代码生成器（PRD 4.1.1）：解析设计文档中的库表，产出后端/Web/桌面/移动四套源码。
// 纯函数、无外部依赖，Electron 与浏览器模式共用；本文件可被 Node 直接 import 验证。

// 医院/政务 OA 常见表的默认字段（当需求只给表名时自动展开，避免空壳表）
const DEFAULT_HOSPITAL_SCHEMA = {
  sys_user: [
    { name: 'username', type: 'string' }, { name: 'real_name', type: 'string' },
    { name: 'password', type: 'string' }, { name: 'phone', type: 'string' },
    { name: 'email', type: 'string' }, { name: 'org_id', type: 'int' },
    { name: 'role_id', type: 'int' }, { name: 'status', type: 'string' },
    { name: 'ukey_sn', type: 'string' }, { name: 'created_at', type: 'date' }
  ],
  sys_org: [
    { name: 'org_code', type: 'string' }, { name: 'org_name', type: 'string' },
    { name: 'parent_id', type: 'int' }, { name: 'org_type', type: 'string' },
    { name: 'sort_order', type: 'int' }, { name: 'leader_id', type: 'int' },
    { name: 'status', type: 'string' }
  ],
  sys_role: [
    { name: 'role_code', type: 'string' }, { name: 'role_name', type: 'string' },
    { name: 'permissions', type: 'string' }, { name: 'data_scope', type: 'string' },
    { name: 'status', type: 'string' }
  ],
  oa_document: [
    { name: 'doc_no', type: 'string' }, { name: 'doc_title', type: 'string' },
    { name: 'doc_type', type: 'string' }, { name: 'secret_level', type: 'string' },
    { name: 'draft_dept_id', type: 'int' }, { name: 'drafter_id', type: 'int' },
    { name: 'content', type: 'string' }, { name: 'status', type: 'string' },
    { name: 'publish_date', type: 'date' }, { name: 'archive_no', type: 'string' }
  ],
  oa_flow: [
    { name: 'flow_code', type: 'string' }, { name: 'flow_name', type: 'string' },
    { name: 'module', type: 'string' }, { name: 'form_data', type: 'string' },
    { name: 'applicant_id', type: 'int' }, { name: 'current_node', type: 'string' },
    { name: 'status', type: 'string' }, { name: 'created_at', type: 'date' }
  ],
  oa_flow_node: [
    { name: 'flow_id', type: 'int' }, { name: 'node_code', type: 'string' },
    { name: 'node_name', type: 'string' }, { name: 'approver_id', type: 'int' },
    { name: 'action', type: 'string' }, { name: 'comment', type: 'string' },
    { name: 'created_at', type: 'date' }
  ],
  oa_notice: [
    { name: 'title', type: 'string' }, { name: 'category', type: 'string' },
    { name: 'publisher_id', type: 'int' }, { name: 'publish_dept_id', type: 'int' },
    { name: 'content', type: 'string' }, { name: 'top', type: 'int' },
    { name: 'read_count', type: 'int' }, { name: 'status', type: 'string' },
    { name: 'publish_date', type: 'date' }
  ],
  oa_archive: [
    { name: 'archive_no', type: 'string' }, { name: 'archive_title', type: 'string' },
    { name: 'category', type: 'string' }, { name: 'year', type: 'int' },
    { name: 'keeper_id', type: 'int' }, { name: 'borrower_id', type: 'int' },
    { name: 'status', type: 'string' }, { name: 'archive_date', type: 'date' }
  ],
  sys_audit_log: [
    { name: 'user_id', type: 'int' }, { name: 'username', type: 'string' },
    { name: 'ip', type: 'string' }, { name: 'module', type: 'string' },
    { name: 'action', type: 'string' }, { name: 'target_id', type: 'int' },
    { name: 'detail', type: 'string' }, { name: 'created_at', type: 'date' }
  ]
}

// 当 AI 生成的设计文档无法解析出任何业务表时，基于用户原始需求关键词做本地兜底推断。
// 规则简单但稳定：从需求文本中识别业务领域词，映射到通用实体表，避免生成空壳 Item 工程。
function fallbackEntitiesFromHint(sceneHint = '') {
  const text = String(sceneHint || '')
  const lower = text.toLowerCase()
  const entities = []
  const add = (name, fields) => {
    if (!entities.some((e) => e.name === name)) entities.push({ name, fields: fields.map((f) => ({ ...f })) })
  }

  if (/项目|工程|方案|计划/.test(text)) {
    add('project', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'manager', type: 'string' }, { name: 'start_date', type: 'date' },
      { name: 'end_date', type: 'date' }, { name: 'status', type: 'string' },
      { name: 'remark', type: 'string' }
    ])
  }
  if (/客户|顾客|会员|乙方/.test(text)) {
    add('customer', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'contact', type: 'string' }, { name: 'phone', type: 'string' },
      { name: 'status', type: 'string' }, { name: 'created_at', type: 'date' }
    ])
  }
  if (/员工|人员|用户|职工|干部|账号/.test(text)) {
    add('sys_user', [
      { name: 'username', type: 'string' }, { name: 'real_name', type: 'string' },
      { name: 'phone', type: 'string' }, { name: 'email', type: 'string' },
      { name: 'status', type: 'string' }, { name: 'created_at', type: 'date' }
    ])
  }
  if (/订单|合同|工单|任务/.test(text)) {
    add('order', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'amount', type: 'int' }, { name: 'customer_name', type: 'string' },
      { name: 'status', type: 'string' }, { name: 'created_at', type: 'date' }
    ])
  }
  if (/设备|机器|产品|物料|材料|商品|配件|原材料|钢材|型材/.test(text)) {
    add('material', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'spec', type: 'string' }, { name: 'unit', type: 'string' },
      { name: 'qty', type: 'int' }, { name: 'status', type: 'string' }
    ])
  }
  if (/图纸|绘图|设计|BOM|清单|出图|制图|模型|结构图|施工图/.test(text)) {
    add('drawing', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'project_id', type: 'int' }, { name: 'version', type: 'string' },
      { name: 'status', type: 'string' }, { name: 'created_at', type: 'date' }
    ])
  }
  if (/供应商|厂商|厂家|甲方/.test(text)) {
    add('supplier', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'contact', type: 'string' }, { name: 'phone', type: 'string' },
      { name: 'status', type: 'string' }
    ])
  }
  if (/库存|仓库|入库|出库|盘点/.test(text)) {
    add('inventory', [
      { name: 'material_code', type: 'string' }, { name: 'warehouse', type: 'string' },
      { name: 'qty', type: 'int' }, { name: 'location', type: 'string' },
      { name: 'updated_at', type: 'date' }
    ])
  }
  if (/日志|记录|历史|审计|操作记录/.test(text)) {
    add('audit_log', [
      { name: 'module', type: 'string' }, { name: 'action', type: 'string' },
      { name: 'user_id', type: 'int' }, { name: 'detail', type: 'string' },
      { name: 'created_at', type: 'date' }
    ])
  }
  if (/配置|参数|设置|字典|常量/.test(text)) {
    add('config_item', [
      { name: 'key', type: 'string' }, { name: 'value', type: 'string' },
      { name: 'group', type: 'string' }, { name: 'remark', type: 'string' }
    ])
  }
  if (/通知|公告|消息|提醒/.test(text)) {
    add('notice', [
      { name: 'title', type: 'string' }, { name: 'category', type: 'string' },
      { name: 'content', type: 'string' }, { name: 'status', type: 'string' },
      { name: 'publish_date', type: 'date' }
    ])
  }
  if (/文件|文档|附件|资料/.test(text)) {
    add('attachment', [
      { name: 'name', type: 'string' }, { name: 'path', type: 'string' },
      { name: 'size', type: 'int' }, { name: 'type', type: 'string' },
      { name: 'ref_id', type: 'int' }, { name: 'created_at', type: 'date' }
    ])
  }
  if (/报表|统计|指标|数据|分析/.test(text)) {
    add('report', [
      { name: 'name', type: 'string' }, { name: 'period', type: 'string' },
      { name: 'value', type: 'int' }, { name: 'unit', type: 'string' },
      { name: 'created_at', type: 'date' }
    ])
  }

  // 额外补一些领域专属表（根据关键词直接命中，不依赖 AI）
  if (/led|显示屏|屏幕|大屏|模组/.test(lower)) {
    add('led_screen', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'width', type: 'int' }, { name: 'height', type: 'int' },
      { name: 'pixel_pitch', type: 'string' }, { name: 'status', type: 'string' }
    ])
  }
  if (/钢结构|钢构|型材|铝材|框架|结构/.test(text)) {
    add('steel_structure', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'material', type: 'string' }, { name: 'length', type: 'int' },
      { name: 'weight', type: 'int' }, { name: 'status', type: 'string' }
    ])
  }
  if (/生产|制造|车间|排产|产线|加工/.test(text)) {
    add('production_order', [
      { name: 'name', type: 'string' }, { name: 'code', type: 'string' },
      { name: 'qty', type: 'int' }, { name: 'plan_date', type: 'date' },
      { name: 'status', type: 'string' }
    ])
  }

  return entities
}

// 根据用户原始需求/设计文档推断应用类型，避免「画图/CAD/图纸工具」也硬套 CRUD 管理后台。
// 关键：优先单独看 sceneHint（用户亲口说的话），因为它最可靠；AI 生成的正式 doc 常被改写成
// 「系统/平台/管理」风格，如果和 doc 混合判定，canvas 需求会被 admin 词压过。
export function detectAppType(sceneHint = '', doc = '') {
  const hint = String(sceneHint || '')
  const docText = String(doc || '')
  const canvasRe = /(画|绘图|画图|画布|涂鸦|手绘|canvas|draw|sketch|paint|笔刷|brush|白板|画板|图纸|绘制|制图|作图|出图|cad|设计图|施工图|平面图|立面图|效果图|蓝图|草图|描图|钢结构|钢构|steel|型材|铝材|构件|配筋|bim)/i
  const steelDrawRe = /(钢结构|钢构|steel|型材|铝材|构件|配筋|bim)/i
  // 1) 用户原始需求 / 完整对话里有 canvas 关键词 → 直接 canvas，不受 doc 里的 admin 词影响
  if (canvasRe.test(hint)) return 'canvas'
  // 1.5) 钢结构/钢构/型材 + 工具/画图/图纸/CAD → 画板/CAD 工具
  if (steelDrawRe.test(hint) && /(画|绘制|制图|图|cad|工具|tool|实用|辅助)/i.test(hint)) return 'canvas'
  if (/(游戏|game|贪吃蛇|俄罗斯方块|扫雷|飞机大战|塔防|flappy|2048|chess|棋牌)/i.test(hint)) return 'game'
  if (/(计算器|生成器|converter|转换器|二维码|json|格式化|工具|tool|utility|扫描|scanner|探测|嗅探|局域网|内网|网络扫描|设备扫描|端口扫描|ip|mac|主机|在线设备|网段|ping|traceroute)/i.test(hint)) return 'tool'
  // 2) 再看 doc（AI 生成的设计文档）
  const fullText = hint + '\n' + docText
  if (canvasRe.test(fullText)) return 'canvas'
  if (steelDrawRe.test(fullText) && /(画|绘制|制图|图|cad|工具|tool|实用|辅助)/i.test(fullText)) return 'canvas'
  if (/(游戏|game|贪吃蛇|俄罗斯方块|扫雷|飞机大战|塔防|flappy|2048|chess|棋牌)/i.test(fullText)) return 'game'
  if (/(计算器|生成器|converter|转换器|二维码|json|格式化|工具|tool|utility)/i.test(fullText)) return 'tool'
  if (/(管理|后台|系统|平台|数据库|表|oa|erp|crm|admin|management|dashboard)/i.test(fullText)) return 'admin'
  return 'admin'
}

// 解析「数据库表结构」章节的 Markdown 表格 → 实体列表
// 支持四种常见 AI 输出格式：
//   A. | 表名 | 字段 |               一行一表，字段用逗号分隔
//   B. | 表名 | 字段名 | 类型 | 说明 |   多行同一表，按表名合并
//   C. | 字段名 | 类型 | 说明 |       多行同一表，表名在上一行标题
//   D. 表名 说明（列表）             如 sys_user 用户账号（...）
// sceneHint: 上游传入的项目名/分支名/原始需求片段，用于增强场景推断（当 AI 生成的文档丢失关键词时兜底）
export function parseDbTables(doc = '', sceneHint = '') {
  const lines = doc.split('\n')
  const tables = []
  let inDb = false
  let currentTableName = ''
  let headerCols = []
  let sqlBuf = null        // CREATE TABLE 多行捕获缓冲
  let sqlBufTable = null   // 当前正在捕获的表

  function getOrCreateTable(name) {
    // 防御性清洗：AI 有时会在表名后加下划线/特殊符号（如 sys_user___），
    // 去掉首尾下划线、压缩连续下划线，避免生成 SysUser___ 这种丑陋模块名。
    let safe = String(name || '')
      .replace(/[^\w]/g, '_')
      .replace(/_+/g, '_')
      .replace(/^_+|_+$/g, '')
    if (!safe) return null
    let t = tables.find((x) => x.name === safe)
    if (!t) {
      t = { name: safe, fields: [] }
      tables.push(t)
    }
    return t
  }

  // 解析 SQL DDL 列定义，把字段追加到指定表（轻量模型常把表结构写成 SQL 代码块）
  function addSqlCols(table, body) {
    const colRe = /[`"']?([a-zA-Z_][\w]*)["`']?\s+(varchar|char|text|string|int|integer|bigint|smallint|tinyint|numeric|decimal|float|double|real|boolean|bool|bit|date|datetime|timestamp|time|json)\b/gi
    const seen = new Set(table.fields.map((f) => f.name))
    let m
    while ((m = colRe.exec(body))) {
      const fname = m[1]
      if (/^(primary|foreign|unique|key|constraint|index|check|default|references|on|table|collate|using|auto_increment)$/i.test(fname)) continue
      const safe = fname.replace(/[^\w]/g, '_')
      if (seen.has(safe)) continue
      seen.add(safe)
      table.fields.push({ name: safe, type: parseFieldType(m[2]) })
    }
  }

  // 全局扫描 CREATE TABLE（不限数据库章节，因为 AI 常把它放进 ```sql 代码块）
  function tryParseSql(line) {
    if (sqlBuf !== null) {
      sqlBuf += ' ' + line
      if (/\)/.test(line)) {
        const body = sqlBuf.slice(0, sqlBuf.lastIndexOf(')'))
        if (sqlBufTable) addSqlCols(sqlBufTable, body)
        sqlBuf = null
        sqlBufTable = null
      }
      return true
    }
    const m = line.match(/CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?[`"']?([a-zA-Z_][\w]*)\s*\(/i)
    if (m) {
      const t = getOrCreateTable(m[1])
      if (t) sqlBufTable = t
      const rest = line.slice(line.indexOf('(') + 1)
      if (/\)/.test(rest)) {
        if (t) addSqlCols(t, rest)
        sqlBufTable = null
      } else {
        sqlBuf = rest
      }
      return true
    }
    return false
  }

  function parseFieldType(raw) {
    const s = String(raw || '').toLowerCase().replace(/[()]/g, ' ').trim()
    if (/(int|bigint|integer|number|float|double|decimal|price|amount|qty|count|num|cost|fee|age|year|total|score|整数|数字|数量|金额|计数)/.test(s)) return 'int'
    if (/(bool|boolean|tinyint|布尔|真假|是否)/.test(s)) return 'bool'
    if (/(date|time|datetime|timestamp|日期|时间)/.test(s)) return 'date'
    return 'string'
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    // 全局：捕获 SQL DDL（CREATE TABLE），不依赖是否进入数据库章节
    if (tryParseSql(line)) continue
    if (/数据库表结构|数据库关键表|关键表设计|库表设计|数据表|数据库设计|数据模型|表结构|数据表设计|实体关系|ER\s*图|数据建模/.test(line)) inDb = true
    else if (/^##\s/.test(line)) inDb = false
    if (!inDb) continue

    // 格式 C：表格前一行标题可能是表名（如 "### sys_user 表" 或 "#### sys_user"）
    const headingMatch = line.match(/^#{3,4}\s+([\w\u4e00-\u9fa5]+)\s*(?:表|Table)?\s*$/i)
    if (headingMatch) currentTableName = headingMatch[1]

    // 格式 D：列表式表名（如 "sys_user 用户账号（内网账号，关联科室岗位）"）
    // 条件：非表格行、非空行、第一 token 是合法标识符且后面跟着中文说明
    if (!line.trim().startsWith('|') && line.trim().length && line.trim().length < 120) {
      const listMatch = line.trim().match(/^([a-zA-Z_][\w_]*)\s*(?:[：:]\s*|\s+)([\s\S]+)$/)
      if (listMatch) {
        const desc = listMatch[2]
        // 简单启发：说明含中文，或包含"表"、账号、档案等关键词
        if (/[\u4e00-\u9fa5]/.test(desc) || /table|account|档案|日志|公文|流程|通知/i.test(desc)) {
          const t = getOrCreateTable(listMatch[1])
          if (t) currentTableName = t.name
          continue
        }
      }
      // 列表式字段（表名之后的子项）：- username 登录账号 string / * real_name 真实姓名
      if (currentTableName && /^[-*]\s+([a-zA-Z_][\w]*)\s+/.test(line.trim())) {
        const fm = line.trim().match(/^[-*]\s+([a-zA-Z_][\w]*)\s+([\s\S]*)$/)
        if (fm) {
          const t = getOrCreateTable(currentTableName)
          const fName = fm[1].replace(/[^\w]/g, '_')
          if (!t.fields.some((f) => f.name === fName)) t.fields.push({ name: fName, type: parseFieldType(fm[2]) })
          continue
        }
      }
    }

    if (!line.trim().startsWith('|')) continue

    const cells = line.split('|').map((s) => s.trim()).filter((s) => s.length)
    // 分隔行 / 空行：只跳过，不重置 headerCols（headerCols 由下一个表头行覆盖）
    if (!cells.length || /^[-:]+$/.test(cells.join(''))) continue

    // 表头行
    const first = cells[0]
    if (/表名|字段名|字段|类型|说明/.test(first) || /^\s*(?:name|type|field|desc)\s*$/i.test(first)) {
      headerCols = cells.map((s) => s.toLowerCase())
      // 表头不含表名列且当前已有表名上下文，视为该表继续
      if (!/表名|table/.test(headerCols[0]) && currentTableName) continue
      // 表头含表名列则清空当前表名上下文，等待下一数据行建立新表
      if (/表名|table/.test(headerCols[0])) currentTableName = ''
      continue
    }

    // 格式 A：第一列是表名，第二列是字段列表（逗号分隔）
    const tableNameCell = first
    const secondCell = cells[1] || ''
    const looksLikeFieldList = /,|，/.test(secondCell) && /\s+(?:int|string|text|date|bool|number)/i.test(secondCell)

    if (looksLikeFieldList) {
      const t = getOrCreateTable(tableNameCell)
      if (!t) continue
      currentTableName = t.name
      const list = secondCell
        .split(/[,，]/)
        .map((f) => f.trim())
        .filter(Boolean)
      for (const f of list) {
        const [fn, ft] = f.split(/\s+/)
        const safeFn = (fn || 'field').replace(/[^\w]/g, '_')
        if (!t.fields.some((x) => x.name === safeFn)) t.fields.push({ name: safeFn, type: ft || 'string' })
      }
      continue
    }

    // 格式 B：第一列是表名，后续列是字段名/类型
    const hasTableNameColumn = headerCols.length && /表名|table/.test(headerCols[0])
    const hasFieldNameColumn = headerCols.length && headerCols.some((h) => /字段名|字段|field|列名/.test(h))
    const typeIndex = headerCols.findIndex((h) => /类型|type/.test(h))
    const fieldNameIndex = headerCols.findIndex((h) => /字段名|字段|field|列名/.test(h))

    if (hasTableNameColumn) {
      const t = getOrCreateTable(tableNameCell)
      if (!t) continue
      currentTableName = t.name
      const fnIdx = fieldNameIndex >= 0 ? fieldNameIndex : 1
      const fName = cells[fnIdx] || ''
      const fType = typeIndex >= 0 ? cells[typeIndex] : ''
      const safeFn = fName.replace(/[^\w]/g, '_')
      if (!safeFn) continue
      if (!t.fields.some((x) => x.name === safeFn)) t.fields.push({ name: safeFn, type: parseFieldType(fType) })
      continue
    }

    if (hasFieldNameColumn || headerCols.length) {
      // 格式 C：只有字段名列，使用 currentTableName 或最近一个表
      let t = null
      if (currentTableName) {
        t = getOrCreateTable(currentTableName)
      } else if (tables.length) {
        t = tables[tables.length - 1]
      } else {
        // 兜底：从表格前一行取可能的表名（非标题的普通文字）
        const prev = lines[i - 1] || ''
        const m = prev.match(/^\s*[-*]?\s*([\w\u4e00-\u9fa5]+)\s*(?:表|Table)?\s*$/)
        if (m) t = getOrCreateTable(m[1])
      }
      if (!t) continue
      currentTableName = t.name
      const fnIdx = fieldNameIndex >= 0 ? fieldNameIndex : 0
      const fName = cells[fnIdx] || ''
      const fType = typeIndex >= 0 ? cells[typeIndex] : ''
      const safeFn = fName.replace(/[^\w]/g, '_')
      if (!safeFn || /字段名|字段|类型/.test(safeFn)) continue
      if (!t.fields.some((x) => x.name === safeFn)) t.fields.push({ name: safeFn, type: parseFieldType(fType) })
      continue
    }

    // 无表头兜底：尝试把第一列当表名，第二列当字段名
    const t = getOrCreateTable(tableNameCell)
    if (!t) continue
    currentTableName = t.name
    const fName = cells[1] || ''
    const fType = cells[2] || ''
    const safeFn = fName.replace(/[^\w]/g, '_')
    if (!safeFn) continue
    if (!t.fields.some((x) => x.name === safeFn)) t.fields.push({ name: safeFn, type: parseFieldType(fType) })
  }

  // 清理空字段；对空表命中已知医院/政务 OA 表名时自动展开默认字段
  for (const t of tables) {
    t.fields = t.fields.filter((f) => f.name && f.name !== 'id' && f.name !== 'field')
    if (!t.fields.length) {
      const defaults = DEFAULT_HOSPITAL_SCHEMA[t.name]
      t.fields = defaults ? defaults.map((f) => ({ ...f })) : [{ name: 'name', type: 'string' }]
    }
  }

  // 场景 fallback：文档或项目名明显是医院/政务 OA，但 AI 没按标准格式输出库表，
  // 自动注入已知核心表，避免用户得到空壳 Item 工程。
  const sceneText = (doc || '') + '\n' + (sceneHint || '')
  // OA 场景判定：要求"强场景词"或"≥3 个弱信号"，避免单个 sys_user 单词就误触发 9 表覆盖
  const OA_STRONG = /医院|政务|卫生|内网|公文|审批|归档|科室|职工|干部|病案|门诊|排班|考勤|人事|档案管理|密码策略|等保|离职|赋予权限|卫生院|卫健|机关/i
  const weakMatches = sceneText.match(/OA|办公|sys_user|oa_document|oa_flow|sys_org|账号|权限|角色|部门|组织|科室|公文|审批|档案/gi) || []
  const isOaScene = OA_STRONG.test(sceneText) || weakMatches.length >= 3
  const hasRealTables = tables.some((t) => t.name !== 'item')
  if (isOaScene && !hasRealTables) {
    tables.length = 0
    for (const [name, fields] of Object.entries(DEFAULT_HOSPITAL_SCHEMA)) {
      tables.push({ name, fields: fields.map((f) => ({ ...f })) })
    }
  }

  // 本地兜底：AI 生成的设计文档无法解析出业务表时，基于用户原始需求关键词推断实体，
  // 避免生成空壳 Item 工程。优先级低于 OA 场景 fallback，高于默认 Item 占位。
  if ((tables.length === 0 || (tables.length === 1 && tables[0].name === 'item')) && sceneHint) {
    const fallback = fallbackEntitiesFromHint(sceneHint)
    if (fallback.length) {
      tables.length = 0
      tables.push(...fallback)
    }
  }

  // 兜底占位表（无识别到任何表时）
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

// 语义化种子值：让自动生成的示例数据更像真实业务，而非“记录1-字段名”
function sampleValue(field, type, n, label) {
  const f = String(field || '').toLowerCase()
  const t = String(type || 'string').toLowerCase()
  const isNum = /(int|integer|bigint|number|float|price|amount|qty|count|num|cost|fee|age|year|total|score)/.test(t)
  const pick = (arr) => arr[(n - 1) % arr.length]
  if (/(^|_)name$|username|姓名|用户名|标题|title/.test(f)) return (label || '记录') + ' ' + n
  if (/(phone|mobile|电话|手机)/.test(f)) return '138' + String(10000000 + n * 137).slice(0, 8)
  if (/(email|邮箱)/.test(f)) return 'user' + n + '@example.com'
  if (/(status|state|状态)/.test(f)) return pick(['启用', '禁用', '待审核'])
  if (/(gender|sex|性别)/.test(f)) return pick(['男', '女'])
  if (/(addr|address|地址)/.test(f)) return pick(['北京市朝阳区', '上海市浦东新区', '广州市天河区', '深圳市南山区'])
  if (/(remark|note|desc|描述|备注|内容|content|简介|info)/.test(f)) return (label || '记录') + ' ' + n + ' 的详细说明'
  if (/(created_at|create_time|updated_at|update_time|time|date|日期|时间)/.test(f)) {
    const m = (n % 12) + 1
    const d = (n % 28) + 1
    return '2026-' + String(m).padStart(2, '0') + '-' + String(d).padStart(2, '0')
  }
  if (isNum) {
    if (/(price|amount|cost|fee|total|金额|费用|总价)/.test(f)) return Math.round(n * 99.5 * 100) / 100
    if (/(age|year|年龄|年份)/.test(f)) return 18 + (n % 40)
    if (/(count|qty|num|total|数量|次数)/.test(f)) return n * 5
    return n * 10
  }
  if (/bool/.test(t)) return 1
  if (/date|time/.test(t)) return '2026-01-01'
  return (label || '记录') + ' ' + n
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
      const body = e.fields
        .filter((f) => f.name !== 'id')
        .map((f) => `  ${f.name} ${sqlType(f.type)}`)
        .join(',\n')
      const cols = body ? '  id INTEGER PRIMARY KEY AUTOINCREMENT,\n' + body : '  id INTEGER PRIMARY KEY AUTOINCREMENT'
      return `db.exec(\`CREATE TABLE IF NOT EXISTS ${e.name} (\n${cols}\n)\`)`
    })
    .join('\n')
  // 自动种子数据：首次启动且表为空时插入示例数据，便于预览联调看到真实 CRUD
  const seedArr = entities.map((e) => {
    const cols = e.fields.filter((f) => f.name !== 'id')
    const label = pascal(e.name)
    return {
      table: e.name,
      cols: cols.map((c) => c.name),
      rows: Array.from({ length: 10 }, (_, i) => i + 1).map((n) =>
        cols.map((f) => sampleValue(f.name, f.type, n, label))
      )
    }
  })
  const seedCode = `// 自动种子数据：首次启动且表为空时插入示例数据，便于预览联调看到真实 CRUD
const SEED = ${JSON.stringify(seedArr, null, 2)};
SEED.forEach(({ table, cols, rows }) => {
  const c = db.prepare('SELECT COUNT(*) AS n FROM ' + table).get().n
  if (c === 0) {
    const stmt = db.prepare('INSERT INTO ' + table + ' (' + cols.join(',') + ') VALUES (' + cols.map(() => '?').join(',') + ')')
    rows.forEach((r) => stmt.run(...r))
    console.log('[seed] ' + table + ' 已插入 ' + rows.length + ' 条示例数据')
  }
})`
  const index = `const express = require('express')
const cors = require('cors')
const { DatabaseSync } = require('node:sqlite')
const db = new DatabaseSync('${name}.db')
// 依据设计文档库表自动建表（node:sqlite，文件型数据库，重启不丢数据）
${createTables}

${seedCode}

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
      const body = e.fields
        .filter((f) => f.name !== 'id')
        .map((f) => `  ${f.name} ${sqlType(f.type)}`)
        .join(',\n')
      const cols = body ? '  id INTEGER PRIMARY KEY AUTOINCREMENT,\n' + body : '  id INTEGER PRIMARY KEY AUTOINCREMENT'
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
function webFiles(name, entities, appType = 'admin', sceneHint = '') {
  if (appType === 'canvas') return canvasWebFiles(name, entities)
  if (appType === 'tool') return toolWebFiles(name, entities, sceneHint)
  const pkg = {
    name: `${name}-web`,
    version: '1.0.0',
    scripts: { dev: 'vite', build: 'vite build', preview: 'vite preview' },
    dependencies: { vue: '^3.4.0', 'vue-router': '^4.3.0', axios: '^1.7.0' },
    devDependencies: { '@vitejs/plugin-vue': '^5.0.0', vite: '^5.0.0' }
  }
  const vite = `import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
// 联调真实数据时由 DevThink 主进程注入 API_TARGET（动态端口，避免与已占用的 3001 冲突）
const API_TARGET = process.env.API_TARGET || 'http://127.0.0.1:3001'
export default defineConfig({ plugins: [vue()], server: { host: '127.0.0.1', port: 5180, proxy: { '/api': API_TARGET } } })
`
  const main = `import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
createApp(App).use(router).mount('#app')
`
  const appVue = `<template>
  <div class="app">
    <aside class="sidebar">
      <div class="brand">
        <div class="logo">${name.charAt(0).toUpperCase()}</div>
        <div class="title">${name}<span>管理后台</span></div>
      </div>
      <nav>
        <router-link to="/" class="nav-item" exact-active-class="active">📊 仪表盘</router-link>
        ${entities.map((e) => `<router-link to="/${plural(camel(e.name))}" class="nav-item" exact-active-class="active">📑 ${pascal(e.name)}</router-link>`).join('\n        ')}
      </nav>
      ${entities.length > 8 ? `<div class="side-foot">共 ${entities.length} 个模块</div>` : ''}
    </aside>
    <main class="main">
      <header class="topbar">
        <div>
          <h2>{{ $route.path === '/' ? '仪表盘' : ($route.meta?.title || '管理') }}</h2>
          <div class="crumb">DevThink 自动生成 · {{ today }}</div>
        </div>
        <div class="user">
          <div class="avatar">${name.charAt(0).toUpperCase()}</div>
          <div class="uname">管理员</div>
        </div>
      </header>
      <div class="content">
        <router-view />
      </div>
    </main>
  </div>
</template>
<script setup>
const today = new Date().toLocaleDateString('zh-CN', { year: 'numeric', month: 'long', day: 'numeric' })
</script>
<style>
* { box-sizing: border-box; }
body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif; background: #f3f4f6; color: #1f2937; }
.app { display: flex; min-height: 100vh; }
.sidebar { width: 220px; background: #111827; color: #fff; flex-shrink: 0; display: flex; flex-direction: column; }
.brand { display: flex; align-items: center; gap: 12px; padding: 20px; border-bottom: 1px solid rgba(255,255,255,0.08); }
.logo { width: 38px; height: 38px; border-radius: 10px; background: linear-gradient(135deg,#2563eb,#4f46e5); display: grid; place-items: center; font-weight: 700; font-size: 18px; }
.title { font-weight: 600; font-size: 15px; line-height: 1.2; }
.title span { display: block; font-size: 11px; color: #9ca3af; font-weight: 400; }
.sidebar nav { display: flex; flex-direction: column; padding: 12px; gap: 6px; flex: 1; overflow: auto; }
.nav-item { padding: 10px 12px; border-radius: 8px; color: #d1d5db; text-decoration: none; font-size: 14px; transition: all .15s; }
.nav-item:hover, .nav-item.active { background: rgba(255,255,255,0.1); color: #fff; }
.side-foot { padding: 12px; font-size: 12px; color: #6b7280; border-top: 1px solid rgba(255,255,255,0.08); }
.main { flex: 1; display: flex; flex-direction: column; min-width: 0; }
.topbar { height: 64px; background: #fff; border-bottom: 1px solid #e5e7eb; display: flex; align-items: center; justify-content: space-between; padding: 0 28px; }
.topbar h2 { font-size: 18px; margin: 0; color: #111827; }
.crumb { font-size: 12px; color: #9ca3af; margin-top: 2px; }
.user { display: flex; align-items: center; gap: 10px; }
.avatar { width: 34px; height: 34px; border-radius: 50%; background: linear-gradient(135deg,#2563eb,#4f46e5); color: #fff; display: grid; place-items: center; font-weight: 700; }
.uname { font-size: 13px; color: #6b7280; }
.content { flex: 1; padding: 28px; overflow: auto; }
h2 { margin: 0 0 20px; font-size: 20px; color: #111827; }
.card { background: #fff; border-radius: 12px; padding: 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.06); }
.btn { display: inline-flex; align-items: center; gap: 6px; padding: 8px 16px; border-radius: 8px; border: none; font-size: 14px; cursor: pointer; transition: all .15s; }
.btn-primary { background: #2563eb; color: #fff; }
.btn-primary:hover { background: #1d4ed8; }
.btn-danger { background: #fee2e2; color: #dc2626; }
.btn-danger:hover { background: #fecaca; }
input, select, textarea { padding: 8px 12px; border: 1px solid #d1d5db; border-radius: 8px; font-size: 14px; outline: none; }
input:focus, select:focus, textarea:focus { border-color: #2563eb; box-shadow: 0 0 0 3px rgba(37,99,235,0.12); }
</style>
`
  const dashboard = `<template>
  <div class="dashboard">
    <div class="welcome">
      <div>
        <h2>欢迎使用 {{ sysName }} 管理后台</h2>
        <p>{{ today }} · 共 {{ entities.length }} 个数据模块 · 系统运行正常</p>
      </div>
      <div class="welcome-badge">DevThink 自动生成</div>
    </div>

    <div class="section-title">数据概览</div>
    <div class="grid">
      <div class="stat-card" v-for="(s, i) in stats" :key="i">
        <div class="stat-icon">{{ s.icon }}</div>
        <div class="stat-info">
          <div class="stat-label">{{ s.label }}</div>
          <div class="stat-num">{{ s.count }}</div>
        </div>
        <router-link :to="s.link" class="stat-arrow">→</router-link>
      </div>
    </div>

    <div class="row">
      <div class="card chart-card">
        <div class="card-title">各模块记录数</div>
        <div class="bars">
          <div class="bar-row" v-for="(s, i) in stats" :key="i">
            <span class="bar-label">{{ s.label }}</span>
            <div class="bar-track"><div class="bar-fill" :style="{ width: barWidth(s.count) }"></div></div>
            <span class="bar-val">{{ s.count }}</span>
          </div>
          <div v-if="!stats.length" class="a-empty">暂无模块</div>
        </div>
      </div>
      <div class="card activity-card">
        <div class="card-title">最近动态</div>
        <ul class="activity">
          <li v-for="(a, i) in activities" :key="i">
            <span class="dot"></span>
            <div class="a-body">
              <div class="a-title">{{ a.title }}</div>
              <div class="a-meta">{{ a.module }} · {{ a.time }}</div>
            </div>
          </li>
          <li v-if="!activities.length" class="a-empty">暂无动态</li>
        </ul>
      </div>
    </div>
  </div>
</template>
<script setup>
import { ref, onMounted, computed } from 'vue'
import { apis } from '../api/client.js'
const sysName = '${name}'
const today = new Date().toLocaleDateString('zh-CN', { year: 'numeric', month: 'long', day: 'numeric' })
const entities = [
${entities.map((e) => `  { key: '${camel(e.name)}', label: '${pascal(e.name)}', link: '/${plural(camel(e.name))}', icon: '📑' }`).join(',\n')}
]
const counts = ref({})
const recent = ref({})
async function loadAll() {
  await Promise.all(entities.map(async (e) => {
    try {
      const r = await apis[e.key].list()
      const arr = Array.isArray(r.data) ? r.data : []
      counts.value[e.key] = arr.length
      recent.value[e.key] = arr.slice(0, 3)
    } catch (err) { counts.value[e.key] = 0 }
  }))
}
onMounted(loadAll)
const stats = computed(() => entities.map((e) => ({ ...e, count: counts.value[e.key] ?? '—' })))
const maxCount = computed(() => Math.max(0, ...entities.map((e) => Number(counts.value[e.key]) || 0)))
function barWidth(c) { const n = Number(c) || 0; return (maxCount.value ? (n / maxCount.value) * 100 : 0) + '%' }
const activities = computed(() => {
  const list = []
  entities.forEach((e) => {
    const arr = recent.value[e.key] || []
    arr.slice(0, 1).forEach((row) => {
      const first = Object.entries(row).find(([k]) => k !== 'id')
      const title = String(first ? first[1] : '记录').slice(0, 40)
      list.push({ title, module: e.label, time: row.created_at || row.create_time || '刚刚' })
    })
  })
  return list
})
</script>
<style scoped>
.dashboard { max-width: 1100px; }
.welcome { display: flex; align-items: center; justify-content: space-between; background: linear-gradient(135deg,#2563eb,#4f46e5); border-radius: 14px; padding: 22px 26px; color: #fff; margin-bottom: 24px; }
.welcome h2 { margin: 0; font-size: 20px; }
.welcome p { margin: 6px 0 0; font-size: 13px; opacity: 0.9; }
.welcome-badge { background: rgba(255,255,255,0.18); padding: 6px 12px; border-radius: 20px; font-size: 12px; }
.section-title { font-size: 15px; font-weight: 600; color: #374151; margin: 4px 0 14px; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 18px; }
.stat-card { background: #fff; border-radius: 12px; padding: 18px; display: flex; align-items: center; gap: 14px; box-shadow: 0 1px 3px rgba(0,0,0,0.06); transition: transform .15s, box-shadow .15s; }
.stat-card:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.08); }
.stat-icon { width: 46px; height: 46px; border-radius: 12px; background: #eff6ff; display: grid; place-items: center; font-size: 22px; }
.stat-info { flex: 1; }
.stat-label { font-size: 14px; color: #6b7280; }
.stat-num { font-size: 24px; font-weight: 700; color: #111827; line-height: 1.2; }
.stat-arrow { width: 32px; height: 32px; border-radius: 8px; background: #f3f4f6; color: #2563eb; display: grid; place-items: center; text-decoration: none; font-weight: 700; }
.row { display: grid; grid-template-columns: 1.4fr 1fr; gap: 18px; margin-top: 22px; }
.card-title { font-size: 15px; font-weight: 600; color: #111827; margin-bottom: 16px; }
.chart-card, .activity-card { padding: 20px; }
.bars { display: flex; flex-direction: column; gap: 14px; }
.bar-row { display: flex; align-items: center; gap: 12px; }
.bar-label { width: 88px; font-size: 13px; color: #4b5563; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.bar-track { flex: 1; height: 12px; background: #f3f4f6; border-radius: 6px; overflow: hidden; }
.bar-fill { height: 100%; background: linear-gradient(90deg,#2563eb,#4f46e5); border-radius: 6px; transition: width .4s; }
.bar-val { width: 36px; text-align: right; font-size: 13px; color: #374151; font-weight: 600; }
.activity { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 14px; }
.activity li { display: flex; gap: 12px; align-items: flex-start; }
.dot { width: 8px; height: 8px; border-radius: 50%; background: #2563eb; margin-top: 6px; flex-shrink: 0; }
.a-title { font-size: 14px; color: #111827; }
.a-meta { font-size: 12px; color: #9ca3af; margin-top: 2px; }
.a-empty { color: #9ca3af; font-size: 13px; padding: 8px 0; }
</style>
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
  <div class="entity-page">
    <div class="page-header">
      <h2>{{ meta[entity]?.label }} 管理</h2>
      <div class="tools">
        <input v-model="q" class="search" placeholder="🔍 搜索关键字..." />
        <button class="btn btn-danger" :disabled="!selected.length" @click="removeMany">批量删除 ({{ selected.length }})</button>
      </div>
    </div>
    <div class="card">
      <table v-if="paged.length" class="data-table">
        <thead><tr>
          <th style="width:40px"><input type="checkbox" :checked="allChecked" @change="toggleAll" /></th>
          <th v-for="c in cols" :key="c">{{ c }}</th>
          <th style="width:150px">操作</th>
        </tr></thead>
        <tbody>
          <tr v-for="row in paged" :key="row.id">
            <td><input type="checkbox" :value="row.id" v-model="selected" /></td>
            <td v-for="c in cols" :key="c">{{ row[c] }}</td>
            <td>
              <button class="link-btn" @click="edit(row)">编辑</button>
              <button class="link-btn danger" @click="remove(row.id)">删除</button>
            </td>
          </tr>
        </tbody>
      </table>
      <div v-else class="empty">暂无数据，请在下方添加。</div>
      <div class="pager" v-if="filtered.length > pageSize">
        <button class="btn" :disabled="page<=1" @click="page--">上一页</button>
        <span class="pager-info">第 {{ page }} / {{ totalPages }} 页 · 共 {{ filtered.length }} 条</span>
        <button class="btn" :disabled="page>=totalPages" @click="page++">下一页</button>
      </div>
    </div>
    <div class="card form-card">
      <h3>{{ editingId ? '编辑记录 #' + editingId : '新增记录' }}</h3>
      <div class="form-grid">
        <div v-for="f in formFields" :key="f" class="field">
          <label>{{ f }}</label>
          <input v-model="form[f]" :placeholder="'请输入 ' + f" />
        </div>
      </div>
      <div class="form-actions">
        <button class="btn btn-primary" @click="submit">{{ editingId ? '保存修改' : '添加' }}</button>
        <button v-if="editingId" class="btn" @click="cancelEdit">取消</button>
      </div>
    </div>
  </div>
</template>
<script setup>
import { ref, onMounted, computed, watch } from 'vue'
import { apis, meta } from '../api/client.js'
const props = defineProps({ entity: String })
const rows = ref([])
const form = ref({})
const editingId = ref(null)
const q = ref('')
const page = ref(1)
const pageSize = 8
const selected = ref([])
const cols = computed(() => {
  const all = rows.value[0] ? Object.keys(rows.value[0]) : (meta[props.entity]?.fields || [])
  return all.filter((c) => c !== 'id')
})
const formFields = computed(() => meta[props.entity]?.fields || [])
const filtered = computed(() => {
  const k = q.value.trim().toLowerCase()
  if (!k) return rows.value
  return rows.value.filter((r) => cols.value.some((c) => String(r[c]).toLowerCase().includes(k)))
})
const totalPages = computed(() => Math.max(1, Math.ceil(filtered.value.length / pageSize)))
const paged = computed(() => filtered.value.slice((page.value - 1) * pageSize, page.value * pageSize))
const allChecked = computed(() => paged.value.length > 0 && paged.value.every((r) => selected.value.includes(r.id)))
function resetForm() { editingId.value = null; form.value = {}; selected.value = []; q.value = ''; page.value = 1 }
async function load() { try { const r = await apis[props.entity].list(); rows.value = r.data || [] } catch (e) { rows.value = [] } }
watch(() => props.entity, () => { resetForm(); load() })
watch(filtered, () => { page.value = 1 })
function toggleAll(e) { selected.value = e.target.checked ? paged.value.map((r) => r.id) : [] }
async function submit() {
  if (editingId.value) { await apis[props.entity].update(editingId.value, form.value); editingId.value = null; form.value = {} }
  else { await apis[props.entity].create(form.value); form.value = {} }
  await load()
}
function edit(row) { editingId.value = row.id; form.value = { ...row } }
function cancelEdit() { editingId.value = null; form.value = {} }
async function remove(id) { await apis[props.entity].remove(id); selected.value = selected.value.filter((x) => x !== id); await load() }
async function removeMany() { for (const id of selected.value) await apis[props.entity].remove(id); selected.value = []; await load() }
onMounted(load)
</script>
<style scoped>
.entity-page { display: flex; flex-direction: column; gap: 20px; }
.page-header { display: flex; align-items: center; justify-content: space-between; gap: 12px; flex-wrap: wrap; }
.page-header h2 { margin: 0; font-size: 20px; color: #111827; }
.tools { display: flex; gap: 10px; align-items: center; }
.search { min-width: 220px; }
.form-card h3 { margin: 0 0 18px; font-size: 16px; color: #111827; }
.form-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 16px; margin-bottom: 18px; }
.field { display: flex; flex-direction: column; gap: 6px; }
.field label { font-size: 13px; color: #4b5563; font-weight: 500; }
.field input { width: 100%; }
.form-actions { display: flex; gap: 10px; }
.data-table { width: 100%; border-collapse: collapse; font-size: 14px; }
.data-table th { text-align: left; padding: 12px; color: #6b7280; font-weight: 600; border-bottom: 1px solid #e5e7eb; background: #f9fafb; }
.data-table td { padding: 14px 12px; border-bottom: 1px solid #f3f4f6; color: #374151; }
.data-table tr:last-child td { border-bottom: none; }
.data-table tr:hover td { background: #f9fafb; }
.link-btn { background: none; border: none; color: #2563eb; cursor: pointer; font-size: 13px; padding: 0 6px; }
.link-btn.danger { color: #dc2626; }
.link-btn:hover { text-decoration: underline; }
.empty { padding: 40px; text-align: center; color: #9ca3af; font-size: 14px; }
.pager { display: flex; align-items: center; justify-content: center; gap: 14px; padding: 14px; border-top: 1px solid #f3f4f6; }
.pager-info { font-size: 13px; color: #6b7280; }
</style>
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

// ---------- 在线画板 / Canvas 工具（画图、涂鸦、白板） ----------
function canvasWebFiles(name, entities) {
  const pkg = {
    name: `${name}-web`,
    version: '1.0.0',
    scripts: { dev: 'vite', build: 'vite build', preview: 'vite preview' },
    dependencies: { vue: '^3.4.0' },
    devDependencies: { '@vitejs/plugin-vue': '^5.0.0', vite: '^5.0.0' }
  }
  const vite = `import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
// 联调真实数据时由 DevThink 主进程注入 API_TARGET（动态端口，避免与已占用的 3001 冲突）
const API_TARGET = process.env.API_TARGET || 'http://127.0.0.1:3001'
export default defineConfig({ plugins: [vue()], server: { host: '127.0.0.1', port: 5180, proxy: { '/api': API_TARGET } } })
`
  const main = `import { createApp } from 'vue'
import App from './App.vue'
createApp(App).mount('#app')
`
  const appVue = `<template>
  <div class="app">
    <header class="topbar">
      <div class="brand">
        <div class="logo">${name.charAt(0).toUpperCase()}</div>
        <div class="title">${name}<span>在线画板</span></div>
      </div>
      <div class="actions">
        <button class="btn" @click="newCanvas">🗑️ 清空</button>
        <button class="btn" @click="undo" :disabled="!history.length">↩️ 撤销</button>
        <button class="btn" @click="redo" :disabled="!redoStack.length">↪️ 重做</button>
        <button class="btn primary" @click="exportPng">💾 导出 PNG</button>
      </div>
    </header>
    <div class="workspace">
      <aside class="toolbar">
        <div class="tool-group">
          <label>工具</label>
          <button v-for="t in tools" :key="t.key" class="tool-btn" :class="{ active: tool === t.key }" @click="tool = t.key">{{ t.icon }}</button>
        </div>
        <div class="tool-group">
          <label>形状</label>
          <button v-for="s in shapes" :key="s.key" class="tool-btn" :class="{ active: tool === s.key }" @click="tool = s.key">{{ s.icon }}</button>
        </div>
        <div class="tool-group">
          <label>颜色</label>
          <div class="colors">
            <span v-for="c in colors" :key="c" class="color-dot" :class="{ active: color === c }" :style="{ background: c }" @click="color = c"></span>
          </div>
          <input type="color" v-model="color" />
        </div>
        <div class="tool-group">
          <label>粗细 {{ size }}px</label>
          <input type="range" min="1" max="30" v-model.number="size" />
        </div>
        <div class="tool-group">
          <label>画布</label>
          <button class="btn" @click="bg = '#ffffff'">白底</button>
          <button class="btn" @click="bg = '#1f2937'">黑底</button>
        </div>
      </aside>
      <main class="canvas-wrap">
        <canvas ref="cvs" @mousedown="start" @mousemove="move" @mouseup="end" @mouseleave="end" @touchstart.prevent="touchStart" @touchmove.prevent="touchMove" @touchend.prevent="end"></canvas>
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, watch } from 'vue'
const cvs = ref(null)
const ctx = ref(null)
const tool = ref('pen')
const color = ref('#111827')
const size = ref(4)
const bg = ref('#ffffff')
const drawing = ref(false)
const startPos = ref({ x: 0, y: 0 })
const history = ref([])
const redoStack = ref([])
const snapshot = ref(null)

const tools = [
  { key: 'pen', icon: '✏️ 画笔' },
  { key: 'eraser', icon: '🧽 橡皮' }
]
const shapes = [
  { key: 'line', icon: '📏 直线' },
  { key: 'rect', icon: '⬜ 矩形' },
  { key: 'circle', icon: '⭕ 圆形' }
]
const colors = ['#111827', '#dc2626', '#2563eb', '#16a34a', '#f59e0b', '#9333ea', '#ffffff']

onMounted(() => { resize(); window.addEventListener('resize', resize) })
watch(bg, () => { fillBg(); saveState() })

function resize() {
  const canvas = cvs.value
  if (!canvas) return
  const parent = canvas.parentElement
  canvas.width = parent.clientWidth
  canvas.height = parent.clientHeight
  ctx.value = canvas.getContext('2d')
  fillBg()
  saveState()
}
function fillBg() {
  const c = ctx.value
  if (!c) return
  c.fillStyle = bg.value
  c.fillRect(0, 0, c.canvas.width, c.canvas.height)
}
function saveState() {
  if (!ctx.value) return
  history.value.push(ctx.value.canvas.toDataURL())
  if (history.value.length > 20) history.value.shift()
}
function restoreFrom(url) {
  const img = new Image()
  img.src = url
  img.onload = () => {
    const c = ctx.value
    c.clearRect(0, 0, c.canvas.width, c.canvas.height)
    c.drawImage(img, 0, 0)
  }
}
function undo() {
  if (!history.value.length) return
  redoStack.value.push(history.value.pop())
  const url = history.value[history.value.length - 1]
  if (url) restoreFrom(url)
}
function redo() {
  if (!redoStack.value.length) return
  const url = redoStack.value.pop()
  history.value.push(url)
  restoreFrom(url)
}
function newCanvas() { if (confirm('确定清空画布？')) { fillBg(); saveState() } }
function exportPng() {
  const link = document.createElement('a')
  link.download = '${name}-' + new Date().getTime() + '.png'
  link.href = ctx.value.canvas.toDataURL()
  link.click()
}
function pos(e) {
  const rect = cvs.value.getBoundingClientRect()
  return { x: e.clientX - rect.left, y: e.clientY - rect.top }
}
function start(e) {
  drawing.value = true
  startPos.value = pos(e)
  snapshot.value = ctx.value.canvas.toDataURL()
  ctx.value.beginPath()
  ctx.value.moveTo(startPos.value.x, startPos.value.y)
  ctx.value.strokeStyle = tool.value === 'eraser' ? bg.value : color.value
  ctx.value.lineWidth = size.value
  ctx.value.lineCap = 'round'
  ctx.value.lineJoin = 'round'
}
function move(e) {
  if (!drawing.value) return
  const p = pos(e)
  const c = ctx.value
  if (tool.value === 'pen' || tool.value === 'eraser') {
    c.lineTo(p.x, p.y)
    c.stroke()
  } else {
    if (snapshot.value) restoreFrom(snapshot.value)
    drawShape(startPos.value, p)
  }
}
function end() {
  if (!drawing.value) return
  drawing.value = false
  saveState()
  redoStack.value = []
}
function drawShape(a, b) {
  const c = ctx.value
  c.strokeStyle = color.value
  c.lineWidth = size.value
  if (tool.value === 'line') {
    c.beginPath(); c.moveTo(a.x, a.y); c.lineTo(b.x, b.y); c.stroke()
  } else if (tool.value === 'rect') {
    c.strokeRect(a.x, a.y, b.x - a.x, b.y - a.y)
  } else if (tool.value === 'circle') {
    const r = Math.hypot(b.x - a.x, b.y - a.y)
    c.beginPath(); c.arc(a.x, a.y, r, 0, Math.PI * 2); c.stroke()
  }
}
function touchStart(e) { const t = e.touches[0]; start(t) }
function touchMove(e) { const t = e.touches[0]; move(t) }
</script>

<style>
* { box-sizing: border-box; }
body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f3f4f6; }
.app { display: flex; flex-direction: column; height: 100vh; }
.topbar { height: 60px; background: #fff; border-bottom: 1px solid #e5e7eb; display: flex; align-items: center; justify-content: space-between; padding: 0 20px; }
.brand { display: flex; align-items: center; gap: 12px; }
.logo { width: 34px; height: 34px; border-radius: 10px; background: linear-gradient(135deg,#2563eb,#4f46e5); display: grid; place-items: center; color: #fff; font-weight: 700; }
.title { font-weight: 600; font-size: 16px; }
.title span { display: block; font-size: 11px; color: #6b7280; font-weight: 400; }
.actions { display: flex; gap: 10px; }
.btn { padding: 7px 14px; border: 1px solid #d1d5db; border-radius: 8px; background: #fff; cursor: pointer; font-size: 13px; }
.btn.primary { background: #2563eb; color: #fff; border-color: #2563eb; }
.btn:disabled { opacity: .5; cursor: not-allowed; }
.workspace { flex: 1; display: flex; overflow: hidden; }
.toolbar { width: 200px; background: #111827; color: #fff; padding: 16px; display: flex; flex-direction: column; gap: 18px; overflow: auto; }
.tool-group { display: flex; flex-direction: column; gap: 8px; }
.tool-group label { font-size: 12px; color: #9ca3af; }
.tool-btn { text-align: left; padding: 8px 10px; border-radius: 8px; border: none; background: rgba(255,255,255,0.06); color: #fff; cursor: pointer; font-size: 13px; }
.tool-btn.active { background: #2563eb; }
.colors { display: flex; flex-wrap: wrap; gap: 6px; }
.color-dot { width: 22px; height: 22px; border-radius: 50%; cursor: pointer; border: 2px solid transparent; }
.color-dot.active { border-color: #fff; }
input[type=range] { width: 100%; }
input[type=color] { width: 100%; height: 32px; border: none; padding: 0; background: none; }
.canvas-wrap { flex: 1; position: relative; background: #e5e7eb; display: flex; }
canvas { background: #fff; box-shadow: 0 4px 20px rgba(0,0,0,0.08); cursor: crosshair; width: 100%; height: 100%; }
</style>
`

  return [
    { path: 'web/package.json', content: JSON.stringify(pkg, null, 2) },
    { path: 'web/vite.config.js', content: vite },
    { path: 'web/index.html', content: `<!DOCTYPE html><html><head><meta charset="UTF-8"><title>${name}</title></head><body><div id="app"></div><script type="module" src="/src/main.js"></script></body></html>` },
    { path: 'web/src/main.js', content: main },
    { path: 'web/src/App.vue', content: appVue }
  ]
}

// ---------- 工具类 Web 应用（局域网扫描 / 通用实用工具）----------
function toolWebFiles(name, entities, sceneHint = '') {
  const hint = String(sceneHint || '').toLowerCase()
  const isNetworkScanner = /(局域网|lan|内网|网络扫描|设备扫描|端口扫描|ip|mac|主机|在线设备|网段|扫描|scanner|探测)/i.test(hint)

  const pkg = {
    name: `${name}-web`,
    version: '1.0.0',
    scripts: { dev: 'vite', build: 'vite build', preview: 'vite preview' },
    dependencies: { vue: '^3.4.0' },
    devDependencies: { '@vitejs/plugin-vue': '^5.0.0', vite: '^5.0.0' }
  }
  const vite = `import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
export default defineConfig({ plugins: [vue()], server: { host: '127.0.0.1', port: 5180 } })
`
  const main = `import { createApp } from 'vue'
import App from './App.vue'
createApp(App).mount('#app')
`
  const displayTitle = isNetworkScanner ? '局域网扫描工具' : '在线工具'

  const scannerAppVue = `<template>
  <div class="app">
    <header class="topbar">
      <div class="brand">
        <div class="logo">${name.charAt(0).toUpperCase()}</div>
        <div class="title">${name}<span>${displayTitle}</span></div>
      </div>
      <div class="actions">
        <button class="btn" @click="clearResults">清空结果</button>
        <button class="btn primary" :disabled="scanning" @click="startScan">{{ scanning ? '扫描中…' : '开始扫描' }}</button>
      </div>
    </header>
    <main class="workspace">
      <section class="panel controls">
        <h3>扫描配置</h3>
        <label>目标网段</label>
        <input v-model="range" placeholder="192.168.1.0/24" />
        <label>超时（毫秒）</label>
        <input type="number" v-model.number="timeout" min="100" max="5000" step="100" />
        <div class="options">
          <label><input type="checkbox" v-model="probePorts" /> 端口探测（80/443/22/3389）</label>
          <label><input type="checkbox" v-model="guessVendor" /> 厂商识别</label>
        </div>
        <p class="tip">💡 浏览器环境受安全沙箱限制，真实局域网扫描需配合桌面端或后端代理；此处为可交互演示，数据为模拟。</p>
      </section>
      <section class="panel results">
        <div class="summary">
          <div class="stat"><b>{{ results.length }}</b><span>在线设备</span></div>
          <div class="stat"><b>{{ activeCount }}</b><span>活跃主机</span></div>
          <div class="stat"><b>{{ portCount }}</b><span>开放端口</span></div>
        </div>
        <table>
          <thead>
            <tr><th>IP 地址</th><th>MAC 地址</th><th>主机名 / 厂商</th><th>延迟</th><th>开放端口</th><th>状态</th></tr>
          </thead>
          <tbody>
            <tr v-for="(r, i) in results" :key="i">
              <td>{{ r.ip }}</td>
              <td>{{ r.mac }}</td>
              <td>{{ r.vendor }}</td>
              <td>{{ r.latency }} ms</td>
              <td>{{ r.ports.join(', ') || '-' }}</td>
              <td><span class="badge" :class="r.status">{{ r.status === 'online' ? '在线' : '离线' }}</span></td>
            </tr>
            <tr v-if="!results.length"><td colspan="6" class="empty">暂无扫描结果，点击右上角「开始扫描」</td></tr>
          </tbody>
        </table>
      </section>
    </main>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
const range = ref('192.168.1.0/24')
const timeout = ref(800)
const probePorts = ref(true)
const guessVendor = ref(true)
const scanning = ref(false)
const results = ref([])
const activeCount = computed(() => results.value.filter(r => r.status === 'online').length)
const portCount = computed(() => results.value.reduce((sum, r) => sum + r.ports.length, 0))

const vendors = ['Apple', 'Xiaomi', 'Huawei', 'TP-LINK', 'Dell', 'Hikvision', 'Intel', 'Generic']
function randomMac() {
  return Array.from({ length: 6 }, () => Math.floor(Math.random() * 256).toString(16).padStart(2, '0')).join(':').toUpperCase()
}
function randomIp(prefix) {
  return prefix + Math.floor(Math.random() * 254 + 1)
}
function startScan() {
  scanning.value = true
  results.value = []
  const prefix = range.value.replace(/\\.\\d+\\/\\d+$/, '.')
  let count = 0
  const total = 24
  const interval = setInterval(() => {
    count++
    const online = Math.random() > 0.55
    if (online) {
      const ports = probePorts.value ? [80, 443, 22, 3389].filter(() => Math.random() > 0.6) : []
      results.value.push({
        ip: randomIp(prefix),
        mac: randomMac(),
        vendor: guessVendor.value ? vendors[Math.floor(Math.random() * vendors.length)] : 'Unknown',
        latency: Math.floor(Math.random() * 80 + 2),
        ports,
        status: 'online'
      })
    }
    if (count >= total) {
      clearInterval(interval)
      scanning.value = false
    }
  }, timeout.value / 6)
}
function clearResults() { results.value = [] }
</script>

<style>
* { box-sizing: border-box; }
body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f3f4f6; }
.app { display: flex; flex-direction: column; height: 100vh; }
.topbar { height: 60px; background: #fff; border-bottom: 1px solid #e5e7eb; display: flex; align-items: center; justify-content: space-between; padding: 0 20px; }
.brand { display: flex; align-items: center; gap: 12px; }
.logo { width: 34px; height: 34px; border-radius: 10px; background: linear-gradient(135deg,#2563eb,#4f46e5); display: grid; place-items: center; color: #fff; font-weight: 700; }
.title { font-weight: 600; font-size: 16px; }
.title span { display: block; font-size: 11px; color: #6b7280; font-weight: 400; }
.actions { display: flex; gap: 10px; }
.btn { padding: 8px 16px; border: 1px solid #d1d5db; border-radius: 8px; background: #fff; cursor: pointer; font-size: 13px; }
.btn.primary { background: #2563eb; color: #fff; border-color: #2563eb; }
.btn:disabled { opacity: .5; cursor: not-allowed; }
.workspace { flex: 1; display: flex; gap: 16px; padding: 16px; overflow: hidden; }
.panel { background: #fff; border-radius: 12px; box-shadow: 0 1px 3px rgba(0,0,0,0.06); padding: 16px; }
.controls { width: 280px; display: flex; flex-direction: column; gap: 10px; }
.controls h3 { margin: 0 0 6px; }
.controls label { font-size: 12px; color: #6b7280; }
.controls input[type=text], .controls input[type=number] { padding: 8px 10px; border: 1px solid #d1d5db; border-radius: 6px; }
.options { display: flex; flex-direction: column; gap: 6px; margin-top: 6px; }
.options label { display: flex; align-items: center; gap: 6px; color: #374151; font-size: 13px; }
.tip { font-size: 12px; color: #9ca3af; line-height: 1.5; margin-top: auto; }
.results { flex: 1; display: flex; flex-direction: column; overflow: hidden; }
.summary { display: flex; gap: 16px; margin-bottom: 12px; }
.stat { background: #f9fafb; border-radius: 8px; padding: 10px 16px; min-width: 90px; }
.stat b { display: block; font-size: 20px; color: #111827; }
.stat span { font-size: 12px; color: #6b7280; }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th, td { padding: 10px 12px; border-bottom: 1px solid #e5e7eb; text-align: left; }
th { background: #f9fafb; color: #6b7280; font-weight: 500; }
.empty { text-align: center; color: #9ca3af; padding: 40px; }
.badge { padding: 3px 8px; border-radius: 12px; font-size: 12px; }
.badge.online { background: #dcfce7; color: #166534; }
.badge.offline { background: #fee2e2; color: #991b1b; }
</style>
`

  // 通用工具兜底界面（当不是已知具体工具时）：一个简洁的任务/待办/剪贴板工具
  const genericAppVue = `<template>
  <div class="app">
    <header class="topbar">
      <div class="brand">
        <div class="logo">${name.charAt(0).toUpperCase()}</div>
        <div class="title">${name}<span>在线工具</span></div>
      </div>
    </header>
    <main class="workspace">
      <section class="panel">
        <h3>🛠️ 工具工作台</h3>
        <p class="muted">这是一个由 DevThink 根据你的需求自动生成的工具类应用。你可以继续在左侧「改代码」模式下描述想要的改动，比如增加输入框、按钮、计算逻辑、转换规则等。</p>
        <div class="card">
          <input v-model="input" placeholder="输入内容…" />
          <button class="btn primary" @click="process">处理</button>
          <pre v-if="output">{{ output }}</pre>
        </div>
      </section>
    </main>
  </div>
</template>

<script setup>
import { ref } from 'vue'
const input = ref('')
const output = ref('')
function process() {
  output.value = '处理结果：' + input.value.split('').reverse().join('')
}
</script>

<style>
* { box-sizing: border-box; }
body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f3f4f6; }
.app { display: flex; flex-direction: column; height: 100vh; }
.topbar { height: 60px; background: #fff; border-bottom: 1px solid #e5e7eb; display: flex; align-items: center; padding: 0 20px; }
.brand { display: flex; align-items: center; gap: 12px; }
.logo { width: 34px; height: 34px; border-radius: 10px; background: linear-gradient(135deg,#2563eb,#4f46e5); display: grid; place-items: center; color: #fff; font-weight: 700; }
.title { font-weight: 600; font-size: 16px; }
.title span { display: block; font-size: 11px; color: #6b7280; font-weight: 400; }
.workspace { flex: 1; padding: 20px; overflow: auto; }
.panel { max-width: 720px; margin: 0 auto; background: #fff; border-radius: 12px; box-shadow: 0 1px 3px rgba(0,0,0,0.06); padding: 20px; }
.muted { color: #6b7280; line-height: 1.6; }
.card { margin-top: 16px; display: flex; flex-direction: column; gap: 10px; }
input { padding: 10px 12px; border: 1px solid #d1d5db; border-radius: 8px; font-size: 14px; }
.btn { padding: 10px 16px; border: 1px solid #d1d5db; border-radius: 8px; background: #fff; cursor: pointer; }
.btn.primary { background: #2563eb; color: #fff; border-color: #2563eb; }
pre { background: #111827; color: #e2e8f0; padding: 12px; border-radius: 8px; overflow: auto; }
</style>
`

  const appVue = isNetworkScanner ? scannerAppVue : genericAppVue
  return [
    { path: 'web/package.json', content: JSON.stringify(pkg, null, 2) },
    { path: 'web/vite.config.js', content: vite },
    { path: 'web/index.html', content: `<!DOCTYPE html><html><head><meta charset="UTF-8"><title>${name}</title></head><body><div id="app"></div><script type="module" src="/src/main.js"></script></body></html>` },
    { path: 'web/src/main.js', content: main },
    { path: 'web/src/App.vue', content: appVue }
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
// 常用中文项目名 → 拼音，避免「方案分支 2」被过滤成「-2」
const PINYIN_MAP = {
  方: 'fang', 案: 'an', 分: 'fen', 支: 'zhi', 主: 'zhu', 医: 'yi', 院: 'yuan',
  政: 'zheng', 府: 'fu', 内: 'nei', 网: 'wang', 办: 'ban', 公: 'gong', 系: 'xi',
  统: 'tong', 管: 'guan', 理: 'li', 后: 'hou', 台: 'tai', 测: 'ce', 试: 'shi',
  智: 'zhi', 能: 'neng', 平: 'ping', 应: 'ying', 用: 'yong', 数: 'shu', 据: 'ju',
  库: 'ku', 接: 'jie', 口: 'kou', 前: 'qian', 端: 'duan', 移: 'yi', 动: 'dong',
  桌: 'zhuo', 面: 'mian', 云: 'yun', 服: 'fu', 务: 'wu', 工: 'gong', 具: 'ju',
  自: 'zi', 生: 'sheng', 成: 'cheng', 开: 'kai', 发: 'fa', 产: 'chan', 品: 'pin',
  客: 'ke', 户: 'hu', 订: 'ding', 单: 'dan', 销: 'xiao', 售: 'shou', 采: 'cai',
  购: 'gou', 仓: 'cang', 物: 'wu', 流: 'liu', 财: 'cai', 人: 'ren', 力: 'li',
  资: 'zi', 源: 'yuan', 考: 'kao', 勤: 'qin', 薪: 'xin', 绩: 'ji', 效: 'xiao',
  招: 'zhao', 聘: 'pin', 培: 'pei', 训: 'xun', 会: 'hui', 议: 'yi', 邮: 'you',
  件: 'jian', 日: 'ri', 程: 'cheng', 任: 'ren', 务: 'wu', 文: 'wen', 档: 'dang',
  知: 'zhi', 识: 'shi', 审: 'shen', 批: 'pi', 流: 'liu', 程: 'cheng', 权: 'quan',
  限: 'xian', 角: 'jiao', 色: 'se', 部: 'bu', 门: 'men', 岗: 'gang', 位: 'wei',
  科: 'ke', 室: 'shi', 病: 'bing', 药: 'yao', 处: 'chu', 验: 'yan', 住: 'zhu',
  诊: 'zhen', 挂: 'gua', 号: 'hao', 收: 'shou', 费: 'fei', 保: 'bao', 结: 'jie',
  算: 'suan', 排: 'pai', 班: 'ban', 手: 'shou', 术: 'shu', 护: 'hu', 嘱: 'zhu',
  历: 'li', 随: 'sui', 访: 'fang', 健: 'jian', 康: 'kang', 疫: 'yi', 苗: 'miao',
  核: 'he', 酸: 'suan', 抗: 'kang', 原: 'yuan', 隔: 'ge', 离: 'li', 转: 'zhuan',
  急: 'ji', 救: 'jiu', 输: 'shu', 血: 'xue', 麻: 'ma', 醉: 'zui', 重: 'zhong',
  监: 'jian', 供: 'gong', 氧: 'yang', 呼: 'hu', 吸: 'xi', 心: 'xin', 电: 'dian',
  除: 'chu', 颤: 'chan', 起: 'qi', 搏: 'bo', 注: 'zhu', 射: 'she', 液: 'ye',
  换: 'huan', 缝: 'feng', 合: 'he', 清: 'qing', 创: 'chuang', 止: 'zhi', 包: 'bao',
  扎: 'za', 固: 'gu', 定: 'ding', 复: 'fu', 苏: 'su', 评: 'ping', 估: 'gu',
  观: 'guan', 察: 'cha', 巡: 'xun', 视: 'shi', 宣: 'xuan', 教: 'jiao', 咨: 'zi',
  询: 'xun', 满: 'man', 意: 'yi', 度: 'du', 投: 'tou', 诉: 'su', 建: 'jian',
  议: 'yi', 反: 'fan', 馈: 'kui', 改: 'gai', 进: 'jin', 质: 'zhi', 感: 'gan',
  疗: 'liao', 安: 'an', 全: 'quan', 不: 'bu', 良: 'liang', 事: 'shi', 件: 'jian',
  风: 'feng', 险: 'xian', 预: 'yu', 警: 'jing', 演: 'yan', 练: 'lian', 继: 'ji',
  续: 'xu', 学: 'xue', 科: 'ke', 研: 'yan', 带: 'dai', 修: 'xiu', 实: 'shi',
  习: 'xi', 规: 'gui', 专: 'zhuan', 基: 'ji', 地: 'di', 设: 'she', 点: 'dian',
  临: 'lin', 床: 'chuang', 路: 'lu', 径: 'jing', 种: 'zhong', 控: 'kong', 降: 'jiang',
  本: 'ben', 增: 'zeng', 运: 'yun', 营: 'ying', 析: 'xi', 决: 'jue', 策: 'ce',
  持: 'chi', 大: 'da', 屏: 'ping', 可: 'ke', 视: 'shi', 化: 'hua', 报: 'bao',
  表: 'biao', 仪: 'yi', 盘: 'pan', 菜: 'cai', 单: 'dan', 通: 'tong', 告: 'gao',
  归: 'gui', 档: 'dang', 计: 'ji', 志: 'zhi', 新: 'xin', 老: 'lao', 年: 'nian',
  儿: 'er', 童: 'tong', 妇: 'fu', 产: 'chan', 精: 'jing', 神: 'shen', 皮: 'pi',
  肤: 'fu', 骨: 'gu', 外: 'wai', 眼: 'yan', 耳: 'er', 鼻: 'bi', 喉: 'hou',
  口: 'kou', 腔: 'qiang', 胸: 'xiong', 腹: 'fu', 中: 'zhong', 西: 'xi', 合: 'he',
  康: 'kang', 复: 'fu', 绿: 'lv', 色: 'se', 道: 'dao'
}
function toAsciiName(name) {
  if (!name) return 'MyApp'
  const converted = String(name)
    .split('')
    .map((c) => PINYIN_MAP[c] || c)
    .join('')
  const safe = converted.replace(/[^a-zA-Z0-9_-]/g, '')
  return safe.replace(/^-+|-+$/g, '') || 'MyApp'
}

// 从用户原始需求中提取一个像样的项目英文名，避免「主方案 2」变成 fanganfenzhi-2
export function extractProjectName(sceneHint = '') {
  const text = String(sceneHint || '')
  // 画板 / CAD / 图纸 / 制图 / 钢构设计 / 钢结构工具 等一律优先命中，避免 branchName 污染
  const hasDraw = /(画|绘图|画图|画布|涂鸦|手绘|canvas|draw|sketch|paint|白板|画板|图纸|绘制|制图|作图|出图|cad|设计图|施工图|平面图|立面图|效果图|蓝图|草图|描图)/i.test(text)
  const hasSteel = /(钢结构|钢构|steel|型材|铝材|构件|配筋|bim)/i.test(text)
  if (hasDraw || hasSteel) {
    if (/(led|显示屏|大屏|屏幕|display)/i.test(text) && /(钢|结构|structure|构件|图纸|draw|cad)/i.test(text)) return 'led-steel-cad'
    if (/(led|显示屏|大屏|屏幕|display)/i.test(text)) return 'led-cad'
    if (/(钢|结构|structure|构件|图纸|draw|cad)/i.test(text)) return 'steel-cad'
    return 'drawing-board'
  }
  if (/游戏|game|贪吃蛇|俄罗斯方块|扫雷|飞机/i.test(text)) return 'mini-game'
  // 工具类：优先按具体工具命项目名，避免被通用中文名词污染
  if (/局域网|lan|内网|网络扫描|设备扫描|端口扫描|ip.scan|mac.scan|主机探测/i.test(text)) return 'lan-scanner'
  if (/计算器|calc/i.test(text)) return 'calculator'
  if (/二维码|qrcode|条码|barcode/i.test(text)) return 'qr-tool'
  if (/json|格式化|yaml|xml|toml/i.test(text)) return 'json-tool'
  if (/密码生成|随机密码|password/i.test(text)) return 'password-generator'
  if (/uuid|guid/i.test(text)) return 'uuid-generator'
  if (/base64|md5|sha|hash|编码|解码|codec/i.test(text)) return 'codec-tool'
  if (/单位换算|汇率|转换器|converter/i.test(text)) return 'converter'
  if (/颜色选择|取色|color.picker|调色板/i.test(text)) return 'color-picker'
  if (/医院|his|oa|政务|办公|公文|审批/i.test(text)) return 'oa-admin'
  if (/商城|电商|购物|store|shop/i.test(text)) return 'shop-admin'
  if (/仓库|库存|仓储|wms/i.test(text)) return 'wms-admin'
  if (/crm|客户|销售/i.test(text)) return 'crm-admin'
  // 从需求语句里提取第一个中文名词并转拼音
  const words = text.split(/[\s，。！？、]+/).filter(Boolean)
  for (const w of words) {
    if (w.length >= 2 && /[\u4e00-\u9fa5]/.test(w)) {
      const pinyin = toAsciiName(w).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '')
      if (pinyin && pinyin.length >= 3 && !/^fangan(fenzhi)?$/.test(pinyin) && !/^zhu(fangan)?$/.test(pinyin)) return pinyin
    }
  }
  return ''
}

export function generateProjectFiles({ name = 'MyApp', doc = '', sceneHint = '' } = {}) {
  const appType = detectAppType(sceneHint, doc)
  // 项目名优先从 sceneHint 提取，其次从 doc 提取，最后才用传入的 name（避免 branchName/旧 pack.name 污染）
  const derivedName = extractProjectName(sceneHint) || extractProjectName(doc) || name
  const safe = toAsciiName(derivedName)
  const entities = parseDbTables(doc, sceneHint || safe)
  const files = [
    ...backendFiles(safe, entities),
    ...webFiles(safe, entities, appType, sceneHint),
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

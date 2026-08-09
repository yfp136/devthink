// 真实服务器监控采集器（PRD 二期「部署+监控」）
// 通过 runtime.ssh（Electron 桌面端 ssh2）在服务器本地一次性采集
// CPU / 内存 / 磁盘 / Nginx / 站点 HTTP 状态，并解析为结构化指标。
// 浏览器预览环境无 ssh，调用方需先判断 runtime.ssh 是否存在再调用。

// 告警阈值：命中即产生告警。可调。
export const THRESHOLDS = {
  cpu: 85, // CPU 使用率 %
  mem: 90, // 内存使用率 %
  disk: 90 // 根分区使用率 %
}

// 构造一次 SSH 往返即可拿全的采集脚本。
// 用 ===TAG=== 分隔各段，便于本地解析；任一命令失败都不影响其它段（|| true）。
function buildCommand(siteUrl) {
  const site = siteUrl || 'http://127.0.0.1'
  return [
    'echo ===HOST===',
    'hostname; uname -srm',
    'echo ===UPTIME===',
    "uptime | sed 's/^ *//'",
    'echo ===CPU===',
    "top -bn1 2>/dev/null | grep '%Cpu' | head -1 || echo NO_TOP",
    'echo ===MEM===',
    "free -m | sed -n '1,2p'",
    'echo ===DISK===',
    'df -h /',
    "df -h /var/www 2>/dev/null || echo NO_WWW",
    'echo ===NGINX===',
    '(systemctl is-active nginx 2>/dev/null || echo unknown)',
    'echo ---conn---',
    "(ss -s 2>/dev/null | grep 'TCP:' || echo NO_SS)",
    'echo ===SITE===',
    `curl -s -o /dev/null -w '%{http_code}' --max-time 5 ${site} 2>/dev/null || echo ERR`
  ].join('\n')
}

// 按标签切出某段原始输出
function section(raw, tag) {
  const parts = raw.split(`===${tag}===`)
  if (parts.length < 2) return ''
  const rest = parts[1]
  const next = rest.indexOf('===', 1)
  return (next >= 0 ? rest.slice(0, next) : rest).trim()
}

function parseMetrics(raw, domain) {
  const hostRaw = section(raw, 'HOST')
  const cpuRaw = section(raw, 'CPU')
  const memRaw = section(raw, 'MEM')
  const diskRaw = section(raw, 'DISK')
  const nginxRaw = section(raw, 'NGINX')
  const uptimeRaw = section(raw, 'UPTIME')
  const siteRaw = section(raw, 'SITE')

  // CPU：top 输出的 idle 百分比 → 使用率 = 100 - idle
  let cpu = null
  const idle = cpuRaw.match(/(\d+(?:\.\d+)?)\s*id/)
  if (idle) cpu = Math.max(0, Math.min(100, 100 - parseFloat(idle[1])))

  // 内存：free -m 第二行 Mem
  let mem = { total: null, used: null, usedPct: null }
  const memLine = memRaw.split('\n').find((l) => l.startsWith('Mem'))
  if (memLine) {
    const c = memLine.split(/\s+/).filter(Boolean)
    const total = parseInt(c[1], 10)
    const used = parseInt(c[2], 10)
    if (!isNaN(total) && total > 0) mem = { total, used, usedPct: Math.round((used / total) * 100) }
  }

  // 磁盘：每段一行挂载点，取 / 与 /var/www
  const disk = { root: null, www: null }
  for (const line of diskRaw.split('\n')) {
    if (!line || line.startsWith('Filesystem') || line.startsWith('NO_WWW')) continue
    const c = line.split(/\s+/).filter(Boolean)
    // Filesystem Size Used Avail Use% Mounted
    const usePct = parseInt((c[4] || '0').replace('%', ''), 10)
    const entry = {
      total: c[1], used: c[2], avail: c[3],
      usedPct: isNaN(usePct) ? null : usePct, mount: c[5]
    }
    if (c[5] === '/') disk.root = entry
    else if (c[5] && c[5].startsWith('/var/www')) disk.www = entry
  }

  // Nginx 状态 + TCP 连接数
  const nginxState = nginxRaw.split('\n')[0].trim()
  const connPart = nginxRaw.split('---conn---')[1] || ''
  let tcpEstab = null
  const tcp = connPart.match(/TCP:\s*\d+\s*\(estab\s*(\d+)/)
  if (tcp) tcpEstab = parseInt(tcp[1], 10)
  const nginx = { state: nginxState, active: nginxState === 'active', tcpEstab }

  // 站点 HTTP 状态（有域名测 https，否则测本地 80）
  const code = (siteRaw.match(/^\d{3}/) || [null])[0]
  const site = {
    url: domain ? `https://${domain}` : 'http://127.0.0.1',
    httpCode: code || null,
    ok: code === '200' || code === '301' || code === '302'
  }

  // 负载
  const load = (uptimeRaw.match(/load average:\s*([\d.]+)/) || [])[1]
  const hostLines = hostRaw.split('\n')

  return {
    host: { name: (hostLines[0] || '').trim(), kernel: (hostLines[1] || '').trim() },
    uptime: uptimeRaw.replace(/\n/g, ' ').trim(),
    load1: load != null ? parseFloat(load) : null,
    cpu,
    mem,
    disk,
    nginx,
    site,
    raw
  }
}

// 采集一次真实指标。返回 { ok, metrics?, error? }
export async function fetchMetrics(server, ssh) {
  if (!ssh) return { ok: false, error: '当前环境不支持真实 SSH（请用桌面端体验）' }
  if (!server.secret) return { ok: false, error: '请先填写密码 / 密钥' }
  const siteUrl = server.domain ? `https://${server.domain}` : 'http://127.0.0.1'
  try {
    const out = await ssh.exec({
      host: server.ip,
      port: server.port || 22,
      username: server.sshUser,
      password: server.authType === 'password' ? server.secret : undefined,
      privateKey: server.authType === 'key' ? server.secret : undefined,
      commands: [buildCommand(siteUrl)]
    })
    if (!out.ok) return { ok: false, error: out.error || 'SSH 执行失败' }
    const metrics = parseMetrics(out.output || '', server.domain)
    metrics.ts = Date.now()
    return { ok: true, metrics }
  } catch (e) {
    return { ok: false, error: e.message }
  }
}

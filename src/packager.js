// 真实打包编排器（PRD 二期「真实打包出包」）。
// 与旧版「模拟进度条」不同：本模块在桌面端（Electron）真正写盘并执行构建命令，
// 产出真实安装包；在浏览器预览环境则明确告知「需桌面端」。
// 工具链缺失的目标（Tauri/Rust、Android SDK）不再假装成功，如实报告缺什么。
import { runtime, PLATFORM } from './runtime.js'

function hostPlatform() {
  // 渲染进程无法用 process.platform 可靠判断，且为跨进程；这里仅用于决定产出格式提示。
  if (typeof navigator !== 'undefined' && /Win/.test(navigator.userAgent)) return 'win'
  if (typeof navigator !== 'undefined' && /Mac/.test(navigator.userAgent)) return 'mac'
  return 'linux'
}

// ---------- 各目标真实构建 ----------

// Web 前端：npm install → vite build（相对 base）→ 打包 dist 为 zip 真出包
async function packWeb({ base, root, safe, pack, log }) {
  const zipCmd =
    hostPlatform() === 'win'
      ? `cd web && powershell -Command "Compress-Archive -Path dist -DestinationPath ../${safe}-web.zip -Force"`
      : `cd web && zip -r "../${safe}-web.zip" dist`
  const steps = [
    { cmd: 'cd web && npm install', label: 'web: npm install（vue / vue-router / axios / vite）' },
    { cmd: 'cd web && npx vite build --base ./', label: 'web: vite build（相对 base，产出 web/dist）' },
    { cmd: zipCmd, label: `web: 打包 dist 为 ${safe}-web.zip` }
  ]
  const r = await runtime.shell.run({ cwd: root, steps })
  const artifact = `${root}/${safe}-web.zip`
  if (!r.ok) {
    return { target: 'Web 前端（静态站点 / 可部署包）', status: 'error', log: r.output, artifacts: [], error: '构建失败' }
  }
  log.push('Web 打包完成 → ' + artifact)
  return {
    target: 'Web 前端（静态站点 / 可部署包）',
    status: 'done',
    log: r.output,
    artifacts: [{ name: `${safe}-web.zip`, path: artifact }],
    error: null
  }
}

// 桌面端（Electron）：生成真实 Electron 包裹工程，用 electron-builder 真出安装包（dmg/exe/AppImage）
async function packElectronDesktop({ base, root, safe, pack, log }) {
  const ver = pack.version || '1.0.0'
  const plat = hostPlatform()
  // 检测 electron-builder 是否可解析
  const probe = await runtime.shell.run({
    cwd: root,
    steps: [`cd electron-desktop 2>/dev/null && node -e "try{require.resolve('electron-builder');console.log('HAVE_EB')}catch(e){console.log('NO_EB')}"`]
  })
  const hasEB = probe.output && probe.output.includes('HAVE_EB')
  if (!hasEB) {
    return {
      target: '桌面端（Electron 安装包）',
      status: 'skipped',
      log: '未检测到 electron-builder。在桌面端工程执行 `npm i -D electron electron-builder` 后，即可真出 macOS dmg / Windows exe / Linux AppImage（首次会下载 Electron 二进制，约 150MB，需联网）。',
      artifacts: [],
      error: '缺少 electron-builder'
    }
  }
  const mainJs = `const { app, BrowserWindow } = require('electron')
const path = require('path')
function create() {
  const win = new BrowserWindow({ width: 1100, height: 720, webPreferences: { nodeIntegration: false, contextIsolation: true } })
  win.loadFile(path.join(__dirname, '../web/dist/index.html'))
}
app.whenReady().then(create)
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit() })
`
  const pkg = {
    name: `${safe}-desktop`,
    version: ver,
    main: 'main.js',
    scripts: { start: 'electron .', dist: 'electron-builder' },
    devDependencies: { electron: '^31.0.0', 'electron-builder': '^24.0.0' },
    build: {
      appId: `com.devthink.${safe.replace(/-/g, '')}`,
      productName: safe,
      files: ['main.js', 'web/dist/**/*'],
      directories: { output: 'dist-out' }
    }
  }
  const wrapFiles = [
    { path: 'main.js', content: mainJs },
    { path: 'package.json', content: JSON.stringify(pkg, null, 2) }
  ]
  const written = await runtime.fs.writeFiles({ base: `${base}/electron-desktop`, files: wrapFiles })
  if (!written.ok) return { target: '桌面端（Electron 安装包）', status: 'error', log: '写入 Electron 包裹工程失败', artifacts: [], error: '写盘失败' }
  const targetFlag = plat === 'mac' ? '--mac' : plat === 'win' ? '--win' : '--linux'
  const r = await runtime.shell.run({
    cwd: written.base,
    steps: [
      { cmd: 'npm install', label: 'electron-desktop: npm install（electron + electron-builder，首次约 150MB）' },
      { cmd: `npx electron-builder ${targetFlag} --publish=never`, label: `electron-desktop: electron-builder 打包（${plat}）` }
    ]
  })
  if (!r.ok) return { target: '桌面端（Electron 安装包）', status: 'error', log: r.output, artifacts: [], error: '打包失败' }
  log.push('Electron 桌面端打包完成 → ' + written.base + '/dist-out')
  return {
    target: '桌面端（Electron 安装包）',
    status: 'done',
    log: r.output,
    artifacts: [{ name: `${safe}-desktop-${plat}`, path: `${written.base}/dist-out` }],
    error: null
  }
}

// Tauri 桌面端：检测 Rust/tauri-cli，缺则如实说明（不再假装）
async function packTauri({ root, safe, log }) {
  const probe = await runtime.shell.run({ cwd: root, steps: ['command -v cargo >/dev/null 2>&1 && echo HAVE_RUST || echo NO_RUST'] })
  const hasRust = probe.output && probe.output.includes('HAVE_RUST')
  if (!hasRust) {
    return {
      target: '桌面端（Tauri，轻量离线单机）',
      status: 'skipped',
      log: '未检测到 Rust 工具链（cargo）。Tauri 需安装 Rust + Tauri CLI 才能真出原生桌面包。安装：https://tauri.app/start/ 。当前生成工程已含 desktop/ Tauri 脚手架，装好工具链后执行 `cd desktop && npm run tauri build` 即可。',
      artifacts: [],
      error: '缺少 Rust 工具链'
    }
  }
  return { target: '桌面端（Tauri）', status: 'skipped', log: 'Rust 已具备；Tauri 构建较重，建议在终端手动执行 `cd desktop && npm run tauri build`。', artifacts: [], error: null }
}

// Android：检测 gradle/SDK，缺则如实说明
async function packAndroid({ root, safe, log }) {
  const probe = await runtime.shell.run({ cwd: root, steps: ['command -v gradle >/dev/null 2>&1 && echo HAVE_GRADLE || echo NO_GRADLE'] })
  const hasGradle = probe.output && probe.output.includes('HAVE_GRADLE')
  if (!hasGradle) {
    return {
      target: '移动端（Android APK）',
      status: 'skipped',
      log: '未检测到 Gradle / Android SDK。生成工程已含 mobile/android 标准 Gradle 工程，需用 Android Studio 打开并配置 SDK 后 `./gradlew assembleRelease` 真出 APK（受 PRD 5.2.5 约束：正式包需 release keystore 签名）。',
      artifacts: [],
      error: '缺少 Android SDK'
    }
  }
  return { target: '移动端（Android APK）', status: 'skipped', log: 'Gradle 已具备；建议在 Android Studio 中 `./gradlew assembleRelease`。', artifacts: [], error: null }
}

// ---------- 主入口 ----------
export async function packProject({ files, pack }) {
  const safe = (pack.name || 'MyApp').replace(/[^\w-]/g, '') || 'MyApp'
  const base = `pack/${safe}`
  const results = []
  const log = []

  if (PLATFORM !== 'electron') {
    return {
      platform: 'browser',
      ok: false,
      message: '真实打包需在 DevThink 桌面端（Electron）中运行；当前为浏览器预览，仅展示流程。请在桌面端打开本工程后重试「一键打包」。',
      results: []
    }
  }

  // 1) 写工程到磁盘
  const written = await runtime.fs.writeFiles({ base, files })
  if (!written.ok) return { platform: 'electron', ok: false, log: '写入工程文件失败', results }
  const root = written.base
  log.push('工程已写入：' + root)

  // 2) 各目标真实打包
  results.push(await packWeb({ base, root, safe, pack, log }))
  results.push(await packElectronDesktop({ base, root, safe, pack, log }))
  results.push(await packTauri({ root, safe, log }))
  results.push(await packAndroid({ root, safe, log }))

  const ok = results.some((r) => r.status === 'done')
  return { platform: 'electron', ok, log: log.join('\n'), results }
}

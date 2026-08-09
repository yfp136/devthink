// 本地文件读取：把客户提供的文档转成纯文本，作为上下文交给大模型分析。
// 在浏览器与 Electron 渲染进程（都是 Chromium）中统一运行，无需主进程介入。
import mammoth from 'mammoth'
// pdfjs 的 worker 用 Vite 的 ?url 导入，打包后仍可正确解析
import PdfWorker from 'pdfjs-dist/build/pdf.worker.min.mjs?url'

// 直接按文本读取的扩展名（业务文档、代码、数据）
const TEXT_EXT = new Set([
  'txt', 'md', 'markdown', 'csv', 'tsv', 'json', 'js', 'ts', 'jsx', 'tsx', 'vue',
  'py', 'java', 'go', 'c', 'cpp', 'h', 'hpp', 'sh', 'bat', 'ps1', 'yml', 'yaml',
  'xml', 'html', 'htm', 'css', 'scss', 'sql', 'log', 'env', 'ini', 'toml', 'gitignore'
])
const IMAGE_EXT = new Set(['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp'])

// 文本上下文上限：约 20 万字符（~200KB），超出截断避免超出模型上下文
const MAX_CHARS = 200000

function extOf(name) {
  const m = String(name || '').toLowerCase().match(/\.([a-z0-9]+)$/)
  return m ? m[1] : ''
}

async function readDocx(file) {
  const buf = await file.arrayBuffer()
  const res = await mammoth.extractRawText({ arrayBuffer: buf })
  return res.value || ''
}

let _pdfjs = null
async function getPdfjs() {
  if (_pdfjs) return _pdfjs
  const lib = await import('pdfjs-dist')
  lib.GlobalWorkerOptions.workerSrc = PdfWorker
  _pdfjs = lib
  return _pdfjs
}

async function readPdf(file) {
  const buf = await file.arrayBuffer()
  const pdfjsLib = await getPdfjs()
  const doc = await pdfjsLib.getDocument({ data: buf }).promise
  let text = ''
  for (let i = 1; i <= doc.numPages; i++) {
    const page = await doc.getPage(i)
    const content = await page.getTextContent()
    text += (content.items || []).map((it) => it.str || '').join(' ') + '\n'
  }
  return text
}

// 读取单个文件为文本。
// 返回 { name, size, type, text, truncated?, image?, unsupported? }
export async function readFileAsText(file, maxChars = MAX_CHARS) {
  const name = file.name || '未命名文件'
  const ext = extOf(name)
  const size = file.size || 0

  if (IMAGE_EXT.has(ext)) {
    return { name, size, type: 'image', text: '', image: true, note: '图片暂不支持文本分析（如需识别图文，请改用支持视觉的多模态模型）' }
  }

  try {
    if (TEXT_EXT.has(ext) || !ext) {
      let text = await file.text()
      if (text.length > maxChars) {
        return { name, size, type: 'text', text: text.slice(0, maxChars) + `\n\n…[内容过长，已截断至前 ${maxChars} 字符]…`, truncated: true }
      }
      return { name, size, type: 'text', text }
    }
    if (ext === 'docx') {
      const text = await readDocx(file)
      if (text.length > maxChars) {
        return { name, size, type: 'docx', text: text.slice(0, maxChars) + `\n\n…[内容过长，已截断至前 ${maxChars} 字符]…`, truncated: true }
      }
      return { name, size, type: 'docx', text }
    }
    if (ext === 'pdf') {
      const text = await readPdf(file)
      if (!text.trim()) {
        return { name, size, type: 'pdf', text: '', unsupported: true, note: '该 PDF 未包含可提取的文本内容（可能是扫描件/纯图片），无法分析。' }
      }
      if (text.length > maxChars) {
        return { name, size, type: 'pdf', text: text.slice(0, maxChars) + `\n\n…[内容过长，已截断至前 ${maxChars} 字符]…`, truncated: true }
      }
      return { name, size, type: 'pdf', text }
    }
  } catch (e) {
    return { name, size, type: ext, text: '', unsupported: true, note: '解析失败：' + (e && e.message ? e.message : String(e)) }
  }

  // 未知类型：尽量当文本读
  try {
    return { name, size, type: 'unknown', text: await file.text() }
  } catch {
    return { name, size, type: 'unknown', text: '', unsupported: true, note: '暂不支持该文件类型（可尝试导出为 .txt / .md / .pdf / .docx 后上传）。' }
  }
}

// 把多个附件拼成喂给模型的上下文块
export function buildContext(attachments) {
  const valid = (attachments || []).filter((a) => a && a.text && a.text.trim())
  if (!valid.length) return ''
  const blocks = valid.map((a) => {
    const tag = a.truncated ? '（已截断）' : ''
    return `<<上传文档: ${a.name}${tag}>>\n${a.text}\n<</上传文档>>`
  })
  return '以下是用户上传的本地文件内容，请结合其进行分析：\n\n' + blocks.join('\n\n')
}

export function formatSize(bytes) {
  if (!bytes && bytes !== 0) return ''
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / 1024 / 1024).toFixed(1) + ' MB'
}

// 读取图片为 dataURL（base64），用于「微信式截图 / 图片消息」在对话里直接展示。
// 与 readFileAsText 不同：这里不抽取文本，而是保留整图供前端 <img> 渲染。
// 注意：当前默认模型为纯文本，图片仅作展示；配置了多模态模型 + runtime 改造后可让 AI 读图。
const MAX_IMAGE_BYTES = 12 * 1024 * 1024
export async function readImage(file) {
  const name = file.name || '截图.png'
  const size = file.size || 0
  if (size > MAX_IMAGE_BYTES) {
    return { name, size, type: 'image', unsupported: true, note: '图片过大（>12MB），请压缩后再发送' }
  }
  const dataUrl = await new Promise((resolve, reject) => {
    const fr = new FileReader()
    fr.onload = () => resolve(fr.result)
    fr.onerror = () => reject(fr.error || new Error('读取图片失败'))
    fr.readAsDataURL(file)
  })
  return { name, size, type: 'image', dataUrl }
}

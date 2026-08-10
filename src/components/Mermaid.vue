<script setup>
import { ref, onMounted, watch } from 'vue'
import mermaid from 'mermaid'

mermaid.initialize({ startOnLoad: false, theme: 'default', securityLevel: 'loose' })

const props = defineProps({ code: { type: String, required: true } })
const el = ref(null)
let seq = 0

function escapeHtml(text) {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;')
}

async function draw() {
  if (!el.value) return
  const id = 'mmd-' + Date.now() + '-' + seq++
  try {
    const { svg } = await mermaid.render(id, props.code)
    // mermaid 11 在语法错误时不会抛错，而是 resolve 并返回带炸弹图的错误 SVG
    if (svg && (svg.includes('Syntax error') || svg.includes('Parse error'))) {
      throw new Error('Mermaid 语法错误')
    }
    el.value.innerHTML = svg
  } catch (e) {
    el.value.innerHTML =
      '<div style="color:#dc2626;padding-bottom:8px">Mermaid 图表解析失败：' +
      escapeHtml(String(e?.message || e)) +
      '</div><pre style="background:#f8fafc;border:1px solid #e2e8f0;border-radius:6px;padding:10px;overflow:auto">' +
      escapeHtml(props.code) +
      '</pre>'
  }
}
onMounted(draw)
watch(() => props.code, draw)
</script>

<template>
  <div class="mermaid" ref="el"></div>
</template>

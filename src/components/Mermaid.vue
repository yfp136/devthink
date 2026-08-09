<script setup>
import { ref, onMounted, watch } from 'vue'
import mermaid from 'mermaid'

mermaid.initialize({ startOnLoad: false, theme: 'default', securityLevel: 'loose' })

const props = defineProps({ code: { type: String, required: true } })
const el = ref(null)
let seq = 0

async function draw() {
  if (!el.value) return
  const id = 'mmd-' + Date.now() + '-' + seq++
  try {
    const { svg } = await mermaid.render(id, props.code)
    el.value.innerHTML = svg
  } catch (e) {
    el.value.innerHTML = '<pre style="color:#dc2626">Mermaid 解析失败：' + (e?.message || e) + '</pre>'
  }
}
onMounted(draw)
watch(() => props.code, draw)
</script>

<template>
  <div class="mermaid" ref="el"></div>
</template>

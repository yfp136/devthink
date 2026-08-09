<script setup>
import { computed } from 'vue'
import { marked } from 'marked'
import DOMPurify from 'dompurify'
import Mermaid from './Mermaid.vue'

const props = defineProps({ source: { type: String, default: '' } })

marked.setOptions({ breaks: true, gfm: true })

function parse(src) {
  const lines = src.split('\n')
  const out = []
  let buf = []
  const flush = () => {
    if (buf.length) {
      out.push({ type: 'md', content: buf.join('\n') })
      buf = []
    }
  }
  let i = 0
  while (i < lines.length) {
    const m = lines[i].match(/^```(\w*)\s*$/)
    if (m) {
      flush()
      const lang = m[1] || ''
      const code = []
      i++
      while (i < lines.length && !/^```\s*$/.test(lines[i])) {
        code.push(lines[i])
        i++
      }
      i++ // skip closing fence
      out.push({ type: lang === 'mermaid' ? 'mermaid' : 'code', content: code.join('\n'), lang })
    } else {
      buf.push(lines[i])
      i++
    }
  }
  flush()
  return out
}

const segments = computed(() => parse(props.source || ''))
function mdHtml(s) {
  return DOMPurify.sanitize(marked.parse(s))
}
</script>

<template>
  <div class="markdown">
    <template v-for="(seg, idx) in segments" :key="idx">
      <Mermaid v-if="seg.type === 'mermaid'" :code="seg.content" />
      <pre v-else-if="seg.type === 'code'"><code>{{ seg.content }}</code></pre>
      <div v-else v-html="mdHtml(seg.content)"></div>
    </template>
  </div>
</template>

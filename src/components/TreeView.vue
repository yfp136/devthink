<script setup>
defineProps({ node: { type: Object, required: true }, depth: { type: Number, default: 0 } })
const emit = defineEmits(['open'])
</script>

<template>
  <div class="indent">
    <div
      :class="node.type === 'dir' ? 'dir' : 'file'"
      :style="node.type === 'file' ? 'cursor:pointer' : ''"
      @click="node.type === 'file' && emit('open', node)"
    >
      {{ node.type === 'dir' ? '📁' : '📄' }} {{ node.name }}
    </div>
    <TreeView
      v-for="c in node.children || []"
      :key="c.name"
      :node="c"
      :depth="depth + 1"
      @open="$emit('open', $event)"
    />
  </div>
</template>

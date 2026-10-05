<script setup lang="ts">
defineProps<{ visible: boolean; label: string }>();
const emit = defineEmits<{ (e: 'toggle'): void }>();
</script>

<template>
  <button class="hardware-visibility" type="button" :aria-label="label" :title="label" :aria-pressed="visible" @click="emit('toggle')">
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
      <path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12Z" />
      <circle cx="12" cy="12" r="3" />
      <path v-if="!visible" d="m3 3 18 18" />
    </svg>
  </button>
</template>

<style scoped>
.hardware-visibility { display: inline-flex; align-items: center; justify-content: center; width: 32px; height: 32px; padding: 0; border: 1px solid transparent; border-radius: 6px; background: transparent; color: var(--text-secondary); cursor: pointer; flex-shrink: 0; }
.hardware-visibility:hover { color: var(--text-primary); background: var(--surface-hover); }
.hardware-visibility:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
</style>

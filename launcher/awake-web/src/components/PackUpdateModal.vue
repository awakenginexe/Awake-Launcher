<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue';
import type { InstalledPack } from '../features/library/packUpdates.ts';
import type { PackVersion } from '../bridge/catalog.ts';
import type { MessageKey } from '../i18n/catalogs.ts';

const props = defineProps<{ pack: InstalledPack; version: PackVersion; decide: (choice: 'update' | 'skip' | 'skipVersion' | 'disable') => Promise<void>; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (event: 'close'): void }>();
const dialog = ref<HTMLElement>();
const working = ref(false);
const error = ref('');
const previousFocus = document.activeElement as HTMLElement | null;
const background = new Map<HTMLElement, boolean>();
onMounted(() => {
  for (const child of dialog.value?.closest('main')?.children ?? []) {
    if (child instanceof HTMLElement && !child.contains(dialog.value!)) { background.set(child, child.inert); child.inert = true; }
  }
  dialog.value?.querySelector<HTMLButtonElement>('.btn-primary')?.focus();
});
onUnmounted(() => {
  background.forEach((inert, element) => { element.inert = inert; });
  if (previousFocus?.isConnected) previousFocus.focus();
});
function close() { if (!working.value) emit('close'); }
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close(); }
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled)') ?? [])];
  const first = controls[0], last = controls.at(-1);
  if (!controls.includes(document.activeElement as HTMLElement)) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
  else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
async function choose(choice: 'update' | 'skip' | 'skipVersion' | 'disable') {
  if (working.value) return;
  working.value = true; error.value = '';
  try { await props.decide(choice); }
  catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { working.value = false; }
}
</script>

<template>
  <div class="modal-overlay" @click.self="close" @keydown="keydown">
    <section ref="dialog" class="modal-dialog pack-update-dialog" role="dialog" aria-modal="true" aria-labelledby="pack-update-title" :aria-busy="working">
      <header class="modal-header"><h2 id="pack-update-title">{{ t('packUpdateAvailable') }}</h2><button class="modal-close-btn" :disabled="working" :aria-label="t('close')" @click="close">✕</button></header>
      <div class="modal-body">
        <h3>{{ pack.name }}</h3>
        <p>{{ t('packInstalled') }}: {{ pack.versionName || pack.versionId }}</p>
        <p>{{ t('packAvailable') }}: {{ version.name }}</p>
        <p class="setting-hint">{{ [version.minecraft, version.loader].filter(Boolean).join(' · ') }}</p>
        <p v-if="error" role="alert">{{ error }}</p>
      </div>
      <footer class="modal-footer">
        <button class="btn-primary" :disabled="working" @click="choose('update')">{{ t('packUpdate') }}</button>
        <button class="btn-secondary" :disabled="working" @click="choose('skip')">{{ t('packSkipTime') }}</button>
        <button class="btn-secondary" :disabled="working" @click="choose('skipVersion')">{{ t('packSkipVersion') }}</button>
        <button class="btn-secondary" :disabled="working" @click="choose('disable')">{{ t('packDoNotRemind') }}</button>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.pack-update-dialog { max-width: 640px; }
h2 { margin: 0; font-size: 1rem; }
h3 { overflow-wrap: anywhere; }
.modal-footer { flex-wrap: wrap; }
</style>

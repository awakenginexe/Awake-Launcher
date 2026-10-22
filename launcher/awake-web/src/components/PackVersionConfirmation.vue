<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';

const props = defineProps<{ confirmation: { instanceName: string; versionName: string; warning: string }; decide: (confirmed: boolean) => Promise<void>; t: (key: MessageKey) => string }>();
const dialog = ref<HTMLElement>();
const working = ref(false);
const error = ref('');
const previousFocus = document.activeElement as HTMLElement | null;
const background = new Map<HTMLElement, boolean>();
onMounted(() => {
  for (const child of dialog.value?.closest('main')?.children ?? []) {
    if (child instanceof HTMLElement && !child.contains(dialog.value!)) { background.set(child, child.inert); child.inert = true; }
  }
  dialog.value?.querySelector<HTMLButtonElement>('.btn-secondary')?.focus();
});
onUnmounted(() => {
  background.forEach((inert, element) => { element.inert = inert; });
  if (previousFocus?.isConnected) previousFocus.focus();
});
async function choose(confirmed: boolean) {
  if (working.value) return;
  working.value = true; error.value = '';
  try { await props.decide(confirmed); }
  catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { working.value = false; }
}
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); void choose(false); }
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled)') ?? [])];
  const first = controls[0], last = controls.at(-1);
  if (!controls.includes(document.activeElement as HTMLElement)) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
  else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
</script>

<template>
  <div class="modal-overlay" @click.self="choose(false)" @keydown="keydown">
    <section ref="dialog" class="modal-dialog pack-version-confirmation" role="alertdialog" aria-modal="true" aria-labelledby="pack-confirm-title" aria-describedby="pack-confirm-warning" :aria-busy="working">
      <header class="modal-header">
        <h2 id="pack-confirm-title">{{ t('packChangeVersion') }}</h2>
        <button class="modal-close-btn" type="button" :disabled="working" :aria-label="t('close')" @click="choose(false)">✕</button>
      </header>
      <div class="modal-body">
        <h3>{{ confirmation.instanceName }}</h3>
        <p class="pack-confirm-version">{{ t('packVersion') }}: <strong>{{ confirmation.versionName }}</strong></p>
        <p id="pack-confirm-warning">{{ confirmation.warning }}</p>
        <p v-if="error" role="alert">{{ error }}</p>
      </div>
      <footer class="modal-footer">
        <button class="btn-secondary" type="button" :disabled="working" @click="choose(false)">{{ t('cancel') }}</button>
        <button class="btn-primary" type="button" :disabled="working" @click="choose(true)">{{ t(working ? 'working' : 'packChangeVersion') }}</button>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.pack-version-confirmation { max-width: 560px; }
h2 { margin: 0; font-size: 1rem; }
h3 { margin: 0 0 10px; font-size: 1.15rem; }
h3, p { overflow-wrap: anywhere; }
.pack-confirm-version { color: var(--text-secondary); }
.pack-confirm-version strong { color: var(--text-primary); }
#pack-confirm-warning { margin: 20px 0 0; line-height: 1.65; white-space: pre-line; }
</style>

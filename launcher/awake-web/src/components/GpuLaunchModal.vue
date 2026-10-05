<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import type { GpuSettings } from '../features/library/hardware.ts';
import GpuPicker from './GpuPicker.vue';
import type { GpuService } from './GpuPicker.vue';

const props = defineProps<{ settings: GpuSettings; service: GpuService; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (e: 'close'): void; (e: 'continue'): void }>();
const dialog = ref<HTMLElement>();
const mode = ref(props.settings.mode);
const saving = ref(false);
const error = ref('');
const previousFocus = document.activeElement as HTMLElement | null;
const background = new Map<HTMLElement, boolean>();
onMounted(() => {
  for (const child of dialog.value?.closest('main')?.children ?? []) {
    if (child instanceof HTMLElement && !child.contains(dialog.value!)) { background.set(child, child.inert); child.inert = true; }
  }
  dialog.value?.querySelector<HTMLButtonElement>('.modal-close-btn')?.focus();
});
onUnmounted(() => {
  background.forEach((inert, element) => { element.inert = inert; });
  if (previousFocus?.isConnected) previousFocus.focus();
});
function close() { if (!saving.value) emit('close'); }
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close(); }
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled), select:not(:disabled)') ?? [])];
  const first = controls[0], last = controls.at(-1);
  if (!controls.includes(document.activeElement as HTMLElement)) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
  else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
async function save() {
  if (saving.value) return;
  saving.value = true; error.value = '';
  try { await props.service.select(mode.value); emit('continue'); }
  catch (failure) { error.value = failure instanceof Error ? failure.message : String(failure); }
  finally { saving.value = false; }
}
</script>

<template>
  <div class="modal-overlay" @click.self="close" @keydown="keydown">
    <section ref="dialog" class="modal-dialog gpu-launch-dialog" role="dialog" aria-modal="true" aria-labelledby="gpu-launch-title" :aria-busy="saving">
      <header class="modal-header">
        <div class="modal-title-group"><h2 id="gpu-launch-title">{{ t('gpuLaunchTitle') }}</h2></div>
        <button class="modal-close-btn" type="button" :disabled="saving" :aria-label="t('close')" @click="close">✕</button>
      </header>
      <div class="modal-body">
        <p class="setting-hint">{{ t('gpuLaunchHint') }}</p>
        <fieldset :disabled="saving">
          <GpuPicker :initial="settings" :service="service" :t="t" deferred @choose="mode = $event" />
        </fieldset>
        <p v-if="error" role="alert">{{ error }}</p>
      </div>
      <footer class="modal-footer">
        <button class="btn-secondary" type="button" :disabled="saving" @click="close">{{ t('cancel') }}</button>
        <button class="btn-primary" type="button" :disabled="saving" @click="save">{{ t(saving ? 'working' : 'gpuSaveContinue') }}</button>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.gpu-launch-dialog { max-width: 560px; }
fieldset { border: 0; margin: 0; padding: 0; min-width: 0; }
.modal-title-group h2 { margin: 0; font-size: 1rem; }
</style>

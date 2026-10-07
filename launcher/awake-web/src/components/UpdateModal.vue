<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import type { UpdateState } from '../features/library/model.ts';
import type { MessageKey } from '../i18n/catalogs.ts';

const props = defineProps<{ state: UpdateState; busy: boolean; embedded?: boolean; t: (key: MessageKey) => string }>();
const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'check'): void;
  (e: 'download', kind: 'setup' | 'portable' | 'release'): void;
  (e: 'automatic', enabled: boolean): void;
}>();
const dialog = ref<HTMLElement>();
const previousFocus = document.activeElement as HTMLElement | null;
const updating = computed(() => ['downloading', 'installing'].includes(props.state.status));
const title = computed(() => props.t(props.state.status === 'downloading' ? 'updateDownloading' : props.state.status === 'installing' ? 'updateInstalling' : props.state.status === 'available' ? 'updateAvailable' : props.state.status === 'checking' ? 'updateChecking' : props.state.status === 'upToDate' ? 'updateCurrent' : props.state.status === 'error' ? 'updateFailed' : 'updatesTitle'));
const background = new Map<HTMLElement, boolean>();
onMounted(() => {
  if (props.embedded) return;
  for (const child of dialog.value?.closest('main')?.children ?? []) {
    if (child instanceof HTMLElement && !child.contains(dialog.value!)) { background.set(child, child.inert); child.inert = true; }
  }
  dialog.value?.querySelector<HTMLButtonElement>('.modal-close-btn')?.focus();
});
watch(() => props.state.status, async () => {
  await nextTick();
  const active = document.activeElement;
  if (!dialog.value?.contains(active) || (active instanceof HTMLButtonElement && active.disabled)) dialog.value?.querySelector<HTMLButtonElement>('.modal-close-btn')?.focus();
});
onUnmounted(() => {
  if (props.embedded) return;
  background.forEach((inert, element) => { element.inert = inert; });
  if (previousFocus?.isConnected) previousFocus.focus();
  else document.querySelector<HTMLButtonElement>('.library-header button')?.focus();
});
function trapFocus(event: KeyboardEvent) {
  if (props.embedded) return;
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), summary') ?? [])];
  const first = controls[0], last = controls.at(-1);
  if (!controls.includes(document.activeElement as HTMLElement)) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
  else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
</script>

<template>
  <div :class="embedded ? 'update-embedded' : 'modal-overlay'" @click.self="!embedded && emit('close')" @keydown="trapFocus">
    <section ref="dialog" class="modal-dialog update-dialog" :role="embedded ? 'group' : 'dialog'" :aria-modal="embedded ? undefined : true" :aria-label="title">
      <header v-if="!embedded" class="modal-header">
        <span class="update-product">Awake Launcher</span>
        <button class="modal-close-btn" :aria-label="t('close')" @click="emit('close')">✕</button>
      </header>
      <div class="modal-body update-body">
        <div class="update-heading" role="status" aria-live="polite">
          <svg class="update-icon" :class="{ 'is-checking': state.status === 'checking' }" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true">
            <path v-if="state.status === 'upToDate'" d="m5 12 4 4L19 6" />
            <path v-else-if="state.status === 'error'" d="M12 4v10m0 4v2" />
            <template v-else><path d="M20 7v5h-5M4 17v-5h5" /><path d="M6 6a8 8 0 0 1 13 2M18 18A8 8 0 0 1 5 16" /></template>
          </svg>
          <h2>{{ title }}</h2>
          <p v-if="state.status === 'checking'">{{ t('updateCheckingHint') }}</p>
          <p v-else-if="state.status === 'available'">{{ t('updateAvailableHint') }}</p>
          <p v-else-if="state.status === 'upToDate'">{{ t('updateCurrentHint') }}</p>
          <p v-else-if="state.status === 'error'">{{ t('updateFailedHint') }}</p>
          <p v-else-if="state.status === 'unavailable'">{{ t('updateUnsupported') }}</p>
          <p v-else-if="updating">{{ t('updateSetupHint') }}</p>
        </div>
        <div v-if="state.status === 'downloading'" class="update-progress"><progress :value="state.progress" max="100" :aria-label="t('updateDownloading')" /><span>{{ state.progress }}%</span></div>
        <dl v-if="state.currentVersion" class="update-versions">
          <div><dt>{{ t('updateInstalled') }}</dt><dd>{{ state.currentVersion }}</dd></div>
          <div><dt>{{ t('updateLatest') }}</dt><dd>{{ state.latestVersion || t('updateNotChecked') }}</dd></div>
        </dl>
        <details v-if="state.notes" class="update-notes" open>
          <summary>{{ t('updateNotes') }}</summary>
          <p>{{ state.notes }}</p>
        </details>
        <p v-if="state.error && state.status === 'available'" role="alert">{{ state.error }}</p>
        <details v-if="state.status === 'error' && state.error" class="update-notes"><summary>{{ t('technicalDetails') }}</summary><p>{{ state.error }}</p></details>
        <label v-if="state.status !== 'unavailable'" class="update-automatic"><input type="checkbox" :checked="state.automatic" :disabled="busy || updating" @change="emit('automatic', ($event.target as HTMLInputElement).checked)" /><span>{{ t('updateAutomatic') }}</span></label>
        <p v-if="state.status === 'available'" class="update-download-hint">{{ t(state.portable ? 'updatePortableHint' : 'updateSetupHint') }}</p>
      </div>
      <footer class="modal-footer update-footer">
        <button v-if="embedded && state.status === 'available'" class="btn-secondary" :disabled="busy || updating" @click="emit('check')">{{ t('checkForUpdates') }}</button>
        <button v-if="state.status === 'available' && state.hasRelease" class="btn-secondary" :disabled="busy" @click="emit('download', 'release')">{{ t('updateReleasePage') }}</button>
        <button v-if="state.status === 'available' && state.canInstall" class="btn-primary" :disabled="busy" @click="emit('download', 'setup')">{{ t('updateDownloadSetup') }}</button>
        <button v-else-if="state.status === 'available' && state.portable && state.hasPortable" class="btn-primary" :disabled="busy" @click="emit('download', 'portable')">{{ t('updateDownloadPortable') }}</button>
        <button v-else-if="state.status === 'available' && state.hasRelease" class="btn-primary" :disabled="busy" @click="emit('download', 'release')">{{ t('updateReleasePage') }}</button>
        <button v-else-if="state.status !== 'unavailable'" class="btn-primary" :disabled="busy || updating || state.status === 'checking'" @click="emit('check')">{{ t(updating ? state.status === 'installing' ? 'updateInstalling' : 'updateDownloading' : state.status === 'error' ? 'retry' : 'checkForUpdates') }}</button>
        <button v-else class="btn-primary" @click="emit('close')">{{ t('close') }}</button>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.update-dialog { max-width: 520px; }
.update-product { color: var(--text-secondary); font-weight: 500; }
.update-body { padding: 28px; gap: 22px; }
.update-heading h2 { font-size: 1.7rem; font-weight: 600; line-height: 1.25; margin: 16px 0 8px; }
.update-heading p, .update-download-hint { color: var(--text-secondary); margin: 0; }
.update-icon { width: 30px; height: 30px; color: #93c5fd; }
.is-checking { animation: update-check 1.4s linear infinite; }
.update-versions { display: flex; gap: 36px; margin: 0; padding: 16px 0; border-block: 1px solid var(--border); }
.update-versions dt { color: var(--text-secondary); font-size: .85rem; }
.update-versions dd { margin: 3px 0 0; font-size: 1.25rem; font-weight: 500; }
.update-notes summary { cursor: pointer; color: var(--text-primary); font-weight: 500; }
.update-notes p { white-space: pre-wrap; overflow-wrap: anywhere; color: var(--text-secondary); font-size: .9rem; line-height: 1.7; max-height: 160px; overflow-y: auto; margin-bottom: 0; }
.update-automatic { display: flex; align-items: center; gap: 10px; cursor: pointer; color: var(--text-secondary); }
.update-automatic input { accent-color: var(--accent); width: 17px; height: 17px; flex-shrink: 0; }
.update-download-hint { font-size: .85rem; }
.update-footer { flex-wrap: wrap; }
.update-embedded .update-dialog { width: 100%; max-height: none; border: 0; background: none; box-shadow: none; }
.update-embedded .update-body { padding: 20px 0; }
.update-embedded .update-footer { padding: 16px 0; }
.update-progress { display: flex; align-items: center; gap: 12px; }
.update-progress progress { width: 100%; accent-color: var(--accent); }
@keyframes update-check { to { transform: rotate(360deg); } }
:global(.reduced-motion) .is-checking { animation: none; }
@media (prefers-reduced-motion: reduce) { .is-checking { animation: none; } }
@media (max-width: 600px) { .update-body { padding: 20px; } .update-heading h2 { font-size: 1.4rem; } }
</style>

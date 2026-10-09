<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import type { Instance } from '../features/library/model.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
import { BridgeError } from '../bridge/client.ts';
import '../styles/instance-editor.css';
import JavaPicker from './JavaPicker.vue';
import type { JavaService } from './JavaPicker.vue';
import JvmPresetPicker from './JvmPresetPicker.vue';
import { jvmPresets, type JvmPreset } from '../features/library/jvm.ts';
import ModBrowserModal from './ModBrowserModal.vue';
import type { ModService } from '../bridge/mods.ts';
import PackVersionPicker from './PackVersionPicker.vue';
import PackLinkPicker from './PackLinkPicker.vue';
import type { PackService } from './PackVersionPicker.vue';
import type { InstalledPack } from '../features/library/packUpdates.ts';

interface Row { id: string; name: string; detail?: string; enabled?: boolean; status?: string }
interface EditorSettings { minMemory: number; maxMemory: number; width: number; height: number; fullscreen: boolean; overrideMemory: boolean; overrideWindow: boolean; jvmArgs: string; jvmPreset: JvmPreset; useGlobalJvmArgs: boolean }
interface JvmConfig { jvmArgs: string; jvmPreset: JvmPreset }
interface Details { ok: boolean; error?: string; field?: string; rows?: Row[]; text?: string; settings?: EditorSettings; running?: boolean; pack?: InstalledPack; jvmConfig?: { local: JvmConfig; global: JvmConfig } }
const props = defineProps<{
  instance: Instance; initialSection?: string; revision?: number; busy: boolean; t: (key: MessageKey) => string;
  details: (id: string, section: string) => Promise<unknown>;
  command: (id: string, command: string, payload?: unknown) => Promise<unknown>;
  javaService: JavaService;
  modService: ModService;
  packService: PackService;
}>();
const emit = defineEmits<{ (event: 'close'): void }>();
const sections: { id: string; label: MessageKey }[] = [
  { id: 'overview', label: 'editorOverview' }, { id: 'log', label: 'editorConsole' },
  { id: 'versions', label: 'editorVersions' }, { id: 'mods', label: 'editorMods' },
  { id: 'resourcepacks', label: 'editorResourcePacks' }, { id: 'shaderpacks', label: 'editorShaderPacks' },
  { id: 'worlds', label: 'editorWorlds' }, { id: 'screenshots', label: 'editorScreenshots' },
  { id: 'notes', label: 'editorNotes' }, { id: 'java', label: 'javaRuntime' }, { id: 'settings', label: 'settings' }, { id: 'otherlogs', label: 'editorOtherLogs' },
];
const section = ref(props.initialSection || 'overview');
const data = ref<Details | null>(null);
const loading = ref(false);
const working = ref(false);
const error = ref('');
const fieldErrors = ref<Record<string, string>>({});
const validationAttempted = ref(false);
const feedback = ref('');
const notes = ref('');
const savedNotes = ref('');
const settings = ref<EditorSettings | null>(null);
const savedSettings = ref('');
let localJvm: JvmConfig | undefined;
const wrap = ref(true);
const filter = ref('');
const pendingRemoval = ref<Row | null>(null);
const pendingNavigation = ref<string | null>(null);
const showModBrowser = ref(false);
const dialog = ref<HTMLElement>();
const confirmation = ref<HTMLElement>();
let navigationInvoker: HTMLElement | null = null;
const invoker = document.activeElement as HTMLElement | null;
let revision = 0;
let closed = false;
let poll: ReturnType<typeof setTimeout> | undefined;
const title = computed(() => props.t(sections.find(item => item.id === section.value)?.label || 'editorOverview'));
const disabled = computed(() => props.busy || working.value || loading.value);
const rows = computed(() => (data.value?.rows || []).filter(row => `${row.name} ${row.detail || ''}`.toLowerCase().includes(filter.value.toLowerCase())));
const fileSection = computed(() => ['mods', 'resourcepacks', 'shaderpacks'].includes(section.value));
const dirty = computed(() => section.value === 'notes' ? notes.value !== savedNotes.value : section.value === 'settings' && JSON.stringify(settings.value) !== savedSettings.value);
const editable = computed(() => section.value === 'settings' || section.value === 'notes');
const settingsLocked = computed(() => working.value || props.instance.running);
const validationErrors = computed(() => {
  const value = settings.value;
  const errors: Record<string, string> = {};
  if (!value) return errors;
  for (const key of ['minMemory', 'maxMemory', 'width', 'height'] as const) {
    const memory = key.endsWith('Memory');
    if (!value[memory ? 'overrideMemory' : 'overrideWindow']) continue;
    if (!Number.isInteger(value[key]) || value[key] < (memory ? 128 : 320) || value[key] > (memory ? 1048576 : 16384))
      errors[key] = props.t(memory ? 'editorMemoryRange' : 'editorWindowRange');
  }
  if (value.overrideMemory && value.minMemory > value.maxMemory) errors.minMemory = props.t('editorMemoryOrder');
  if (!value.useGlobalJvmArgs) {
    if (!jvmPresets.some(preset => preset.id === value.jvmPreset)) errors.jvmPreset = props.t('editorJvmPresetError');
    if (value.jvmArgs.length > 8192 || value.jvmArgs.includes('\0')) errors.jvmArgs = props.t('editorJvmArgsError');
  }
  return errors;
});
const visibleErrors = computed(() => ({ ...(validationAttempted.value ? validationErrors.value : {}), ...fieldErrors.value }));

async function focusError(field?: string) {
  await nextTick();
  const group = field && Object.keys(settings.value || {}).includes(field) ? dialog.value?.querySelector<HTMLElement>(`[data-setting="${field}"]`) : null;
  const target = group?.querySelector<HTMLElement>('input:not(:disabled), textarea:not(:disabled), button:not(:disabled)') || group || dialog.value?.querySelector<HTMLElement>('.editor-error');
  const reducedMotion = document.querySelector('.launcher')?.classList.contains('reduced-motion') || matchMedia('(prefers-reduced-motion: reduce)').matches;
  target?.scrollIntoView({ block: 'center', behavior: reducedMotion ? 'instant' : 'smooth' });
  target?.focus({ preventScroll: true });
}

function stopPoll() { clearTimeout(poll); poll = undefined; }
function schedulePoll() {
  stopPoll();
  if (!closed && section.value === 'log' && document.visibilityState === 'visible' && document.hasFocus()) {
    poll = setTimeout(() => { void load(true); }, 2500);
  }
}
async function load(quiet = false) {
  stopPoll();
  const request = ++revision;
  const activeSection = section.value;
  if (!quiet) { loading.value = true; data.value = null; error.value = ''; fieldErrors.value = {}; validationAttempted.value = false; }
  if (activeSection === 'java') { loading.value = false; data.value = { ok: true }; return; }
  try {
    const result = await props.details(props.instance.id, activeSection) as Details;
    if (closed || request !== revision) return;
    if (!result.ok) throw new Error(result.error || props.t('editorReadError'));
    data.value = result;
    error.value = '';
    if (activeSection === 'notes') notes.value = savedNotes.value = result.text || '';
    if (activeSection === 'settings') {
      settings.value = result.settings ? { ...result.settings } : null;
      localJvm = result.jvmConfig?.local ? { ...result.jvmConfig.local } : undefined;
      savedSettings.value = JSON.stringify(settings.value);
    }
  } catch (reason) {
    if (!closed && request === revision) error.value = reason instanceof Error ? reason.message : String(reason);
  } finally {
    if (!closed && request === revision) { loading.value = false; schedulePoll(); }
  }
}
async function run(name: string, payload?: unknown, refresh = true) {
  if (disabled.value) return false;
  const activeSection = section.value;
  const request = ++revision;
  stopPoll(); working.value = true; error.value = ''; feedback.value = '';
  try {
    const result = await props.command(props.instance.id, name, payload) as Details;
    if (closed || request !== revision) return false;
    if (!result.ok) {
      const message = result.error || props.t('editorActionError');
      const field = result.field;
      if (name === 'saveSettings' && field && Object.keys(settings.value || {}).includes(field)) fieldErrors.value = { [field]: message };
      else error.value = message;
      return false;
    }
    pendingRemoval.value = null;
    feedback.value = name === 'saveNotes' || name === 'saveSettings' ? props.t('editorSaved') : name === 'copyLog' ? props.t('editorConsoleCopied') : '';
    if (refresh && activeSection === section.value) await load();
    return !error.value;
  } catch (reason) {
    if (!closed && request === revision) {
      const message = reason instanceof Error ? reason.message : String(reason);
      const field = reason instanceof BridgeError ? reason.field : undefined;
      if (name === 'saveSettings' && field && Object.keys(settings.value || {}).includes(field)) fieldErrors.value = { [field]: message };
      else error.value = message;
    }
    return false;
  } finally {
    if (!closed) {
      working.value = false; schedulePoll();
      if (error.value || Object.keys(fieldErrors.value).length) await focusError(Object.keys(fieldErrors.value)[0]);
    }
  }
}
async function save() {
  if (disabled.value) return false;
  if (!dirty.value) return true;
  if (section.value === 'settings') {
    validationAttempted.value = true;
    fieldErrors.value = {};
    const firstError = Object.keys(validationErrors.value)[0];
    if (firstError) { await focusError(firstError); return false; }
    return run('saveSettings', settings.value);
  }
  return run('saveNotes', notes.value);
}
async function saveAndClose() { if (await save()) emit('close'); }
async function saveAndContinue() {
  const target = pendingNavigation.value;
  pendingNavigation.value = null;
  if (!await save()) return;
  if (target === 'close') emit('close');
  else if (target) changeSection(target);
}
function toggleJvmInheritance() {
  const value = settings.value;
  if (!value) return;
  if (value.useGlobalJvmArgs) localJvm = { jvmArgs: value.jvmArgs, jvmPreset: value.jvmPreset };
  const config = value.useGlobalJvmArgs ? data.value?.jvmConfig?.global : localJvm;
  if (config) Object.assign(value, config);
}
function changeSection(id: string) {
  if (working.value || id === section.value) return;
  if (dirty.value) { pendingNavigation.value = id; return; }
  section.value = id; filter.value = ''; pendingRemoval.value = null; feedback.value = ''; void load();
}
function close() {
  if (working.value) return;
  if (dirty.value) { pendingNavigation.value = 'close'; return; }
  emit('close');
}
function discardChanges() {
  const target = pendingNavigation.value; pendingNavigation.value = null;
  if (target === 'close') { emit('close'); return; }
  notes.value = savedNotes.value;
  if (savedSettings.value) settings.value = JSON.parse(savedSettings.value) as EditorSettings | null;
  if (target) changeSection(target);
}
function activity() {
  stopPoll();
  if (section.value !== 'log') return;
  ++revision;
  loading.value = false;
  if (document.visibilityState === 'visible' && document.hasFocus()) void load(true);
}
function keyboard(event: KeyboardEvent) {
  if (showModBrowser.value) return;
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); if (pendingNavigation.value) pendingNavigation.value = null; else if (pendingRemoval.value) pendingRemoval.value = null; else close(); }
  if (event.key !== 'Tab') return;
  const scope = pendingNavigation.value ? confirmation.value : dialog.value;
  const items = [...(scope?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), textarea:not(:disabled), select:not(:disabled), [tabindex="0"]') || [])];
  const first = items[0]; const last = items.at(-1);
  if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
watch(() => props.instance.id, () => { pendingRemoval.value = null; void load(); });
watch(() => props.revision, () => { if (!working.value && !dirty.value) void load(); });
watch(settings, () => { fieldErrors.value = {}; }, { deep: true });
watch(showModBrowser, async value => {
  if (value) return;
  await load();
  await nextTick();
  if (!closed) dialog.value?.querySelector<HTMLButtonElement>('.editor-download-mods')?.focus();
});
watch(pendingNavigation, async value => {
  if (value) navigationInvoker = document.activeElement as HTMLElement | null;
  await nextTick();
  if (value) confirmation.value?.querySelector<HTMLButtonElement>('button')?.focus();
  else navigationInvoker?.focus();
});
onMounted(async () => {
  document.addEventListener('visibilitychange', activity); window.addEventListener('focus', activity); window.addEventListener('blur', activity);
  await nextTick(); dialog.value?.querySelector<HTMLButtonElement>('button')?.focus(); void load();
});
onUnmounted(() => {
  closed = true; ++revision; stopPoll();
  document.removeEventListener('visibilitychange', activity); window.removeEventListener('focus', activity); window.removeEventListener('blur', activity);
  invoker?.focus();
});
</script>

<template>
  <div class="modal-overlay" @click.self="close">
    <section ref="dialog" class="modal-dialog create-instance-window instance-editor-window" role="dialog" aria-modal="true" aria-labelledby="editor-title" :inert="showModBrowser" @keydown="keyboard">
      <header class="create-modal-header" :inert="pendingNavigation !== null">
        <div class="create-header-left">
          <img v-if="instance.iconUrl" :src="instance.iconUrl" alt="" class="editor-instance-icon" />
          <span v-else class="editor-instance-initial" aria-hidden="true">{{ instance.name.slice(0, 1) }}</span>
          <div class="create-header-titles"><h2 id="editor-title">{{ instance.name }}</h2><p class="create-subtitle">{{ [instance.minecraftVersion, instance.loader, instance.loaderVersion].filter(Boolean).join(' · ') }}</p></div>
        </div>
        <button class="modal-close-btn" :disabled="working" :aria-label="t('close')" @click="close">✕</button>
      </header>
      <div class="editor-workspace" :inert="pendingNavigation !== null">
        <nav class="platform-sidebar editor-sidebar" :aria-label="t('editorSidebarLabel')">
          <button v-for="item in sections" :key="item.id" :data-section="item.id" class="platform-tab" :class="{ 'is-active': section === item.id }" :aria-current="section === item.id ? 'page' : undefined" :disabled="working" @click="changeSection(item.id)">{{ t(item.label) }}</button>
        </nav>
        <div class="editor-content" :aria-busy="loading || working">
          <div class="editor-section-header"><h3>{{ title }}</h3><button class="btn-subtle" :disabled="disabled || dirty" @click="fileSection ? run('refreshFiles', { section }) : load()">{{ t('refresh') }}</button></div>
          <div v-if="error" class="editor-message editor-error" role="alert" tabindex="-1"><p>{{ error }}</p><button v-if="!dirty" class="btn-subtle" :disabled="disabled" @click="load()">{{ t('retry') }}</button></div>
          <p v-if="feedback" class="editor-feedback" role="status">{{ feedback }}</p>
          <p v-if="loading" class="editor-message" role="status">{{ t('working') }}</p>
          <template v-else-if="data">
            <JavaPicker v-if="section === 'java'" :instance-id="instance.id" :revision="revision" :service="javaService" :t="t" />
            <template v-else-if="section === 'log'">
              <div class="editor-toolbar"><button class="btn-subtle" :disabled="disabled || !data.text" @click="run('copyLog', undefined, false)">{{ t('editorCopy') }}</button><button class="btn-subtle" :disabled="disabled || !data.text || !data.running" :title="!data.running ? t('editorClearLogHint') : undefined" @click="run('clearLog')">{{ t('editorClear') }}</button><label class="check"><input v-model="wrap" type="checkbox" />{{ t('editorWrapLines') }}</label><span class="editor-live-status">{{ data.running ? t('running') : t('editorStopped') }}</span></div>
              <pre v-if="data.text" class="editor-console" :class="{ 'wrap-lines': wrap }" tabindex="0" :aria-label="t('editorMinecraftConsole')">{{ data.text }}</pre><p v-else class="editor-message">{{ t('editorNoConsoleOutput') }}</p>
            </template>
            <template v-else-if="section === 'notes'"><label class="visually-hidden" for="editor-notes">{{ t('editorNotes') }}</label><textarea id="editor-notes" v-model="notes" class="glass-input editor-notes" :disabled="working" :placeholder="t('editorNotesPlaceholder')"></textarea></template>
            <template v-else-if="section === 'settings'">
              <div v-if="settings" class="editor-settings">
                <fieldset><legend>{{ t('editorMemory') }}</legend>
                  <p v-if="instance.running" class="editor-hint" role="status">{{ t('editorStopGameSettings') }}</p>
                  <label class="check" data-setting="overrideMemory" tabindex="-1"><input v-model="settings.overrideMemory" type="checkbox" :disabled="settingsLocked" :aria-invalid="Boolean(visibleErrors.overrideMemory)" :aria-describedby="visibleErrors.overrideMemory ? 'editor-memory-switch-error' : undefined" />{{ t('editorUseInstanceMemory') }}<span v-if="visibleErrors.overrideMemory" id="editor-memory-switch-error" class="editor-field-error" role="alert">{{ visibleErrors.overrideMemory }}</span></label>
                  <p v-if="!settings.overrideMemory" class="editor-hint editor-inheritance">{{ t('editorGlobalMemory') }}</p>
                  <div class="editor-fields">
                    <label data-setting="minMemory" tabindex="-1">{{ t('editorMinimumMemory') }}<input v-model.number="settings.minMemory" class="glass-input" type="number" min="128" max="1048576" :disabled="settingsLocked || !settings.overrideMemory" :aria-invalid="Boolean(visibleErrors.minMemory)" :aria-describedby="visibleErrors.minMemory ? 'editor-min-memory-error' : undefined" /><span v-if="visibleErrors.minMemory" id="editor-min-memory-error" class="editor-field-error" role="alert">{{ visibleErrors.minMemory }}</span></label>
                    <label data-setting="maxMemory" tabindex="-1">{{ t('editorMaximumMemory') }}<input v-model.number="settings.maxMemory" class="glass-input" type="number" min="128" max="1048576" :disabled="settingsLocked || !settings.overrideMemory" :aria-invalid="Boolean(visibleErrors.maxMemory)" :aria-describedby="visibleErrors.maxMemory ? 'editor-max-memory-error' : undefined" /><span v-if="visibleErrors.maxMemory" id="editor-max-memory-error" class="editor-field-error" role="alert">{{ visibleErrors.maxMemory }}</span></label>
                  </div>
                </fieldset>
                <fieldset><legend>{{ t('gameWindow') }}</legend>
                  <label class="check" data-setting="overrideWindow" tabindex="-1"><input v-model="settings.overrideWindow" type="checkbox" :disabled="settingsLocked" :aria-invalid="Boolean(visibleErrors.overrideWindow)" />{{ t('editorUseInstanceWindow') }}<span v-if="visibleErrors.overrideWindow" class="editor-field-error" role="alert">{{ visibleErrors.overrideWindow }}</span></label>
                  <div class="editor-fields">
                    <label data-setting="width" tabindex="-1">{{ t('editorWidth') }}<input v-model.number="settings.width" class="glass-input" type="number" min="320" max="16384" :disabled="settingsLocked || !settings.overrideWindow" :aria-invalid="Boolean(visibleErrors.width)" :aria-describedby="visibleErrors.width ? 'editor-width-error' : undefined" /><span v-if="visibleErrors.width" id="editor-width-error" class="editor-field-error" role="alert">{{ visibleErrors.width }}</span></label>
                    <label data-setting="height" tabindex="-1">{{ t('editorHeight') }}<input v-model.number="settings.height" class="glass-input" type="number" min="320" max="16384" :disabled="settingsLocked || !settings.overrideWindow" :aria-invalid="Boolean(visibleErrors.height)" :aria-describedby="visibleErrors.height ? 'editor-height-error' : undefined" /><span v-if="visibleErrors.height" id="editor-height-error" class="editor-field-error" role="alert">{{ visibleErrors.height }}</span></label>
                  </div>
                  <label class="check" data-setting="fullscreen" tabindex="-1"><input v-model="settings.fullscreen" type="checkbox" :disabled="settingsLocked || !settings.overrideWindow" :aria-invalid="Boolean(visibleErrors.fullscreen)" />{{ t('maximizeOnStart') }}<span v-if="visibleErrors.fullscreen" class="editor-field-error" role="alert">{{ visibleErrors.fullscreen }}</span></label>
                </fieldset>
                <fieldset><legend>{{ t('jvmArguments') }}</legend>
                  <label class="check" data-setting="useGlobalJvmArgs" tabindex="-1"><input v-model="settings.useGlobalJvmArgs" type="checkbox" :disabled="settingsLocked" @change="toggleJvmInheritance" />{{ t('jvmUseGlobal') }}<span v-if="visibleErrors.useGlobalJvmArgs" class="editor-field-error" role="alert">{{ visibleErrors.useGlobalJvmArgs }}</span></label>
                  <div data-setting="jvmPreset" tabindex="-1"><JvmPresetPicker id="instance-jvm-preset" v-model="settings.jvmPreset" :disabled="settingsLocked || settings.useGlobalJvmArgs" :t="t" /><p v-if="visibleErrors.jvmPreset" class="editor-field-error" role="alert">{{ visibleErrors.jvmPreset }}</p></div>
                  <div data-setting="jvmArgs" tabindex="-1"><label for="instance-jvm-args">{{ t('jvmArguments') }}</label>
                    <textarea id="instance-jvm-args" v-model="settings.jvmArgs" class="glass-input jvm-arguments" rows="3" maxlength="8192" spellcheck="false" :disabled="settingsLocked || settings.useGlobalJvmArgs || settings.jvmPreset !== 'custom'" :aria-invalid="Boolean(visibleErrors.jvmArgs)" :aria-describedby="visibleErrors.jvmArgs ? 'editor-jvm-args-error' : undefined" />
                    <p v-if="visibleErrors.jvmArgs" id="editor-jvm-args-error" class="editor-field-error" role="alert">{{ visibleErrors.jvmArgs }}</p>
                  </div>
                  <p class="editor-hint">{{ t('jvmInstanceHint') }} {{ t('jvmMemoryHint') }}</p>
                </fieldset>
              </div><p v-else class="editor-message">{{ t('editorSettingsUnavailable') }}</p>
            </template>
            <template v-else>
              <PackVersionPicker v-if="section === 'versions' && data.pack" :id="instance.id" :pack="data.pack" :running="instance.running" :service="packService" :t="t" />
              <PackLinkPicker v-if="section === 'versions' && (!data.pack || data.pack.requiresLink)" :id="instance.id" :pack="data.pack" :running="instance.running" :busy="disabled" :service="packService" :t="t" @linked="load()" />
              <div class="editor-toolbar"><input v-if="data.rows?.length" v-model="filter" class="glass-input editor-filter" type="search" :aria-label="t('editorSearchAria').replace('{section}', title)" :placeholder="t('editorSearchPlaceholder').replace('{section}', title.toLowerCase())" /><button v-if="fileSection" class="btn-subtle" :disabled="disabled || instance.running" @click="run('addFiles', { section })">{{ t('editorImportFiles') }}</button><button v-if="section === 'mods'" class="btn-subtle editor-download-mods" :disabled="disabled || instance.running" @click="showModBrowser = true">{{ t('editorDownloadMods') }}</button><button v-if="section !== 'versions'" class="btn-subtle" :disabled="disabled" @click="run('openFolder', { section }, false)">{{ t('folder') }}</button></div>
              <div v-if="pendingRemoval" class="editor-remove-confirm" role="alert"><p>{{ t('editorRemoveConfirm').replace('{name}', pendingRemoval.name) }}</p><button class="btn-subtle" :disabled="disabled" @click="pendingRemoval = null">{{ t('cancel') }}</button><button class="btn-subtle" :disabled="disabled" @click="run('removeFile', { section, id: pendingRemoval.id })">{{ t('editorRemoveFile') }}</button></div>
              <ul v-if="rows.length" class="editor-file-list"><li v-for="row in rows" :key="row.id" class="editor-file-row"><label v-if="section === 'mods' && typeof row.enabled === 'boolean'" class="editor-mod-toggle"><input type="checkbox" :checked="row.enabled" :disabled="disabled" :aria-label="t('editorEnableMod').replace('{name}', row.name)" @change="run('toggleMod', { id: row.id, enabled: ($event.target as HTMLInputElement).checked })" /></label><div class="editor-file-copy"><strong>{{ row.name }}</strong><p v-if="row.detail">{{ row.detail }}</p></div><span v-if="row.status" class="editor-file-status">{{ row.status }}</span><button v-if="fileSection" class="btn-subtle" :disabled="disabled" :aria-label="t('editorRemoveAria').replace('{name}', row.name)" @click="pendingRemoval = row">{{ t('editorRemove') }}</button></li></ul>
              <p v-else class="editor-message">{{ filter ? t('editorNoMatchingItems') : t('editorNoItems').replace('{section}', title.toLowerCase()) }}</p>
              <p v-if="section === 'versions'" class="editor-hint">{{ t('editorVersionsHint') }}</p>
              <button v-if="section === 'overview'" class="btn-subtle editor-save" :disabled="disabled" @click="run('openAdvanced', { section: 'servers' }, false)">{{ t('editorManageServers') }}</button>
              <pre v-if="data.text" class="editor-console wrap-lines" tabindex="0">{{ data.text }}</pre>
            </template>
          </template>
        </div>
      </div>
      <footer class="create-modal-footer editor-footer" :inert="pendingNavigation !== null"><button class="btn-subtle" :disabled="disabled || dirty" @click="run('openAdvanced', { section }, false)">{{ t('editorAdvancedTools') }}</button><span class="secondary-note">{{ dirty ? t('editorUnsavedChanges') : working ? t('working') : '' }}</span><button v-if="editable" class="btn-subtle editor-save" :disabled="disabled || !dirty || (section === 'settings' && instance.running)" @click="save">{{ t('editorSave') }}</button><button class="btn-primary" :disabled="disabled || (dirty && section === 'settings' && instance.running)" @click="dirty ? saveAndClose() : close()">{{ t(dirty ? 'editorSaveAndClose' : 'close') }}</button></footer>
      <div v-if="pendingNavigation" class="editor-confirm-overlay" @click.self="pendingNavigation = null">
        <section ref="confirmation" class="modal-dialog editor-discard-confirm" role="alertdialog" aria-modal="true" aria-labelledby="editor-discard-title" aria-describedby="editor-discard-description">
          <h3 id="editor-discard-title">{{ t('editorUnsavedChanges') }}</h3><p id="editor-discard-description">{{ t('editorDiscardDescription') }}</p>
          <div class="editor-confirm-actions"><button class="btn-subtle" @click="pendingNavigation = null">{{ t('editorKeepEditing') }}</button><button class="btn-primary" :disabled="disabled || (section === 'settings' && instance.running)" @click="saveAndContinue">{{ t('editorSaveChanges') }}</button><button class="btn-subtle editor-discard-button" @click="discardChanges">{{ t('editorDiscardChanges') }}</button></div>
        </section>
      </div>
    </section>
    <ModBrowserModal v-if="showModBrowser" :instance="instance" :service="modService" :t="t" @close="showModBrowser = false" />
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import type { Instance } from '../features/library/model.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
import '../styles/instance-editor.css';
import JavaPicker from './JavaPicker.vue';
import type { JavaService } from './JavaPicker.vue';

interface Row { id: string; name: string; detail?: string; enabled?: boolean; status?: string }
interface EditorSettings { minMemory: number; maxMemory: number; width: number; height: number; fullscreen: boolean; overrideMemory: boolean; overrideWindow: boolean }
interface Details { ok: boolean; error?: string; rows?: Row[]; text?: string; settings?: EditorSettings; running?: boolean }
const props = defineProps<{
  instance: Instance; initialSection?: string; revision?: number; busy: boolean; t: (key: MessageKey) => string;
  details: (id: string, section: string) => Promise<unknown>;
  command: (id: string, command: string, payload?: unknown) => Promise<unknown>;
  javaService: JavaService;
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
const feedback = ref('');
const notes = ref('');
const savedNotes = ref('');
const settings = ref<EditorSettings | null>(null);
const savedSettings = ref('');
const wrap = ref(true);
const filter = ref('');
const pendingRemoval = ref<Row | null>(null);
const pendingNavigation = ref<string | null>(null);
const dialog = ref<HTMLElement>();
const invoker = document.activeElement as HTMLElement | null;
let revision = 0;
let closed = false;
let poll: ReturnType<typeof setTimeout> | undefined;
const title = computed(() => props.t(sections.find(item => item.id === section.value)?.label || 'editorOverview'));
const disabled = computed(() => props.busy || working.value || loading.value);
const rows = computed(() => (data.value?.rows || []).filter(row => `${row.name} ${row.detail || ''}`.toLowerCase().includes(filter.value.toLowerCase())));
const fileSection = computed(() => ['mods', 'resourcepacks', 'shaderpacks'].includes(section.value));
const dirty = computed(() => section.value === 'notes' ? notes.value !== savedNotes.value : section.value === 'settings' && JSON.stringify(settings.value) !== savedSettings.value);
const settingsValid = computed(() => settings.value && [settings.value.minMemory, settings.value.maxMemory].every(value => Number.isInteger(value) && value >= 128 && value <= 1048576) && settings.value.minMemory <= settings.value.maxMemory && [settings.value.width, settings.value.height].every(value => Number.isInteger(value) && value >= 320 && value <= 16384));

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
  if (!quiet) { loading.value = true; data.value = null; error.value = ''; }
  if (activeSection === 'java') { loading.value = false; data.value = { ok: true }; return; }
  try {
    const result = await props.details(props.instance.id, activeSection) as Details;
    if (closed || request !== revision) return;
    if (!result.ok) throw new Error(result.error || props.t('editorReadError'));
    data.value = result;
    error.value = '';
    if (activeSection === 'notes') notes.value = savedNotes.value = result.text || '';
    if (activeSection === 'settings') { settings.value = result.settings ? { ...result.settings } : null; savedSettings.value = JSON.stringify(settings.value); }
  } catch (reason) {
    if (!closed && request === revision) error.value = reason instanceof Error ? reason.message : String(reason);
  } finally {
    if (!closed && request === revision) { loading.value = false; schedulePoll(); }
  }
}
async function run(name: string, payload?: unknown, refresh = true) {
  if (disabled.value) return;
  const activeSection = section.value;
  const request = ++revision;
  stopPoll(); working.value = true; error.value = ''; feedback.value = '';
  try {
    const result = await props.command(props.instance.id, name, payload) as Details;
    if (closed || request !== revision) return;
    if (!result.ok) throw new Error(result.error || props.t('editorActionError'));
    pendingRemoval.value = null;
    feedback.value = name === 'saveNotes' || name === 'saveSettings' ? props.t('editorSaved') : name === 'copyLog' ? props.t('editorConsoleCopied') : '';
    if (refresh && activeSection === section.value) await load();
  } catch (reason) {
    if (!closed && request === revision) error.value = reason instanceof Error ? reason.message : String(reason);
  } finally { if (!closed) { working.value = false; schedulePoll(); } }
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
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); if (pendingNavigation.value) pendingNavigation.value = null; else if (pendingRemoval.value) pendingRemoval.value = null; else close(); }
  if (event.key !== 'Tab') return;
  const items = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), textarea:not(:disabled), select:not(:disabled), [tabindex="0"]') || [])];
  const first = items[0]; const last = items.at(-1);
  if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
watch(() => props.instance.id, () => { pendingRemoval.value = null; void load(); });
watch(() => props.revision, () => { if (!working.value && !dirty.value) void load(); });
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
    <section ref="dialog" class="modal-dialog create-instance-window instance-editor-window" role="dialog" aria-modal="true" aria-labelledby="editor-title" @keydown="keyboard">
      <header class="create-modal-header">
        <div class="create-header-left">
          <img v-if="instance.iconUrl" :src="instance.iconUrl" alt="" class="editor-instance-icon" />
          <span v-else class="editor-instance-initial" aria-hidden="true">{{ instance.name.slice(0, 1) }}</span>
          <div class="create-header-titles"><h2 id="editor-title">{{ instance.name }}</h2><p class="create-subtitle">{{ [instance.minecraftVersion, instance.loader, instance.loaderVersion].filter(Boolean).join(' · ') }}</p></div>
        </div>
        <button class="modal-close-btn" :disabled="working" :aria-label="t('close')" @click="close">✕</button>
      </header>
      <div class="editor-workspace">
        <nav class="platform-sidebar editor-sidebar" :aria-label="t('editorSidebarLabel')">
          <button v-for="item in sections" :key="item.id" :data-section="item.id" class="platform-tab" :class="{ 'is-active': section === item.id }" :aria-current="section === item.id ? 'page' : undefined" :disabled="working" @click="changeSection(item.id)">{{ t(item.label) }}</button>
        </nav>
        <div class="editor-content" :aria-busy="loading || working">
          <div class="editor-section-header"><h3>{{ title }}</h3><button class="btn-subtle" :disabled="disabled || dirty" @click="fileSection ? run('refreshFiles', { section }) : load()">{{ t('refresh') }}</button></div>
          <div v-if="pendingNavigation" class="editor-remove-confirm editor-discard-confirm" role="alert"><p>{{ t('editorDiscardPrompt') }}</p><button class="btn-subtle" @click="pendingNavigation = null">{{ t('editorKeepEditing') }}</button><button class="btn-subtle" @click="discardChanges">{{ t('editorDiscardChanges') }}</button></div>
          <div v-if="error" class="editor-message editor-error" role="alert"><p>{{ error }}</p><button v-if="!dirty" class="btn-subtle" :disabled="disabled" @click="load()">{{ t('retry') }}</button></div>
          <p v-if="feedback" class="editor-feedback" role="status">{{ feedback }}</p>
          <p v-if="loading" class="editor-message" role="status">{{ t('working') }}</p>
          <template v-else-if="data">
            <JavaPicker v-if="section === 'java'" :instance-id="instance.id" :revision="revision" :service="javaService" :t="t" />
            <template v-else-if="section === 'log'">
              <div class="editor-toolbar"><button class="btn-subtle" :disabled="disabled || !data.text" @click="run('copyLog', undefined, false)">{{ t('editorCopy') }}</button><button class="btn-subtle" :disabled="disabled || !data.text || !data.running" :title="!data.running ? t('editorClearLogHint') : undefined" @click="run('clearLog')">{{ t('editorClear') }}</button><label class="check"><input v-model="wrap" type="checkbox" />{{ t('editorWrapLines') }}</label><span class="editor-live-status">{{ data.running ? t('running') : t('editorStopped') }}</span></div>
              <pre v-if="data.text" class="editor-console" :class="{ 'wrap-lines': wrap }" tabindex="0" :aria-label="t('editorMinecraftConsole')">{{ data.text }}</pre><p v-else class="editor-message">{{ t('editorNoConsoleOutput') }}</p>
            </template>
            <template v-else-if="section === 'notes'"><label class="visually-hidden" for="editor-notes">{{ t('editorNotes') }}</label><textarea id="editor-notes" v-model="notes" class="glass-input editor-notes" :disabled="working" :placeholder="t('editorNotesPlaceholder')"></textarea><button class="btn-primary editor-save" :disabled="disabled || !dirty" @click="run('saveNotes', notes)">{{ t('editorSaveNotes') }}</button></template>
            <template v-else-if="section === 'settings'">
              <div v-if="settings" class="editor-settings">
                <fieldset><legend>{{ t('editorMemory') }}</legend><label class="check"><input v-model="settings.overrideMemory" type="checkbox" :disabled="working" />{{ t('editorUseInstanceMemory') }}</label><div class="editor-fields"><label>{{ t('editorMinimumMemory') }}<input v-model.number="settings.minMemory" class="glass-input" type="number" min="128" max="1048576" :disabled="working || !settings.overrideMemory" /></label><label>{{ t('editorMaximumMemory') }}<input v-model.number="settings.maxMemory" class="glass-input" type="number" min="128" max="1048576" :disabled="working || !settings.overrideMemory" /></label></div></fieldset>
                <fieldset><legend>{{ t('gameWindow') }}</legend><label class="check"><input v-model="settings.overrideWindow" type="checkbox" :disabled="working" />{{ t('editorUseInstanceWindow') }}</label><div class="editor-fields"><label>{{ t('editorWidth') }}<input v-model.number="settings.width" class="glass-input" type="number" min="320" max="16384" :disabled="working || !settings.overrideWindow" /></label><label>{{ t('editorHeight') }}<input v-model.number="settings.height" class="glass-input" type="number" min="320" max="16384" :disabled="working || !settings.overrideWindow" /></label></div><label class="check"><input v-model="settings.fullscreen" type="checkbox" :disabled="working || !settings.overrideWindow" />{{ t('maximizeOnStart') }}</label></fieldset>
                <p v-if="!settingsValid" class="editor-hint" role="status">{{ t('editorSettingsHint') }}</p><button class="btn-primary editor-save" :disabled="disabled || !dirty || !settingsValid" @click="run('saveSettings', settings)">{{ t('editorSaveSettings') }}</button>
              </div><p v-else class="editor-message">{{ t('editorSettingsUnavailable') }}</p>
            </template>
            <template v-else>
              <div class="editor-toolbar"><input v-if="data.rows?.length" v-model="filter" class="glass-input editor-filter" type="search" :aria-label="t('editorSearchAria').replace('{section}', title)" :placeholder="t('editorSearchPlaceholder').replace('{section}', title.toLowerCase())" /><button v-if="fileSection" class="btn-subtle" :disabled="disabled" @click="run('addFiles', { section })">{{ t('editorImportFiles') }}</button><button v-if="section !== 'versions'" class="btn-subtle" :disabled="disabled" @click="run('openFolder', { section }, false)">{{ t('folder') }}</button></div>
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
      <footer class="create-modal-footer editor-footer"><button class="btn-subtle" :disabled="disabled" @click="run('openAdvanced', { section }, false)">{{ t('editorAdvancedTools') }}</button><span class="secondary-note">{{ dirty ? t('editorUnsavedChanges') : working ? t('working') : '' }}</span><button class="btn-primary" :disabled="working" @click="close">{{ t('close') }}</button></footer>
    </section>
  </div>
</template>

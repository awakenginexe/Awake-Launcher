<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import type { Instance } from '../features/library/model.ts';
import type { ModEntry, ModProgress, ModProvider, ModResult, ModReview, ModService, ModVersion, QueuedMod } from '../bridge/mods.ts';
import modrinthIcon from '../assets/icons/modrinth.svg';
import curseforgeIcon from '../assets/icons/curseforge.svg';
import '../styles/mod-browser.css';

const props = defineProps<{ instance: Instance; service: ModService; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (event: 'close'): void }>();
const dialog = ref<HTMLElement>();
const searchInput = ref<HTMLInputElement>();
const provider = ref<ModProvider>('modrinth');
const query = ref('');
const sort = ref('');
const sorts = ref<{ id: string; name: string }[]>([]);
const mods = ref<ModEntry[]>([]);
const selected = ref<ModEntry | null>(null);
const versions = ref<ModVersion[]>([]);
const versionId = ref('');
const queue = ref<QueuedMod[]>([]);
const review = ref<ModReview | null>(null);
const result = ref<ModResult | null>(null);
const progress = ref<ModProgress>({ current: 0, total: 0, status: '' });
const phase = ref<'browse' | 'review' | 'install' | 'done'>('browse');
const loading = ref(false);
const versionsLoading = ref(false);
const preparing = ref(false);
const hasMore = ref(false);
const error = ref('');
const versionError = ref('');
const failedIcons = ref(new Set<string>());
let searchRevision = 0;
let versionRevision = 0;
let timer: ReturnType<typeof setTimeout> | undefined;
let searchController: AbortController | undefined;
let versionController: AbortController | undefined;
let operationController: AbortController | undefined;
let disposed = false;
const previousFocus = document.activeElement as HTMLElement | null;
const background = new Map<HTMLElement, boolean>();
const locked = computed(() => preparing.value || phase.value === 'install');
const compatibility = computed(() => [props.instance.minecraftVersion, props.instance.loader].filter(Boolean).join(' · '));
const chosenVersion = computed(() => versions.value.find(version => version.id === versionId.value));
const queued = computed(() => queue.value.some(item => item.provider === provider.value && item.projectId === selected.value?.id && item.versionId === versionId.value));
const percent = computed(() => progress.value.total > 0 ? Math.max(0, Math.min(100, progress.value.current / progress.value.total * 100)) : undefined);
const website = computed(() => {
  try { const url = new URL(selected.value?.website || ''); return url.protocol === 'https:' && ['modrinth.com', 'curseforge.com', 'www.curseforge.com'].includes(url.hostname) ? url.href : ''; }
  catch { return ''; }
});
const message = (problem: unknown) => problem instanceof Error ? problem.message : String(problem);

async function search(append = false) {
  clearTimeout(timer);
  searchController?.abort(); searchController = new AbortController();
  const revision = ++searchRevision;
  const offset = append ? mods.value.length : 0;
  loading.value = true; error.value = '';
  if (!append) { mods.value = []; hasMore.value = false; }
  try {
    const response = await props.service.search(props.instance.id, provider.value, query.value.trim(), sort.value, offset, searchController.signal);
    if (disposed || revision !== searchRevision) return;
    const existing = append ? mods.value : [];
    const ids = new Set(existing.map(item => item.id));
    mods.value = [...existing, ...response.mods.filter(item => !ids.has(item.id))];
    hasMore.value = response.hasMore; sorts.value = response.sorts;
  } catch (problem) { if (!disposed && revision === searchRevision) error.value = message(problem); }
  finally { if (!disposed && revision === searchRevision) loading.value = false; }
}
function queueSearch() {
  clearTimeout(timer); searchController?.abort(); ++searchRevision;
  versionController?.abort(); ++versionRevision;
  selected.value = null; versions.value = []; versionId.value = ''; versionError.value = ''; versionsLoading.value = false;
  loading.value = true;
  timer = setTimeout(() => void search(), 250);
}
async function choose(mod: ModEntry) {
  versionController?.abort(); versionController = new AbortController();
  const revision = ++versionRevision;
  selected.value = mod; versions.value = []; versionId.value = ''; versionError.value = ''; versionsLoading.value = true;
  try {
    const response = await props.service.versions(props.instance.id, provider.value, mod.id, versionController.signal);
    if (disposed || revision !== versionRevision) return;
    selected.value = response.project; versions.value = response.versions;
    versionId.value = response.versions[0]?.id || '';
    versionError.value = response.notice || '';
  } catch (problem) { if (!disposed && revision === versionRevision) versionError.value = message(problem); }
  finally { if (!disposed && revision === versionRevision) versionsLoading.value = false; }
}
function add() {
  if (!selected.value || !chosenVersion.value || props.instance.running || locked.value) return;
  queue.value = queue.value.filter(item => item.provider !== provider.value || item.projectId !== selected.value!.id);
  queue.value.push({ provider: provider.value, projectId: selected.value.id, versionId: versionId.value, name: selected.value.name, version: chosenVersion.value.name, icon: selected.value.icon });
}
async function prepare() {
  if (locked.value || !queue.value.length || props.instance.running) return;
  preparing.value = true; error.value = ''; operationController = new AbortController();
  try {
    review.value = await props.service.prepare(props.instance.id, queue.value.map(({ provider, projectId, versionId }) => ({ provider, projectId, versionId })), operationController.signal);
    if (!disposed) { phase.value = 'review'; await nextTick(); dialog.value?.querySelector<HTMLElement>('.mod-review-heading')?.focus(); }
  } catch (problem) { if (!disposed) error.value = message(problem); }
  finally { preparing.value = false; }
}
async function install() {
  if (!review.value || locked.value || props.instance.running) return;
  phase.value = 'install'; error.value = ''; operationController = new AbortController();
  progress.value = { current: 0, total: 0, status: props.t('modDownloading') };
  try {
    const response = await props.service.install(props.instance.id, review.value.reviewId, value => { if (!disposed) progress.value = value; }, operationController.signal);
    if (disposed) return;
    result.value = { ...response, installed: response.installed || [], failed: response.failed || [], warnings: response.warnings || [] };
    const completed = new Set((result.value.installed || []).map(item => `${item.provider}/${item.projectId}`));
    queue.value = queue.value.filter(item => !completed.has(`${item.provider}/${item.projectId}`));
    phase.value = 'done';
  } catch (problem) { if (!disposed) { error.value = message(problem); review.value = null; phase.value = 'browse'; } }
}
function cancel() { operationController?.abort(); }
function close() { if (!locked.value) emit('close'); }
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close(); }
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), select:not(:disabled), a[href], [tabindex="0"]') || [])].filter(item => item.getClientRects().length > 0);
  const first = controls[0], last = controls.at(-1);
  if (!controls.includes(document.activeElement as HTMLElement)) { event.preventDefault(); (event.shiftKey ? last : first)?.focus(); }
  else if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
watch(provider, () => { sort.value = ''; sorts.value = []; queueSearch(); });
watch(query, queueSearch);
watch(sort, queueSearch);
onMounted(() => {
  for (const child of dialog.value?.closest('main')?.children || []) {
    if (child instanceof HTMLElement && !child.contains(dialog.value!)) { background.set(child, child.inert); child.inert = true; }
  }
  searchInput.value?.focus(); void search();
});
onUnmounted(() => {
  disposed = true; ++searchRevision; ++versionRevision; clearTimeout(timer);
  searchController?.abort(); versionController?.abort(); operationController?.abort();
  background.forEach((inert, element) => { element.inert = inert; });
  void nextTick(() => { if (previousFocus?.isConnected) previousFocus.focus(); });
});
</script>

<template>
  <div class="modal-overlay mod-browser-overlay" @click.self="close" @keydown="keydown">
    <section ref="dialog" class="modal-dialog mod-browser" role="dialog" aria-modal="true" aria-labelledby="mod-browser-title">
      <header class="mod-browser-header">
        <div><h2 id="mod-browser-title">{{ t('editorDownloadMods') }}</h2><p>{{ instance.name }}<span class="mod-compatibility">{{ compatibility }}</span></p></div>
        <button class="modal-close-btn" :disabled="locked" :aria-label="t('close')" @click="close">✕</button>
      </header>
      <p v-if="instance.running" class="mod-notice" role="status">{{ t('modStopGame') }}</p>
      <div v-if="phase === 'browse'" class="mod-browse-workspace">
        <div class="mod-browse-main">
          <div class="mod-browser-tools">
            <nav class="mod-provider-tabs" :aria-label="t('sources')"><button v-for="source in (['modrinth', 'curseforge'] as const)" :key="source" :data-mod-provider="source" :aria-pressed="provider === source" :disabled="locked" @click="provider = source"><img :src="source === 'modrinth' ? modrinthIcon : curseforgeIcon" alt="" />{{ source === 'modrinth' ? 'Modrinth' : 'CurseForge' }}</button></nav>
            <label class="visually-hidden" for="mod-browser-search">{{ t('modSearch') }}</label><input id="mod-browser-search" ref="searchInput" v-model="query" class="glass-input" type="search" :placeholder="t('modSearch')" :disabled="locked" autocomplete="off" />
            <label class="visually-hidden" for="mod-browser-sort">{{ t('sort') }}</label><select id="mod-browser-sort" v-model="sort" class="glass-input" :disabled="locked"><option value="">{{ t('modRecommended') }}</option><option v-for="item in sorts" :key="item.id" :value="item.id">{{ item.name }}</option></select>
          </div>
          <div class="mod-search-results" :aria-busy="loading">
            <p v-if="loading && !mods.length" class="mod-empty" role="status">{{ t('modSearching') }}</p>
            <ul v-else class="mod-result-list"><li v-for="mod in mods" :key="`${mod.provider}/${mod.id}`"><button class="mod-result" :class="{ 'is-selected': selected?.id === mod.id }" :aria-pressed="selected?.id === mod.id" :disabled="locked || loading" @click="choose(mod)"><img v-if="mod.icon && !failedIcons.has(mod.icon)" :src="mod.icon" loading="lazy" decoding="async" alt="" @error="failedIcons.add(mod.icon)" /><span v-else class="mod-icon-placeholder" aria-hidden="true">{{ mod.name.slice(0, 1) }}</span><span class="mod-result-copy"><strong>{{ mod.name }}</strong><span class="mod-author">{{ mod.author }}</span><span class="mod-summary">{{ mod.description }}</span></span><span v-if="queue.some(item => item.provider === mod.provider && item.projectId === mod.id)" class="mod-row-queued">{{ t('modQueued') }}</span></button></li></ul>
            <p v-if="!loading && !error && !mods.length" class="mod-empty">{{ t('modNoResults') }}</p>
            <button v-if="hasMore" class="btn-subtle mod-load-more" :disabled="loading || locked" @click="search(true)">{{ t(loading ? 'modSearching' : 'loadMore') }}</button>
          </div>
        </div>
        <aside class="mod-details-pane">
          <div class="mod-details-scroll">
            <template v-if="selected"><div class="mod-detail-heading"><img v-if="selected.icon && !failedIcons.has(selected.icon)" :src="selected.icon" alt="" @error="failedIcons.add(selected.icon)" /><span v-else class="mod-icon-placeholder" aria-hidden="true">{{ selected.name.slice(0, 1) }}</span><div><h3>{{ selected.name }}</h3><p>{{ selected.author }}</p></div></div><p class="mod-detail-description">{{ selected.description }}</p><a v-if="website" :href="website" target="_blank" rel="noopener noreferrer" class="mod-website">{{ t('modProjectPage') }} ↗</a><label for="mod-browser-version">{{ t('modCompatibleVersion') }}</label><select id="mod-browser-version" v-model="versionId" class="glass-input" :disabled="versionsLoading || locked || !versions.length"><option v-if="!versions.length" value="">{{ t(versionsLoading ? 'working' : 'modNoVersions') }}</option><option v-for="version in versions" :key="version.id" :value="version.id">{{ version.name }}{{ version.type !== 'release' ? ` · ${version.type}` : '' }}</option></select><p v-if="versionError" class="mod-notice" role="alert">{{ versionError }}</p><p v-if="chosenVersion" class="mod-version-detail">{{ chosenVersion.minecraft }} · {{ chosenVersion.loader }}<br />{{ chosenVersion.filename }}</p><button class="btn-primary mod-add-button" :disabled="!chosenVersion || queued || locked || instance.running" @click="add">{{ t(queued ? 'modQueued' : 'modAddQueue') }}</button></template>
            <p v-else class="mod-detail-placeholder">{{ t('modSelectHint') }}</p>
          </div>
          <div class="mod-queue"><h3>{{ t('modQueue') }} <span>{{ queue.length }}</span></h3><p v-if="!queue.length" class="mod-queue-empty">{{ t('modQueueEmpty') }}</p><ul v-else><li v-for="item in queue" :key="`${item.provider}/${item.projectId}`"><div><strong>{{ item.name }}</strong><span>{{ item.version }}</span></div><button class="quiet" :disabled="locked" :aria-label="t('modRemoveQueue').replace('{name}', item.name)" @click="queue = queue.filter(row => row !== item)">✕</button></li></ul></div>
        </aside>
      </div>
      <div v-else class="mod-review-body">
        <h3 class="mod-review-heading" tabindex="-1">{{ t(phase === 'review' ? 'modReviewTitle' : phase === 'install' ? 'modDownloading' : result?.canceled ? 'modCanceled' : result?.ok ? 'modInstalled' : 'modPartialFailure') }}</h3>
        <p v-if="phase === 'review'" class="mod-review-hint">{{ t('modReviewHint') }}</p>
        <template v-if="phase === 'install'"><p class="mod-progress-status" role="status">{{ progress.status }}</p><progress :value="percent" max="100" :aria-label="t('modDownloading')"></progress></template>
        <ul v-if="phase !== 'done'" class="mod-review-list"><li v-for="item in review?.items" :key="`${item.provider}/${item.projectId}`"><div><strong>{{ item.name }}</strong><p>{{ item.version }} · {{ item.filename }}</p><p v-if="item.requiredBy.length" class="mod-dependency-reason">{{ t('modRequiredBy').replace('{names}', item.requiredBy.join(', ')) }}</p></div><span class="mod-review-kind">{{ t(item.dependency ? 'modDependency' : item.maybeInstalled ? 'modExistingFile' : 'modSelected') }}</span></li></ul>
        <template v-else><p v-if="result?.installed.length" class="mod-success" role="status">{{ t('modInstalledCount').replace('{count}', String(result.installed.length)) }}</p><ul v-if="result?.failed.length" class="mod-failed-list"><li v-for="item in result.failed" :key="`${item.provider}/${item.projectId}`"><strong>{{ item.name }}</strong><p>{{ item.error }}</p></li></ul><p v-if="result?.error" class="mod-notice">{{ result.error }}</p></template>
        <ul v-if="(phase === 'done' ? result?.warnings : review?.warnings)?.length" class="mod-warning-list"><li v-for="warning in (phase === 'done' ? result?.warnings : review?.warnings)" :key="warning">{{ warning }}</li></ul>
      </div>
      <p v-if="error" class="mod-browser-error" role="alert">{{ error }}<button v-if="phase === 'browse' && !queue.length" class="btn-subtle" @click="search()">{{ t('retry') }}</button></p>
      <footer class="mod-browser-footer"><p>{{ phase === 'browse' ? t('modCompatibilityHint').replace('{compatibility}', compatibility) : '' }}</p><button v-if="locked" class="btn-subtle" @click="cancel">{{ t('cancel') }}</button><template v-else><button v-if="phase === 'review'" class="btn-subtle" @click="phase = 'browse'; review = null">{{ t('modBack') }}</button><button v-else-if="phase === 'done' && queue.length" class="btn-subtle" @click="phase = 'browse'; review = null">{{ t('modBack') }}</button><button class="btn-subtle" @click="close">{{ t('close') }}</button><button v-if="phase === 'browse'" class="btn-primary" :disabled="!queue.length || instance.running" @click="prepare">{{ t(preparing ? 'working' : 'modReviewQueue').replace('{count}', String(queue.length)) }}</button><button v-else-if="phase === 'review'" class="btn-primary" :disabled="instance.running" @click="install">{{ t('modInstallCount').replace('{count}', String(review?.items.length || 0)) }}</button><button v-else-if="phase === 'done' && queue.length" class="btn-primary" :disabled="instance.running" @click="prepare">{{ t('retry') }}</button></template></footer>
    </section>
  </div>
</template>

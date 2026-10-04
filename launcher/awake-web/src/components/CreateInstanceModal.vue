<script setup lang="ts">
import { computed, onScopeDispose, ref, watch } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import type { CatalogResult, MinecraftVersion, PackEntry, PackVersion } from '../bridge/catalog.ts';
import '../styles/pack-catalog.css';
import grassIcon from '../assets/icons/grass.svg';
import customIcon from '../assets/icons/custom.svg';
import importIcon from '../assets/icons/import.svg';
import atlauncherIcon from '../assets/icons/atlauncher.svg';
import curseforgeIcon from '../assets/icons/curseforge.svg';
import ftbIcon from '../assets/icons/ftb.svg';
import modrinthIcon from '../assets/icons/modrinth.svg';
import technicIcon from '../assets/icons/technic.svg';

const props = defineProps<{
  initialTab?: string; groups: string[]; busy: boolean; t: (key: MessageKey) => string;
  searchPacks: (provider: string, query: string, offset: number) => Promise<CatalogResult>;
  packVersions: (provider: string, id: string) => Promise<CatalogResult>;
  minecraftVersions: () => Promise<CatalogResult>;
  browseArchive: () => Promise<CatalogResult>;
}>();
const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'create-quick', payload: { name: string; version: string; loader: string; group: string }): void;
  (e: 'install-pack', payload: { provider: string; packId: string; versionId: string; name: string; group: string }): void;
  (e: 'import-archive', payload: { url: string; name: string; group: string }): void;
}>();
const tabs = [
  { id: 'custom', label: 'Custom', icon: customIcon },
  { id: 'import', label: 'Import', icon: importIcon },
  { id: 'atlauncher', label: 'ATLauncher', icon: atlauncherIcon },
  { id: 'curseforge', label: 'CurseForge', icon: curseforgeIcon },
  { id: 'ftb', label: 'FTB', icon: ftbIcon },
  { id: 'ftb-legacy', label: 'FTB Legacy', icon: ftbIcon },
  { id: 'ftb-app', label: 'ftbAppImport', icon: ftbIcon },
  { id: 'modrinth', label: 'Modrinth', icon: modrinthIcon },
  { id: 'technic', label: 'Technic', icon: technicIcon },
];
const loaders = ['Vanilla', 'NeoForge', 'Forge', 'Fabric', 'Quilt'];
const activeTab = ref(props.initialTab || 'custom');
const instanceName = ref('');
const instanceGroup = ref('');
const selectedVersion = ref('');
const selectedLoader = ref('Fabric');
const availableVersions = ref<MinecraftVersion[]>([]);
const versionSearchQuery = ref('');
const filterReleases = ref(true);
const filterSnapshots = ref(false);
const packSearchQuery = ref('');
const packs = ref<PackEntry[]>([]);
const selectedPack = ref<PackEntry | null>(null);
const versions = ref<PackVersion[]>([]);
const packVersionId = ref('');
const loading = ref(false);
const versionsLoading = ref(false);
const error = ref('');
const versionError = ref('');
const hasMore = ref(false);
const nextOffset = ref(0);
const importUrl = ref('');
const importFileName = ref('');
const failedIcons = ref(new Set<string>());
let searchRevision = 0;
let versionRevision = 0;
let timer: ReturnType<typeof setTimeout> | undefined;
const providerTab = computed(() => !['custom', 'import'].includes(activeTab.value));
const tabLabel = (id: string) => {
  const tab = tabs.find(item => item.id === id);
  if (id === 'custom') return props.t('custom');
  if (id === 'import') return props.t('import');
  if (id === 'ftb-app') return props.t('ftbAppImport');
  return tab?.label || '';
};
const providerIcon = computed(() => tabs.find(tab => tab.id === activeTab.value)?.icon || grassIcon);
const currentInstanceIcon = computed(() => selectedPack.value?.icon && !failedIcons.value.has(selectedPack.value.icon) ? selectedPack.value.icon : activeTab.value === 'custom' ? grassIcon : providerIcon.value);
const suggestedName = computed(() => selectedPack.value?.name || (activeTab.value === 'import' ? importFileName.value.replace(/\.[^.]+$/, '') : selectedVersion.value ? `${selectedVersion.value}${selectedLoader.value === 'Vanilla' ? '' : ` ${selectedLoader.value}`}` : ''));
const displayName = computed(() => instanceName.value.trim() || suggestedName.value);
const filteredVersions = computed(() => availableVersions.value.filter(v =>
  v.version.toLowerCase().includes(versionSearchQuery.value.toLowerCase()) && (v.type === 'release' ? filterReleases.value : filterSnapshots.value)));
const canSubmit = computed(() => !props.busy && !loading.value && !versionsLoading.value && Boolean(displayName.value) &&
  (activeTab.value === 'custom' ? Boolean(selectedVersion.value) : activeTab.value === 'import' ? Boolean(importUrl.value.trim()) : Boolean(selectedPack.value && packVersionId.value)));

async function loadCatalog(append = false) {
  clearTimeout(timer);
  const revision = ++searchRevision;
  const provider = activeTab.value;
  loading.value = true;
  error.value = '';
  if (!append) {
    packs.value = []; hasMore.value = false; nextOffset.value = 0;
    selectedPack.value = null; versions.value = []; packVersionId.value = ''; versionsLoading.value = false; ++versionRevision;
  }
  try {
    if (provider === 'custom') {
      const result = await props.minecraftVersions();
      if (revision !== searchRevision) return;
      availableVersions.value = result.minecraftVersions;
      selectedVersion.value = result.minecraftVersions.find(v => v.recommended && v.type === 'release')?.version || result.minecraftVersions.find(v => v.type === 'release')?.version || '';
    } else if (provider !== 'import') {
      const result = await props.searchPacks(provider, packSearchQuery.value.trim(), nextOffset.value);
      if (revision !== searchRevision) return;
      const existing = append ? packs.value : [];
      const ids = new Set(existing.map(pack => pack.id));
      packs.value = [...existing, ...result.packs.filter(pack => !ids.has(pack.id))];
      nextOffset.value += result.packs.length;
      hasMore.value = result.hasMore;
    }
  } catch (problem) {
    if (revision === searchRevision) error.value = problem instanceof Error && problem.message ? problem.message : props.t('catalogLoadError');
  } finally { if (revision === searchRevision) loading.value = false; }
}
async function selectModpack(pack: PackEntry) {
  selectedPack.value = pack;
  packVersionId.value = ''; versions.value = []; versionError.value = '';
  const revision = ++versionRevision;
  versionsLoading.value = true;
  try {
    const result = await props.packVersions(activeTab.value, pack.id);
    if (revision !== versionRevision) return;
    versions.value = result.versions;
    packVersionId.value = result.versions[0]?.id || '';
  } catch (problem) {
    if (revision === versionRevision) versionError.value = problem instanceof Error && problem.message ? problem.message : props.t('catalogVersionsError');
  } finally { if (revision === versionRevision) versionsLoading.value = false; }
}
async function browseArchive() {
  error.value = '';
  try {
    const result = await props.browseArchive();
    if (result.archiveUrl) { importUrl.value = result.archiveUrl; importFileName.value = result.fileName || ''; }
  } catch (problem) { error.value = problem instanceof Error && problem.message ? problem.message : props.t('archiveBrowseError'); }
}
function submit() {
  if (!canSubmit.value) return;
  const shared = { name: displayName.value, group: instanceGroup.value.trim() };
  if (activeTab.value === 'custom') emit('create-quick', { ...shared, version: selectedVersion.value, loader: selectedLoader.value });
  else if (activeTab.value === 'import') emit('import-archive', { ...shared, url: importUrl.value.trim() });
  else if (selectedPack.value) emit('install-pack', { ...shared, provider: activeTab.value, packId: selectedPack.value.id, versionId: packVersionId.value });
}
watch(() => props.initialTab, tab => { if (tab) activeTab.value = tab; });
watch(activeTab, () => {
  clearTimeout(timer); ++searchRevision; ++versionRevision;
  selectedPack.value = null; versions.value = []; packVersionId.value = ''; versionError.value = ''; versionsLoading.value = false;
  packSearchQuery.value = ''; void loadCatalog();
}, { immediate: true });
watch(packSearchQuery, () => {
  clearTimeout(timer); ++searchRevision; ++versionRevision;
  selectedPack.value = null; packs.value = []; versions.value = []; packVersionId.value = ''; hasMore.value = false; versionsLoading.value = false;
  if (providerTab.value) { loading.value = true; timer = setTimeout(() => void loadCatalog(), 300); }
}, { flush: 'sync' });
onScopeDispose(() => { clearTimeout(timer); ++searchRevision; ++versionRevision; });
</script>

<template>
  <div class="modal-overlay" @click.self="emit('close')">
    <form class="modal-dialog create-instance-window glass-surface" role="dialog" aria-modal="true" :aria-label="t('createInstanceTitle')" @submit.prevent="submit">
      <header class="create-modal-header">
        <div class="create-header-left">
          <div class="cube-icon-box"><img :src="providerIcon" alt="" width="24" height="24" /></div>
          <div class="create-header-titles"><h2>{{ t('createInstanceTitle') }}</h2><p class="create-subtitle">{{ tabLabel(activeTab) }}</p></div>
        </div>
        <button class="modal-close-btn" type="button" :aria-label="t('close')" @click="emit('close')">✕</button>
      </header>
      <div class="instance-metadata-card">
        <div class="meta-icon-container"><div class="block-icon-art"><img :src="currentInstanceIcon" class="block-icon-img" alt="" @error="failedIcons.add(currentInstanceIcon)" /></div></div>
        <div class="meta-fields-grid">
          <div class="meta-field-row name-row"><label for="new-inst-name">{{ t('name') }}</label><input id="new-inst-name" v-model="instanceName" class="glass-input" :placeholder="suggestedName" autocomplete="off" /></div>
          <div class="meta-field-row"><label for="new-inst-group">{{ t('group') }}</label><input id="new-inst-group" v-model="instanceGroup" class="glass-input" list="new-inst-groups" :placeholder="t('allGroups')" /><datalist id="new-inst-groups"><option v-for="g in groups" :key="g" :value="g" /></datalist></div>
        </div>
      </div>
      <div class="create-workspace-body">
        <aside class="platform-sidebar" :aria-label="t('sources')"><button v-for="tab in tabs" :key="tab.id" :data-source="tab.id" class="platform-tab" :class="{ 'is-active': activeTab === tab.id }" type="button" @click="activeTab = tab.id"><span class="platform-icon"><img :src="tab.icon" class="platform-tab-icon" alt="" /></span><span class="platform-label">{{ tabLabel(tab.id) }}</span><span v-if="activeTab === tab.id" class="active-chevron">›</span></button></aside>
        <div class="platform-content">
          <div v-if="error" class="catalog-status" role="alert"><p>{{ error }}</p><button class="btn-subtle" type="button" @click="loadCatalog()">{{ t('retry') }}</button></div>
          <div v-if="activeTab === 'custom'" class="tab-pane-container">
            <div class="custom-tab-header"><h3>{{ t('custom') }}</h3><p class="custom-subtitle-text">{{ t('customSubtitle') }}</p></div>
            <div class="version-search-box"><input v-model="versionSearchQuery" type="search" class="version-search-input" :placeholder="t('searchVersions')" :aria-label="t('searchVersions')" /></div>
            <div class="catalog-filter-row"><label class="filter-check"><input v-model="filterReleases" type="checkbox" />{{ t('releases') }}</label><label class="filter-check"><input v-model="filterSnapshots" type="checkbox" />{{ t('snapshots') }} / Beta / Alpha</label><button class="btn-subtle" type="button" :disabled="loading" @click="loadCatalog()">{{ t('refresh') }}</button></div>
            <p v-if="loading" class="catalog-status" role="status">{{ t('working') }}</p>
            <div class="version-table-container"><table class="version-table"><thead><tr><th>{{ t('versionLabel') }}</th><th>{{ t('released') }}</th><th>{{ t('type') }}</th></tr></thead><tbody><tr v-for="v in filteredVersions" :key="v.version" class="version-row" :class="{ 'is-selected': selectedVersion === v.version }"><td><button class="version-choice" type="button" :aria-pressed="selectedVersion === v.version" @click="selectedVersion = v.version">{{ v.version }}</button></td><td>{{ v.released }}</td><td>{{ v.type }}</td></tr></tbody></table></div>
            <div class="mod-loader-card"><label for="new-inst-loader">{{ t('loaderLabel') }}</label><select id="new-inst-loader" v-model="selectedLoader" class="glass-select"><option v-for="loader in loaders" :key="loader" :value="loader">{{ loader }}</option></select></div>
          </div>
          <div v-else-if="activeTab === 'import'" class="tab-pane-container">
            <div class="custom-tab-header"><h3>{{ t('import') }}</h3><p class="custom-subtitle-text">{{ t('importExplanation') }}</p></div>
            <div class="import-panel"><button class="import-drop-zone" type="button" @click="browseArchive">{{ t('browseLocalArchive') }}</button><p v-if="importFileName" class="selected-file-badge">{{ importFileName }}</p><label for="import-url">{{ t('archiveUrl') }}</label><input id="import-url" v-model="importUrl" type="text" class="glass-input" :placeholder="t('archiveUrlPlaceholder')" /></div>
          </div>
          <div v-else class="tab-pane-container">
            <div class="platform-catalog-wrap">
              <div class="platform-catalog-header"><div class="platform-title-group"><h3>{{ tabLabel(activeTab) }}</h3><p v-if="activeTab === 'ftb-app'" class="custom-subtitle-text">{{ t('ftbAppExplanation') }}</p></div><button class="btn-subtle" type="button" :disabled="loading" @click="loadCatalog()">{{ t('refresh') }}</button></div>
              <div class="version-search-box"><input v-model="packSearchQuery" class="version-search-input" type="search" :placeholder="`${t('searchPacks')} ${tabLabel(activeTab)}`" :aria-label="t('searchPacks')" /></div>
              <div class="modpack-list-container" :aria-busy="loading">
                <div v-for="pack in packs" :key="pack.id" class="modpack-card" :class="{ 'is-selected': selectedPack?.id === pack.id }">
                  <div class="pack-logo-box"><img :src="pack.icon && !failedIcons.has(pack.icon) ? pack.icon : providerIcon" class="pack-logo-img" alt="" @error="failedIcons.add(pack.icon)" /></div>
                  <div class="modpack-info"><h4>{{ pack.name }}</h4><p class="modpack-description">{{ pack.description }}</p><p class="modpack-meta">{{ [pack.author, pack.minecraft, pack.loader, pack.downloads ? t('downloadsCount').replace('{count}', String(pack.downloads)) : ''].filter(Boolean).join(' · ') }}</p></div>
                  <button class="btn-subtle" type="button" :aria-pressed="selectedPack?.id === pack.id" @click="selectModpack(pack)">{{ selectedPack?.id === pack.id ? t('packSelected') : t('selectPack') }}</button>
                </div>
                <p v-if="loading" class="catalog-status" role="status">{{ t('working') }}</p>
                <p v-else-if="!error && !packs.length" class="catalog-status">{{ t('noPacks') }}</p>
                <button v-if="hasMore" class="btn-subtle catalog-more" type="button" :disabled="loading" @click="loadCatalog(true)">{{ t('loadMore') }}</button>
              </div>
              <div v-if="selectedPack" class="pack-version-selection">
                <p v-if="versionsLoading" role="status">{{ t('working') }}</p>
                <div v-else-if="versionError" role="alert"><p>{{ versionError }}</p><button class="btn-subtle" type="button" @click="selectModpack(selectedPack)">{{ t('retry') }}</button></div>
                <template v-else><label for="pack-version">{{ t('packVersion') }}</label><select id="pack-version" v-model="packVersionId" class="glass-select"><option v-for="v in versions" :key="v.id" :value="v.id">{{ [v.name, v.minecraft, v.loader].filter(Boolean).join(' · ') }}</option></select><p v-if="!versions.length">{{ t('noPackVersions') }}</p></template>
              </div>
            </div>
          </div>
        </div>
      </div>
      <footer class="create-modal-footer"><span class="secondary-note">{{ displayName }}</span><div class="footer-right"><button class="btn-subtle" type="button" @click="emit('close')">{{ t('cancel') }}</button><button class="btn-primary ok-button" type="submit" :disabled="!canSubmit">{{ t('ok') }}</button></div></footer>
    </form>
  </div>
</template>

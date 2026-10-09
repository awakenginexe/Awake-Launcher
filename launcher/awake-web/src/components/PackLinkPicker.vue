<script setup lang="ts">
import { onUnmounted, ref } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import type { PackEntry, PackVersion } from '../bridge/catalog.ts';
import type { InstalledPack } from '../features/library/packUpdates.ts';
import type { PackService } from './PackVersionPicker.vue';

const props = defineProps<{ id: string; pack?: InstalledPack; running: boolean; busy: boolean; service: PackService; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (event: 'linked'): void }>();
const provider = ref(props.pack?.provider || 'curseforge');
const query = ref(props.pack?.name || '');
const packs = ref<PackEntry[]>([]);
const hasMore = ref(false);
let offset = 0;
const selected = ref<PackEntry>();
const versions = ref<PackVersion[]>([]);
const release = ref('');
const loading = ref(false);
const error = ref('');
let revision = 0;
onUnmounted(() => { ++revision; });
function reset() { ++revision; packs.value = []; hasMore.value = false; offset = 0; selected.value = undefined; versions.value = []; release.value = ''; error.value = ''; loading.value = false; }
async function search(more = false) {
  if (!more) reset();
  const request = ++revision;
  loading.value = true;
  try {
    const result = await props.service.search(provider.value, query.value.trim(), offset);
    if (request === revision) { packs.value.push(...result.packs); hasMore.value = result.hasMore; offset += 25; }
  } catch (reason) { if (request === revision) error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { if (request === revision) loading.value = false; }
}
async function select(pack: PackEntry) {
  const request = ++revision;
  selected.value = pack; versions.value = []; release.value = ''; error.value = ''; loading.value = true;
  try {
    const result = await props.service.releases(provider.value, pack.id);
    if (request === revision) versions.value = result.versions;
  } catch (reason) { if (request === revision) error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { if (request === revision) loading.value = false; }
}
async function link() {
  if (!selected.value || !release.value || loading.value || props.running || props.busy) return;
  const request = ++revision;
  loading.value = true; error.value = '';
  try {
    await props.service.link(props.id, provider.value, selected.value.id, release.value);
    if (request === revision) emit('linked');
  } catch (reason) { if (request === revision) error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { if (request === revision) loading.value = false; }
}
</script>

<template>
  <section class="pack-link-section" :aria-busy="loading">
    <h3>{{ t('packLinkTitle') }}</h3>
    <p class="editor-hint">{{ t('packLinkExplanation') }}</p>
    <p v-if="running" class="editor-hint">{{ t('packLinkStopGame') }}</p>
    <div class="editor-toolbar">
      <label for="pack-link-provider">{{ t('packLinkProvider') }}</label>
      <select id="pack-link-provider" v-model="provider" class="glass-input" :disabled="loading || busy || running" @change="reset">
        <option value="curseforge">CurseForge</option><option value="modrinth">Modrinth</option><option value="atlauncher">ATLauncher</option>
      </select>
    </div>
    <div class="editor-toolbar">
      <label for="pack-link-query">{{ t('searchPacks') }}</label>
      <input id="pack-link-query" v-model="query" type="search" class="glass-input" :disabled="loading || busy || running" @input="reset" @keydown.enter.prevent="search()" />
      <button class="btn-subtle pack-link-search" :disabled="loading || busy || running" @click="search()">{{ t('packLinkSearch') }}</button>
    </div>
    <p v-if="loading" class="editor-hint" role="status">{{ t('working') }}</p>
    <p v-if="error" role="alert">{{ error }}</p>
    <ul v-if="packs.length" class="editor-file-list pack-link-results">
      <li v-for="pack in packs" :key="pack.id" class="editor-file-row"><div class="editor-file-copy"><strong>{{ pack.name }}</strong><p>{{ pack.author }}</p></div><button class="btn-subtle" :aria-pressed="selected?.id === pack.id" :disabled="loading || busy || running" @click="select(pack)">{{ selected?.id === pack.id ? t('packSelected') : t('selectPack') }}</button></li>
    </ul>
    <p v-else-if="!loading" class="editor-hint">{{ t('packLinkSearchHint') }}</p>
    <button v-if="hasMore" class="btn-subtle pack-link-more" :disabled="loading || busy || running" @click="search(true)">{{ t('loadMore') }}</button>
    <template v-if="selected">
      <label for="pack-link-release">{{ t('packLinkInstalledRelease') }}</label>
      <div class="editor-toolbar">
        <select id="pack-link-release" v-model="release" class="glass-input" :disabled="loading || busy || running || !versions.length"><option value="">{{ t('packLinkChooseRelease') }}</option><option v-for="version in versions" :key="version.id" :value="version.id">{{ [version.name, version.minecraft, version.loader].filter(Boolean).join(' · ') }}</option></select>
        <button class="btn-primary pack-link-confirm" :disabled="loading || busy || running || !release" @click="link">{{ t('packLinkConfirm') }}</button>
      </div>
      <p v-if="!loading && !versions.length && !error" class="editor-hint">{{ t('noPackVersions') }}</p>
    </template>
  </section>
</template>

<style scoped>
.pack-link-section { padding-bottom: 18px; margin-bottom: 18px; border-bottom: 1px solid var(--border); }
.editor-toolbar { flex-wrap: wrap; }
.glass-input { flex: 1; min-width: 0; max-width: 100%; }
.pack-link-results { max-height: 180px; overflow: auto; }
.pack-link-confirm { color: var(--accent-contrast); background: #1d4ed8; }
</style>

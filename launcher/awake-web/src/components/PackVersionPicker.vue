<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import type { InstalledPack } from '../features/library/packUpdates.ts';
import type { CatalogResult, PackVersion } from '../bridge/catalog.ts';

export interface PackService {
  versions: (id: string) => Promise<CatalogResult>;
  update: (id: string, version: string) => Promise<unknown>;
  reminder: (id: string, enabled: boolean) => Promise<unknown>;
}
const props = defineProps<{ id: string; pack: InstalledPack; running: boolean; service: PackService; t: (key: MessageKey) => string }>();
const versions = ref<PackVersion[]>([]);
const selected = ref('');
const loading = ref(false);
const error = ref('');
const reminders = ref(props.pack.reminders);
const skippedVersion = ref(props.pack.skippedVersion);
let closed = false;
onUnmounted(() => { closed = true; });
async function check() {
  if (loading.value) return;
  loading.value = true; error.value = '';
  try {
    const result = await props.service.versions(props.id);
    if (closed) return;
    versions.value = result.versions;
    selected.value = result.versions[0]?.id || '';
  } catch (reason) { if (!closed) error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { loading.value = false; }
}
async function update() {
  loading.value = true; error.value = '';
  try { await props.service.update(props.id, selected.value); }
  catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { loading.value = false; }
}
async function setReminders(enabled: boolean) {
  loading.value = true; error.value = '';
  try {
    await props.service.reminder(props.id, enabled);
    reminders.value = enabled;
    if (enabled) skippedVersion.value = '';
  }
  catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { loading.value = false; }
}
onMounted(check);
</script>

<template>
  <section class="pack-version-section" :aria-busy="loading">
    <div class="editor-toolbar"><div class="editor-file-copy"><strong>{{ pack.name }}</strong><p>{{ t('packInstalled') }}: {{ pack.versionName || pack.versionId }}</p></div><button class="btn-subtle" :disabled="loading" @click="check">{{ t(loading ? 'working' : 'packCheckUpdates') }}</button></div>
    <label :for="`pack-version-${id}`">{{ t('packVersion') }}</label>
    <div class="editor-toolbar">
      <select :id="`pack-version-${id}`" v-model="selected" class="glass-input" :disabled="loading || !versions.length">
        <option v-for="version in versions" :key="version.id" :value="version.id">{{ [version.name, version.minecraft, version.loader].filter(Boolean).join(' · ') }}{{ version.id === pack.versionId ? ` (${t('packInstalled')})` : '' }}</option>
      </select>
      <button class="btn-primary" :disabled="loading || running || !selected || selected === pack.versionId" @click="update">{{ t('packChangeVersion') }}</button>
    </div>
    <p v-if="!loading && !versions.length && !error" class="editor-hint">{{ t('noPackVersions') }}</p>
    <p v-if="error" role="alert">{{ error }}</p>
    <label class="editor-mod-toggle"><input type="checkbox" :checked="reminders" :disabled="loading" @change="setReminders(!reminders)" />{{ t('packRemindLaunch') }}</label>
    <button v-if="skippedVersion && reminders" class="btn-subtle" :disabled="loading" @click="setReminders(true)">{{ t('packResetReminder') }}</button>
  </section>
</template>

<style scoped>
.pack-version-section { margin-bottom: 18px; padding-bottom: 18px; border-bottom: 1px solid var(--border-color); }
.editor-toolbar { flex-wrap: wrap; }
select { flex: 1; min-width: 0; max-width: 100%; }
.editor-mod-toggle { display: flex; gap: 8px; margin-top: 12px; }
</style>

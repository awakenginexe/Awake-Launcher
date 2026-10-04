<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import '../styles/java-picker.css';

export interface JavaSettings {
  ok: boolean; error?: string; profile: string; inherited: boolean; globalProfile: string;
  path: string; version: string; vendor: string; majors: number[]; running: boolean; canceled?: boolean;
}
export interface JavaService {
  settings: (id: string) => Promise<unknown>;
  select: (id: string, profile: string) => Promise<unknown>;
  browse: (id: string) => Promise<unknown>;
}
const props = defineProps<{ instanceId?: string; revision?: number; service: JavaService; t: (key: MessageKey) => string }>();
const options: { id: string; name: string | MessageKey; translated?: boolean; badge: MessageKey; description: MessageKey }[] = [
  { id: 'awake', name: 'Awake Optimized', badge: 'javaRecommended', description: 'javaAwakeDescription' },
  { id: 'minecraft', name: 'Minecraft Default', badge: 'javaMostCompatible', description: 'javaMinecraftDescription' },
  { id: 'microsoft', name: 'Microsoft OpenJDK', badge: 'javaPerformanceCompatibility', description: 'javaProviderDescription' },
  { id: 'graalvm', name: 'GraalVM', badge: 'javaExperimentalPerformance', description: 'javaGraalDescription' },
  { id: 'temurin', name: 'Eclipse Temurin', badge: 'javaStable', description: 'javaProviderDescription' },
  { id: 'zulu', name: 'Azul Zulu', badge: 'javaAlternative', description: 'javaProviderDescription' },
  { id: 'oracle', name: 'Oracle OpenJDK', badge: 'javaStandard', description: 'javaProviderDescription' },
  { id: 'custom', name: 'javaCustom', translated: true, badge: 'javaAdvanced', description: 'javaCustomDescription' },
];
const data = ref<JavaSettings>();
const working = ref(false);
const error = ref('');
const saved = ref(false);
const choice = computed(() => options.find(option => option.id === data.value?.profile) || options[0]);
const descriptions = [...new Set(options.map(option => option.description))];
const name = (option: typeof options[number]) => option.translated ? props.t(option.name as MessageKey) : option.name;
async function load() {
  working.value = true; error.value = '';
  try {
    const result = await props.service.settings(props.instanceId || '') as JavaSettings;
    if (!result.ok) throw new Error(result.error || props.t('editorReadError'));
    data.value = result;
  } catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { working.value = false; }
}
async function select(profile: string, browse = false) {
  if (working.value || data.value?.running) return;
  working.value = true; error.value = ''; saved.value = false;
  try {
    const result = await (browse ? props.service.browse(props.instanceId || '') : props.service.select(props.instanceId || '', profile)) as JavaSettings;
    if (result.canceled) return;
    if (!result.ok) throw new Error(result.error || props.t('editorActionError'));
    data.value = result; saved.value = true;
  } catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { working.value = false; }
}
onMounted(load);
watch(() => props.revision, () => { if (!working.value) void load(); });
</script>

<template>
  <section class="java-picker" :aria-label="t('javaRuntime')" :aria-busy="working">
    <div class="java-picker-heading"><h3>{{ t('javaRuntime') }}</h3><span class="editor-feedback" role="status">{{ working ? t('working') : saved ? t('editorSaved') : '' }}</span></div>
    <p class="java-picker-hint">{{ instanceId ? t('javaInstanceHint') : t('javaGlobalHint') }}</p>
    <p v-if="error" class="editor-error" role="alert">{{ error }} <button class="btn-subtle" :disabled="working" @click="load">{{ t('retry') }}</button></p>
    <template v-if="data">
      <label v-if="instanceId" class="check java-inherit"><input type="checkbox" :checked="data.inherited" :disabled="working || data.running" @change="select(($event.target as HTMLInputElement).checked ? 'inherit' : data.profile)" />{{ t('javaUseGlobal') }}<span>{{ name(options.find(option => option.id === data?.globalProfile) || options[0]) }}</span></label>
      <p v-if="data.majors.length" class="java-picker-hint">{{ t('javaCompatibleVersions').replace('{versions}', data.majors.join(', ')) }}</p>
      <div class="java-options" :class="{ 'is-unavailable': data.running || data.inherited }" role="group" :aria-label="t('javaRuntime')">
        <button v-for="option in options" :key="option.id" type="button" class="java-option" :class="{ 'is-selected': !data.inherited && data.profile === option.id }" :aria-pressed="!data.inherited && data.profile === option.id" :disabled="working || data.running || data.inherited" :data-java-profile="option.id" @click="select(option.id, option.id === 'custom' && (data.profile !== 'custom' || !data.path))">
          <strong>{{ name(option) }}</strong><span class="java-option-badge">{{ t(option.badge) }}</span>
        </button>
      </div>
      <div class="java-picker-description">
        <p>{{ t(choice.description) }}</p>
        <span v-for="description in descriptions" :key="description" class="java-description-size" aria-hidden="true">{{ t(description) }}</span>
      </div>
      <div v-if="data.profile === 'custom'" class="java-custom-path"><span>{{ t('javaExecutable') }}</span><output>{{ data.path || t('javaChooseExecutable') }}</output><button type="button" class="btn-subtle" :disabled="working || data.running || data.inherited" @click="select('custom', true)">{{ t('javaBrowse') }}</button></div>
      <details v-if="data.path && data.profile !== 'custom'" class="java-last-runtime"><summary>{{ t('javaLastRuntime') }}</summary><p>{{ [data.vendor, data.version].filter(Boolean).join(' · ') }}</p><output>{{ data.path }}</output></details>
      <p v-if="data.running" class="java-picker-hint">{{ t('javaStopGame') }}</p>
    </template>
  </section>
</template>

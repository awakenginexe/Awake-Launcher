<script setup lang="ts">
import { onMounted, ref } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
export interface GpuService {
  settings: () => Promise<unknown>;
  select: (mode: string) => Promise<unknown>;
  openWindows: () => Promise<unknown>;
}
interface GpuSettings { ok: boolean; supported: boolean; mode: string; devices: { name: string }[]; powerSavingName?: string; highPerformanceName?: string }
const props = defineProps<{ service: GpuService; t: (key: MessageKey) => string }>();
const data = ref<GpuSettings>();
const working = ref(false);
const error = ref('');
const saved = ref(false);
async function run(mode?: string) {
  if (working.value) return;
  working.value = true; error.value = ''; saved.value = false;
  try {
    data.value = await (mode ? props.service.select(mode) : props.service.settings()) as GpuSettings;
    saved.value = Boolean(mode);
  } catch (failure) { error.value = failure instanceof Error ? failure.message : String(failure); }
  finally { working.value = false; }
}
async function openWindows() {
  try { await props.service.openWindows(); }
  catch (failure) { error.value = failure instanceof Error ? failure.message : String(failure); }
}
onMounted(() => { void run(); });
</script>
<template>
  <div class="setting-block gpu-picker">
    <label for="gpu-preference">{{ t('gpuSelection') }}</label>
    <template v-if="data?.supported">
      <ul class="gpu-devices"><li v-for="(device, index) in data.devices" :key="index">{{ device.name }}</li></ul>
      <select id="gpu-preference" class="styled-input" :value="data.mode" :disabled="working" @change="run(($event.target as HTMLSelectElement).value)">
        <option value="automatic">{{ t('gpuAutomatic') }}</option>
        <option value="powerSaving">{{ t('gpuPowerSaving') }}{{ data.powerSavingName ? ` — ${data.powerSavingName}` : '' }}</option>
        <option value="highPerformance">{{ t('gpuHighPerformance') }}{{ data.highPerformanceName ? ` — ${data.highPerformanceName}` : '' }}</option>
      </select>
      <p class="setting-hint">{{ t('gpuGlobalHint') }}</p>
      <p class="setting-hint">{{ t('gpuWindowsHint') }}</p>
      <button class="quiet" type="button" @click="openWindows">{{ t('gpuWindowsSettings') }}</button>
      <p v-if="saved" class="setting-hint" role="status">{{ t('gpuSaved') }}</p>
    </template>
    <p v-else-if="data" class="setting-hint">{{ t('gpuUnsupported') }}</p>
    <p v-if="error" role="alert">{{ error }} <button class="quiet" @click="run()">{{ t('retry') }}</button></p>
  </div>
</template>

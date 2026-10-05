<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import type { GpuSettings } from '../features/library/hardware.ts';
import { privateGpuName } from '../features/library/hardware.ts';
import { useHardwarePrivacy } from '../composables/useHardwarePrivacy.ts';
import HardwareVisibility from './HardwareVisibility.vue';
import ThemedSelect from './ThemedSelect.vue';
export interface GpuService {
  settings: () => Promise<unknown>;
  select: (mode: string) => Promise<unknown>;
  openWindows: () => Promise<unknown>;
}
const props = defineProps<{ service: GpuService; t: (key: MessageKey) => string; initial?: GpuSettings; deferred?: boolean }>();
const emit = defineEmits<{ (e: 'choose', mode: string): void }>();
const { gpuVisible } = useHardwarePrivacy();
const data = ref<GpuSettings | undefined>(props.initial);
const working = ref(false);
const error = ref('');
const saved = ref(false);
const options = computed(() => [
  { value: 'automatic', label: props.t('gpuAutomatic') },
  { value: 'powerSaving', label: props.t('gpuPowerSaving') + (data.value?.powerSavingName ? ` — ${privateGpuName(data.value.powerSavingName, gpuVisible.value)}` : '') },
  { value: 'highPerformance', label: props.t('gpuHighPerformance') + (data.value?.highPerformanceName ? ` — ${privateGpuName(data.value.highPerformanceName, gpuVisible.value)}` : '') },
]);
async function run(mode?: string) {
  if (mode && props.deferred && data.value) {
    data.value = { ...data.value, mode };
    emit('choose', mode);
    return;
  }
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
onMounted(() => { if (!data.value) void run(); });
</script>
<template>
  <div class="setting-block gpu-picker">
    <div class="hardware-label"><label for="gpu-preference">{{ t('gpuSelection') }}</label><HardwareVisibility :visible="gpuVisible" :label="t(gpuVisible ? 'hideGpu' : 'showGpu')" @toggle="gpuVisible = !gpuVisible" /></div>
    <p v-if="working" class="setting-hint" role="status">{{ t('working') }}</p>
    <template v-if="data?.supported">
      <ul class="gpu-devices"><li v-for="(device, index) in data.devices" :key="index">{{ privateGpuName(device.name, gpuVisible) }}</li></ul>
      <ThemedSelect id="gpu-preference" :label="t('gpuSelection')" :model-value="data.mode" :options="options" :disabled="working" @update:model-value="run($event)" />
      <p class="setting-hint">{{ t('gpuGlobalHint') }}</p>
      <p class="setting-hint">{{ t('gpuWindowsHint') }}</p>
      <button class="quiet" type="button" @click="openWindows">{{ t('gpuWindowsSettings') }}</button>
      <p v-if="saved" class="setting-hint" role="status">{{ t('gpuSaved') }}</p>
    </template>
    <p v-else-if="data" class="setting-hint">{{ t('gpuUnsupported') }}</p>
    <p v-if="error" role="alert">{{ error }} <button class="quiet" @click="run()">{{ t('retry') }}</button></p>
  </div>
</template>

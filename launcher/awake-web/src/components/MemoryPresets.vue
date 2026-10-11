<script setup lang="ts">
import type { MessageKey } from '../i18n/catalogs.ts';
import HardwareVisibility from './HardwareVisibility.vue';
import { useHardwarePrivacy } from '../composables/useHardwarePrivacy.ts';
import { ramPresets, memoryRisk, presetFits, installedMemoryMb } from '../features/library/memory.ts';

defineProps<{ modelValue: number; totalMemoryMb: number; disabled?: boolean; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (event: 'update:modelValue', value: number): void }>();
const { ramVisible } = useHardwarePrivacy();
</script>

<template>
  <div class="ram-presets-row" role="group" :aria-label="t('maxRamLabel')">
    <button v-for="preset in ramPresets" :key="preset.mb" type="button" class="pill-btn ram-preset"
      :class="[memoryRisk(preset.mb, totalMemoryMb), { 'is-active': modelValue === preset.mb }]"
      :aria-pressed="modelValue === preset.mb"
      :disabled="disabled || !presetFits(preset.mb, totalMemoryMb)"
      :title="!presetFits(preset.mb, totalMemoryMb) ? t('ramPresetUnavailable') : t(memoryRisk(preset.mb, totalMemoryMb) === 'danger' ? 'ramDanger' : memoryRisk(preset.mb, totalMemoryMb) === 'caution' ? 'ramCaution' : 'ramSafe')"
      @click="emit('update:modelValue', preset.mb)">{{ preset.label }}</button>
  </div>
  <div class="ram-legend setting-hint">
    <span class="safe">{{ t('ramSafe') }}</span><span class="caution">{{ t('ramCaution') }}</span><span class="danger">{{ t('ramDanger') }}</span>
  </div>
  <div v-if="totalMemoryMb" class="hardware-label ram-hint">
    <span class="setting-hint installed-ram">{{ t('installedRam') }}: {{ ramVisible ? installedMemoryMb(totalMemoryMb) / 1024 : '-----' }} GB</span>
    <HardwareVisibility :visible="ramVisible" :label="t(ramVisible ? 'hideRam' : 'showRam')" @toggle="ramVisible = !ramVisible" />
  </div>
  <p class="setting-hint">{{ t('ramOverrideHint') }}</p>
</template>

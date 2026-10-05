<script setup lang="ts">
import { computed } from 'vue';
import { jvmPresets, parseJvmPreset } from '../features/library/jvm.ts';
import type { JvmPreset } from '../features/library/jvm.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
const props = defineProps<{ id: string; modelValue: JvmPreset; disabled?: boolean; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (e: 'update:modelValue', value: JvmPreset): void }>();
const choice = computed(() => jvmPresets.find(p => p.id === props.modelValue) ?? jvmPresets[0]!);
</script>

<template>
  <div class="setting-block jvm-preset-picker">
    <label :for="id">{{ t('jvmPreset') }}</label>
    <select :id="id" class="styled-input" :value="modelValue" :disabled="disabled" @change="emit('update:modelValue', parseJvmPreset(($event.target as HTMLSelectElement).value))">
      <option v-for="preset in jvmPresets" :key="preset.id" :value="preset.id">{{ t(preset.label) }}</option>
    </select>
    <p class="setting-hint">{{ t(choice.description) }}</p>
  </div>
</template>

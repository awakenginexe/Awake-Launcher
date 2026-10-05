<script setup lang="ts">
import { computed } from 'vue';
import { jvmPresets, parseJvmPreset } from '../features/library/jvm.ts';
import type { JvmPreset } from '../features/library/jvm.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
import ThemedSelect from './ThemedSelect.vue';
const props = defineProps<{ id: string; modelValue: JvmPreset; disabled?: boolean; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (e: 'update:modelValue', value: JvmPreset): void }>();
const choice = computed(() => jvmPresets.find(p => p.id === props.modelValue) ?? jvmPresets[0]!);
const options = computed(() => jvmPresets.map(preset => ({ value: preset.id, label: props.t(preset.label) })));
</script>

<template>
  <div class="setting-block jvm-preset-picker">
    <label :for="id">{{ t('jvmPreset') }}</label>
    <ThemedSelect :id="id" :label="t('jvmPreset')" :model-value="modelValue" :options="options" :disabled="disabled" @update:model-value="emit('update:modelValue', parseJvmPreset($event))" />
    <p class="setting-hint">{{ t(choice.description) }}</p>
  </div>
</template>

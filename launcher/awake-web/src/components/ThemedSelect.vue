<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import '../styles/language-select.css';

const props = defineProps<{ id: string; label: string; modelValue: string; options: { value: string; label: string }[]; disabled?: boolean }>();
const emit = defineEmits<{ (event: 'update:modelValue', value: string): void }>();
const trigger = ref<HTMLButtonElement>();
const menu = ref<HTMLElement>();
const open = ref(false);
const active = ref(0);
const position = ref<Record<string, string>>({ visibility: 'hidden' });
const selected = computed(() => props.options.find(option => option.value === props.modelValue));
function close() { open.value = false; }
async function show(index = props.options.findIndex(option => option.value === props.modelValue)) {
  if (props.disabled || !props.options.length) return;
  active.value = Math.max(0, index);
  position.value = { visibility: 'hidden' };
  open.value = true;
  await nextTick();
  await placeMenu();
}
async function placeMenu() {
  if (!trigger.value || !menu.value || !open.value) return;
  const rect = trigger.value.getBoundingClientRect();
  const width = Math.min(rect.width, window.innerWidth - 16);
  position.value = { ...position.value, width: `${width}px` };
  await nextTick();
  if (!menu.value || !open.value) return;
  const below = window.innerHeight - rect.bottom - 14;
  const above = rect.top - 14;
  const upward = below < menu.value.scrollHeight && above > below;
  const height = Math.min(menu.value.scrollHeight, Math.max(0, upward ? above : below));
  position.value = {
    left: `${Math.max(8, Math.min(rect.left, window.innerWidth - width - 8))}px`,
    top: `${upward ? rect.top - height - 6 : rect.bottom + 6}px`,
    width: `${width}px`, maxHeight: `${height}px`,
  };
}
function choose(index: number) {
  const option = props.options[index];
  if (!option || props.disabled) return;
  emit('update:modelValue', option.value);
  close();
  trigger.value?.focus();
}
function move(index: number) {
  active.value = (index + props.options.length) % props.options.length;
  void nextTick(() => menu.value?.children[active.value]?.scrollIntoView({ block: 'nearest' }));
}
function keyboard(event: KeyboardEvent) {
  if (props.disabled || !props.options.length) return;
  const key = event.key;
  if (key === 'Escape' && open.value) { event.preventDefault(); event.stopPropagation(); close(); }
  else if (key === 'Tab') close();
  else if (key === 'ArrowDown' || key === 'ArrowUp' || key === 'Home' || key === 'End') {
    event.preventDefault();
    const index = key === 'Home' ? 0 : key === 'End' ? props.options.length - 1 : active.value + (key === 'ArrowDown' ? 1 : -1);
    if (!open.value) void show(key === 'Home' || key === 'End' ? index : undefined);
    else move(index);
  } else if (key === 'Enter' || key === ' ') {
    event.preventDefault();
    if (open.value) choose(active.value); else void show();
  } else if (key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) {
    const start = open.value ? active.value + 1 : 0;
    const index = props.options.findIndex((_, offset) => props.options[(start + offset) % props.options.length]!.label.toLocaleLowerCase().startsWith(key.toLocaleLowerCase()));
    if (index >= 0) {
      event.preventDefault();
      const match = (start + index) % props.options.length;
      if (open.value) move(match); else void show(match);
    }
  }
}
function outside(event: PointerEvent) {
  if (!trigger.value?.contains(event.target as Node) && !menu.value?.contains(event.target as Node)) close();
}
function scroll(event: Event) { if (open.value && !menu.value?.contains(event.target as Node)) void placeMenu(); }
watch(() => props.disabled, value => { if (value) close(); });
onMounted(() => {
  document.addEventListener('pointerdown', outside);
  document.addEventListener('scroll', scroll, true);
  window.addEventListener('resize', close);
});
onUnmounted(() => {
  document.removeEventListener('pointerdown', outside);
  document.removeEventListener('scroll', scroll, true);
  window.removeEventListener('resize', close);
});
</script>

<template>
  <button :id="id" ref="trigger" type="button" class="language-picker-trigger themed-select-trigger" role="combobox" :value="modelValue" :aria-label="label" aria-haspopup="listbox" :aria-controls="`${id}-listbox`" :aria-expanded="open" :aria-activedescendant="open ? `${id}-option-${active}` : undefined" :disabled="disabled" @click="open ? close() : show()" @keydown="keyboard" @blur="close">
    <span>{{ selected?.label }}</span>
    <svg class="language-picker-chevron" :class="{ 'is-open': open }" viewBox="0 0 16 16" aria-hidden="true"><path d="m4 6 4 4 4-4" /></svg>
  </button>
  <Teleport to="body">
    <div v-if="open" :id="`${id}-listbox`" ref="menu" class="language-picker-menu themed-select-menu" :style="position" role="listbox" :aria-label="label" @pointerdown.prevent>
      <div v-for="(option, index) in options" :id="`${id}-option-${index}`" :key="option.value" :data-value="option.value" class="language-picker-option" role="option" :aria-selected="modelValue === option.value" :class="{ 'is-active': active === index, 'is-selected': modelValue === option.value }" @pointermove="active = index" @click="choose(index)">
        <span>{{ option.label }}</span>
        <svg v-if="modelValue === option.value" class="language-picker-check" viewBox="0 0 16 16" aria-hidden="true"><path d="m3.5 8 3 3 6-6" /></svg>
      </div>
    </div>
  </Teleport>
</template>

<style>
.themed-select-trigger { font-size: 0.9rem; }
.themed-select-trigger > span { min-width: 0; overflow-wrap: anywhere; }
.themed-select-menu { position: fixed; right: auto; z-index: 1100; overflow-y: auto; font-size: 0.9rem; }
.themed-select-menu .language-picker-option { gap: 12px; }
</style>

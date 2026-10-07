<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, shallowRef, watch } from 'vue';
import SkinPreview from './SkinPreview.vue';
import ThemedSelect from './ThemedSelect.vue';
import { parseSkinState, skinChanged } from '../features/library/skins.ts';
import type { SkinEntry, SkinService, SkinState, SkinVariant } from '../features/library/skins.ts';
import type { AccountItem } from '../features/library/model.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
const props = defineProps<{ accounts: AccountItem[]; accountId: string; service: SkinService; t: (key: MessageKey) => string }>();
const emit = defineEmits<{ (event: 'close'): void; (event: 'account', id: string): void }>();
const dialog = ref<HTMLElement>();
const state = shallowRef<SkinState | null>(null), selected = shallowRef<SkinEntry | null>(null);
const variant = ref<SkinVariant>('CLASSIC'), capeId = ref(''), username = ref('');
const busy = ref(false), failure = ref(''), message = ref('');
const minecraftReset = ref(false);
const previousFocus = document.activeElement as HTMLElement | null;
let revision = 0;
const defaults = computed(() => state.value?.defaults.filter(item => item.variant === variant.value) ?? []);
const accountOptions = computed(() => props.accounts.map(account => ({ value: account.id, label: account.name, imageUrl: account.headUrl })));
const capeUrl = computed(() => state.value?.capes.find(cape => cape.id === capeId.value)?.textureUrl ?? '');
const hasChanges = computed(() => minecraftReset.value || skinChanged(state.value, selected.value, variant.value, capeId.value));
const canApply = computed(() => !busy.value && state.value?.editable && hasChanges.value);
function pick(entry: SkinEntry) { selected.value = entry; variant.value = entry.variant; minecraftReset.value = false; message.value = ''; }
function resetCurrent() {
  selected.value = state.value?.current ?? null;
  variant.value = selected.value?.variant ?? 'CLASSIC';
  capeId.value = state.value?.capeId ?? '';
  minecraftReset.value = false; failure.value = ''; message.value = '';
}
function resetMinecraft() {
  if (!state.value?.minecraftDefault) return;
  resetCurrent();
  selected.value = state.value.minecraftDefault;
  variant.value = selected.value.variant;
  minecraftReset.value = true; message.value = props.t('skinResetPending');
}
function changeModel(value: SkinVariant) {
  minecraftReset.value = false; message.value = '';
  variant.value = value;
  if (selected.value?.id.startsWith('default/')) {
    const name = selected.value.name;
    selected.value = state.value?.defaults.find(entry => entry.name === name && entry.variant === value) ?? null;
  }
}
function accept(value: unknown, id: string, preview = false) {
  const result = parseSkinState(value);
  if (result.accountId !== id) throw new Error(props.t('skinAccountChanged'));
  state.value = result;
  minecraftReset.value = false;
  selected.value = preview ? result.preview ?? result.current : result.current;
  variant.value = selected.value?.variant ?? 'CLASSIC';
  capeId.value = result.capeId;
  return result;
}
async function loadCapeTextures(generation: number, id: string) {
  if (!state.value?.capes.some(cape => !cape.textureUrl)) return;
  const result = parseSkinState(await props.service.command(id, 'capes'));
  if (generation !== revision) return;
  if (result.accountId !== id) throw new Error(props.t('skinAccountChanged'));
  state.value = result;
}
async function load() {
  const generation = ++revision, id = props.accountId;
  state.value = null; selected.value = null; failure.value = ''; message.value = ''; busy.value = true;
  try {
    const result = await props.service.state(id);
    if (generation !== revision) return;
    accept(result, id);
    await loadCapeTextures(generation, id);
  }
  catch (error) { if (generation === revision) failure.value = error instanceof Error ? error.message : String(error); }
  finally { if (generation === revision) busy.value = false; }
}
async function run(command: string) {
  if (busy.value) return;
  if (command === 'apply' && minecraftReset.value) command = 'reset';
  const generation = ++revision, id = props.accountId;
  busy.value = true; failure.value = ''; message.value = '';
  try {
    const result = await props.service.command(id, command, { id: selected.value?.id ?? '', variant: variant.value, capeId: capeId.value, username: username.value.trim() });
    if (generation !== revision) return;
    accept(result, id, command === 'browse' || command === 'lookup');
    if (command === 'apply' || command === 'reset') message.value = props.t('skinSaved');
    await loadCapeTextures(generation, id);
  } catch (error) { if (generation === revision) failure.value = error instanceof Error ? error.message : String(error); }
  finally { if (generation === revision) busy.value = false; }
}
function keydown(event: KeyboardEvent) {
  if (event.key === 'Escape') { event.stopPropagation(); if (!busy.value) emit('close'); }
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled), select:not(:disabled), [tabindex="0"]') ?? [])];
  const first = controls[0], last = controls.at(-1);
  if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}
watch(() => props.accountId, load, { immediate: true });
onMounted(() => dialog.value?.querySelector<HTMLButtonElement>('.modal-close-btn')?.focus());
onUnmounted(() => { revision++; if (previousFocus?.isConnected) previousFocus.focus(); });
</script>
<template>
  <div class="modal-overlay skins-overlay" @click.self="!busy && emit('close')">
    <section ref="dialog" class="modal-dialog glass-surface skins-dialog" role="dialog" aria-modal="true" :aria-label="t('skins')" :aria-busy="busy" @keydown="keydown">
      <header class="modal-header"><h2>{{ t('skins') }}</h2><button class="modal-close-btn" :disabled="busy" :aria-label="t('close')" @click="emit('close')">✕</button></header>
      <div class="modal-body skins-body">
        <div class="skin-tools">
          <div class="skin-field"><label for="skin-account">{{ t('accounts') }}</label><ThemedSelect id="skin-account" :label="t('accounts')" :model-value="accountId" :options="accountOptions" :disabled="busy" @update:model-value="id => emit('account', id)" /></div>
          <p v-if="state && !state.editable" class="skin-readonly">{{ t('skinSignInHint') }}</p>
          <form class="skin-search" @submit.prevent="run('lookup')"><label class="visually-hidden" for="skin-username">{{ t('skinUsername') }}</label><input id="skin-username" v-model="username" class="styled-input" maxlength="16" :disabled="busy" :placeholder="t('skinUsername')" autocomplete="off" /><button class="btn-subtle" :disabled="busy || !/^[A-Za-z0-9_]{1,16}$/.test(username.trim())">{{ t('skinSearch') }}</button></form>
          <p class="skin-search-hint">{{ t('skinSearchHint') }}</p>
          <button type="button" class="btn-subtle skin-select-file" :disabled="busy" @click="run('browse')">{{ t('skinSelectFile') }}</button>
          <div class="skin-model-field"><span>{{ t('skinModel') }}</span><div class="skin-model-options" role="group" :aria-label="t('skinModel')"><button type="button" class="btn-subtle" :aria-pressed="variant === 'CLASSIC'" :disabled="busy" @click="changeModel('CLASSIC')">{{ t('skinClassic') }}</button><button type="button" class="btn-subtle" :aria-pressed="variant === 'SLIM'" :disabled="busy" @click="changeModel('SLIM')">{{ t('skinSlim') }}</button></div><small>{{ t('skinModelHint') }}</small></div>
          <div class="skin-defaults"><h3>{{ t('skinDefaults') }}</h3><div class="skin-default-grid"><button v-for="entry in defaults" :key="entry.id" type="button" class="skin-default" :class="{ selected: selected?.id === entry.id }" :aria-pressed="selected?.id === entry.id" :disabled="busy" @click="pick(entry)"><img :src="entry.previewUrl" alt="" width="32" height="72" /><span>{{ entry.name }}</span></button></div></div>
          <button v-if="state?.current" type="button" class="btn-subtle" :disabled="busy" @click="pick(state.current)">{{ t('skinCurrent') }}</button>
          <div class="skin-capes"><h3>{{ t('skinCape') }}</h3><div class="skin-cape-grid" role="group" :aria-label="t('skinCape')">
            <button type="button" class="skin-cape-choice" :aria-pressed="capeId === ''" :disabled="busy || !state?.editable" @click="capeId = ''"><span class="skin-cape-none" aria-hidden="true">⊘</span><span>{{ t('skinNoCape') }}</span></button>
            <button v-for="cape in state?.capes" :key="cape.id" type="button" class="skin-cape-choice" :aria-pressed="capeId === cape.id" :disabled="busy || !state?.editable" @click="capeId = cape.id"><span class="skin-cape-image" aria-hidden="true" :style="{ backgroundImage: cape.textureUrl ? `url(${cape.textureUrl})` : undefined }" /><span>{{ cape.name }}</span></button>
          </div></div>
        </div>
        <div class="skin-display"><SkinPreview :texture-url="selected?.textureUrl ?? ''" :variant="variant" :name="selected?.name ?? ''" :cape-url="capeUrl" :t="t" /><p class="skin-selected-name">{{ selected?.name ?? t('skinNoPreview') }}</p><p v-if="busy" class="skin-feedback" role="status">{{ t('skinWorking') }}</p><p v-if="failure" class="skin-feedback skin-error" role="alert">{{ failure }}</p><p v-if="message" class="skin-feedback" role="status">{{ message }}</p></div>
      </div>
      <footer class="modal-footer"><div class="skin-reset-actions"><button type="button" class="btn-subtle skin-reset-current" :disabled="busy || !hasChanges" @click="resetCurrent">{{ t('skinReset') }}</button><button type="button" class="btn-subtle skin-reset-minecraft" :disabled="busy || !state?.editable || !state?.minecraftDefault" @click="resetMinecraft">{{ t('skinResetMinecraft') }}</button></div><button type="button" class="btn-primary" :disabled="!canApply" @click="run('apply')">{{ t('skinApply') }}</button></footer>
    </section>
  </div>
</template>
<style scoped>
.skins-overlay { backdrop-filter: none; -webkit-backdrop-filter: none; }
.skins-dialog { width: min(1100px, calc(100vw - 48px)); max-width: 1100px; height: min(720px, calc(100vh - 48px)); max-height: none; background: #0a121e; }
.skins-body { display: grid; grid-template-columns: minmax(300px, 1.15fr) minmax(280px, 1fr); gap: 28px; overflow: hidden; }
.skin-tools { display: flex; flex-direction: column; gap: 16px; min-width: 0; min-height: 0; overflow-y: auto; padding: 2px 12px 2px 2px; scrollbar-gutter: stable; overscroll-behavior: contain; }
.skin-tools > * { flex-shrink: 0; }
.skin-display { display: flex; flex-direction: column; min-height: 0; overflow: hidden; }
.skin-field { display: grid; gap: 8px; font-size: 13px; }
.skin-field select { width: 100%; }
.skin-search { display: flex; gap: 8px; }
.skin-search input { width: 100%; min-width: 0; }
.skin-select-file { align-self: flex-start; }
.skin-model-field { display: grid; gap: 9px; font-size: 13px; }
.skin-model-options { display: flex; gap: 8px; }
.skin-model-options [aria-pressed="true"] { color: var(--accent); background: rgba(160,221,204,.12); border-color: var(--accent); }
.skin-model-field small, .skin-readonly, .skin-search-hint { color: var(--text-secondary); font-size: 12px; line-height: 1.5; }
.skin-search-hint { margin-top: -8px; }
.skin-defaults h3, .skin-capes h3 { font-size: 14px; margin: 0 0 12px; }
.skin-default-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }
.skin-default { padding: 12px 6px 8px; display: grid; justify-items: center; gap: 6px; color: inherit; background: rgba(255,255,255,.035); border: 1px solid rgba(255,255,255,.1); border-radius: 10px; font: inherit; font-size: 12px; }
.skin-default img { image-rendering: pixelated; object-fit: contain; }
.skin-default.selected { border-color: var(--accent); background: rgba(160,221,204,.1); }
.skin-default:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.skin-cape-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; }
.skin-cape-choice { padding: 12px 6px 8px; display: grid; justify-items: center; align-content: start; gap: 8px; border: 1px solid rgba(255,255,255,.12); border-radius: 10px; background: rgba(255,255,255,.035); font-size: 12px; }
.skin-cape-choice[aria-pressed="true"] { border-color: var(--accent); background: rgba(160,221,204,.1); }
.skin-cape-image { display: block; width: 40px; height: 64px; background-size: 256px 128px; background-position: -4px -4px; background-repeat: no-repeat; image-rendering: pixelated; }
.skin-cape-none { display: grid; place-items: center; height: 64px; font-size: 28px; color: var(--text-secondary); }
.skin-selected-name { text-align: center; font-weight: 600; margin: 20px 0 0; }
.skin-feedback { font-size: 13px; line-height: 1.5; text-align: center; overflow-wrap: anywhere; }
.skin-error { color: var(--danger, #ff9d9d); }
.skins-dialog .modal-footer { justify-content: space-between; }
.skin-reset-actions { display: flex; gap: 10px; }
.skins-dialog .btn-primary:disabled { box-shadow: none; background: rgba(255,255,255,.08); color: var(--text-secondary); }
@media (max-width: 760px) { .skins-dialog { width: calc(100vw - 24px); } .skins-body { grid-template-columns: 1fr; overflow-y: auto; } .skin-tools { overflow: visible; } .skin-display { grid-row: 1; overflow: visible; } }
</style>

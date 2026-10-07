<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import { useLauncher } from './composables/useLauncher.ts';
import { presentInstances } from './features/library/model.ts';
import type { Instance, SortMode } from './features/library/model.ts';
import './styles/context-menu.css';
import AccountsModal from './components/AccountsModal.vue';
import SettingsModal from './components/SettingsModal.vue';
import CreateInstanceModal from './components/CreateInstanceModal.vue';
import InstanceEditorModal from './components/InstanceEditorModal.vue';
import AppMenuModal from './components/AppMenuModal.vue';
import UpdateModal from './components/UpdateModal.vue';
import GpuLaunchModal from './components/GpuLaunchModal.vue';
import SkinsModal from './components/SkinsModal.vue';

const { state, status, selected, busy, failure, artwork, artworkLoading, artworkFailure, reducedMotion, systemMotion, t, connect, select, launch, action, preference, searchPacks, packVersions, minecraftVersions, browseArchive, instanceDetails, instanceCommand, editorRevision, accountsRequest, javaService, skinService, gpuService, gpuChoice, continueGpuLaunch, updateService } = useLauncher();
const query = ref('');
const group = ref('');
const pinnedOnly = ref(false);
const searchInput = ref<HTMLInputElement>();
const viewOptions = ref<HTMLDetailsElement>();
const moreActions = ref<HTMLDetailsElement>();
const contextMenuElement = ref<HTMLDivElement>();
const contextMenu = ref<{ instance: Instance; x: number; y: number } | null>(null);
const contextMenuInvoker = ref<HTMLElement | null>(null);
const failedIcons = ref(new Set<string>());
const isHidden = ref(typeof document !== 'undefined' && document.visibilityState === 'hidden');

const showAccounts = ref(false);
const showSkins = ref(false);
const skinAccountId = ref('');
const activeAccount = computed(() => state.value.accounts.find(account => account.active));
function openSkins(id = activeAccount.value?.id ?? '') {
  closePopovers(); closeContextMenu(); showAccounts.value = false;
  skinAccountId.value = id; showSkins.value = true;
}
watch(accountsRequest, () => {
  closePopovers(); closeContextMenu();
  showSettings.value = false; showCreate.value = false; showAppMenu.value = false;
  editingInstance.value = null; showAccounts.value = true;
  showSkins.value = false;
});
const showSettings = ref(false);
const showCreate = ref(false);
const createModalTab = ref('custom');
const showAppMenu = ref(false);
const showUpdates = ref(false);
let updatePresentation = 0;
function markUpdatePresentation(presentation: number) { updatePresentation = Math.max(updatePresentation, presentation); }
const editingInstance = ref<Instance | null>(null);
const editorInstance = computed(() => state.value.instances.find(instance => instance.id === editingInstance.value?.id) || editingInstance.value);
watch(() => [state.value.updates.presentation, showAccounts.value, showSkins.value, showSettings.value, showCreate.value, showAppMenu.value, editingInstance.value, state.value.modalActive], () => {
  if (state.value.updates.presentation <= updatePresentation || showAccounts.value || showSkins.value || showSettings.value || showCreate.value || showAppMenu.value || editingInstance.value || state.value.modalActive) return;
  updatePresentation = state.value.updates.presentation;
  closePopovers(); closeContextMenu(); showUpdates.value = true;
}, { immediate: true });
watch(() => [showUpdates.value, state.value.updates.status], async () => {
  if (showUpdates.value && state.value.updates.status === 'available') {
    await nextTick();
    if (showUpdates.value) await updateService.acknowledge().catch(() => {});
  }
});
const editorSection = ref('overview');
function openEditor(instance: Instance, section = 'overview') {
  closePopovers(); editorSection.value = section; editingInstance.value = instance;
}

function openCreate(tab = 'custom') {
  createModalTab.value = tab;
  showCreate.value = true;
}

const shown = computed(() => presentInstances(state.value.instances, query.value, group.value, pinnedOnly.value, state.value.sortMode, state.value.locale));
const groups = computed(() => [...new Set(state.value.instances.map(i => i.group).filter(Boolean))].sort(new Intl.Collator(state.value.locale).compare));
const filtered = computed(() => Boolean(query.value || group.value || pinnedOnly.value));
const enabled = computed(() => status.value === 'ready' && !busy.value && !state.value.deletion.active);
const selectedStatus = computed(() => selected.value?.running ? 'running' : selected.value?.broken ? 'broken' : selected.value?.canLaunch ? 'ready' : 'unavailable');
const canPlay = computed(() => enabled.value && selected.value?.canLaunch && !selected.value.running && !selected.value.broken);
const metadata = computed(() => [selected.value?.minecraftVersion ? `${t('minecraft')} ${selected.value.minecraftVersion}` : '', [selected.value?.loader, selected.value?.loaderVersion].filter(Boolean).join(' ')].filter(Boolean));

function clearFilters() { query.value = ''; group.value = ''; pinnedOnly.value = false; }
function changeSort(event: Event) { void preference('sortMode', (event.target as HTMLSelectElement).value as SortMode); }
function closePopovers() { if (viewOptions.value) viewOptions.value.open = false; if (moreActions.value) moreActions.value.open = false; }
function onVisibilityChange() { isHidden.value = document.visibilityState === 'hidden'; }

function closeContextMenu(restoreFocus = false) {
  if (!contextMenu.value) return;
  contextMenu.value = null;
  if (restoreFocus) requestAnimationFrame(() => contextMenuInvoker.value?.focus());
}

async function openContextMenu(event: MouseEvent | KeyboardEvent, instance: Instance) {
  if (!enabled.value) return;
  event.preventDefault();
  const row = (event.currentTarget as HTMLElement).closest('.instance-row');
  const activeElement = document.activeElement as HTMLElement | null;
  contextMenuInvoker.value = activeElement && row?.contains(activeElement) ? activeElement : row?.querySelector('.instance-select') ?? null;
  const rowBounds = contextMenuInvoker.value?.getBoundingClientRect();
  const x = event instanceof MouseEvent ? event.clientX : rowBounds?.left ?? 8;
  const y = event instanceof MouseEvent ? event.clientY : rowBounds?.bottom ?? 8;
  contextMenu.value = { instance, x: Math.max(8, Math.min(x, window.innerWidth - 216)), y: Math.max(8, Math.min(y, window.innerHeight - 260)) };
  await nextTick();
  const menu = contextMenuElement.value;
  if (!menu) return;
  const menuBounds = menu.getBoundingClientRect();
  contextMenu.value = {
    ...contextMenu.value!,
    x: Math.max(8, Math.min(contextMenu.value!.x, window.innerWidth - menuBounds.width - 8)),
    y: Math.max(8, Math.min(contextMenu.value!.y, window.innerHeight - menuBounds.height - 8)),
  };
  menu.querySelector<HTMLButtonElement>('button:not(:disabled)')?.focus();
}

function instanceKeydown(event: KeyboardEvent, instance: Instance) {
  if ((event.key === 'F10' && event.shiftKey) || event.key === 'ContextMenu') {
    event.preventDefault();
    void openContextMenu(event, instance);
  }
}

function contextMenuKeydown(event: KeyboardEvent) {
  const menu = contextMenuElement.value;
  if (!menu) return;
  const items = [...menu.querySelectorAll<HTMLButtonElement>('button[role="menuitem"]:not(:disabled)')];
  const index = items.indexOf(document.activeElement as HTMLButtonElement);
  if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); closeContextMenu(true); return; }
  if (event.key === 'ArrowDown' || event.key === 'ArrowUp' || event.key === 'Home' || event.key === 'End') {
    event.preventDefault();
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1 : (index + (event.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length;
    items[next]?.focus();
  }
}

function runContextAction(name: 'launch' | 'edit' | 'folder' | 'rename' | 'changeGroup' | 'copy' | 'export' | 'delete' | 'kill', instance: Instance) {
  closeContextMenu();
  if (name === 'edit') { contextMenuInvoker.value?.focus(); openEditor(instance); return; }
  void action(name, instance.id).finally(() => contextMenuInvoker.value?.focus());
}

function toggleContextPin(instance: Instance) {
  closeContextMenu();
  void preference('pin', { id: instance.id, pinned: !instance.pinned }).finally(() => contextMenuInvoker.value?.focus());
}

function keydown(event: KeyboardEvent) {
  if (showSkins.value) return;
  if (gpuChoice.value) return;
  if (showUpdates.value) { if (event.key === 'Escape') { event.preventDefault(); showUpdates.value = false; } return; }
  if (editingInstance.value) return;
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f') { event.preventDefault(); searchInput.value?.focus(); searchInput.value?.select(); }
  if (event.key === 'Escape') {
    if (contextMenu.value) { closeContextMenu(true); return; }
    if (showAccounts.value || showSettings.value || showCreate.value || showAppMenu.value) {
      showAccounts.value = false;
      showSettings.value = false;
      showCreate.value = false;
      showAppMenu.value = false;
      return;
    }
    closePopovers();
  }
}

function listKeydown(event: KeyboardEvent) {
  const target = event.target as HTMLElement;
  if (!target.matches('.instance-select') || !['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
  event.preventDefault();
  const buttons = [...(event.currentTarget as HTMLElement).querySelectorAll<HTMLButtonElement>('.instance-select')];
  const index = buttons.indexOf(target as HTMLButtonElement);
  const next = event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 : Math.max(0, Math.min(buttons.length - 1, index + (event.key === 'ArrowDown' ? 1 : -1)));
  buttons[next]?.focus();
}

watch(() => state.value.locale, locale => { document.documentElement.lang = locale; }, { immediate: true });
watch(groups, values => { if (group.value && !values.includes(group.value)) group.value = ''; });

onMounted(() => {
  window.addEventListener('keydown', keydown);
  document.addEventListener('visibilitychange', onVisibilityChange);
  void connect();
});

onUnmounted(() => {
  window.removeEventListener('keydown', keydown);
  document.removeEventListener('visibilitychange', onVisibilityChange);
});
</script>

<template>
  <main class="launcher" :class="{ 'reduced-motion': reducedMotion, compact: state.compact, 'is-hidden': isHidden }" :aria-busy="status === 'loading'">
    <a v-if="selected" class="skip-link" href="#play">{{ t('skipToPlay') }}</a>
    <div class="artwork-canvas" aria-hidden="true">
      <Transition name="artwork"><img v-if="artwork" :key="artwork" :src="artwork" alt="" class="artwork-image" /></Transition>
      <div class="artwork-scrim"></div>
    </div>

    <aside class="navigation glass" :inert="showSkins" :aria-label="t('library')">
      <header class="library-header"><button class="wordmark welcome-name" type="button" :disabled="!enabled || !activeAccount" :title="state.accountName" :aria-label="`${t('skins')}: ${state.accountName}`" @click="openSkins()"><img v-if="activeAccount?.headUrl" class="welcome-head" :src="activeAccount.headUrl" width="24" height="24" alt="" /><span>{{ state.accountName ? `${t('welcome')} ${state.accountName}` : t('welcome') }}</span></button><button :disabled="!enabled" class="quiet" @click="showAppMenu = true">{{ t('application') }}</button></header>
      <h2 class="library-heading">{{ t('library') }}</h2>
      <button v-if="['available', 'downloading', 'installing'].includes(state.updates.status)" class="update-shortcut btn-secondary" @click="showUpdates = true">{{ t(state.updates.status === 'available' ? state.updates.portable ? 'updateDownloadPortable' : 'updateDownloadSetup' : state.updates.status === 'downloading' ? 'updateDownloading' : 'updateInstalling') }}<span v-if="state.updates.status === 'available'"> {{ state.updates.latestVersion }}</span></button>
      <div class="search-row">
        <label class="visually-hidden" for="instance-search">{{ t('search') }}</label>
        <input id="instance-search" ref="searchInput" v-model="query" type="search" :placeholder="t('search')" autocomplete="off" :disabled="status !== 'ready'" @keydown.enter="shown[0] && select(shown[0].id)" />
        <details ref="viewOptions" class="view-options">
          <summary class="option-button" :title="t('filters')" :aria-label="t('filters')"><svg viewBox="0 0 20 20" width="18" height="18" aria-hidden="true"><path d="M3 5h14M3 10h14M3 15h14M7 3v4M13 8v4M8 13v4" /></svg></summary>
          <div class="options-panel">
            <label for="sort-mode">{{ t('sort') }}</label>
            <select id="sort-mode" :value="state.sortMode" :disabled="!enabled" @change="changeSort"><option value="Name">{{ t('name') }}</option><option value="LastLaunch">{{ t('lastLaunch') }}</option><option value="TotalTimePlayed">{{ t('playTime') }}</option></select>
            <label for="group-filter">{{ t('group') }}</label>
            <select id="group-filter" v-model="group"><option value="">{{ t('allGroups') }}</option><option v-for="value in groups" :key="value" :value="value">{{ value }}</option></select>
            <label class="check"><input v-model="pinnedOnly" type="checkbox" />{{ t('pinnedOnly') }}</label>
            <label class="check"><input type="checkbox" :checked="state.compact" :disabled="!enabled" @change="preference('compact', ($event.target as HTMLInputElement).checked)" />{{ t('compact') }}</label>
            <label class="check"><input type="checkbox" :checked="state.reducedMotion" :disabled="!enabled" @change="preference('reducedMotion', ($event.target as HTMLInputElement).checked)" />{{ t('reducedMotion') }}</label>
            <p v-if="systemMotion" class="secondary small">{{ t('systemMotion') }}</p>
          </div>
        </details>
      </div>
      <button v-if="filtered" class="clear-filters quiet" @click="clearFilters">{{ t('clearFilters') }}</button>

      <div class="instance-list" @keydown="listKeydown">
        <div v-if="status === 'loading'" class="instance-skeletons" aria-hidden="true">
          <div v-for="n in 5" :key="n" class="skeleton-row">
            <div class="skeleton-icon shimmer"></div>
            <div class="skeleton-copy">
              <div class="skeleton-bar shimmer" style="width: 70%; height: 12px;"></div>
              <div class="skeleton-bar shimmer" style="width: 45%; height: 9px; margin-top: 5px;"></div>
            </div>
          </div>
        </div>
        <div v-else-if="status === 'error'" class="list-message"><p>{{ t('connectionTitle') }}</p><button @click="connect">{{ t('retry') }}</button></div>
        <div v-else-if="!state.instances.length" class="list-empty-placeholder"></div>
        <p v-else-if="!shown.length" class="list-message" role="status">{{ t('noResults') }}</p>
        <ul v-else :aria-label="t('library')">
          <li v-for="instance in shown" :key="instance.id" class="instance-row" :class="{ 'is-selected': state.selectedId === instance.id }" @contextmenu="openContextMenu($event, instance)" @keydown="instanceKeydown($event, instance)">
            <button class="instance-select" :disabled="!enabled" :aria-pressed="state.selectedId === instance.id" :title="instance.name" @click="select(instance.id)">
              <img v-if="instance.iconUrl && !failedIcons.has(instance.iconUrl)" :src="instance.iconUrl" alt="" class="instance-icon" @error="failedIcons.add(instance.iconUrl)" />
              <span v-else class="instance-initial" aria-hidden="true">{{ instance.name.slice(0, 1) }}</span>
              <span class="instance-copy"><span class="instance-name">{{ instance.name }}</span><span v-if="!state.compact" class="instance-meta">{{ [instance.group, instance.minecraftVersion, instance.loader].filter(Boolean).join(' · ') }}</span><span v-if="instance.running || instance.broken" class="instance-state">{{ t(instance.running ? 'running' : 'broken') }}</span></span>
            </button>
            <button class="pin-button quiet" :class="{ 'is-pinned': instance.pinned }" :disabled="!enabled" :aria-pressed="instance.pinned" :aria-label="t(instance.pinned ? 'unpin' : 'pin') + ': ' + instance.name" :title="t(instance.pinned ? 'unpin' : 'pin')" @click="preference('pin', { id: instance.id, pinned: !instance.pinned })"><svg width="16" height="16" viewBox="0 0 20 20" aria-hidden="true"><path d="m6 3 8 0-1 6 3 3H4l3-3-1-6ZM10 12v6" /></svg></button>
          </li>
        </ul>
      </div>

      <footer class="navigation-footer">
        <button class="quiet" :disabled="!enabled" @click="openSkins()">{{ t('skins') }}</button>
        <div class="creation-actions"><button :disabled="!enabled" @click="openCreate('custom')">{{ t('create') }}</button><button class="quiet" :disabled="!enabled" @click="openCreate('import')">{{ t('import') }}</button></div>
        <div class="account-actions"><button class="quiet account-button" :disabled="!enabled" @click="showAccounts = true"><span>{{ t('accounts') }}</span><span class="account-name">{{ state.accountName || t('accountUnavailable') }}</span></button><button class="settings-button" :disabled="!enabled" :title="t('settings')" :aria-label="t('settings')" @click="showSettings = true"><svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" /><circle cx="12" cy="12" r="3" /></svg></button></div>
      </footer>
    </aside>

    <section class="stage" :inert="showSkins" :aria-label="t('selected')">
      <div v-if="state.deletion.active" class="deletion-status notice" role="status" aria-live="polite"><strong>{{ t('deletingInstance') }}</strong><p>{{ state.deletion.name }}</p><progress :aria-label="t('deletingInstance')"></progress></div>
      <div v-if="status === 'error' && failure" class="connection-error notice" role="alert"><h1>{{ t('connectionTitle') }}</h1><p>{{ t(failure.code) }}</p><p v-if="failure.code === 'disconnected'" class="secondary">{{ t('openLauncher') }}</p><details><summary>{{ t('technicalDetails') }}</summary><pre>{{ failure.detail }}</pre></details><button @click="failure.retry">{{ t('retry') }}</button></div>
      <div v-else-if="status === 'ready' && !state.instances.length" class="empty-state"><h1>{{ t('emptyTitle') }}</h1><p class="secondary">{{ t('emptyBody') }}</p><div class="empty-actions"><button :disabled="!enabled" @click="openCreate('custom')">{{ t('create') }}</button><button :disabled="!enabled" @click="openCreate('import')">{{ t('import') }}</button></div></div>
      <p v-else-if="status === 'ready' && !selected" class="choose-instance">{{ t('choose') }}</p>

      <div v-if="status === 'ready' && failure" class="operation-error notice" role="alert"><strong>{{ t('errorTitle') }}</strong><p>{{ t(failure.code) }}</p><details><summary>{{ t('technicalDetails') }}</summary><pre>{{ failure.detail }}</pre></details><div class="notice-actions"><button :disabled="busy" @click="failure.retry">{{ t('retry') }}</button><button class="quiet" @click="failure = null">{{ t('dismiss') }}</button></div></div>

      <div v-if="selected && status === 'ready'" class="selected-instance">
        <Transition name="hero" mode="out-in">
          <div :key="selected.id" class="instance-identity">
            <p v-if="artworkLoading" class="artwork-caption secondary" role="status">{{ t('artworkLoading') }}</p>
            <p v-else-if="!artwork && !artworkFailure" class="artwork-caption secondary">{{ t('noArtwork') }}</p>
            <div v-if="artworkFailure" class="artwork-error" role="alert"><span>{{ t('artworkError') }}</span><details><summary>{{ t('technicalDetails') }}</summary><pre>{{ artworkFailure.detail }}</pre></details><button class="quiet" :disabled="!enabled" @click="artworkFailure.retry">{{ t('artworkRetry') }}</button></div>
            <p v-if="selected.group" class="selected-group">{{ selected.group }}</p>
            <h1>{{ selected.name }}</h1>
            <p v-if="metadata.length" class="selected-meta">{{ metadata.join(' · ') }}</p>
            <div class="identity-actions"><button class="quiet" :disabled="!enabled" @click="openEditor(selected)">{{ t('edit') }}</button><button class="quiet" :disabled="!enabled" @click="action('folder', selected.id)">{{ t('folder') }}</button></div>
          </div>
        </Transition>

        <div class="launch-dock glass">
          <p class="launch-status" role="status">{{ busy ? t('working') : t(selectedStatus) }}</p>
          <button id="play" class="play-button" :disabled="!canPlay" @click="launch()"><svg viewBox="0 0 20 20" width="22" height="22" aria-hidden="true"><path d="m5 3 11 7-11 7Z" /></svg>{{ t(selected.running ? 'running' : 'play') }}</button>
          <details ref="moreActions" class="more-actions"><summary>{{ t('more') }}</summary><div class="action-menu"><button :disabled="!enabled" @click="openEditor(selected, 'mods')">{{ t('manage') }}</button><button :disabled="!enabled" @click="openEditor(selected, 'settings')">{{ t('launchOptions') }}</button><button :disabled="!enabled" @click="openEditor(selected, 'log')">{{ t('logs') }}</button><button :disabled="!enabled" @click="closePopovers(); action('legacy')">{{ t('legacy') }}</button></div></details>
        </div>
      </div>
    </section>

    <div v-if="contextMenu" class="instance-context-backdrop" @pointerdown.self="closeContextMenu(true)">
      <div
        ref="contextMenuElement"
        class="instance-context-menu"
        role="menu"
        :aria-label="contextMenu.instance.name"
        :style="{ left: `${contextMenu.x}px`, top: `${contextMenu.y}px` }"
        @keydown="contextMenuKeydown"
        @contextmenu.prevent
      >
        <p class="instance-context-heading" role="presentation">{{ contextMenu.instance.name }}</p>
        <button role="menuitem" :disabled="!enabled || !contextMenu.instance.canLaunch || contextMenu.instance.running || contextMenu.instance.broken" @click="runContextAction('launch', contextMenu.instance)">{{ t('play') }}</button>
        <button role="menuitem" :disabled="!enabled" @click="runContextAction('edit', contextMenu.instance)">{{ t('edit') }}</button>
        <button role="menuitem" :disabled="!enabled" @click="runContextAction('folder', contextMenu.instance)">{{ t('folder') }}</button>
        <div class="instance-context-separator" role="separator"></div>
        <button role="menuitem" :disabled="!enabled" @click="toggleContextPin(contextMenu.instance)">{{ t(contextMenu.instance.pinned ? 'unpin' : 'pin') }}</button>
        <button role="menuitem" :disabled="!enabled" @click="runContextAction('rename', contextMenu.instance)">{{ t('rename') }}</button>
        <button role="menuitem" :disabled="!enabled" @click="runContextAction('changeGroup', contextMenu.instance)">{{ t('changeGroup') }}</button>
        <button role="menuitem" :disabled="!enabled || contextMenu.instance.running" @click="runContextAction('copy', contextMenu.instance)">{{ t('copy') }}</button>
        <button role="menuitem" :disabled="!enabled || contextMenu.instance.running || contextMenu.instance.broken" @click="runContextAction('export', contextMenu.instance)">{{ t('export') }}</button>
        <div class="instance-context-separator" role="separator"></div>
        <button role="menuitem" class="destructive" :disabled="!enabled || contextMenu.instance.running" @click="runContextAction('delete', contextMenu.instance)">{{ t('delete') }}</button>
        <button role="menuitem" :disabled="!enabled || !contextMenu.instance.running" @click="runContextAction('kill', contextMenu.instance)">{{ t('kill') }}</button>
      </div>
    </div>

    <Transition name="backdrop">
      <div v-if="state.modalActive" class="modal-backdrop" aria-hidden="true"></div>
    </Transition>

    <Transition name="modal">
      <AccountsModal
        v-if="showAccounts"
        :accounts="state.accounts"
        :t="t"
        @close="showAccounts = false"
        @add-microsoft="action('addMicrosoft')"
        @add-offline="name => action('addOffline', name)"
        @set-active="id => action('setDefaultAccount', id)"
        @remove="id => action('removeAccount', id)"
        @refresh="id => action('refreshAccount', id)"
        @skins="id => openSkins(id)"
      />
    </Transition>

    <Transition name="skins"><SkinsModal v-if="showSkins" :accounts="state.accounts" :account-id="skinAccountId" :service="skinService" :t="t" @close="showSkins = false" @account="id => skinAccountId = id" /></Transition>

    <Transition name="modal">
      <SettingsModal
        v-if="showSettings"
        :settings="state.launcherSettings"
        :updates="state.updates"
        :busy="busy"
        :total-memory-mb="state.totalMemoryMb"
        :gpu-service="gpuService"
        :java-service="javaService"
        :compact="state.compact"
        :reduced-motion="state.reducedMotion"
        :locale="state.locale"
        :t="t"
        @close="showSettings = false"
        @update-pref="(key, val) => preference(key, val)"
        @check-updates="action('checkForUpdates')"
        @download-update="kind => updateService.openDownload(kind)"
        @automatic-updates="value => updateService.setAutomatic(value)"
        @updates-viewed="markUpdatePresentation"
      />
    </Transition>

    <Transition name="modal">
      <CreateInstanceModal
        v-if="showCreate"
        :initial-tab="createModalTab"
        :groups="groups"
        :busy="busy"
        :search-packs="searchPacks"
        :pack-versions="packVersions"
        :minecraft-versions="minecraftVersions"
        :browse-archive="browseArchive"
        :t="t"
        @close="showCreate = false"
        @create-quick="payload => { showCreate = false; action('createQuick', JSON.stringify(payload)); }"
        @install-pack="payload => { showCreate = false; action('installPack', JSON.stringify(payload)); }"
        @import-archive="payload => { showCreate = false; action('importArchive', JSON.stringify(payload)); }"
      />
    </Transition>

    <Transition name="modal">
      <InstanceEditorModal v-if="editorInstance" :key="editorInstance.id" :instance="editorInstance" :initial-section="editorSection" :revision="editorRevision" :busy="busy" :t="t" :details="instanceDetails" :command="instanceCommand" :java-service="javaService" @close="editingInstance = null" />
    </Transition>

    <Transition name="modal">
      <GpuLaunchModal v-if="gpuChoice" :settings="gpuChoice.settings" :service="gpuService" :t="t" @close="gpuChoice = null" @continue="continueGpuLaunch" />
      <UpdateModal v-if="showUpdates && !gpuChoice" :state="state.updates" :busy="busy" :t="t" @close="showUpdates = false" @check="action('checkForUpdates')" @download="kind => updateService.openDownload(kind)" @automatic="value => updateService.setAutomatic(value)" />
    </Transition>

    <Transition name="modal">
      <AppMenuModal
        v-if="showAppMenu"
        :compact="state.compact"
        :reduced-motion="state.reducedMotion"
        :t="t"
        @close="showAppMenu = false"
        @action="name => action(name)"
        @update-pref="(key, val) => preference(key, val)"
        @open-accounts="showAccounts = true"
        @open-settings="showSettings = true"
      />
    </Transition>
  </main>
</template>
<style scoped>
.library-header .welcome-name { overflow: visible; border-radius: 4px; display: inline-flex; align-items: center; gap: 8px; min-width: 0; background: none; border: 0; padding: 0; text-align: left; color: inherit; }
.welcome-name span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.welcome-head { image-rendering: pixelated; flex: 0 0 24px; border-radius: 0; }
.welcome-name:focus-visible { outline: 2px solid var(--accent); outline-offset: 4px; border-radius: 3px; }
.skins-enter-active, .skins-leave-active { transition: opacity 120ms ease-out; }
.skins-enter-from, .skins-leave-to { opacity: 0; }
.reduced-motion .skins-enter-active, .reduced-motion .skins-leave-active { transition: none; }
</style>

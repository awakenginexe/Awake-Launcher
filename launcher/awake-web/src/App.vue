<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useLauncher } from './composables/useLauncher.ts';
import { presentInstances } from './features/library/model.ts';
import type { SortMode } from './features/library/model.ts';

const { state, status, selected, busy, failure, artwork, artworkLoading, artworkFailure, reducedMotion, systemMotion, t, connect, select, launch, action, preference } = useLauncher();
const query = ref('');
const group = ref('');
const pinnedOnly = ref(false);
const searchInput = ref<HTMLInputElement>();
const viewOptions = ref<HTMLDetailsElement>();
const moreActions = ref<HTMLDetailsElement>();
const failedIcons = ref(new Set<string>());
const shown = computed(() => presentInstances(state.value.instances, query.value, group.value, pinnedOnly.value, state.value.sortMode, state.value.locale));
const groups = computed(() => [...new Set(state.value.instances.map(i => i.group).filter(Boolean))].sort(new Intl.Collator(state.value.locale).compare));
const filtered = computed(() => Boolean(query.value || group.value || pinnedOnly.value));
const enabled = computed(() => status.value === 'ready' && !busy.value);
const selectedStatus = computed(() => selected.value?.running ? 'running' : selected.value?.broken ? 'broken' : selected.value?.canLaunch ? 'ready' : 'unavailable');
const canPlay = computed(() => enabled.value && selected.value?.canLaunch && !selected.value.running && !selected.value.broken);
const metadata = computed(() => [selected.value?.minecraftVersion ? `${t('minecraft')} ${selected.value.minecraftVersion}` : '', [selected.value?.loader, selected.value?.loaderVersion].filter(Boolean).join(' ')].filter(Boolean));
function clearFilters() { query.value = ''; group.value = ''; pinnedOnly.value = false; }
function changeSort(event: Event) { void preference('sortMode', (event.target as HTMLSelectElement).value as SortMode); }
function closePopovers() { if (viewOptions.value) viewOptions.value.open = false; if (moreActions.value) moreActions.value.open = false; }
function keydown(event: KeyboardEvent) {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f') { event.preventDefault(); searchInput.value?.focus(); searchInput.value?.select(); }
  if (event.key === 'Escape') closePopovers();
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
onMounted(() => { window.addEventListener('keydown', keydown); void connect(); });
onUnmounted(() => window.removeEventListener('keydown', keydown));
</script>

<template>
  <main class="launcher" :class="{ 'reduced-motion': reducedMotion, compact: state.compact }" :aria-busy="status === 'loading'">
    <a v-if="selected" class="skip-link" href="#play">{{ t('skipToPlay') }}</a>
    <div class="artwork-canvas" aria-hidden="true">
      <Transition name="artwork"><img v-if="artwork" :key="artwork" :src="artwork" alt="" class="artwork-image" /></Transition>
      <div class="artwork-scrim"></div>
    </div>

    <aside class="navigation glass" :aria-label="t('library')">
      <header class="library-header"><span class="wordmark">Awake</span><button :disabled="!enabled" class="quiet" @click="action('application')">{{ t('application') }}</button></header>
      <h2 class="library-heading">{{ t('library') }}</h2>
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
        <p v-if="status === 'loading'" role="status" class="list-message">{{ t('loading') }}</p>
        <div v-else-if="status === 'error'" class="list-message"><p>{{ t('connectionTitle') }}</p><button @click="connect">{{ t('retry') }}</button></div>
        <div v-else-if="!state.instances.length" class="list-message"><h3>{{ t('emptyTitle') }}</h3><p class="secondary">{{ t('emptyBody') }}</p></div>
        <p v-else-if="!shown.length" class="list-message" role="status">{{ t('noResults') }}</p>
        <ul v-else :aria-label="t('library')">
          <li v-for="instance in shown" :key="instance.id" class="instance-row" :class="{ 'is-selected': state.selectedId === instance.id }">
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
        <div class="creation-actions"><button :disabled="!enabled" @click="action('create')">{{ t('create') }}</button><button class="quiet" :disabled="!enabled" @click="action('import')">{{ t('import') }}</button></div>
        <div class="account-actions"><button class="quiet account-button" :disabled="!enabled" @click="action('accounts')"><span>{{ t('accounts') }}</span><span class="account-name">{{ state.accountName || t('accountUnavailable') }}</span></button><button class="quiet" :disabled="!enabled" @click="action('settings')">{{ t('settings') }}</button></div>
      </footer>
    </aside>

    <section class="stage" :aria-label="t('selected')">
      <div v-if="status === 'error' && failure" class="connection-error notice" role="alert"><h1>{{ t('connectionTitle') }}</h1><p>{{ t(failure.code) }}</p><p v-if="failure.code === 'disconnected'" class="secondary">{{ t('openLauncher') }}</p><details><summary>{{ t('technicalDetails') }}</summary><pre>{{ failure.detail }}</pre></details><button @click="failure.retry">{{ t('retry') }}</button></div>
      <div v-else-if="status === 'ready' && !state.instances.length" class="empty-state"><h1>{{ t('emptyTitle') }}</h1><p class="secondary">{{ t('emptyBody') }}</p><div class="empty-actions"><button :disabled="!enabled" @click="action('create')">{{ t('create') }}</button><button :disabled="!enabled" @click="action('import')">{{ t('import') }}</button></div></div>
      <p v-else-if="status === 'ready' && !selected" class="choose-instance">{{ t('choose') }}</p>

      <div v-if="status === 'ready' && failure" class="operation-error notice" role="alert"><strong>{{ t('errorTitle') }}</strong><p>{{ t(failure.code) }}</p><details><summary>{{ t('technicalDetails') }}</summary><pre>{{ failure.detail }}</pre></details><div class="notice-actions"><button :disabled="busy" @click="failure.retry">{{ t('retry') }}</button><button class="quiet" @click="failure = null">{{ t('dismiss') }}</button></div></div>

      <div v-if="selected && status === 'ready'" class="selected-instance">
        <div class="instance-identity">
          <p v-if="artworkLoading" class="artwork-caption secondary" role="status">{{ t('artworkLoading') }}</p>
          <p v-else-if="!artwork && !artworkFailure" class="artwork-caption secondary">{{ t('noArtwork') }}</p>
          <div v-if="artworkFailure" class="artwork-error" role="alert"><span>{{ t('artworkError') }}</span><details><summary>{{ t('technicalDetails') }}</summary><pre>{{ artworkFailure.detail }}</pre></details><button class="quiet" :disabled="!enabled" @click="artworkFailure.retry">{{ t('artworkRetry') }}</button></div>
          <p v-if="selected.group" class="selected-group secondary">{{ selected.group }}</p>
          <h1>{{ selected.name }}</h1>
          <p v-if="metadata.length" class="selected-meta">{{ metadata.join(' · ') }}</p>
          <div class="identity-actions"><button class="quiet" :disabled="!enabled" @click="action('edit', selected.id)">{{ t('edit') }}</button><button class="quiet" :disabled="!enabled" @click="action('folder', selected.id)">{{ t('folder') }}</button></div>
        </div>

        <div class="launch-dock glass">
          <p class="launch-status" role="status">{{ busy ? t('working') : t(selectedStatus) }}</p>
          <button id="play" class="play-button" :disabled="!canPlay" @click="launch"><svg viewBox="0 0 20 20" width="22" height="22" aria-hidden="true"><path d="m5 3 11 7-11 7Z" /></svg>{{ t(selected.running ? 'running' : 'play') }}</button>
          <details ref="moreActions" class="more-actions"><summary>{{ t('more') }}</summary><div class="action-menu"><button :disabled="!enabled" @click="closePopovers(); action('manage', selected.id)">{{ t('manage') }}</button><button :disabled="!enabled" @click="closePopovers(); action('launchOptions', selected.id)">{{ t('launchOptions') }}</button><button :disabled="!enabled" @click="closePopovers(); action('logs', selected.id)">{{ t('logs') }}</button><button :disabled="!enabled" @click="closePopovers(); action('legacy')">{{ t('legacy') }}</button></div></details>
        </div>
      </div>
    </section>
  </main>
</template>

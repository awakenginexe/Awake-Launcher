<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue';
import type { AccountItem } from '../features/library/model.ts';
import type { MessageKey } from '../i18n/catalogs.ts';

const props = defineProps<{
  accounts: AccountItem[];
  t: (key: MessageKey) => string;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'add-microsoft'): void;
  (e: 'add-offline', username: string): void;
  (e: 'set-active', id: string): void;
  (e: 'remove', id: string): void;
  (e: 'refresh', id: string): void;
}>();

const offlineUsername = ref('');
const isAddingOffline = ref(false);
const dialog = ref<HTMLElement>();
const previousFocus = document.activeElement as HTMLElement | null;
onMounted(() => dialog.value?.querySelector<HTMLButtonElement>('.btn-primary')?.focus());
onUnmounted(() => { if (previousFocus?.isConnected) previousFocus.focus(); });
function trapFocus(event: KeyboardEvent) {
  if (event.key !== 'Tab') return;
  const controls = [...(dialog.value?.querySelectorAll<HTMLElement>('button:not(:disabled), input:not(:disabled)') ?? [])];
  const first = controls[0], last = controls.at(-1);
  if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
  else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
}

function submitOffline() {
  const name = offlineUsername.value.trim();
  if (!name) return;
  emit('add-offline', name);
  offlineUsername.value = '';
  isAddingOffline.value = false;
}
</script>

<template>
  <div class="modal-overlay" @click.self="emit('close')">
    <div ref="dialog" class="modal-dialog glass-surface" role="dialog" aria-modal="true" :aria-label="t('accounts')" @keydown="trapFocus">
      <header class="modal-header">
        <div class="modal-title-group">
          <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <path d="M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2" />
            <circle cx="12" cy="7" r="4" />
          </svg>
          <h2>{{ t('accounts') }}</h2>
        </div>
        <button class="modal-close-btn" :aria-label="t('close')" @click="emit('close')">✕</button>
      </header>

      <div class="modal-body">
        <div v-if="!accounts.length" class="empty-accounts">
          <div class="empty-icon-badge">
            <svg viewBox="0 0 24 24" width="28" height="28" fill="none" stroke="currentColor" stroke-width="1.8">
              <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2" />
              <circle cx="12" cy="7" r="4" />
            </svg>
          </div>
          <h3>{{ t('noAccountsYet') }}</h3>
          <p>{{ t('noAccountsHint') }}</p>
        </div>

        <ul v-else class="account-card-list">
          <li v-for="acc in accounts" :key="acc.id" class="account-card" :class="{ 'is-active': acc.active }">
            <div class="account-avatar">
              <span class="avatar-letter">{{ acc.name.slice(0, 1).toUpperCase() }}</span>
            </div>
            <div class="account-details">
              <div class="account-title-row">
                <span class="account-player-name">{{ acc.name }}</span>
                <span v-if="acc.active" class="active-tag">{{ t('activeBadge') }}</span>
              </div>
              <span class="account-type-badge" :class="acc.type">{{ acc.type === 'microsoft' ? 'Microsoft' : 'Offline' }}</span>
            </div>
            <div class="account-card-actions">
              <button v-if="!acc.active" class="btn-subtle" @click="emit('set-active', acc.id)">{{ t('setActive') }}</button>
              <button class="btn-icon" :title="t('refreshAccount')" @click="emit('refresh', acc.id)">
                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2">
                  <path d="M23 4v6h-6M1 20v-6h6M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15" />
                </svg>
              </button>
              <button class="btn-icon danger" :title="t('removeAccount')" @click="emit('remove', acc.id)">
                <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2">
                  <path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                </svg>
              </button>
            </div>
          </li>
        </ul>

        <div v-if="isAddingOffline" class="inline-offline-box">
          <input
            v-model="offlineUsername"
            type="text"
            :placeholder="t('enterUsername')"
            class="styled-input"
            autofocus
            @keydown.enter="submitOffline"
            @keydown.esc="isAddingOffline = false"
          />
          <button class="btn-primary" :disabled="!offlineUsername.trim()" @click="submitOffline">{{ t('addOffline') }}</button>
          <button class="btn-subtle" @click="isAddingOffline = false">{{ t('cancel') }}</button>
        </div>
      </div>

      <footer class="modal-footer">
        <button class="btn-primary" @click="emit('add-microsoft')">
          <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor" aria-hidden="true">
            <path d="M2 3h9v9H2zm11 0h9v9h-9zM2 13h9v9H2zm11 0h9v9h-9z" />
          </svg>
          {{ t('addMicrosoft') }}
        </button>
        <button v-if="!isAddingOffline" class="btn-subtle" @click="isAddingOffline = true">
          {{ t('addOffline') }}
        </button>
      </footer>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { MessageKey } from '../i18n/catalogs.ts';
import type { Action } from '../features/library/model.ts';

const props = defineProps<{
  compact: boolean;
  reducedMotion: boolean;
  t: (key: MessageKey) => string;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'action', name: Action): void;
  (e: 'update-pref', key: string, val: unknown): void;
  (e: 'open-accounts'): void;
  (e: 'open-settings'): void;
}>();

function triggerAction(actionName: Action) {
  emit('close');
  emit('action', actionName);
}

function openAccounts() {
  emit('close');
  emit('open-accounts');
}

function openSettings() {
  emit('close');
  emit('open-settings');
}
</script>

<template>
  <div class="modal-overlay" @click.self="emit('close')">
    <div class="modal-dialog app-menu-dialog glass-surface" role="dialog" aria-modal="true" :aria-label="t('appMenuTitle')">
      <header class="modal-header">
        <div class="modal-title-group">
          <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <rect x="3" y="3" width="7" height="7" rx="1.5" />
            <rect x="14" y="3" width="7" height="7" rx="1.5" />
            <rect x="14" y="14" width="7" height="7" rx="1.5" />
            <rect x="3" y="14" width="7" height="7" rx="1.5" />
          </svg>
          <h2>{{ t('appMenuTitle') }}</h2>
        </div>
        <button class="modal-close-btn" :aria-label="t('close')" @click="emit('close')">✕</button>
      </header>

      <div class="modal-body app-menu-body">
        <!-- Folders Section -->
        <section class="menu-section">
          <h3 class="section-title">
            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
            </svg>
            {{ t('folders') }}
          </h3>
          <div class="menu-card-grid">
            <button class="menu-grid-btn" type="button" @click="triggerAction('openRootFolder')">
              <span class="btn-icon">📁</span>
              <span class="btn-label">{{ t('rootFolder') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('openInstancesFolder')">
              <span class="btn-icon">🗂️</span>
              <span class="btn-label">{{ t('instancesFolder') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('openModsFolder')">
              <span class="btn-icon">🧩</span>
              <span class="btn-label">{{ t('modsFolder') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('openLogsFolder')">
              <span class="btn-icon">📜</span>
              <span class="btn-label">{{ t('logsFolder') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('openJavaFolder')">
              <span class="btn-icon">☕</span>
              <span class="btn-label">{{ t('javaFolder') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('openSkinsFolder')">
              <span class="btn-icon">👕</span>
              <span class="btn-label">{{ t('skinsFolder') }}</span>
            </button>
          </div>
        </section>

        <!-- Tools Section -->
        <section class="menu-section">
          <h3 class="section-title">
            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z" />
            </svg>
            {{ t('tools') }}
          </h3>
          <div class="menu-card-grid">
            <button class="menu-grid-btn" type="button" @click="triggerAction('checkForUpdates')">
              <span class="btn-icon">🔄</span>
              <span class="btn-label">{{ t('checkForUpdates') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('logs')">
              <span class="btn-icon">📋</span>
              <span class="btn-label">{{ t('logs') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="openAccounts">
              <span class="btn-icon">👤</span>
              <span class="btn-label">{{ t('accounts') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="openSettings">
              <span class="btn-icon">⚙️</span>
              <span class="btn-label">{{ t('settings') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('clearMetadata')">
              <span class="btn-icon">🧹</span>
              <span class="btn-label">{{ t('clearMetadata') }}</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('reportBug')">
              <span class="btn-icon">🐛</span>
              <span class="btn-label">{{ t('reportBug') }}</span>
            </button>
          </div>
        </section>

        <!-- Display & Preferences -->
        <section class="menu-section">
          <h3 class="section-title">
            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="12" cy="12" r="3" />
              <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
            </svg>
            {{ t('filters') }}
          </h3>
          <div class="menu-toggles-row">
            <label class="toggle-card">
              <span>{{ t('compact') }}</span>
              <input type="checkbox" :checked="compact" @change="emit('update-pref', 'compact', ($event.target as HTMLInputElement).checked)" />
            </label>
            <label class="toggle-card">
              <span>{{ t('reducedMotion') }}</span>
              <input type="checkbox" :checked="reducedMotion" @change="emit('update-pref', 'reducedMotion', ($event.target as HTMLInputElement).checked)" />
            </label>
          </div>
        </section>

        <!-- Community Section -->
        <section class="menu-section">
          <h3 class="section-title">
            <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2" />
              <circle cx="9" cy="7" r="4" />
              <path d="M23 21v-2a4 4 0 0 0-3-3.87" />
              <path d="M16 3.13a4 4 0 0 1 0 7.75" />
            </svg>
            {{ t('community') }}
          </h3>
          <div class="menu-card-grid community-grid">
            <button class="menu-grid-btn" type="button" @click="triggerAction('discord')">
              <span class="btn-icon">💬</span>
              <span class="btn-label">Discord</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('matrix')">
              <span class="btn-icon">🌐</span>
              <span class="btn-label">Matrix</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('reddit')">
              <span class="btn-icon">👽</span>
              <span class="btn-label">Reddit</span>
            </button>
            <button class="menu-grid-btn" type="button" @click="triggerAction('about')">
              <span class="btn-icon">ℹ️</span>
              <span class="btn-label">{{ t('aboutAwake') }}</span>
            </button>
          </div>
        </section>
      </div>
    </div>
  </div>
</template>

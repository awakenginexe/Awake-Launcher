<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, ref, watch } from 'vue';
import type { LauncherSettings, UpdateState } from '../features/library/model.ts';
import UpdateModal from './UpdateModal.vue';
import type { MessageKey } from '../i18n/catalogs.ts';
import '../styles/language-select.css';
import JavaPicker from './JavaPicker.vue';
import type { JavaService } from './JavaPicker.vue';
import GpuPicker from './GpuPicker.vue';
import type { GpuService } from './GpuPicker.vue';
import HardwareVisibility from './HardwareVisibility.vue';
import JvmPresetPicker from './JvmPresetPicker.vue';
import { useHardwarePrivacy } from '../composables/useHardwarePrivacy.ts';
import { ramPresets, memoryRisk, presetFits, installedMemoryMb } from '../features/library/memory.ts';
import logoUrl from '../../../../program_info/awake-launcher.png';

const props = defineProps<{
  settings: LauncherSettings;
  updates: UpdateState;
  busy: boolean;
  totalMemoryMb: number;
  compact: boolean;
  reducedMotion: boolean;
  locale: string;
  t: (key: MessageKey) => string;
  javaService: JavaService;
  gpuService: GpuService;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'update-pref', key: string, value: unknown): void;
  (e: 'check-updates'): void;
  (e: 'download-update', kind: 'setup' | 'portable' | 'release'): void;
  (e: 'automatic-updates', enabled: boolean): void;
  (e: 'updates-viewed', presentation: number): void;
}>();

const activeTab = ref<'general' | 'java' | 'window' | 'about'>('general');
const visitedTabs = ref(new Set(['general']));
watch(activeTab, tab => { visitedTabs.value.add(tab); closeLanguagePicker(); });
watch([activeTab, () => props.updates.presentation], () => {
  if (activeTab.value === 'about') emit('updates-viewed', props.updates.presentation);
});
const { ramVisible } = useHardwarePrivacy();
const languageOptions = [
  { value: 'en', label: 'English' },
  { value: 'th', label: 'ภาษาไทย' },
  { value: 'zh-CN', label: '简体中文' },
  { value: 'zh-TW', label: '繁體中文' },
];
const languagePicker = ref<HTMLDivElement>();
const languageButton = ref<HTMLButtonElement>();
const languageOpen = ref(false);
const activeLanguageIndex = ref(0);
const selectedLanguage = () => Math.max(0, languageOptions.findIndex(option => option.value === props.locale));

function focusLanguageOption(index: number) {
  activeLanguageIndex.value = (index + languageOptions.length) % languageOptions.length;
  void nextTick(() => languagePicker.value?.querySelectorAll<HTMLButtonElement>('[role="option"]')[activeLanguageIndex.value]?.focus());
}

function openLanguagePicker(index = selectedLanguage()) {
  languageOpen.value = true;
  focusLanguageOption(index);
}

function closeLanguagePicker(returnFocus = false) {
  languageOpen.value = false;
  if (returnFocus) void nextTick(() => languageButton.value?.focus());
}

function chooseLanguage(value: string) {
  emit('update-pref', 'language', value);
  closeLanguagePicker(true);
}

function languageKeydown(event: KeyboardEvent) {
  if (event.key === 'Escape') {
    if (!languageOpen.value) return;
    event.preventDefault();
    event.stopPropagation();
    closeLanguagePicker(true);
  } else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
    event.preventDefault();
    if (!languageOpen.value) openLanguagePicker();
    else focusLanguageOption(activeLanguageIndex.value + (event.key === 'ArrowDown' ? 1 : -1));
  } else if (event.key === 'Home' || event.key === 'End') {
    event.preventDefault();
    if (!languageOpen.value) languageOpen.value = true;
    focusLanguageOption(event.key === 'Home' ? 0 : languageOptions.length - 1);
  } else if (event.key === 'Enter' || event.key === ' ') {
    if (!languageOpen.value) {
      event.preventDefault();
      openLanguagePicker();
    } else {
      event.preventDefault();
      chooseLanguage(languageOptions[activeLanguageIndex.value].value);
    }
  } else if (event.key === 'Tab' && languageOpen.value) {
    closeLanguagePicker();
  }
}

function onDocumentPointerDown(event: PointerEvent) {
  if (languageOpen.value && !languagePicker.value?.contains(event.target as Node)) closeLanguagePicker();
}

onMounted(() => document.addEventListener('pointerdown', onDocumentPointerDown));
onUnmounted(() => document.removeEventListener('pointerdown', onDocumentPointerDown));

function selectRam(mb: number) {
  if (!presetFits(mb, props.totalMemoryMb)) return;
  emit('update-pref', 'maxMem', mb);
}

function selectResolution(w: number, h: number) {
  emit('update-pref', 'gameWidth', w);
  emit('update-pref', 'gameHeight', h);
}
</script>

<template>
  <div class="modal-overlay" @click.self="emit('close')">
    <div class="modal-dialog settings-dialog glass-surface" role="dialog" aria-modal="true" :aria-label="t('settings')">
      <header class="modal-header">
        <div class="modal-title-group">
          <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" />
            <circle cx="12" cy="12" r="3" />
          </svg>
          <h2>{{ t('settings') }}</h2>
        </div>
        <button class="modal-close-btn" :aria-label="t('close')" @click="emit('close')">✕</button>
      </header>

      <div class="settings-layout">
        <!-- Settings Category Sidebar -->
        <nav class="settings-nav">
          <button class="settings-tab-btn" :class="{ 'is-active': activeTab === 'general' }" @click="activeTab = 'general'">
            <svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8">
              <path d="M10 2v3M10 15v3M2 10h3M15 10h3M4.93 4.93l2.12 2.12M12.95 12.95l2.12 2.12M4.93 15.07l2.12-2.12M12.95 7.05l2.12-2.12" />
            </svg>
            {{ t('general') }}
          </button>
          <button class="settings-tab-btn" :class="{ 'is-active': activeTab === 'java' }" @click="activeTab = 'java'">
            <svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8">
              <path d="M4 6h12v7a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4V6zM16 8h2a2 2 0 0 1 2 2v0a2 2 0 0 1-2 2h-2" />
            </svg>
            {{ t('javaMemory') }}
          </button>
          <button class="settings-tab-btn" :class="{ 'is-active': activeTab === 'window' }" @click="activeTab = 'window'">
            <svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8">
              <rect x="2" y="3" width="16" height="14" rx="2" />
              <path d="M2 7h16" />
            </svg>
            {{ t('gameWindow') }}
          </button>
          <button class="settings-tab-btn" :class="{ 'is-active': activeTab === 'about' }" @click="activeTab = 'about'">
            <svg viewBox="0 0 20 20" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8">
              <circle cx="10" cy="10" r="8" />
              <path d="M10 9v5M10 6h.01" />
            </svg>
            {{ t('about') }}
          </button>
        </nav>

        <!-- Settings Content Body -->
        <div class="settings-content-pane">
          <!-- General Tab -->
          <div v-show="activeTab === 'general'" class="settings-group">
            <div class="setting-row">
              <div class="setting-copy">
                <label for="settings-lang">{{ t('languageLabel') }}</label>
                <span class="setting-hint">App display language</span>
              </div>
              <div ref="languagePicker" class="language-picker" @keydown="languageKeydown">
                <button
                  id="settings-lang"
                  ref="languageButton"
                  type="button"
                  class="language-picker-trigger"
                  aria-haspopup="listbox"
                  aria-controls="settings-language-listbox"
                  :aria-expanded="languageOpen"
                  @click="languageOpen ? closeLanguagePicker() : openLanguagePicker()"
                >
                  <span>{{ languageOptions[selectedLanguage()].label }}</span>
                  <svg class="language-picker-chevron" :class="{ 'is-open': languageOpen }" viewBox="0 0 16 16" aria-hidden="true">
                    <path d="m4 6 4 4 4-4" />
                  </svg>
                </button>
                <div v-show="languageOpen" id="settings-language-listbox" class="language-picker-menu" role="listbox" :aria-label="t('languageLabel')">
                  <button
                    v-for="(option, index) in languageOptions"
                    :id="`settings-language-${option.value}`"
                    :key="option.value"
                    type="button"
                    tabindex="-1"
                    class="language-picker-option"
                    role="option"
                    :aria-selected="locale === option.value"
                    :class="{ 'is-active': activeLanguageIndex === index, 'is-selected': locale === option.value }"
                    @focus="activeLanguageIndex = index"
                    @click="chooseLanguage(option.value)"
                  >
                    <span>{{ option.label }}</span>
                    <svg v-if="locale === option.value" class="language-picker-check" viewBox="0 0 16 16" aria-hidden="true">
                      <path d="m3.5 8 3 3 6-6" />
                    </svg>
                  </button>
                </div>
              </div>
            </div>

            <div class="setting-row">
              <div class="setting-copy">
                <label for="close-on-launch">{{ t('closeAfterLaunch') }}</label>
                <span class="setting-hint">Free up system resources when playing</span>
              </div>
              <input
                id="close-on-launch"
                type="checkbox"
                class="styled-checkbox"
                :checked="settings.closeOnLaunch"
                @change="emit('update-pref', 'closeOnLaunch', ($event.target as HTMLInputElement).checked)"
              />
            </div>

            <div class="setting-row">
              <div class="setting-copy">
                <label for="compact-view">{{ t('compact') }}</label>
                <span class="setting-hint">Show condensed instance items in library</span>
              </div>
              <input
                id="compact-view"
                type="checkbox"
                class="styled-checkbox"
                :checked="compact"
                @change="emit('update-pref', 'compact', ($event.target as HTMLInputElement).checked)"
              />
            </div>

            <div class="setting-row">
              <div class="setting-copy">
                <label for="reduce-motion">{{ t('reducedMotion') }}</label>
                <span class="setting-hint">Disable blur animations and transitions</span>
              </div>
              <input
                id="reduce-motion"
                type="checkbox"
                class="styled-checkbox"
                :checked="reducedMotion"
                @change="emit('update-pref', 'reducedMotion', ($event.target as HTMLInputElement).checked)"
              />
            </div>
          </div>

          <!-- Java & Memory Tab -->
          <div v-if="visitedTabs.has('java')" v-show="activeTab === 'java'" class="settings-group">
            <JavaPicker :service="javaService" :t="t" />
            <div class="setting-block">
              <label>{{ t('maxRamLabel') }}</label>
              <div class="ram-presets-row">
                <button
                  v-for="preset in ramPresets"
                  :key="preset.mb"
                  type="button"
                  class="pill-btn ram-preset"
                  :class="[memoryRisk(preset.mb, totalMemoryMb), { 'is-active': settings.maxMem === preset.mb }]"
                  :disabled="!presetFits(preset.mb, totalMemoryMb)"
                  :title="!presetFits(preset.mb, totalMemoryMb) ? t('ramPresetUnavailable') : t(memoryRisk(preset.mb, totalMemoryMb) === 'danger' ? 'ramDanger' : memoryRisk(preset.mb, totalMemoryMb) === 'caution' ? 'ramCaution' : 'ramSafe')"
                  @click="selectRam(preset.mb)"
                >
                  {{ preset.label }}
                </button>
              </div>
              <div class="ram-legend setting-hint">
                <span class="safe">{{ t('ramSafe') }}</span>
                <span class="caution">{{ t('ramCaution') }}</span>
                <span class="danger">{{ t('ramDanger') }}</span>
              </div>
              <div v-if="totalMemoryMb" class="hardware-label ram-hint">
                <span class="setting-hint installed-ram">{{ t('installedRam') }}: {{ ramVisible ? installedMemoryMb(totalMemoryMb) / 1024 : '-----' }} GB</span>
                <HardwareVisibility :visible="ramVisible" :label="t(ramVisible ? 'hideRam' : 'showRam')" @toggle="ramVisible = !ramVisible" />
              </div>
              <p class="setting-hint">{{ t('ramOverrideHint') }}</p>
              <div class="inline-input-row" style="margin-top: 10px;">
                <input
                  type="number"
                  :aria-label="t('maxRamLabel')"
                  class="styled-input"
                  style="width: 120px;"
                  min="1024"
                  max="65536"
                  step="512"
                  :value="settings.maxMem"
                  @change="emit('update-pref', 'maxMem', Number(($event.target as HTMLInputElement).value))"
                />
                <span class="unit-label">MB</span>
              </div>
            </div>

            <div class="setting-block" style="margin-top: 18px;">
              <label>{{ t('minRamLabel') }}</label>
              <div class="inline-input-row">
                <input
                  type="number"
                  class="styled-input"
                  style="width: 120px;"
                  min="512"
                  max="16384"
                  step="512"
                  :value="settings.minMem"
                  @change="emit('update-pref', 'minMem', Number(($event.target as HTMLInputElement).value))"
                />
                <span class="unit-label">MB</span>
              </div>
            </div>

            <div class="setting-block">
              <JvmPresetPicker id="global-jvm-preset" :model-value="settings.jvmPreset" :t="t" @update:model-value="emit('update-pref', 'jvmPreset', $event)" />
              <label for="global-jvm-args">{{ t('jvmArguments') }}</label>
              <textarea id="global-jvm-args" class="styled-input jvm-arguments" rows="3" maxlength="8192" spellcheck="false" :disabled="settings.jvmPreset !== 'custom'" :value="settings.jvmArgs" @change="emit('update-pref', 'jvmArgs', ($event.target as HTMLTextAreaElement).value)" />
              <p class="setting-hint">{{ t('jvmGlobalHint') }}</p>
              <p class="setting-hint">{{ t('jvmMemoryHint') }}</p>
            </div>
          </div>

          <!-- Game Window Tab -->
          <div v-if="visitedTabs.has('window')" v-show="activeTab === 'window'" class="settings-group">
            <GpuPicker :service="gpuService" :t="t" />
            <div class="setting-block">
              <label>{{ t('windowSizeLabel') }}</label>
              <div class="ram-presets-row">
                <button type="button" class="pill-btn" :class="{ 'is-active': settings.gameWidth === 854 && settings.gameHeight === 480 }" @click="selectResolution(854, 480)">854 × 480</button>
                <button type="button" class="pill-btn" :class="{ 'is-active': settings.gameWidth === 1280 && settings.gameHeight === 720 }" @click="selectResolution(1280, 720)">1280 × 720</button>
                <button type="button" class="pill-btn" :class="{ 'is-active': settings.gameWidth === 1920 && settings.gameHeight === 1080 }" @click="selectResolution(1920, 1080)">1920 × 1080</button>
              </div>
              <div class="inline-input-row" style="margin-top: 10px;">
                <input
                  type="number"
                  class="styled-input"
                  style="width: 100px;"
                  min="320"
                  max="7680"
                  :value="settings.gameWidth"
                  @change="emit('update-pref', 'gameWidth', Number(($event.target as HTMLInputElement).value))"
                />
                <span>×</span>
                <input
                  type="number"
                  class="styled-input"
                  style="width: 100px;"
                  min="240"
                  max="4320"
                  :value="settings.gameHeight"
                  @change="emit('update-pref', 'gameHeight', Number(($event.target as HTMLInputElement).value))"
                />
              </div>
            </div>

            <div class="setting-row" style="margin-top: 16px;">
              <div class="setting-copy">
                <label for="maximize-game">{{ t('maximizeOnStart') }}</label>
              </div>
              <input
                id="maximize-game"
                type="checkbox"
                class="styled-checkbox"
                :checked="settings.maximizeGame"
                @change="emit('update-pref', 'maximizeGame', ($event.target as HTMLInputElement).checked)"
              />
            </div>
          </div>

          <!-- About Tab -->
          <div v-if="visitedTabs.has('about')" v-show="activeTab === 'about'" class="settings-group about-pane">
            <div class="about-brand">
              <img class="brand-logo" :src="logoUrl" alt="" />
              <div>
                <h3>Awake Launcher</h3>
                <p v-if="settings.version" class="brand-version">{{ settings.version }}</p>
              </div>
            </div>
            <UpdateModal embedded :state="updates" :busy="busy" :t="t" @close="emit('close')" @check="emit('check-updates')" @download="kind => emit('download-update', kind)" @automatic="value => emit('automatic-updates', value)" />
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

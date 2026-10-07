import { computed, onScopeDispose, ref, shallowRef, watch } from 'vue';
import { BridgeError, callNative, connectChannel, subscribe } from '../bridge/client.ts';
import type { NativeObject, ErrorCode } from '../bridge/client.ts';
import { ArtworkSequence, acceptSnapshot, emptySnapshot, isLocalImage, parseSnapshot } from '../features/library/model.ts';
import type { Action, PreferenceKey } from '../features/library/model.ts';
import { catalogs, normalizeLocale } from '../i18n/catalogs.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
import { CatalogClient } from '../bridge/catalog.ts';
import type { GpuSettings } from '../features/library/hardware.ts';

interface Failure { code: ErrorCode | 'artworkError'; detail: string; retry: () => Promise<void> }
interface ArtworkEvent { id: string; url: string; error: string }

export function useLauncher() {
  const editorRevision = ref(0);
  const accountsRequest = ref(0);
  const gpuChoice = shallowRef<{ id: string; settings: GpuSettings } | null>(null);
  const state = shallowRef({ ...emptySnapshot(), locale: normalizeLocale(navigator.language) });
  const status = ref<'loading' | 'ready' | 'error'>('loading');
  const busy = ref(false);
  const failure = shallowRef<Failure | null>(null);
  const artwork = ref('');
  const artworkLoading = ref(false);
  const artworkFailure = shallowRef<Failure | null>(null);
  const systemMotion = ref(matchMedia('(prefers-reduced-motion: reduce)').matches);
  const reducedMotion = computed(() => systemMotion.value || state.value.reducedMotion);
  const selected = computed(() => state.value.instances.find(i => i.id === state.value.selectedId));
  const t = (key: MessageKey): string => catalogs[state.value.locale][key] || catalogs.en[key];
  let native: NativeObject | null = null;
  let catalog: CatalogClient | null = null;
  let disposeSignals: (() => void)[] = [];
  let connectionRevision = 0;
  let stateRevision = 0;
  let earlyArtwork: ArtworkEvent | null = null;
  let cancelArtwork: (() => void) | null = null;
  let lastRetry: (() => Promise<void>) | null = null;
  const sequence = new ArtworkSequence();
  const motionQuery = matchMedia('(prefers-reduced-motion: reduce)');
  const motionChanged = () => { systemMotion.value = motionQuery.matches; };
  motionQuery.addEventListener('change', motionChanged);
  watch(() => state.value.selectedId, id => {
    cancelArtwork?.();
    sequence.begin(id);
    if (!id) artwork.value = '';
    artworkFailure.value = null;
    artworkLoading.value = Boolean(id);
  }, { flush: 'sync' });

  async function decodeArtwork(event: ArtworkEvent) {
    if (event.id !== state.value.selectedId) return;
    cancelArtwork?.();
    const revision = sequence.begin(event.id);
    artworkFailure.value = null;
    artworkLoading.value = Boolean(event.url);
    if (!event.url && !event.error) { artwork.value = ''; artworkLoading.value = false; return; }
    try {
      if (event.error) throw new Error(event.error);
      if (!isLocalImage(event.url)) throw new Error('Native artwork URL is outside awake://ui/');
      await new Promise<void>((resolve, reject) => {
        const image = new Image();
        const timer = setTimeout(() => finish(new Error('Screenshot decode exceeded 8 seconds')), 8_000);
        let finished = false;
        function finish(error?: Error) {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          cancelArtwork = null;
          image.onload = null;
          image.onerror = null;
          if (error) { image.src = ''; reject(error); } else resolve();
        }
        image.onload = () => {
          if (!image.naturalWidth || !image.naturalHeight || image.naturalWidth * image.naturalHeight > 16_777_216) finish(new Error('Screenshot dimensions exceed the display bound'));
          else image.decode().then(() => finish(), () => finish(new Error('Chromium could not decode the screenshot')));
        };
        image.onerror = () => finish(new Error('Chromium could not load the screenshot'));
        cancelArtwork = () => finish(new Error('Screenshot superseded by a newer selection'));
        image.src = event.url;
      });
      if (sequence.current(event.id, revision)) artwork.value = event.url;
    } catch (error) {
      if (sequence.current(event.id, revision)) {
        artwork.value = '';
        artworkFailure.value = { code: 'artworkError', detail: error instanceof Error ? error.message : String(error), retry: () => select(event.id) };
      }
    } finally {
      if (sequence.current(event.id, revision)) artworkLoading.value = false;
    }
  }

  function showFailure(error: unknown, retry: () => Promise<void>) {
    const problem = error instanceof BridgeError ? error : new BridgeError('transport', error instanceof Error ? error.message : String(error));
    failure.value = { code: problem.code, detail: problem.detail, retry };
  }
  function flushEarlyArtwork() {
    if (earlyArtwork?.id === state.value.selectedId) { void decodeArtwork(earlyArtwork); earlyArtwork = null; }
  }
  async function connect() {
    const revision = ++connectionRevision;
    catalog?.dispose();
    catalog = null;
    disposeSignals.forEach(dispose => dispose());
    disposeSignals = [];
    native = null;
    earlyArtwork = null;
    stateRevision = 0;
    status.value = 'loading';
    failure.value = null;
    artwork.value = '';
    artworkLoading.value = false;
    cancelArtwork?.();
    sequence.begin('');
    try {
      const connection = await connectChannel(window);
      if (revision !== connectionRevision) return;
      native = connection;
      catalog = new CatalogClient(connection);
      disposeSignals.push(subscribe(connection, 'accountsRequested', () => {
        if (revision === connectionRevision) accountsRequest.value++;
      }));
      disposeSignals.push(subscribe(connection, 'editorChanged', () => {
        if (revision === connectionRevision) editorRevision.value++;
      }));
      disposeSignals.push(subscribe(connection, 'stateChanged', (raw) => {
        if (revision !== connectionRevision) return;
        try {
          state.value = parseSnapshot(raw);
          stateRevision++;
          flushEarlyArtwork();
        } catch (error) { status.value = 'error'; showFailure(error, connect); }
      }));
      disposeSignals.push(subscribe(connection, 'artworkChanged', (id, url, error) => {
        if (revision !== connectionRevision) return;
        if (typeof id !== 'string' || typeof url !== 'string' || typeof error !== 'string') { showFailure(new BridgeError('protocol', 'Invalid artworkChanged signal'), connect); return; }
        const event = { id, url, error };
        if (!state.value.selectedId && status.value === 'loading') earlyArtwork = event;
        else void decodeArtwork(event);
      }));
      disposeSignals.push(subscribe(connection, 'operationFailed', (operation, detail) => {
        if (revision !== connectionRevision) return;
        showFailure(new BridgeError('operation', `${String(operation)}: ${String(detail)}`), lastRetry || connect);
      }));
      const requestedRevision = stateRevision;
      const response = parseSnapshot(await callNative(connection, 'snapshot'));
      if (revision !== connectionRevision) return;
      state.value = acceptSnapshot(state.value, response, stateRevision, requestedRevision);
      flushEarlyArtwork();
      await callNative(connection, 'frontendReady');
      if (revision === connectionRevision) status.value = 'ready';
    } catch (error) {
      if (revision === connectionRevision) { status.value = 'error'; showFailure(error, connect); }
    }
  }
  async function run(method: string, args: unknown[], timeout = 30_000) {
    if (busy.value) return;
    busy.value = true;
    failure.value = null;
    lastRetry = () => run(method, args, timeout);
    try {
      if (!native || status.value !== 'ready') throw new BridgeError('disconnected', 'The native bridge is not ready');
      let result = await callNative(native, method, args, timeout) as { gpuDiscoveryRequired?: boolean; gpuChoiceRequired?: boolean; gpuSettings?: GpuSettings };
      if (method === 'launchInstance' && result.gpuDiscoveryRequired) {
        await queryCatalog('gpuSettings', []);
        result = await callNative(native, method, args, timeout) as typeof result;
      }
      if (method === 'launchInstance' && result.gpuChoiceRequired && result.gpuSettings) {
        gpuChoice.value = { id: String(args[0]), settings: result.gpuSettings };
      }
    } catch (error) { showFailure(error, () => run(method, args, timeout)); }
    finally { busy.value = false; }
  }
  const select = (id: string) => run('selectInstance', [id]);
  const launch = (id = state.value.selectedId) => run('launchInstance', [id]);
  function continueGpuLaunch() {
    const id = gpuChoice.value?.id;
    gpuChoice.value = null;
    if (id) void launch(id);
  }
  const action = (name: Action, id = '') => name === 'launch' ? launch(id) : run('invokeAction', [name, id], 120_000);
  const preference = (key: PreferenceKey | string, value: unknown) => run('setPreference', [key, value]);
  async function queryCatalog(method: string, args: unknown[]) {
    if (!catalog || status.value !== 'ready') throw new BridgeError('disconnected', 'The native bridge is not ready');
    return catalog.request(method, args);
  }
  const searchPacks = (provider: string, query: string, offset: number) => queryCatalog('searchPacks', [provider, query, offset]);
  const packVersions = (provider: string, id: string) => queryCatalog('packVersions', [provider, id]);
  const minecraftVersions = () => queryCatalog('minecraftVersions', []);
  const browseArchive = () => queryCatalog('browseArchive', []);
  const javaService = {
    settings: (id: string) => javaCall('javaSettings', [id]),
    select: (id: string, profile: string) => javaCall('setJavaProfile', [id, profile]),
    browse: (id: string) => queryCatalog('browseJava', [id]),
  };
  const skinService = {
    state: (id: string) => javaCall('skinState', [id]),
    command: (id: string, command: string, payload: Record<string, unknown> = {}) => queryCatalog('skinCommand', [id, command, payload]),
  };
  async function javaCall(method: string, args: unknown[]): Promise<unknown> {
    if (!native || status.value !== 'ready') throw new BridgeError('disconnected', 'The native bridge is not ready');
    return callNative(native, method, args);
  }
  async function instanceDetails(id: string, section: string): Promise<unknown> {
    if (!native || status.value !== 'ready') throw new BridgeError('disconnected', 'The native bridge is not ready');
    return callNative(native, 'instanceDetails', [id, section]);
  }
  async function instanceCommand(id: string, command: string, payload: unknown = null): Promise<unknown> {
    if (!native || status.value !== 'ready') throw new BridgeError('disconnected', 'The native bridge is not ready');
    return callNative(native, 'instanceCommand', [id, command, payload]);
  }
  const gpuService = {
    settings: () => queryCatalog('gpuSettings', []),
    select: (mode: string) => javaCall('setGpuPreference', [mode]),
    openWindows: () => javaCall('openGpuSettings', []),
  };
  const updateService = {
    acknowledge: () => javaCall('acknowledgeUpdateNotification', []),
    openDownload: (kind: 'setup' | 'portable' | 'release') => run('openUpdateDownload', [kind]),
    setAutomatic: (enabled: boolean) => run('setAutomaticUpdates', [enabled]),
  };
  onScopeDispose(() => {
    connectionRevision++;
    catalog?.dispose();
    cancelArtwork?.();
    sequence.begin('');
    disposeSignals.forEach(dispose => dispose());
    motionQuery.removeEventListener('change', motionChanged);
  });
  return { state, status, selected, busy, failure, artwork, artworkLoading, artworkFailure, reducedMotion, systemMotion, t, connect, select, launch, action, preference, searchPacks, packVersions, minecraftVersions, browseArchive, editorRevision, accountsRequest, instanceDetails, instanceCommand, javaService, skinService, gpuService, gpuChoice, continueGpuLaunch, updateService };
}

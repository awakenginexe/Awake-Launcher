import { computed, onScopeDispose, ref, shallowRef, watch } from 'vue';
import { BridgeError, callNative, connectChannel, subscribe } from '../bridge/client.ts';
import type { NativeObject, ErrorCode } from '../bridge/client.ts';
import { ArtworkSequence, acceptSnapshot, emptySnapshot, isLocalImage, parseSnapshot } from '../features/library/model.ts';
import type { Action } from '../features/library/model.ts';
import { catalogs, normalizeLocale } from '../i18n/catalogs.ts';
import type { MessageKey } from '../i18n/catalogs.ts';

interface Failure { code: ErrorCode | 'artworkError'; detail: string; retry: () => Promise<void> }
interface ArtworkEvent { id: string; url: string; error: string }

export function useLauncher() {
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
      await callNative(native, method, args, timeout);
    } catch (error) { showFailure(error, () => run(method, args, timeout)); }
    finally { busy.value = false; }
  }
  const select = (id: string) => run('selectInstance', [id]);
  const launch = () => run('launchInstance', [state.value.selectedId]);
  const action = (name: Action, id = '') => run('invokeAction', [name, id], 120_000);
  const preference = (key: 'compact' | 'reducedMotion' | 'sortMode' | 'pin', value: unknown) => run('setPreference', [key, value]);
  onScopeDispose(() => {
    connectionRevision++;
    cancelArtwork?.();
    sequence.begin('');
    disposeSignals.forEach(dispose => dispose());
    motionQuery.removeEventListener('change', motionChanged);
  });
  return { state, status, selected, busy, failure, artwork, artworkLoading, artworkFailure, reducedMotion, systemMotion, t, connect, select, launch, action, preference };
}

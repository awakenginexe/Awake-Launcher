import { test } from 'node:test';
import assert from 'node:assert/strict';
import { effectScope } from 'vue';
import { useLauncher } from '../src/composables/useLauncher.ts';
import { emptySnapshot } from '../src/features/library/model.ts';

class Signal {
  listeners = new Set<(...args: unknown[]) => void>();
  connect(listener: (...args: unknown[]) => void) { this.listeners.add(listener); }
  disconnect(listener: (...args: unknown[]) => void) { this.listeners.delete(listener); }
  emit(...args: unknown[]) { for (const listener of this.listeners) listener(...args); }
}
const instance = { id: 'one', name: 'One', group: '', minecraftVersion: '', loader: '', loaderVersion: '', iconUrl: '', pinned: false, canLaunch: true, running: false, broken: false, lastLaunch: 0, totalTimePlayed: 0 };
function environment(overrides: Record<string, unknown> = {}) {
  const calls: string[] = [];
  const stateChanged = new Signal();
  const artworkChanged = new Signal();
  const operationFailed = new Signal();
  const snapshot = { ...emptySnapshot(), instances: [instance], selectedId: 'one' };
  const native = {
    snapshot: (callback: (value: unknown) => void) => { calls.push('snapshot'); callback(snapshot); },
    frontendReady: (callback: (value: unknown) => void) => { calls.push('ready'); callback(undefined); },
    selectInstance: (_id: string, callback: (value: unknown) => void) => callback({ ok: true }),
    launchInstance: (_id: string, callback: (value: unknown) => void) => callback({ ok: true }),
    setPreference: (_key: string, _value: unknown, callback: (value: unknown) => void) => callback({ ok: true }),
    stateChanged, artworkChanged, operationFailed, catalogFinished: new Signal(), editorChanged: new Signal(), accountsRequested: new Signal(), ...overrides,
  };
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { qt: { webChannelTransport: {} }, QWebChannel: class { constructor(_transport: unknown, callback: (channel: unknown) => void) { callback({ objects: { awake: native } }); } } } });
  Object.defineProperty(globalThis, 'matchMedia', { configurable: true, value: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }) });
  const scope = effectScope();
  const launcher = scope.run(() => useLauncher())!;
  return { launcher, scope, native, calls, snapshot, stateChanged, artworkChanged, operationFailed };
}
test('signals are connected before frontendReady; disposal disconnects every listener', async () => {
  const fixture = environment({ frontendReady: (callback: (value: unknown) => void) => { assert.equal(fixture.stateChanged.listeners.size, 1); assert.equal(fixture.artworkChanged.listeners.size, 1); callback(undefined); } });
  await fixture.launcher.connect();
  assert.equal(fixture.launcher.status.value, 'ready');
  assert.equal(fixture.launcher.selected.value?.id, 'one');
  fixture.scope.stop();
  assert.equal(fixture.stateChanged.listeners.size + fixture.artworkChanged.listeners.size + fixture.operationFailed.listeners.size, 0);
});
test('new state signal wins over initial snapshot and preference callback never mutates state', async () => {
  const fixture = environment();
  fixture.native.snapshot = (callback: (value: unknown) => void) => { fixture.stateChanged.emit({ ...fixture.snapshot, locale: 'th_TH', accountName: 'Native account' }); callback(fixture.snapshot); };
  await fixture.launcher.connect();
  assert.equal(fixture.launcher.state.value.accountName, 'Native account');
  assert.equal(fixture.launcher.state.value.locale, 'th');
  await fixture.launcher.preference('compact', true);
  assert.equal(fixture.launcher.state.value.compact, false);
  fixture.stateChanged.emit({ ...fixture.snapshot, compact: true });
  assert.equal(fixture.launcher.state.value.compact, true);
  fixture.scope.stop();
});
test('failed launch shows detail and retry repeats the real operation', async () => {
  let count = 0;
  const fixture = environment({ launchInstance: (_id: string, callback: (value: unknown) => void) => { count++; callback(count === 1 ? { ok: false, error: 'Java runtime missing' } : { ok: true }); } });
  await fixture.launcher.connect();
  await fixture.launcher.launch();
  assert.equal(fixture.launcher.failure.value?.detail, 'Java runtime missing');
  assert.equal(fixture.launcher.selected.value?.id, 'one');
  await fixture.launcher.failure.value?.retry();
  assert.equal(count, 2);
  assert.equal(fixture.launcher.failure.value, null);
  fixture.scope.stop();
});
test('deleted selected instance and stale artwork never survive a native snapshot', async () => {
  const fixture = environment();
  await fixture.launcher.connect();
  fixture.artworkChanged.emit('old', 'file:///secret', 'old error');
  assert.equal(fixture.launcher.artworkFailure.value, null);
  fixture.stateChanged.emit(emptySnapshot());
  assert.equal(fixture.launcher.selected.value, undefined);
  assert.equal(fixture.launcher.artwork.value, '');
  fixture.scope.stop();
});
test('unsafe current artwork produces a visible screenshot failure', async () => {
  const fixture = environment();
  await fixture.launcher.connect();
  fixture.artworkChanged.emit('one', 'file:///secret', '');
  assert.equal(fixture.launcher.artworkFailure.value?.code, 'artworkError');
  assert.equal(fixture.launcher.artwork.value, '');
  fixture.scope.stop();
});
test('switching instances cancels a pending image decode and displays only the current artwork', async () => {
  const images: { onload: (() => void) | null; onerror: (() => void) | null; src: string }[] = [];
  Object.defineProperty(globalThis, 'Image', { configurable: true, value: class {
    onload: (() => void) | null = null;
    onerror: (() => void) | null = null;
    src = '';
    naturalWidth = 1920;
    naturalHeight = 1080;
    constructor() { images.push(this); }
    decode() { return Promise.resolve(); }
  } });
  const fixture = environment();
  await fixture.launcher.connect();
  fixture.artworkChanged.emit('one', 'awake://ui/artwork/old', '');
  assert.equal(images.length, 1);
  fixture.stateChanged.emit({ ...fixture.snapshot, instances: [instance, { ...instance, id: 'two' }], selectedId: 'two' });
  assert.equal(images[0].src, '');
  assert.equal(images[0].onload, null);
  fixture.artworkChanged.emit('two', 'awake://ui/artwork/current', '');
  images[1].onload?.();
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(fixture.launcher.artwork.value, 'awake://ui/artwork/current');
  assert.equal(fixture.launcher.artworkFailure.value, null);
  fixture.artworkChanged.emit('two', '', '');
  assert.equal(fixture.launcher.artwork.value, '');
  fixture.scope.stop();
  Reflect.deleteProperty(globalThis, 'Image');
});

test('native account requests open once per signal and disconnect on disposal', async () => {
  const fixture = environment();
  await fixture.launcher.connect();
  fixture.native.accountsRequested.emit();
  assert.equal(fixture.launcher.accountsRequest.value, 1);
  fixture.scope.stop();
  fixture.native.accountsRequested.emit();
  assert.equal(fixture.launcher.accountsRequest.value, 1);
});

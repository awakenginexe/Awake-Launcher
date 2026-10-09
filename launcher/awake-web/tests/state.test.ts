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
const instance = { id: 'one', name: 'One', group: '', minecraftVersion: '', loader: '', loaderVersion: '', iconUrl: '', pinned: false, canLaunch: true, running: false, broken: false, lastLaunch: 0, lastTimePlayed: 0, totalTimePlayed: 0 };
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
    instanceDetails: (_id: string, _section: string, callback: (value: unknown) => void) => callback({ ok: true }),
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
test('combined playtime includes every instance, regardless of selection, pins or group', async () => {
  const fixture = environment();
  await fixture.launcher.connect();
  const instances = [
    { ...instance, totalTimePlayed: 120, lastTimePlayed: 60, group: 'Mods', pinned: true },
    { ...instance, id: 'two', totalTimePlayed: 3600, lastTimePlayed: 300, group: 'Other' },
  ];
  fixture.stateChanged.emit({ ...fixture.snapshot, instances, selectedId: 'one' });
  assert.equal(fixture.launcher.totalPlaytime.value, 3720);
  fixture.stateChanged.emit({ ...fixture.snapshot, instances, selectedId: 'two' });
  assert.equal(fixture.launcher.totalPlaytime.value, 3720);
  fixture.stateChanged.emit({ ...fixture.snapshot, instances: [instances[1]], selectedId: 'two' });
  assert.equal(fixture.launcher.totalPlaytime.value, 3600);
  fixture.stateChanged.emit({ ...fixture.snapshot, instances: [] });
  assert.equal(fixture.launcher.totalPlaytime.value, 0);
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

test('pack launch choices persist only the requested reminder and skip this time checks again', async () => {
  for (const choice of ['skip', 'skipVersion', 'disable', 'update'] as const) {
    let launches = 0, checks = 0, updates = 0;
    const saved: unknown[] = [];
    const pack = { provider: 'curseforge', name: 'Pack', versionId: '1', versionName: 'Old', reminders: true, skippedVersion: '' };
    const fixture = environment({
      instanceDetails: (_id: string, _section: string, callback: (value: unknown) => void) => callback({ ok: true, pack }),
      launchInstance: (_id: string, callback: (value: unknown) => void) => { launches++; callback({ ok: true }); },
      instanceCommand: (id: string, command: string, payload: { choice: string; version: string }, callback: (value: unknown) => void) => {
        assert.equal(id, 'one'); assert.equal(command, 'packReminder'); saved.push(payload);
        if (payload.choice === 'skipVersion') pack.skippedVersion = payload.version;
        if (payload.choice === 'disable') pack.reminders = false;
        callback({ ok: true });
      },
      updateInstancePack: (id: string, version: string, callback: (value: unknown) => void) => {
        assert.equal(id, 'one'); assert.equal(version, '2'); updates++; callback({ ok: true });
      },
    });
    fixture.native.instancePackVersions = (request: string, id: string, callback: (value: unknown) => void) => {
      assert.equal(id, 'one'); checks++;
      fixture.native.catalogFinished.emit(request, { ok: true, versions: [{ id: '2', name: 'New' }, { id: '1', name: 'Old' }] });
      callback({ ok: true });
    };
    await fixture.launcher.connect();
    await fixture.launcher.launch();
    assert.equal(launches, 0);
    assert.equal(fixture.launcher.packChoice.value?.version.id, '2');
    await fixture.launcher.decidePackUpdate(choice);
    assert.equal(fixture.launcher.packChoice.value, null);
    assert.equal(launches, choice === 'update' ? 0 : 1);
    assert.equal(updates, choice === 'update' ? 1 : 0);
    assert.equal(saved.length, choice === 'disable' || choice === 'skipVersion' ? 1 : 0);
    if (choice !== 'update') {
      await fixture.launcher.launch();
      assert.equal(Boolean(fixture.launcher.packChoice.value), choice === 'skip');
      assert.equal(checks, choice === 'disable' ? 1 : 2);
    }
    fixture.scope.stop();
  }
});

test('failed pack checks allow an offline launch and retain the provider error', async () => {
  let launches = 0;
  const fixture = environment({
    instanceDetails: (_id: string, _section: string, callback: (value: unknown) => void) => callback({ ok: true, pack: { reminders: true } }),
    instancePackVersions: (_request: string, _id: string, callback: (value: unknown) => void) => callback({ ok: false, error: 'Provider offline' }),
    launchInstance: (_id: string, callback: (value: unknown) => void) => { launches++; callback({ ok: true }); },
  });
  await fixture.launcher.connect();
  await fixture.launcher.launch();
  assert.equal(launches, 1);
  assert.equal(fixture.launcher.failure.value?.detail, 'Provider offline');
  assert.equal(fixture.launcher.packChoice.value, null);
  fixture.scope.stop();
});

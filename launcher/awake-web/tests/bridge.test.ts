import { test } from 'node:test';
import assert from 'node:assert/strict';
import { callNative, BridgeError, connectChannel } from '../src/bridge/client.ts';

test('QWebChannel callbacks resolve successful operations', async () => {
  const native = { invokeAction: (action: string, id: string, callback: (result: unknown) => void) => callback({ ok: action === 'edit' && id === 'one' }) };
  assert.deepEqual(await callNative(native, 'invokeAction', ['edit', 'one']), { ok: true });
});
test('backend failure remains visible with native technical detail', async () => {
  await assert.rejects(callNative({ launchInstance: (_id: string, callback: (r: unknown) => void) => callback({ ok: false, error: 'Instance was deleted' }) }, 'launchInstance', ['gone']), (error: unknown) => error instanceof BridgeError && error.code === 'operation' && error.detail === 'Instance was deleted');
});
test('settings failures preserve the field to focus', async () => {
  await assert.rejects(callNative({ instanceCommand: (callback: (r: unknown) => void) => callback({ ok: false, error: 'Invalid RAM', field: 'maxMemory' }) }, 'instanceCommand'), (error: unknown) => error instanceof BridgeError && error.detail === 'Invalid RAM' && 'field' in error && error.field === 'maxMemory');
});
test('missing methods, malformed results, transport errors and timeout reject', async () => {
  await assert.rejects(callNative({}, 'snapshot'), /snapshot/);
  await assert.rejects(callNative({ selectInstance: (callback: (r: unknown) => void) => callback(undefined) }, 'selectInstance'), (e: unknown) => e instanceof BridgeError && e.code === 'protocol');
  await assert.rejects(callNative({ snapshot: () => { throw new Error('transport closed'); } }, 'snapshot'), /transport closed/);
  await assert.rejects(callNative({ snapshot: () => {} }, 'snapshot', [], 5), (e: unknown) => e instanceof BridgeError && e.code === 'timeout');
});
test('standalone browsers report disconnected, never manufacture a backend', async () => {
  await assert.rejects(connectChannel({}), (e: unknown) => e instanceof BridgeError && e.code === 'disconnected');
});

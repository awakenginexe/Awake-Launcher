import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CatalogClient } from '../src/bridge/catalog.ts';

function fixture() {
  const listeners = new Set<(id: string, result: unknown) => void>();
  const requests: { id: string; args: unknown[] }[] = [];
  const signal = { connect: (fn: any) => listeners.add(fn), disconnect: (fn: any) => listeners.delete(fn) };
  const native = { catalogFinished: signal, searchPacks: (id: string, ...args: unknown[]) => {
    const callback = args.pop() as (result: unknown) => void;
    requests.push({ id, args }); callback({ ok: true });
  } };
  const client = new CatalogClient(native);
  const reply = (index: number, result: unknown) => listeners.forEach(fn => fn(requests[index]!.id, result));
  return { client, requests, reply, listeners, native };
}

test('overlapping catalog queries are matched by request ID, including reverse replies', async () => {
  const f = fixture();
  const first = f.client.request('searchPacks', ['modrinth', 'old', 0]);
  const second = f.client.request('searchPacks', ['curseforge', 'new', 25]);
  f.reply(1, { ok: true, packs: [{ id: 'new' }], hasMore: false });
  f.reply(0, { ok: true, packs: [{ id: 'old' }], hasMore: true });
  assert.equal((await first).packs[0].id, 'old');
  assert.equal((await second).packs[0].id, 'new');
  assert.deepEqual(f.requests[1]!.args, ['curseforge', 'new', 25]);
  f.client.dispose();
});

test('provider failures reject, and disposal disconnects and rejects pending queries', async () => {
  const f = fixture();
  const failed = f.client.request('searchPacks', ['curseforge', '', 0]);
  f.reply(0, { ok: false, error: 'API key missing' });
  await assert.rejects(failed, /API key missing/);
  const pending = f.client.request('searchPacks', ['modrinth', '', 0]);
  f.client.dispose();
  await assert.rejects(pending, /disconnected/i);
  assert.equal(f.listeners.size, 0);
});

test('a result delivered before the native acknowledgement is retained', async () => {
  const f = fixture();
  f.native.searchPacks = (id, ...args) => {
    f.listeners.forEach(fn => fn(id, { ok: true, packs: [], hasMore: false }));
    (args.at(-1) as (value: unknown) => void)({ ok: true });
  };
  assert.deepEqual((await f.client.request('searchPacks', ['ftb-app', '', 0])).packs, []);
  f.client.dispose();
});

test('reconnecting clients use different request IDs on the same native object', async () => {
  const f = fixture();
  const previous = f.client.request('searchPacks', ['modrinth', 'old', 0]);
  f.client.dispose();
  await assert.rejects(previous, /disconnected/i);
  const next = new CatalogClient(f.native);
  const current = next.request('searchPacks', ['modrinth', 'current', 0]);
  assert.notEqual(f.requests[0]!.id, f.requests[1]!.id);
  f.reply(0, { ok: false, error: 'Old request replaced' });
  f.reply(1, { ok: true, packs: [{ id: 'current' }] });
  assert.equal((await current).packs[0].id, 'current');
  next.dispose();
});

test('mod install progress is correlated and cancellation retains completed downloads', async () => {
  const replies = new Set<(id: string, result: unknown) => void>();
  const progress = new Set<(id: string, result: unknown) => void>();
  const signal = (listeners: Set<(id: string, result: unknown) => void>) => ({ connect: (fn: any) => listeners.add(fn), disconnect: (fn: any) => listeners.delete(fn) });
  let requestId = '';
  let canceled = '';
  const client = new CatalogClient({
    catalogFinished: signal(replies), modProgress: signal(progress),
    modInstall: (id: string, instance: string, review: string, cb: (value: unknown) => void) => { requestId = id; cb({ ok: true }); },
    modCancel: (id: string, cb: (value: unknown) => void) => { canceled = id; cb({ ok: true }); },
  });
  const controller = new AbortController();
  const updates: unknown[] = [];
  const result = client.request<{ installed: string[]; canceled: boolean }>('modInstall', ['instance', 'review'], { signal: controller.signal, progress: value => updates.push(value), acceptFailure: true });
  progress.forEach(fn => fn('unrelated', { current: 9 }));
  progress.forEach(fn => fn(requestId, { current: 1, total: 2 }));
  controller.abort();
  assert.equal(canceled, requestId);
  replies.forEach(fn => fn(requestId, { ok: false, canceled: true, error: 'Canceled', installed: ['first-mod'] }));
  assert.deepEqual((await result).installed, ['first-mod']);
  assert.deepEqual(updates, [{ current: 1, total: 2 }]);
  client.dispose();
  assert.equal(progress.size, 0);
});

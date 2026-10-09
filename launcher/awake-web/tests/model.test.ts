import { test } from 'node:test';
import assert from 'node:assert/strict';
import { normalizeLocale, catalogs } from '../src/i18n/catalogs.ts';
import { presentInstances, parseSnapshot, emptySnapshot, acceptSnapshot, isLocalImage, ArtworkSequence } from '../src/features/library/model.ts';

const make = (id: string, name: string, extra = {}) => ({ id, name, group: '', minecraftVersion: '', loader: '', loaderVersion: '', iconUrl: '', pinned: false, canLaunch: true, running: false, broken: false, lastLaunch: 0, lastTimePlayed: null, totalTimePlayed: 0, ...extra });
test('snapshot retains deletion status even after the instance disappears from the library', () => {
  const deletion = { active: true, id: 'removed', name: 'Removing instance' };
  assert.deepEqual(parseSnapshot({ ...emptySnapshot(), deletion }).deletion, deletion);
  assert.deepEqual(parseSnapshot({ ...emptySnapshot(), deletion: undefined }).deletion, { active: false, id: '', name: '' });
});
test('account heads accept only native local images', () => {
  const account = { id: 'skin-account', name: 'Player', type: 'microsoft', active: true, valid: true };
  const headUrl = 'awake://ui/images/0123456789abcdef.png';
  const head = (url: string) => (parseSnapshot({ ...emptySnapshot(), accounts: [{ ...account, headUrl: url }] }).accounts[0] as unknown as { headUrl: string }).headUrl;
  assert.equal(head(headUrl), headUrl);
  assert.equal(head('file:///C:/private.png'), '');
  assert.equal(head('https://example.com/tracking.png'), '');
});
test('locale conventions and four complete independent catalogs', () => {
  for (const [input, expected] of [['en_US', 'en'], ['th_TH', 'th'], ['zh_CN', 'zh-CN'], ['zh-Hans', 'zh-CN'], ['zh_TW', 'zh-TW'], ['zh_Hant_HK', 'zh-TW'], ['fr_FR', 'en']]) assert.equal(normalizeLocale(input), expected);
  for (const catalog of Object.values(catalogs)) assert.deepEqual(Object.keys(catalog).sort(), Object.keys(catalogs.en).sort());
  assert.notEqual(catalogs['zh-CN'].settings, catalogs['zh-TW'].settings);
});
test('filter searches names, versions and groups, pins sort first without mutating backend', () => {
  const items = [make('b', 'Beta', { group: 'Mods', lastLaunch: 100 }), make('a', 'Alpha', { minecraftVersion: '1.21', pinned: true }), make('c', 'Charlie', { group: 'Mods', lastLaunch: 200 })];
  assert.deepEqual(presentInstances(items, '', '', false, 'LastLaunch', 'en').map(i => i.id), ['a', 'c', 'b']);
  assert.deepEqual(presentInstances(items, 'mods', 'Mods', false, 'Name', 'en').map(i => i.id), ['b', 'c']);
  assert.equal(presentInstances(items, '1.21', '', true, 'Name', 'en')[0].id, 'a');
  assert.deepEqual(items.map(i => i.id), ['b', 'a', 'c']);
});
test('snapshots are validated and selection never invents a missing instance', () => {
  const snapshot = parseSnapshot({ ...emptySnapshot(), instances: [make('one', 'One')], selectedId: 'deleted', locale: 'th_TH' });
  assert.equal(snapshot.selectedId, '');
  assert.equal(snapshot.locale, 'th');
  assert.throws(() => parseSnapshot({ instances: 'bad' }));
  assert.throws(() => parseSnapshot({ ...emptySnapshot(), instances: [make('one', 'One'), make('one', 'Duplicate')] }));
  assert.throws(() => parseSnapshot({ ...emptySnapshot(), instances: [make('one', 'One', { running: 'false' })] }));
});
test('state signal wins over an older initial snapshot response', () => {
  const original = emptySnapshot();
  const signaled = { ...original, accountName: 'From signal' };
  assert.equal(acceptSnapshot(signaled, original, 1, 0), signaled);
  assert.equal(acceptSnapshot(original, signaled, 0, 0), signaled);
});
test('last session time comes from the native snapshot and missing history stays unknown', () => {
  const read = (extra: Record<string, unknown>) => parseSnapshot({ ...emptySnapshot(), instances: [make('one', 'One', extra)] }).instances[0];
  assert.equal(read({ lastTimePlayed: 3661 }).lastTimePlayed, 3661);
  assert.equal(read({ lastTimePlayed: undefined }).lastTimePlayed, null);
  assert.equal(read({ lastTimePlayed: null }).lastTimePlayed, null);
  for (const value of [-1, Infinity, '60', false]) assert.throws(() => read({ lastTimePlayed: value }));
});
test('artwork accepts only host-authorized local URLs and ignores stale decodes', () => {
  assert.equal(isLocalImage('awake://ui/artwork/123'), true);
  for (const url of ['file:///C:/secret.png', 'https://example.com/a.png', 'data:image/png;base64,AA', 'awake://elsewhere/a.png', 'awake://ui/../secret', 'awake://ui/%2e%2e/secret', 'awake://ui/a?path=C:/secret']) assert.equal(isLocalImage(url), false, url);
  const sequence = new ArtworkSequence();
  const old = sequence.begin('a');
  const current = sequence.begin('b');
  assert.equal(sequence.current('a', old), false);
  assert.equal(sequence.current('b', current), true);
  sequence.begin('');
  assert.equal(sequence.current('b', current), false);
});

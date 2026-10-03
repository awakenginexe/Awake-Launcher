import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

// The transport and data below exist only in this test server; production has no fallback backend.
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.AWAKE_TEST_PLAYWRIGHT || 'playwright');
const fixture = `
(() => {
  const options = new URLSearchParams(location.search);
  if (options.has('disconnected')) return;
  const signal = () => ({ listeners: [], connect(fn) { this.listeners.push(fn); }, disconnect(fn) { this.listeners = this.listeners.filter(item => item !== fn); }, emit(...args) { this.listeners.forEach(fn => fn(...args)); } });
  const count = Number(options.get('count') || 50);
  const state = { instances: Array.from({length: count}, (_, i) => ({id: 'fixture-' + i, name: 'Test fixture ' + String(i).padStart(2, '0'), group: i % 2 ? 'Test group' : '', minecraftVersion: '1.21.1', loader: '', loaderVersion: '', iconUrl: '', pinned: false, canLaunch: true, running: false, broken: false, lastLaunch: 0, totalTimePlayed: 0})), selectedId: count ? 'fixture-0' : '', locale: options.get('locale') || 'en_US', reducedMotion: false, compact: false, sortMode: 'Name', accountName: '' };
  const host = {stateChanged: signal(), artworkChanged: signal(), operationFailed: signal(), snapshot(cb) { cb(structuredClone(state)); }, frontendReady(cb) { this.artworkChanged.emit(state.selectedId, '', ''); cb(); }, selectInstance(id, cb) { state.selectedId = id; this.stateChanged.emit(structuredClone(state)); this.artworkChanged.emit(id, '', ''); cb({ok:true}); }, launchInstance(id, cb) { window.__nativeTest.calls.push(['launch', id]); cb({ok:true}); }, invokeAction(action, id, cb) { window.__nativeTest.calls.push([action, id]); cb({ok:true}); }, setPreference(key, value, cb) { if (key === 'pin') state.instances.find(i => i.id === value.id).pinned = value.pinned; else state[key] = value; this.stateChanged.emit(structuredClone(state)); cb({ok:true}); } };
  window.__nativeTest = {calls: [], state};
  window.qt = {webChannelTransport: {}};
  window.QWebChannel = class { constructor(transport, callback) { callback({objects:{awake:host}}); } };
})();
`;
const dist = resolve('dist');
const server = createServer(async (request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1');
  if (url.pathname === '/favicon.ico') { response.writeHead(204).end(); return; }
  if (url.pathname === '/qwebchannel.js') { response.setHeader('Content-Type', 'application/javascript'); response.end(fixture); return; }
  const pathname = url.pathname === '/' ? '/index.html' : url.pathname;
  const path = resolve(dist, '.' + pathname);
  if (!path.startsWith(dist + '/') && !path.startsWith(dist + '\\')) { response.writeHead(403).end(); return; }
  try { const content = await readFile(path); response.setHeader('Content-Type', ({ '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css' })[extname(path)] || 'application/octet-stream'); response.end(content); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ headless: true, channel: process.env.AWAKE_TEST_BROWSER || 'chrome' });
await mkdir('.test-output', { recursive: true });
const errors = [];
const requests = [];
try {
  for (const locale of ['en_US', 'th_TH', 'zh_CN', 'zh_TW']) {
    const page = await browser.newPage({ viewport: { width: 900, height: 650 } });
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    page.on('request', request => { if (!request.url().startsWith(origin)) requests.push(request.url()); });
    await page.goto(`${origin}/?locale=${locale}`);
    await page.waitForSelector('.instance-select');
    assert.equal(await page.locator('.instance-select').count(), 50);
    await page.screenshot({ path: `.test-output/fixture-${locale}-900.png` });
    await page.keyboard.press('Control+f');
    assert.equal(await page.locator('#instance-search').evaluate(element => element === document.activeElement), true);
    await page.locator('#instance-search').fill('fixture 49');
    assert.equal(await page.locator('.instance-select').count(), 1);
    await page.keyboard.press('Enter');
    await page.locator('#play').click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['launch', 'fixture-49']);
    await page.locator('#instance-search').fill('nothing-matches');
    assert.equal(await page.locator('.instance-select').count(), 0);
    await page.locator('.clear-filters').click();
    await page.locator('.instance-select').nth(1).click();
    await page.locator('.identity-actions button').nth(0).click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['edit', 'fixture-1']);
    await page.locator('.identity-actions button').nth(1).click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['folder', 'fixture-1']);
    await page.locator('.creation-actions button').nth(0).click();
    assert.equal((await page.evaluate(() => window.__nativeTest.calls.at(-1)))[0], 'create');
    await page.locator('.creation-actions button').nth(1).click();
    assert.equal((await page.evaluate(() => window.__nativeTest.calls.at(-1)))[0], 'import');
    await page.locator('.account-actions button').nth(0).click();
    assert.equal((await page.evaluate(() => window.__nativeTest.calls.at(-1)))[0], 'accounts');
    await page.locator('.account-actions button').nth(1).click();
    assert.equal((await page.evaluate(() => window.__nativeTest.calls.at(-1)))[0], 'settings');
    await page.locator('.library-header button').click();
    assert.equal((await page.evaluate(() => window.__nativeTest.calls.at(-1)))[0], 'application');
    await page.locator('.pin-button').nth(1).click();
    assert.equal(await page.locator('.instance-row').first().locator('.instance-name').textContent(), 'Test fixture 01');
    await page.locator('.option-button').click();
    await page.locator('#sort-mode').selectOption('LastLaunch');
    assert.equal(await page.evaluate(() => window.__nativeTest.state.sortMode), 'LastLaunch');
    await page.locator('#group-filter').selectOption('Test group');
    assert.equal(await page.locator('.instance-select').count(), 25);
    await page.locator('.options-panel input[type=checkbox]').nth(0).check();
    assert.equal(await page.locator('.instance-select').count(), 1);
    await page.locator('.options-panel input[type=checkbox]').nth(1).check();
    await page.locator('.options-panel input[type=checkbox]').nth(2).check();
    assert.equal(await page.locator('.launcher').evaluate(element => element.classList.contains('reduced-motion') && element.classList.contains('compact')), true);
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('.view-options').getAttribute('open'), null);
    await page.locator('.more-actions summary').click();
    for (const [index, action] of ['manage', 'launchOptions', 'logs', 'legacy'].entries()) {
      if (index) await page.locator('.more-actions summary').click();
      await page.locator('.action-menu button').nth(index).click();
      assert.equal((await page.evaluate(() => window.__nativeTest.calls.at(-1)))[0], action);
    }
    for (const size of [[640, 480], [1920, 1080], [3840, 2160]]) {
      await page.setViewportSize({ width: size[0], height: size[1] });
      const nav = await page.locator('.navigation').boundingBox();
      const play = await page.locator('.launch-dock').boundingBox();
      assert.ok(nav && play && nav.x + nav.width <= play.x + 1, `${locale} controls overlap at ${size}`);
      assert.ok(play.y + play.height <= size[1], `${locale} Play clips at ${size}`);
    }
    await page.close();
  }
  const empty = await browser.newPage();
  await empty.goto(`${origin}/?count=0`);
  await empty.waitForSelector('.empty-state');
  assert.equal(await empty.locator('#play').count(), 0);
  await empty.close();
  const disconnected = await browser.newPage();
  await disconnected.goto(`${origin}/?disconnected=1`);
  await disconnected.waitForSelector('.connection-error');
  assert.equal(await disconnected.locator('.instance-select').count(), 0);
  assert.equal(await disconnected.locator('.creation-actions button').first().isDisabled(), true);
  await disconnected.locator('.connection-error > button').click();
  await disconnected.waitForSelector('.connection-error');
  await disconnected.screenshot({ path: '.test-output/disconnected-900.png' });
  await disconnected.close();
  assert.deepEqual(errors, []);
  assert.deepEqual(requests, []);
  console.log('PASS: four locales; 50/empty/disconnected instances; search; selection/Play; all bridged actions; pin/group/sort/density/reduced motion; Escape; 640/900/1080p/4K; no page errors or remote requests. Test fixture only.');
} finally {
  await browser.close();
  await new Promise(resolve => server.close(resolve));
}

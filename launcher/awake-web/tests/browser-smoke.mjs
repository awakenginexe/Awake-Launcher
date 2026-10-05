import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.AWAKE_TEST_PLAYWRIGHT || 'playwright');
// Only this test server supplies a fixture transport. Production uses Qt and live provider APIs.
const fixture = `(() => {
  const options = new URLSearchParams(location.search);
  if (options.has('disconnected')) return;
  const signal = () => ({ listeners: [], connect(fn) { this.listeners.push(fn); }, disconnect(fn) { this.listeners = this.listeners.filter(item => item !== fn); }, emit(...args) { this.listeners.forEach(fn => fn(...args)); } });
  const count = Number(options.get('count') ?? 50);
  const state = { instances: Array.from({length: count}, (_, i) => ({id: 'fixture-' + i, name: 'Test fixture ' + String(i).padStart(2, '0'), group: i % 2 ? 'Test group' : '', minecraftVersion: '1.21.1', loader: '', loaderVersion: '', iconUrl: '', pinned: false, canLaunch: true, running: false, broken: false, lastLaunch: 0, totalTimePlayed: 0})), selectedId: count ? 'fixture-0' : '', locale: options.get('locale') || 'en_US', reducedMotion: false, compact: false, sortMode: 'Name', accountName: '' };
  state.totalMemoryMb = Number(options.get('ram') || 32768);
  state.launcherSettings = {maxMem:8192,minMem:512,jvmPreset:'balanced',jvmArgs:'',version:'0.4.0'};
  state.updates = {status: options.get('updateStatus') || 'idle', currentVersion:'0.3.0', latestVersion:'0.4.0', notes:'Browser test fixture: improved launcher updates. <script>window.__unsafeNotes = true</script>', error:'Network error fixture', automatic:true, portable:options.has('portable'), presentation:options.has('updates') ? 1 : 0, setupUrl:'https://github.com/awakenginexe/Awake-Launcher/releases/download/v0.4.0/Awake-Launcher-v0.4.0-Windows-x64-Setup.exe', portableUrl:'https://github.com/awakenginexe/Awake-Launcher/releases/download/v0.4.0/Awake-Launcher-v0.4.0-Windows-x64.zip', releaseUrl:'https://github.com/awakenginexe/Awake-Launcher/releases/tag/v0.4.0'};
  const host = {
    openUpdateDownload(kind, cb) { window.__nativeTest.calls.push(['openUpdateDownload',kind]); cb({ok:true}); },
    setAutomaticUpdates(enabled, cb) { state.updates.automatic = enabled; this.stateChanged.emit(structuredClone(state)); cb({ok:true}); },
    acknowledgeUpdateNotification(cb) { window.__nativeTest.calls.push(['acknowledgeUpdateNotification']); cb({ok:true}); },
    gpuSettings(cb) { cb({ok:true,supported:true,mode:state.gpuMode || 'automatic',devices:[{name:'NVIDIA GeForce RTX 3070 Ti'},{name:'Intel UHD Graphics 770'}].slice(0,Number(options.get('gpuCount') || 2)),powerSavingName:'Intel UHD Graphics 770',highPerformanceName:'NVIDIA GeForce RTX 3070 Ti'}); },
    setGpuPreference(mode, cb) { if (state.failGpuSave) { cb({ok:false,error:'GPU save failure fixture'}); return; } state.gpuMode = mode; state.gpuSeen = true; window.__nativeTest.calls.push(['setGpuPreference',mode]); this.gpuSettings(cb); },
    openGpuSettings(cb) { window.__nativeTest.calls.push(['openGpuSettings']); cb({ok:true}); },
    javaSettings(id, cb) { cb(structuredClone(id ? window.__nativeTest.instanceJava : window.__nativeTest.globalJava)); },
    setJavaProfile(id, profile, cb) {
      window.__nativeTest.calls.push(['setJavaProfile', id, profile]);
      const data = id ? window.__nativeTest.instanceJava : window.__nativeTest.globalJava;
      data.profile = profile === 'inherit' ? window.__nativeTest.globalJava.profile : profile; data.inherited = profile === 'inherit';
      cb(structuredClone(data));
    },
    browseJava(request, id, cb) {
      window.__nativeTest.calls.push(['browseJava', id]); cb({ok:true});
      const data = id ? window.__nativeTest.instanceJava : window.__nativeTest.globalJava;
      data.profile = 'custom'; data.inherited = false; data.path = 'C:/fixture-java/bin/javaw.exe'; data.version = '21';
      this.catalogFinished.emit(request, structuredClone(data));
    },
    stateChanged: signal(), artworkChanged: signal(), operationFailed: signal(), catalogFinished: signal(), editorChanged: signal(), accountsRequested: signal(),
    instanceDetails(id, section, cb) {
      window.__nativeTest.calls.push(['instanceDetails', id, section]);
      const editor = window.__nativeTest.editor;
      if (editor.failSection === section) { cb({ok:false,error:'Fixture editor read failed'}); return; }
      cb({ok:true, name:'Test fixture', section, running:section === 'log', rows:section === 'mods' ? editor.mods : section === 'versions' ? [{id:'minecraft',name:'Minecraft',detail:'1.21.1'}] : [], text:section === 'notes' ? editor.notes : section === 'log' ? editor.log : '', settings:section === 'settings' ? editor.settings : undefined,jvmConfig:{local:editor.localJvm || {jvmPreset:'custom',jvmArgs:''},global:{jvmPreset:state.launcherSettings.jvmPreset,jvmArgs:state.launcherSettings.jvmArgs}}});
    },
    instanceCommand(id, command, payload, cb) {
      window.__nativeTest.calls.push(['instanceCommand', id, command, payload]);
      const editor = window.__nativeTest.editor;
      if (command === 'saveNotes') editor.notes = payload;
      if (command === 'saveSettings') {
        if (!payload.useGlobalJvmArgs) editor.localJvm = {jvmPreset:payload.jvmPreset,jvmArgs:payload.jvmArgs};
        editor.settings = payload.useGlobalJvmArgs ? {...payload,jvmPreset:state.launcherSettings.jvmPreset,jvmArgs:state.launcherSettings.jvmArgs} : payload;
      }
      if (command === 'toggleMod') editor.mods.find(row => row.id === payload.id).enabled = payload.enabled;
      if (command === 'removeFile') editor.mods = editor.mods.filter(row => row.id !== payload.id);
      if (command === 'clearLog') editor.log = '';
      cb({ok:true});
    },
    snapshot(cb) { cb(structuredClone(state)); },
    frontendReady(cb) { this.artworkChanged.emit(state.selectedId, '', ''); cb(); },
    selectInstance(id, cb) { state.selectedId = id; this.stateChanged.emit(structuredClone(state)); this.artworkChanged.emit(id, '', ''); cb({ok:true}); },
    launchInstance(id, cb) { if (options.has('gpuPrompt') && !state.gpuSeen && Number(options.get('gpuCount') || 2) > 1) { this.gpuSettings(gpuSettings => cb({ok:true,gpuChoiceRequired:true,gpuSettings})); return; } window.__nativeTest.calls.push(['launch', id]); cb({ok:true}); },
    invokeAction(action, id, cb) { window.__nativeTest.calls.push([action, id]); if (action === 'checkForUpdates') { state.updates.presentation++; state.updates.status = 'checking'; this.stateChanged.emit(structuredClone(state)); setTimeout(() => {state.updates.status = 'upToDate'; this.stateChanged.emit(structuredClone(state));}, 200); } cb({ok:true}); },
    setPreference(key, value, cb) {
      window.__nativeTest.calls.push(['preference', key, value]);
      if (key === 'pin') state.instances.find(i => i.id === value.id).pinned = value.pinned;
      else if (key === 'language') state.locale = value;
      else if (key === 'maxMem' || key === 'minMem' || key === 'jvmArgs' || key === 'jvmPreset') state.launcherSettings[key] = value;
      else state[key] = value;
      this.stateChanged.emit(structuredClone(state)); cb({ok:true});
    },
    minecraftVersions(id, cb) { cb({ok:true}); this.catalogFinished.emit(id, {ok:true, minecraftVersions:[{version:'1.21.1',released:'2024-08-08',type:'release',recommended:true}]}); },
    searchPacks(id, provider, query, offset, cb) {
      window.__nativeTest.calls.push(['searchPacks', provider, query, offset]); cb({ok:true});
      setTimeout(() => this.catalogFinished.emit(id, query === 'fail' ? {ok:false,error:'Test provider unavailable'} : {ok:true,hasMore:offset === 0,packs:query === 'empty' ? [] : [{id:provider + '-' + offset,name:provider + ' API fixture ' + offset,author:'Test API',description:'Fixture for real bridge wiring',downloads:'42',icon:'',minecraft:'1.21.1',loader:'Fabric'}]}), query === 'slow' ? 1000 : 20);
    },
    packVersions(id, provider, packId, cb) { cb({ok:true}); this.catalogFinished.emit(id, {ok:true,versions:[{id:'release-123',name:'Actual API release',minecraft:'1.21.1',loader:'Fabric'}]}); },
    browseArchive(id, cb) { cb({ok:true}); this.catalogFinished.emit(id, {ok:true,archiveUrl:'file:///C:/test/fixture.mrpack',fileName:'fixture.mrpack'}); }
  };
  window.__nativeTest = {calls: [], state, host, globalJava:{ok:true,profile:'awake',globalProfile:'awake',inherited:false,path:'',version:'',vendor:'',majors:[],running:false},instanceJava:{ok:true,profile:'awake',globalProfile:'awake',inherited:true,path:'',version:'',vendor:'',majors:[21],running:false},editor:{mods:[{id:'test.jar',name:'Test mod',detail:'test.jar',enabled:true}],notes:'Existing notes',log:'[12:00:00] Minecraft test console',settings:{minMemory:512,maxMemory:4096,width:854,height:480,fullscreen:false,overrideMemory:true,overrideWindow:true,jvmArgs:'',jvmPreset:'balanced',useGlobalJvmArgs:true}}}; window.qt = {webChannelTransport: {}};
  window.QWebChannel = class { constructor(transport, callback) { callback({objects:{awake:host}}); } };
})();`;
const dist = resolve('dist');
const server = createServer(async (request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1');
  if (url.pathname === '/favicon.ico') { response.writeHead(204).end(); return; }
  if (url.pathname === '/qwebchannel.js') { response.setHeader('Content-Type', 'application/javascript'); response.end(fixture); return; }
  const path = resolve(dist, '.' + (url.pathname === '/' ? '/index.html' : url.pathname));
  if (!path.startsWith(dist + '/') && !path.startsWith(dist + '\\')) { response.writeHead(403).end(); return; }
  try { response.setHeader('Content-Type', ({ '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css', '.svg': 'image/svg+xml' })[extname(path)] || 'application/octet-stream'); response.end(await readFile(path)); }
  catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ headless: true, channel: process.env.AWAKE_TEST_BROWSER || 'chrome' });
const output = resolve(process.env.AWAKE_TEST_OUTPUT || '.test-output');
await mkdir(output, { recursive: true });
const errors = [];
const requests = [];
try {
  for (const locale of ['en_US', 'th', 'zh_CN', 'zh_TW']) {
    const page = await browser.newPage({viewport:{width:1100,height:750}});
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(`${origin}/?gpuPrompt=1&locale=${locale}`);
    await page.locator('#play:not(:disabled)').waitFor();
    await page.locator('#play').click();
    await page.locator('.gpu-launch-dialog').waitFor();
    assert.equal(await page.locator('.gpu-devices li').first().textContent(), 'NVIDIA GeForce -----');
    assert.equal(await page.locator('#gpu-preference').textContent().then(text => text.includes('3070')), false);
    assert.equal(await page.evaluate(() => document.activeElement.matches('.gpu-launch-dialog .modal-close-btn')), true);
    await page.keyboard.press('Shift+Tab');
    assert.equal(await page.evaluate(() => document.activeElement.matches('.gpu-launch-dialog .btn-primary')), true);
    await page.locator('#gpu-preference').selectOption('highPerformance');
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.filter(call => ['launch','setGpuPreference'].includes(call[0]))), []);
    await page.keyboard.press('Escape');
    await page.locator('.gpu-launch-dialog').waitFor({state:'detached'});
    assert.equal(await page.locator('#play').evaluate(el => el === document.activeElement), true);
    await page.locator('#play').click();
    await page.locator('.gpu-launch-dialog').waitFor();
    assert.equal(await page.locator('#gpu-preference').inputValue(), 'automatic');
    await page.locator('.gpu-launch-dialog .hardware-visibility').click();
    assert.equal(await page.locator('.gpu-devices li').first().textContent(), 'NVIDIA GeForce RTX 3070 Ti');
    assert.equal(await page.locator('#gpu-preference').textContent().then(text => text.includes('3070')), true);
    await page.locator('.hardware-visibility').click();
    await page.screenshot({path:resolve(output, `gpu-launch-private-${locale}.png`)});
    await page.locator('#gpu-preference').selectOption('highPerformance');
    await page.evaluate(() => { window.__nativeTest.state.failGpuSave = true; });
    await page.locator('.gpu-launch-dialog .btn-primary').click();
    await page.locator('.gpu-launch-dialog [role=alert]').waitFor();
    assert.equal(await page.evaluate(() => window.__nativeTest.calls.some(call => call[0] === 'launch')), false);
    await page.evaluate(() => { window.__nativeTest.state.failGpuSave = false; });
    await page.locator('.gpu-launch-dialog .btn-primary').click();
    await page.locator('.gpu-launch-dialog').waitFor({state:'detached'});
    await page.waitForFunction(() => window.__nativeTest.calls.some(call => call[0] === 'launch'));
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.filter(call => ['setGpuPreference','launch'].includes(call[0]))), [['setGpuPreference','highPerformance'],['launch','fixture-0']]);
    await page.locator('#play').click();
    assert.equal(await page.locator('.gpu-launch-dialog').count(), 0);
    await page.locator('.account-actions .settings-button').click();
    await page.locator('.settings-tab-btn').nth(1).click();
    assert.equal(await page.locator('#global-jvm-preset').inputValue(), 'balanced');
    assert.equal(await page.locator('#global-jvm-args').isDisabled(), true);
    for (const preset of ['compatible','performance','custom']) {
      await page.locator('#global-jvm-preset').selectOption(preset);
      await page.waitForFunction(p => window.__nativeTest.state.launcherSettings.jvmPreset === p, preset);
    }
    await page.locator('#global-jvm-args').fill('-Dawake.global=true', {timeout:1000});
    await page.locator('#global-jvm-args').press('Tab');
    await page.waitForFunction(() => window.__nativeTest.state.launcherSettings.jvmArgs === '-Dawake.global=true');
    await page.locator('#global-jvm-preset').selectOption('balanced');
    assert.equal(await page.locator('#global-jvm-args').isDisabled(), true);
    assert.equal(await page.locator('#global-jvm-args').inputValue(), '-Dawake.global=true');
    await page.locator('#global-jvm-preset').selectOption('custom');
    await page.waitForFunction(() => window.__nativeTest.state.launcherSettings.jvmPreset === 'custom');
    assert.equal(await page.locator('.installed-ram').textContent().then(text => text.includes('----- GB')), true);
    await page.locator('.ram-hint .hardware-visibility').click();
    assert.equal(await page.locator('.installed-ram').textContent().then(text => text.includes('32 GB')), true);
    await page.locator('.ram-hint .hardware-visibility').click();
    await page.screenshot({path:resolve(output, `ram-private-${locale}.png`)});
    await page.locator('.settings-tab-btn').nth(2).click();
    assert.equal(await page.locator('.gpu-devices li').first().textContent(), 'NVIDIA GeForce -----');
    await page.locator('.gpu-picker .hardware-visibility').click();
    assert.equal(await page.locator('.gpu-devices li').first().textContent(), 'NVIDIA GeForce RTX 3070 Ti');
    await page.locator('.gpu-picker .hardware-visibility').click();
    await page.screenshot({path:resolve(output, `gpu-settings-private-${locale}.png`)});
    await page.keyboard.press('Escape');
    await page.locator('.instance-row').first().click({button:'right'});
    await page.locator('.instance-context-menu button').nth(1).click();
    await page.locator('[data-section=settings]').click();
    await page.locator('#instance-jvm-args').waitFor();
    assert.equal(await page.locator('#instance-jvm-args').isDisabled(), true);
    await page.locator('.editor-settings fieldset').last().locator('input[type=checkbox]').uncheck();
    await page.locator('#instance-jvm-preset').selectOption('custom');
    await page.locator('#instance-jvm-args').fill('-Dawake.instance=true');
    await page.locator('.editor-settings .editor-save').click();
    await page.waitForFunction(() => window.__nativeTest.editor.settings.jvmArgs === '-Dawake.instance=true');
    assert.equal(await page.evaluate(() => window.__nativeTest.editor.settings.useGlobalJvmArgs), false);
    assert.equal(await page.evaluate(() => window.__nativeTest.state.launcherSettings.jvmArgs), '-Dawake.global=true');
    for (const preset of ['compatible','balanced','performance']) {
      await page.locator('#instance-jvm-preset').selectOption(preset);
      await page.locator('.editor-settings .editor-save').click();
      await page.waitForFunction(p => window.__nativeTest.editor.settings.jvmPreset === p, preset);
      assert.equal(await page.locator('#instance-jvm-args').isDisabled(), true);
      assert.equal(await page.locator('#instance-jvm-args').inputValue(), '-Dawake.instance=true');
    }
    await page.locator('#instance-jvm-preset').selectOption('custom');
    await page.locator('.editor-settings .editor-save').click();
    await page.waitForFunction(() => window.__nativeTest.editor.settings.jvmPreset === 'custom');
    assert.equal(await page.locator('#instance-jvm-args').isEnabled(), true);
    await page.locator('.editor-settings fieldset').last().locator('input[type=checkbox]').check();
    await page.locator('.editor-settings .editor-save').click();
    await page.waitForFunction(() => window.__nativeTest.editor.settings.useGlobalJvmArgs);
    assert.equal(await page.locator('#instance-jvm-args').inputValue(), '-Dawake.global=true');
    await page.locator('.editor-settings fieldset').last().locator('input[type=checkbox]').uncheck();
    assert.equal(await page.locator('#instance-jvm-preset').inputValue(), 'custom');
    assert.equal(await page.locator('#instance-jvm-args').inputValue(), '-Dawake.instance=true');
    await page.locator('.editor-settings .editor-save').click();
    await page.waitForFunction(() => !window.__nativeTest.editor.settings.useGlobalJvmArgs);
    assert.equal(await page.evaluate(() => window.__nativeTest.editor.settings.jvmArgs), '-Dawake.instance=true');
    await page.screenshot({path:resolve(output, `jvm-instance-${locale}.png`)});
    await page.close();
  }
  for (const locale of ['en_US', 'th', 'zh_CN', 'zh_TW']) {
    const page = await browser.newPage({ viewport: {width:1100,height:750} });
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(`${origin}/?locale=${locale}&updates&updateStatus=available`);
    await page.locator('.update-dialog').waitFor();
    assert.equal(await page.locator('.instance-select').first().evaluate(el => Boolean(el.closest('[inert]'))), true);
    assert.equal(await page.locator('.update-dialog').evaluate(el => getComputedStyle(el).fontFamily.includes('K2D')), true);
    assert.equal(await page.evaluate(() => window.__unsafeNotes), undefined);
    assert.ok((await page.locator('.update-notes p').textContent()).includes('<script>'));
    await page.locator('.update-automatic input').uncheck();
    assert.equal(await page.locator('.update-automatic input').isChecked(), false);
    await page.locator('.update-footer .btn-primary').click();
    assert.equal(await page.evaluate(() => window.__nativeTest.calls.some(call => call[0] === 'openUpdateDownload' && call[1] === 'setup')), true);
    await page.locator('.modal-close-btn').focus();
    await page.keyboard.press('Shift+Tab');
    assert.equal(await page.locator('.update-footer .btn-primary').evaluate(el => el === document.activeElement), true);
    await page.keyboard.press('Tab');
    assert.equal(await page.locator('.modal-close-btn').evaluate(el => el === document.activeElement), true);
    await page.screenshot({path:resolve(output, `updates-${locale}.png`)});
    await page.setViewportSize({width:640,height:480});
    const bounds = await page.locator('.update-dialog').boundingBox();
    assert.ok(bounds && bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.width <= 640 && bounds.y + bounds.height <= 480);
    await page.keyboard.press('Escape');
    await page.locator('.update-dialog').waitFor({state:'detached'});
    assert.equal(await page.locator('.update-dialog').count(), 0);
    await page.close();
  }
  for (const status of ['checking','upToDate','error']) {
    const page = await browser.newPage({viewport:{width:800,height:600}, reducedMotion:'reduce'});
    await page.goto(`${origin}/?updates&updateStatus=${status}`);
    await page.locator('.update-dialog').waitFor();
    assert.equal(await page.locator('.update-icon').evaluate(el => getComputedStyle(el).animationName), 'none');
    if (status === 'checking') assert.equal(await page.locator('.update-footer .btn-primary').isDisabled(), true);
    if (status === 'error') {
      await page.locator('.update-footer .btn-primary').click();
      await page.waitForFunction(() => document.activeElement?.classList.contains('modal-close-btn'));
      await page.keyboard.press('Shift+Tab');
      assert.equal(await page.locator('.update-automatic input').evaluate(el => el === document.activeElement), true);
    }
    await page.screenshot({path:resolve(output, `updates-${status}.png`)});
    await page.close();
  }
  {
    const page = await browser.newPage();
    await page.goto(`${origin}/?updates&updateStatus=available&portable`);
    await page.locator('.update-footer .btn-primary').click();
    assert.equal(await page.evaluate(() => window.__nativeTest.calls.some(call => call[0] === 'openUpdateDownload' && call[1] === 'portable')), true);
    await page.close();
  }
  for (const locale of ['en_US', 'th', 'zh_CN', 'zh_TW']) {
    const page = await browser.newPage({ viewport: { width: 1100, height: 750 } });
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    page.on('request', request => { if (!request.url().startsWith(origin)) requests.push(request.url()); });
    await page.goto(`${origin}/?locale=${locale}`);
    await page.waitForSelector('.instance-select');
    assert.equal(await page.locator('.instance-select').count(), 50);
    assert.equal(await page.locator('.launcher').evaluate(el => el.classList.contains('compact')), false);
    await page.keyboard.press('Control+f');
    await page.locator('#instance-search').fill('fixture 49');
    assert.equal(await page.locator('.instance-select').count(), 1);
    await page.keyboard.press('Enter'); await page.locator('#play').click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['launch', 'fixture-49']);
    await page.locator('#instance-search').fill('');
    await page.locator('.instance-select').nth(1).click({button:'right'});
    await page.locator('.instance-context-menu').waitFor();
    await page.screenshot({path:resolve(output, `context-${locale}.png`)});
    await page.locator('.instance-context-menu button').nth(1).click();
    await page.locator('.instance-editor-window').waitFor();
    await page.waitForFunction(() => window.__nativeTest.calls.at(-1)?.[0] === 'instanceDetails');
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['instanceDetails', 'fixture-1', 'overview']);
    await page.locator('.editor-sidebar button[data-section="java"]').click();
    await page.locator('.java-options').waitFor();
    assert.equal(await page.locator('.java-option').count(), 8);
    assert.equal(await page.locator('.java-option:disabled').count(), 8);
    await page.locator('.java-inherit input').uncheck();
    await page.locator('[data-java-profile="microsoft"]').click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['setJavaProfile', 'fixture-1', 'microsoft']);
    await page.locator('[data-java-profile="custom"]').click();
    await page.locator('.java-custom-path output').filter({hasText:'C:/fixture-java/bin/javaw.exe'}).waitFor();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['browseJava', 'fixture-1']);
    await page.screenshot({path:resolve(output, `java-instance-${locale}.png`)});
    await page.locator('.java-inherit input').check();
    await page.locator('.editor-sidebar button[data-section="mods"]').click();
    await page.locator('.editor-mod-toggle input').waitFor();
    await page.locator('.editor-mod-toggle input').uncheck();
    await page.waitForFunction(() => !window.__nativeTest.editor.mods[0].enabled);
    await page.locator('.editor-file-row button').click();
    assert.equal(await page.evaluate(() => window.__nativeTest.editor.mods.length), 1);
    await page.locator('.editor-remove-confirm button').first().click();
    assert.equal(await page.evaluate(() => window.__nativeTest.editor.mods.length), 1);
    await page.locator('.editor-file-row button').click();
    await page.locator('.editor-remove-confirm button').last().click();
    await page.waitForFunction(() => window.__nativeTest.editor.mods.length === 0);
    await page.locator('.editor-toolbar button').first().click();
    await page.waitForFunction(() => window.__nativeTest.calls.some(call => call[0] === 'instanceCommand' && call[2] === 'addFiles'));
    await page.locator('.editor-sidebar button[data-section="notes"]').click();
    await page.locator('#editor-notes').fill('Saved notes from the editor');
    await page.locator('.editor-save').click();
    await page.waitForFunction(() => window.__nativeTest.editor.notes === 'Saved notes from the editor');
    await page.locator('#editor-notes').fill('Unwanted unsaved notes');
    await page.keyboard.press('Escape');
    await page.locator('.editor-discard-confirm').waitFor();
    await page.locator('.editor-discard-confirm button').first().click();
    assert.equal(await page.locator('#editor-notes').inputValue(), 'Unwanted unsaved notes');
    await page.locator('.editor-sidebar button[data-section="settings"]').click();
    await page.locator('.editor-discard-confirm button').last().click();
    await page.locator('.editor-fields input').first().fill('1024');
    await page.locator('.editor-save').click();
    await page.waitForFunction(() => window.__nativeTest.editor.settings.minMemory === 1024);
    await page.locator('.editor-sidebar button[data-section="log"]').click();
    await page.locator('.editor-console').waitFor();
    if (locale === 'en_US') {
      const reads = await page.evaluate(() => window.__nativeTest.calls.filter(call => call[0] === 'instanceDetails' && call[2] === 'log').length);
      await page.waitForTimeout(2800);
      assert.ok(await page.evaluate(() => window.__nativeTest.calls.filter(call => call[0] === 'instanceDetails' && call[2] === 'log').length) > reads, 'Active console did not refresh');
      await page.evaluate(() => { window.__originalHasFocus = document.hasFocus; document.hasFocus = () => false; window.dispatchEvent(new Event('blur')); });
      const pausedReads = await page.evaluate(() => window.__nativeTest.calls.filter(call => call[0] === 'instanceDetails').length);
      await page.waitForTimeout(2800);
      assert.equal(await page.evaluate(() => window.__nativeTest.calls.filter(call => call[0] === 'instanceDetails').length), pausedReads, 'Unfocused console still refreshed');
      await page.evaluate(() => { document.hasFocus = window.__originalHasFocus; window.dispatchEvent(new Event('focus')); });
    }
    await page.locator('.editor-toolbar input[type=checkbox]').uncheck();
    assert.equal(await page.locator('.editor-console').evaluate(el => el.classList.contains('wrap-lines')), false);
    await page.locator('.editor-toolbar button').first().click();
    await page.waitForFunction(() => window.__nativeTest.calls.some(call => call[0] === 'instanceCommand' && call[2] === 'copyLog'));
    const consoleHeading = await page.locator('.editor-section-header h3').textContent();
    if (locale !== 'en_US') assert.notEqual(consoleHeading.trim(), 'Console');
    await page.evaluate(() => document.fonts.ready);
    const fontStyle = await page.locator('.editor-section-header h3').evaluate(el => ({family:getComputedStyle(el).fontFamily,weight:getComputedStyle(el).fontWeight}));
    if (['en_US','th'].includes(locale)) { assert.ok(fontStyle.family.includes('K2D')); assert.equal(fontStyle.weight, '600'); }
    const session = await page.context().newCDPSession(page);
    await session.send('DOM.enable'); await session.send('CSS.enable');
    const {root} = await session.send('DOM.getDocument');
    const {nodeId} = await session.send('DOM.querySelector', {nodeId:root.nodeId,selector:'.editor-section-header h3'});
    const {fonts} = await session.send('CSS.getPlatformFontsForNode', {nodeId});
    if (['en_US','th'].includes(locale)) assert.ok(fonts.some(font => font.isCustomFont && font.familyName === 'K2D' && font.postScriptName === 'K2D-SemiBold'), JSON.stringify(fonts));
    await session.detach();
    await page.screenshot({path:resolve(output, `editor-${locale}.png`)});
    await page.locator('.editor-toolbar button').nth(1).click();
    await page.waitForFunction(() => window.__nativeTest.editor.log === '');
    await page.evaluate(() => { window.__nativeTest.editor.failSection = 'worlds'; });
    await page.locator('.editor-sidebar button[data-section="worlds"]').click();
    await page.locator('.editor-error').filter({hasText:'Fixture editor read failed'}).waitFor();
    await page.evaluate(() => { window.__nativeTest.editor.failSection = ''; });
    await page.locator('.editor-error button').click();
    await page.locator('.editor-error').waitFor({state:'detached'});
    for (const section of ['versions','resourcepacks','shaderpacks','worlds','screenshots','otherlogs']) {
      const tab = page.locator('.editor-sidebar button[data-section="' + section + '"]');
      await tab.click();
      const label = await tab.textContent();
      await page.locator('.editor-section-header h3').filter({hasText:label}).waitFor();
      if (locale !== 'en_US') assert.ok(!['Versions','Resource packs','Shader packs','Worlds','Screenshots','Other logs'].includes(label.trim()), 'Untranslated editor tab');
    }
    for (const [width,height] of [[640,480],[1920,1080]]) {
      await page.setViewportSize({width,height});
      const footer = await page.locator('.editor-footer button').last().boundingBox();
      assert.ok(footer && footer.x + footer.width <= width && footer.y + footer.height <= height, `Editor footer clipped at ${width}x${height}`);
      assert.equal(await page.locator('.instance-editor-window').evaluate(el => el.scrollWidth > el.clientWidth), false);
    }
    await page.setViewportSize({width:1100,height:750});
    await page.locator('.editor-sidebar button[data-section="log"]').click();
    await page.locator('.editor-section-header h3').filter({hasText:await page.locator('.editor-sidebar button[data-section="log"]').textContent()}).waitFor();
    await page.waitForFunction(() => document.querySelector('.editor-content').getAttribute('aria-busy') === 'false');
    await page.locator('.editor-footer button').first().focus();
    await page.keyboard.press('Tab'); await page.keyboard.press('Tab');
    assert.equal(await page.locator('.instance-editor-window button').first().evaluate(el => el === document.activeElement), true);
    await page.keyboard.press('Escape');
    await page.locator('.instance-editor-window').waitFor({state:'detached'});
    await page.waitForFunction(() => document.querySelectorAll('.instance-select')[1] === document.activeElement);
    const readsBeforeClose = await page.evaluate(() => window.__nativeTest.calls.filter(call => call[0] === 'instanceDetails').length);
    await page.waitForTimeout(2700);
    assert.equal(await page.evaluate(() => window.__nativeTest.calls.filter(call => call[0] === 'instanceDetails').length), readsBeforeClose);
    await page.locator('.instance-row').first().hover();
    await page.waitForTimeout(300);
    const hoverBounds = await page.locator('.instance-row').first().evaluate(row => { const bounds = row.getBoundingClientRect(); const list = row.closest('.instance-list').getBoundingClientRect(); return {right:bounds.right,left:bounds.left,listRight:list.right,listLeft:list.left}; });
    assert.ok(hoverBounds.right <= hoverBounds.listRight + 1 && hoverBounds.left >= hoverBounds.listLeft - 1, 'Hovered instance row is clipped');
    for (const [index, command] of [[0,'launch'], [2,'folder'], [4,'rename'], [5,'changeGroup'], [6,'copy'], [7,'export'], [8,'delete']]) {
      await page.locator('.instance-select').filter({hasText:'Test fixture 01'}).click({button:'right'});
      await page.locator('.instance-context-menu button').nth(index).click();
      await page.waitForFunction(command => window.__nativeTest.calls.at(-1)?.[0] === command, command);
      assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), [command, 'fixture-1']);
    }
    await page.locator('.instance-select').filter({hasText:'Test fixture 01'}).click({button:'right'});
    await page.locator('.instance-context-menu button').nth(3).click();
    await page.waitForFunction(() => window.__nativeTest.state.instances.find(i => i.id === 'fixture-1').pinned);
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['preference', 'pin', {id:'fixture-1', pinned:true}]);
    await page.evaluate(() => { window.__nativeTest.state.instances.find(i => i.id === 'fixture-1').running = true; window.__nativeTest.host.stateChanged.emit(structuredClone(window.__nativeTest.state)); });
    await page.locator('.instance-select').filter({hasText:'Test fixture 01'}).click({button:'right'});
    await page.locator('.instance-context-menu button').nth(9).click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['kill', 'fixture-1']);
    await page.evaluate(() => { window.__nativeTest.state.instances.find(i => i.id === 'fixture-1').running = false; window.__nativeTest.host.stateChanged.emit(structuredClone(window.__nativeTest.state)); });
    await page.locator('.instance-select').nth(2).focus(); await page.keyboard.press('Shift+F10');
    await page.locator('.instance-context-menu').waitFor(); await page.keyboard.press('Escape');
    await page.waitForFunction(() => document.querySelectorAll('.instance-select')[2] === document.activeElement);
    assert.equal(await page.locator('.instance-select').nth(2).evaluate(el => el === document.activeElement), true);
    await page.evaluate(() => window.__nativeTest.host.accountsRequested.emit());
    await page.locator('.empty-accounts').waitFor();
    assert.equal(await page.locator('.modal-footer .btn-primary').evaluate(el => el === document.activeElement), true);
    if (['en_US','th'].includes(locale)) {
      const body = await page.locator('.empty-accounts p').evaluate(el => ({family:getComputedStyle(el).fontFamily,weight:getComputedStyle(el).fontWeight}));
      assert.ok(body.family.includes('K2D')); assert.equal(body.weight, '400');
      const regularSession = await page.context().newCDPSession(page);
      await regularSession.send('DOM.enable'); await regularSession.send('CSS.enable');
      const {root} = await regularSession.send('DOM.getDocument');
      const {nodeId} = await regularSession.send('DOM.querySelector', {nodeId:root.nodeId,selector:'.empty-accounts p'});
      const {fonts} = await regularSession.send('CSS.getPlatformFontsForNode', {nodeId});
      assert.ok(fonts.some(font => font.isCustomFont && font.postScriptName === 'K2D-Regular'), JSON.stringify(fonts));
      await regularSession.detach();
    }
    await page.waitForTimeout(300);
    await page.screenshot({path:resolve(output, `accounts-${locale}.png`)});
    await page.locator('.modal-footer .btn-primary').click();
    assert.equal(await page.evaluate(() => window.__nativeTest.calls.at(-1)[0]), 'addMicrosoft');
    await page.keyboard.press('Tab'); await page.keyboard.press('Tab');
    assert.equal(await page.locator('.modal-close-btn').evaluate(el => el === document.activeElement), true);
    await page.keyboard.press('Escape');
    await page.locator('.empty-accounts').waitFor({state:'detached'});
    await page.locator('.account-actions .settings-button').click();
    await page.locator('.settings-tab-btn').nth(1).click();
    await page.locator('.java-options').waitFor();
    assert.equal(await page.locator('.wordmark').textContent(), ({en_US:'Welcome!',th:'ยินดีต้อนรับ!','zh_CN':'欢迎！','zh_TW':'歡迎！'})[locale]);
    assert.equal(await page.locator('.ram-preset').count(), 9);
    assert.equal(await page.locator('.ram-preset.safe').count(), 6);
    assert.equal(await page.locator('.ram-preset.caution').count(), 2);
    assert.equal(await page.locator('.ram-preset.danger').count(), 1);
    await page.locator('.ram-preset').last().scrollIntoViewIfNeeded();
    await page.screenshot({path:resolve(output, `memory-32gb-${locale}.png`)});
    await page.locator('.ram-preset').last().click();
    assert.equal(await page.evaluate(() => window.__nativeTest.state.launcherSettings.maxMem), 32768);
    await page.evaluate(() => { window.__nativeTest.state.totalMemoryMb = 16384; window.__nativeTest.host.stateChanged.emit(structuredClone(window.__nativeTest.state)); });
    await page.waitForFunction(() => document.querySelectorAll('.ram-preset:disabled').length === 3);
    const maxMemory = page.locator('.settings-dialog input[type="number"]').first();
    await maxMemory.fill('32768'); await maxMemory.press('Tab');
    assert.equal(await page.evaluate(() => window.__nativeTest.calls.at(-1)[2]), 32768);
    await page.evaluate(() => { window.__nativeTest.state.totalMemoryMb = 32768; window.__nativeTest.state.accountName = 'AwakePlayer'; window.__nativeTest.host.stateChanged.emit(structuredClone(window.__nativeTest.state)); });
    await page.waitForFunction(() => document.querySelector('.wordmark').textContent.includes('AwakePlayer'));
    assert.equal(await page.locator('.java-option').count(), 8);
    await page.locator('.modal-overlay').evaluate(async el => {
      await document.fonts.ready;
      await Promise.all(el.getAnimations({subtree: true}).map(animation => animation.finished));
    });
    await page.evaluate(() => {
      const host = window.__nativeTest.host;
      const select = host.setJavaProfile.bind(host);
      window.__nativeTest.selectJavaImmediately = select;
      host.setJavaProfile = (id, profile, cb) => select(id, profile, result => setTimeout(() => cb(result), 180));
    });
    for (const profile of ['minecraft', 'awake', 'graalvm', 'zulu']) {
      const frames = await page.evaluate(profile => new Promise(resolve => {
        const pane = document.querySelector('.settings-content-pane');
        const grid = document.querySelector('.java-options');
        const description = document.querySelector('.java-picker-description');
        const memory = document.querySelector('.setting-block');
        const samples = [];
        const start = performance.now();
        const sample = () => {
          samples.push([grid.getBoundingClientRect().y, description.getBoundingClientRect().height,
            memory.getBoundingClientRect().y, pane.scrollTop, Number(getComputedStyle(grid.firstElementChild).opacity)]);
          if (performance.now() - start < 400) requestAnimationFrame(sample);
          else resolve(samples);
        };
        sample();
        document.querySelector(`[data-java-profile="${profile}"]`).click();
      }), profile);
      for (const [column, label] of ['choices position', 'description height', 'memory position', 'scroll position', 'choice opacity'].entries()) {
        const values = frames.map(frame => frame[column]);
        assert.ok(Math.max(...values) - Math.min(...values) < .1, `${locale}/${profile}: Java ${label} flickers (${Math.min(...values)} to ${Math.max(...values)})`);
      }
    }
    await page.evaluate(() => { window.__nativeTest.host.setJavaProfile = window.__nativeTest.selectJavaImmediately; });
    await page.locator('[data-java-profile="zulu"]').click();
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['setJavaProfile', '', 'zulu']);
    await page.screenshot({path:resolve(output, `java-global-${locale}.png`)});
    if (locale !== 'en_US') assert.notEqual((await page.locator('.java-picker-heading h3').textContent()).trim(), 'Java runtime');
    await page.locator('[data-java-profile="awake"]').focus(); await page.keyboard.press('Enter');
    await page.waitForFunction(() => window.__nativeTest.globalJava.profile === 'awake');
    await page.locator('.settings-tab-btn').first().click();
    await page.locator('.settings-tab-btn').nth(2).click();
    await page.locator('#gpu-preference').waitFor();
    assert.equal(await page.locator('.gpu-devices li').count(), 2);
    await page.locator('#gpu-preference').selectOption('highPerformance');
    await page.waitForFunction(() => window.__nativeTest.state.gpuMode === 'highPerformance');
    assert.deepEqual(await page.evaluate(() => window.__nativeTest.calls.at(-1)), ['setGpuPreference','highPerformance']);
    await page.screenshot({path:resolve(output, `gpu-global-${locale}.png`)});
    await page.locator('.settings-tab-btn').first().click();
    for (const code of ['zh-CN','zh-TW','th','en']) {
      await page.locator('#settings-lang').click();
      await page.locator('#settings-language-' + code).click();
      await page.waitForFunction(code => document.documentElement.lang === code, code);
      assert.equal(await page.locator('#settings-lang').getAttribute('aria-expanded'), 'false');
    }
    await page.locator('#settings-lang').focus(); await page.keyboard.press('ArrowDown'); await page.keyboard.press('End'); await page.keyboard.press('Enter');
    await page.waitForFunction(() => document.documentElement.lang === 'zh-TW');
    await page.locator('#settings-lang').click(); await page.screenshot({path:resolve(output, `language-${locale}.png`)});
    await page.keyboard.press('Escape'); assert.equal(await page.locator('.settings-dialog').isVisible(), true);
    await page.keyboard.press('Escape');
    await page.locator('.creation-actions button').first().click();
    await page.locator('.version-choice').first().waitFor();
    await page.locator('.create-modal-footer button[type=submit]').click();
    const custom = await page.evaluate(() => window.__nativeTest.calls.at(-1));
    assert.equal(custom[0], 'createQuick'); assert.equal(JSON.parse(custom[1]).version, '1.21.1');
    await page.locator('.creation-actions button').first().click();
    for (const provider of ['curseforge','modrinth','atlauncher','ftb','ftb-legacy','ftb-app','technic']) {
      await page.locator('.platform-tab[data-source="' + provider + '"]').click();
      await page.waitForFunction(provider => document.querySelector('.modpack-info h4')?.textContent === provider + ' API fixture 0', provider);
      assert.equal(await page.locator('.create-modal-footer button[type=submit]').isDisabled(), true);
      await page.locator('.modpack-card button').first().click();
      await page.locator('#pack-version option').waitFor({state:'attached'});
      assert.equal(await page.locator('#pack-version').inputValue(), 'release-123');
      assert.equal(await page.locator('.create-modal-footer button[type=submit]').isEnabled(), true);
      if (provider === 'curseforge') {
        await page.locator('.catalog-more').click();
        await page.waitForFunction(() => document.querySelectorAll('.modpack-card').length === 2);
        await page.screenshot({path:resolve(output, `provider-${locale}.png`)});
      }
    }
    await page.locator('.create-modal-footer button[type=submit]').click();
    const install = await page.evaluate(() => window.__nativeTest.calls.at(-1));
    assert.equal(install[0], 'installPack'); assert.equal(JSON.parse(install[1]).provider, 'technic'); assert.equal(JSON.parse(install[1]).versionId, 'release-123');
    await page.locator('.creation-actions button').first().click();
    await page.locator('.platform-tab[data-source="modrinth"]').click();
    await page.locator('.platform-content input[type=search]').fill('fail');
    await page.getByText('Test provider unavailable').waitFor();
    assert.equal(await page.locator('.modpack-card').count(), 0);
    await page.locator('.platform-content input[type=search]').fill('empty');
    await page.waitForFunction(() => !document.querySelector('.modpack-list-container').getAttribute('aria-busy') || document.querySelector('.modpack-list-container').getAttribute('aria-busy') === 'false');
    assert.equal(await page.locator('.modpack-card').count(), 0);
    await page.locator('.platform-content input[type=search]').fill('slow');
    await page.waitForFunction(() => window.__nativeTest.calls.some(call => call[0] === 'searchPacks' && call[2] === 'slow'));
    await page.locator('.platform-tab[data-source="curseforge"]').click();
    await page.waitForFunction(() => document.querySelector('.modpack-info h4')?.textContent?.startsWith('curseforge'));
    await page.waitForTimeout(1100);
    assert.equal(await page.locator('.modpack-info h4').first().textContent(), 'curseforge API fixture 0');
    for (const [width,height] of [[640,480],[1920,1080]]) {
      await page.setViewportSize({width,height});
      const footer = await page.locator('.create-modal-footer button[type=submit]').boundingBox();
      assert.ok(footer && footer.x + footer.width <= width && footer.y + footer.height <= height, `Create footer clipped at ${width}x${height}`);
      assert.equal(await page.locator('.create-instance-window').evaluate(el => el.scrollWidth > el.clientWidth), false);
    }
    await page.keyboard.press('Escape');
    await page.locator('.creation-actions button').nth(1).click();
    await page.locator('.import-drop-zone').click();
    await page.locator('#import-url').waitFor();
    await page.waitForFunction(() => document.querySelector('#import-url').value.startsWith('file:'));
    await page.locator('.create-modal-footer button[type=submit]').click();
    const imported = await page.evaluate(() => window.__nativeTest.calls.at(-1));
    assert.equal(imported[0],'importArchive'); assert.equal(JSON.parse(imported[1]).url,'file:///C:/test/fixture.mrpack');
    await page.close();
  }
  const page = await browser.newPage();
  await page.goto(`${origin}/?count=0`); await page.locator('.empty-state').waitFor();
  await page.goto(`${origin}/?disconnected=1`); await page.locator('.connection-error').waitFor();
  assert.equal(await page.locator('.instance-select').count(),0); await page.close();
  assert.deepEqual(errors, []); assert.deepEqual(requests, []);
  console.log('Browser checks passed: Vue instance editor actions, console focus polling, notes/settings persistence, inline discard/remove confirmations, keyboard/focus/layout, hover clipping, context actions, languages, provider routes, stale replies, import and disconnected state.');
} finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }

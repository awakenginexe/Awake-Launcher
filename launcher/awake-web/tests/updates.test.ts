import test from 'node:test';
import assert from 'node:assert/strict';
import { emptySnapshot, parseSnapshot } from '../src/features/library/model.ts';

test('update state survives native snapshots without exposing download URLs', () => {
  const updates = { status: 'available', currentVersion: '0.2.0', latestVersion: '0.3.0', notes: '<script>unsafe</script>', error: '', automatic: true, portable: true, presentation: 1, setupUrl: 'https://github.com/release.exe', portableUrl: '', releaseUrl: 'https://github.com/release' };
  const snapshot = parseSnapshot({ ...emptySnapshot(), updates });
  assert.equal(snapshot.updates.status, 'available');
  assert.equal(snapshot.updates.currentVersion, '0.2.0');
  assert.equal(snapshot.updates.hasSetup, true);
  assert.equal(snapshot.updates.hasPortable, false);
  assert.equal('setupUrl' in snapshot.updates, false);
  assert.equal(snapshot.updates.notes, '<script>unsafe</script>');
});

test('old or unsupported native snapshots have an honest unavailable state', () => {
  const snapshot = parseSnapshot({ ...emptySnapshot(), updates: {} });
  assert.equal(snapshot.updates.status, 'unavailable');
  assert.equal(snapshot.updates.presentation, 0);
});

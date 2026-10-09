import test from 'node:test';
import assert from 'node:assert/strict';
import { availablePackUpdate } from '../src/features/library/packUpdates.ts';

const versions = [
  { id: '3', name: 'New', minecraft: '1.21', loader: 'Forge' },
  { id: '2', name: 'Installed', minecraft: '1.20', loader: 'Forge' },
  { id: '1', name: 'Old', minecraft: '1.20', loader: 'Forge' },
];
const pack = { provider: 'curseforge', name: 'Pack', versionId: '2', versionName: 'Installed', reminders: true, skippedVersion: '' };
test('only offers a newer provider version', () => {
  assert.equal(availablePackUpdate(pack, versions)?.id, '3');
  assert.equal(availablePackUpdate({ ...pack, versionId: '3' }, versions), undefined);
  assert.equal(availablePackUpdate({ ...pack, versionId: '4', versionName: 'Removed' }, versions), undefined);
});
test('reminders and skipped versions are scoped to this pack', () => {
  assert.equal(availablePackUpdate({ ...pack, reminders: false }, versions), undefined);
  assert.equal(availablePackUpdate({ ...pack, skippedVersion: '3' }, versions), undefined);
  assert.equal(availablePackUpdate({ ...pack, skippedVersion: '1' }, versions)?.id, '3');
});
test('Modrinth falls back to the manifest version name, never guesses from an unknown version', () => {
  assert.equal(availablePackUpdate({ ...pack, provider: 'modrinth', versionId: 'manifest-id' }, versions)?.id, '3');
  assert.equal(availablePackUpdate({ ...pack, provider: 'modrinth', versionId: 'manifest-id', versionName: '1.0' }, versions.map(version => ({ ...version, versionNumber: version.id === '2' ? '1.0' : '2.0' })))?.id, '3');
  assert.equal(availablePackUpdate({ ...pack, provider: 'modrinth', versionId: 'unknown', versionName: 'Unknown' }, versions), undefined);
  assert.equal(availablePackUpdate(pack, []), undefined);
});

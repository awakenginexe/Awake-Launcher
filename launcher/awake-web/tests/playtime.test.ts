import test from 'node:test';
import assert from 'node:assert/strict';
import { formatPlaytime, formatLastPlayed } from '../src/features/library/playtime.ts';

test('playtime preserves seconds, minutes and hours without wrapping at one day', () => {
  assert.equal(formatPlaytime(0, 'en'), '0s');
  assert.equal(formatPlaytime(59, 'en'), '59s');
  assert.equal(formatPlaytime(60, 'en'), '1m');
  assert.equal(formatPlaytime(3661, 'en'), '1h 1m 1s');
  assert.equal(formatPlaytime(90000, 'en'), '25h');
  assert.equal(formatPlaytime(null, 'en'), '—');
  for (const locale of ['th', 'zh-CN', 'zh-TW']) assert.ok(formatPlaytime(3661, locale).length > 0);
});
test('last played uses the recorded timestamp and never invents a missing date', () => {
  assert.equal(formatLastPlayed(0, 'en'), null);
  assert.equal(formatLastPlayed(NaN, 'en'), null);
  assert.equal(formatLastPlayed(1e20, 'en'), null);
  const timestamp = Date.UTC(2026, 9, 9, 12, 30);
  assert.equal(formatLastPlayed(timestamp, 'en'), new Intl.DateTimeFormat('en', { dateStyle: 'medium', timeStyle: 'short' }).format(timestamp));
});

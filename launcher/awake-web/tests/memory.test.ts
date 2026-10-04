import { test } from 'node:test';
import assert from 'node:assert/strict';
import { memoryRisk, presetFits } from '../src/features/library/memory.ts';

test('32 GB follows the requested safe, caution and danger choices', () => {
  for (const gb of [2, 4, 6, 8, 12, 16]) assert.equal(memoryRisk(gb * 1024, 32768), 'safe');
  for (const gb of [20, 24]) assert.equal(memoryRisk(gb * 1024, 32768), 'caution');
  assert.equal(memoryRisk(32768, 32768), 'danger');
  assert.equal(presetFits(32768, 32768), true);
  assert.equal(presetFits(32768, 16384), false);
});

test('thresholds scale with installed RAM and unknown capacity is never labeled safe', () => {
  assert.equal(memoryRisk(8192, 16384), 'safe');
  assert.equal(memoryRisk(12288, 16384), 'caution');
  assert.equal(memoryRisk(16384, 16384), 'danger');
  assert.equal(memoryRisk(4096, 0), 'unknown');
  assert.equal(presetFits(4096, 0), false);
  assert.equal(presetFits(32768, 32700), false);
});

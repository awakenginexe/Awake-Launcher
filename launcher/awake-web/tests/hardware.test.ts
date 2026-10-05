import { test } from 'node:test';
import assert from 'node:assert/strict';
import { privateGpuName } from '../src/features/library/hardware.ts';

test('hidden GPU names keep only the vendor family, never the model', () => {
  assert.equal(privateGpuName('NVIDIA GeForce RTX 3070 Ti', false), 'NVIDIA GeForce -----');
  assert.equal(privateGpuName('AMD Radeon RX 7900 XTX', false), 'AMD Radeon -----');
  assert.equal(privateGpuName('Intel(R) UHD Graphics 770', false), 'Intel Graphics -----');
  assert.equal(privateGpuName('Unknown adapter 123', false), 'GPU -----');
  assert.equal(privateGpuName('NVIDIA GeForce RTX 3070 Ti', true), 'NVIDIA GeForce RTX 3070 Ti');
});

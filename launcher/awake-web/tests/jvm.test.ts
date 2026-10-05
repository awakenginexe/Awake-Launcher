import { test } from 'node:test';
import assert from 'node:assert/strict';
import { jvmPresets, parseJvmPreset } from '../src/features/library/jvm.ts';

test('JVM presets have four choices and reject unknown bridge values', () => {
  assert.deepEqual(jvmPresets.map(p => p.id), ['compatible', 'balanced', 'performance', 'custom']);
  for (const preset of jvmPresets) assert.equal(parseJvmPreset(preset.id), preset.id);
  assert.equal(parseJvmPreset('maximum-fps'), 'compatible');
  assert.equal(parseJvmPreset(null), 'compatible');
});

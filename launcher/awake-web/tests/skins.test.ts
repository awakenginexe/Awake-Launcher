import test from 'node:test';
import assert from 'node:assert/strict';
import { skinBoxes, parseSkinState, skinChanged, projectSkin } from '../src/features/library/skins.ts';

test('skin geometry preserves Minecraft atlas bounds, slim arms and overlay layers', () => {
  for (const variant of ['CLASSIC', 'SLIM'] as const) {
    const boxes = skinBoxes(variant, false);
    assert.equal(boxes.length, 12);
    const arm = boxes.find(box => box.id === 'right-arm')!;
    assert.equal(arm.width, variant === 'SLIM' ? 3 : 4);
    const head = boxes.find(box => box.id === 'head')!;
    assert.deepEqual(head.faces.find(face => face.side === 'front')!.uv, [8, 8, 8, 8]);
    assert.equal(boxes.find(box => box.id === 'head-overlay')!.width, 9);
    for (const box of boxes) {
      assert.equal(box.faces.length, 6);
      for (const face of box.faces) {
        const [x, y, width, height] = face.uv;
        assert.ok(x >= 0 && y >= 0 && x + width <= 64 && y + height <= 64);
      }
    }
  }
  const cape = skinBoxes('CLASSIC', true).find(box => box.id === 'cape')!;
  assert.equal(cape.atlasHeight, 32);
  assert.ok(cape.z < 0);
});
test('skin results reject remote textures and keep account identity', () => {
  const entry = { id: 'current/account', name: 'Player', variant: 'SLIM', textureUrl: 'awake://ui/images/0123456789abcdef.png', previewUrl: '' };
  const state = { accountId: 'account', current: entry, defaults: [entry], capes: [], capeId: '', editable: true };
  assert.equal(parseSkinState(state).current?.variant, 'SLIM');
  assert.equal(parseSkinState({ ...state, current: { ...entry, textureUrl: 'https://example.com/skin.png' } }).current, null);
  assert.throws(() => parseSkinState({ ...state, accountId: null }));
});
test('Apply requires a changed texture, model or owned cape, even across different skin IDs', () => {
  const entry = { id: 'current/account', name: 'Player', variant: 'SLIM' as const, textureHash: 'a'.repeat(64), textureUrl: 'awake://ui/images/0123456789abcdef.png', previewUrl: '' };
  const state = parseSkinState({ accountId: 'account', current: entry, defaults: [], capes: [], capeId: 'cape-a', editable: true });
  assert.equal(skinChanged(state, state.current, 'SLIM', 'cape-a'), false);
  assert.equal(skinChanged(state, { ...state.current!, id: 'default/alex/SLIM' }, 'SLIM', 'cape-a'), false);
  assert.equal(skinChanged(state, state.current, 'CLASSIC', 'cape-a'), true);
  assert.equal(skinChanged(state, state.current, 'SLIM', ''), true);
  assert.equal(skinChanged(state, { ...state.current!, textureHash: 'b'.repeat(64) }, 'SLIM', 'cape-a'), true);
  assert.equal(skinChanged(state, null, 'SLIM', 'cape-a'), false);
});

test('software projection culls rear faces and paints far faces before near faces', () => {
  const front = projectSkin('CLASSIC', false, 0, 0);
  assert.equal(front.length, 12);
  assert.deepEqual(front.find(face => face.part === 'head')!.uv, [8, 8, 8, 8]);
  const rotated = projectSkin('SLIM', true, -25, -10);
  assert.equal(rotated.length, 39);
  assert.ok(rotated.every((face, index) => index === 0 || face.depth >= rotated[index - 1]!.depth));
});

test('cape artwork faces outward and keeps its left-to-right orientation', () => {
  const outside = projectSkin('CLASSIC', true, 180, 0).find(face => face.part === 'cape')!;
  assert.deepEqual(outside.uv, [1, 1, 10, 16]);
  assert.ok(outside.points[0]![0] < outside.points[1]![0]);
  const inside = projectSkin('CLASSIC', true, 0, 0).find(face => face.part === 'cape')!;
  assert.deepEqual(inside.uv, [12, 1, 10, 16]);
});

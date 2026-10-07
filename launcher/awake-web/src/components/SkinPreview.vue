<script setup lang="ts">
import { onMounted, onUnmounted, reactive, ref, shallowRef, watch } from 'vue';
import { projectSkin, rasterSkin } from '../features/library/skins.ts';
import type { SkinVariant } from '../features/library/skins.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
const props = defineProps<{ textureUrl: string; variant: SkinVariant; name: string; capeUrl?: string; t: (key: MessageKey) => string }>();
const yaw = ref(-25), pitch = ref(-10), zoom = ref(9);
const stage = ref<HTMLElement>(), canvas = ref<HTMLCanvasElement>();
const texture = shallowRef<ImageData>(), cape = shallowRef<ImageData>();
const layers: { id: string; label: MessageKey }[] = [
  { id: 'cape', label: 'skinCape' }, { id: 'body-overlay', label: 'skinJacket' },
  { id: 'left-arm-overlay', label: 'skinLeftSleeve' }, { id: 'right-arm-overlay', label: 'skinRightSleeve' },
  { id: 'left-leg-overlay', label: 'skinLeftPantsLeg' }, { id: 'right-leg-overlay', label: 'skinRightPantsLeg' }, { id: 'head-overlay', label: 'skinHat' },
];
const visible = reactive<Record<string, boolean>>(Object.fromEntries(layers.map(layer => [layer.id, true])));
let generation = 0, frame = 0;
const observer = new ResizeObserver(scheduleDraw);
function loadImage(url: string): Promise<ImageData | undefined> {
  if (!url) return Promise.resolve(undefined);
  return new Promise(resolve => {
    const image = new Image();
    image.onload = () => {
      const source = document.createElement('canvas'); source.width = image.width; source.height = image.height;
      const context = source.getContext('2d');
      if (!context) { resolve(undefined); return; }
      context.drawImage(image, 0, 0); resolve(context.getImageData(0, 0, image.width, image.height));
    };
    image.onerror = () => resolve(undefined); image.src = url;
  });
}
watch(() => [props.textureUrl, props.capeUrl], async () => {
  const revision = ++generation;
  const images = await Promise.all([loadImage(props.textureUrl), loadImage(props.capeUrl ?? '')]);
  if (revision !== generation) return;
  texture.value = images[0]; cape.value = images[1]; scheduleDraw();
}, { immediate: true });
watch([yaw, pitch, zoom, () => props.variant], scheduleDraw);
watch(visible, scheduleDraw);
function scheduleDraw() { if (!frame) frame = requestAnimationFrame(draw); }
function draw() {
  frame = 0;
  const target = canvas.value, host = stage.value, context = target?.getContext('2d');
  if (!target || !host || !context) return;
  const ratio = window.devicePixelRatio || 1, width = host.clientWidth, height = host.clientHeight;
  if (target.width !== Math.round(width * ratio)) target.width = Math.round(width * ratio);
  if (target.height !== Math.round(height * ratio)) target.height = Math.round(height * ratio);
  const faces = projectSkin(props.variant, Boolean(cape.value), yaw.value, pitch.value, visible);
  const pixels = rasterSkin(faces, texture.value, cape.value, target.width, target.height, zoom.value * ratio);
  context.putImageData(new ImageData(pixels, target.width, target.height), 0, 0);
  target.dataset.yaw = String(yaw.value); target.dataset.variant = props.variant;
  target.dataset.layers = JSON.stringify(visible);
}
onMounted(() => { if (stage.value) observer.observe(stage.value); scheduleDraw(); });
onUnmounted(() => { generation++; observer.disconnect(); cancelAnimationFrame(frame); });
let drag: { x: number; y: number; id: number } | null = null;
function pointerDown(event: PointerEvent) {
  if (event.button !== 0) return;
  drag = { x: event.clientX, y: event.clientY, id: event.pointerId };
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}
function pointerMove(event: PointerEvent) {
  if (!drag || drag.id !== event.pointerId) return;
  yaw.value = (yaw.value + (event.clientX - drag.x) * 0.6) % 360;
  pitch.value = Math.max(-60, Math.min(60, pitch.value - (event.clientY - drag.y) * 0.4));
  drag.x = event.clientX; drag.y = event.clientY;
}
function rotate(event: KeyboardEvent) {
  if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home'].includes(event.key)) return;
  event.preventDefault();
  if (event.key === 'Home') { yaw.value = -25; pitch.value = -10; }
  else if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') yaw.value += event.key === 'ArrowLeft' ? -15 : 15;
  else pitch.value = Math.max(-60, Math.min(60, pitch.value + (event.key === 'ArrowUp' ? 10 : -10)));
}
</script>
<template>
  <div class="skin-preview">
    <div class="skin-preview-layout">
    <div ref="stage" class="skin-stage" tabindex="0" role="group" :aria-label="`${t('skinPreview')}: ${name}. ${t('skinRotateHint')}`"
      @pointerdown="pointerDown" @pointermove="pointerMove" @pointerup="drag = null" @pointercancel="drag = null" @lostpointercapture="drag = null" @keydown="rotate">
      <canvas ref="canvas" class="skin-canvas" aria-hidden="true" />
      <span v-if="!textureUrl" class="skin-placeholder">{{ t('skinNoPreview') }}</span>
    </div>
    <fieldset class="skin-layer-options">
      <legend>{{ t('skinParts') }}</legend>
      <label v-for="layer in layers" :key="layer.id"><span>{{ t(layer.label) }}</span><input v-model="visible[layer.id]" type="checkbox" role="switch" :data-skin-part="layer.id" /></label>
      <small>{{ t('skinPartsHint') }}</small>
    </fieldset>
    </div>
    <div class="skin-view-controls">
      <button type="button" class="btn-subtle" :aria-label="t('skinRotateLeft')" @click="yaw -= 30">↶</button>
      <label>{{ t('skinZoom') }}<input v-model.number="zoom" type="range" min="5" max="11" step="0.5" /></label>
      <button type="button" class="btn-subtle" :aria-label="t('skinRotateRight')" @click="yaw += 30">↷</button>
    </div>
    <p class="skin-rotate-hint">{{ t('skinRotateHint') }}</p>
  </div>
</template>
<style scoped>
.skin-preview { display: flex; flex-direction: column; flex: 1; min-height: 0; width: 100%; min-width: 0; }
.skin-preview-layout { display: flex; flex: 1; min-height: 0; gap: 12px; }
.skin-layer-options { border: 0; padding: 12px 0; margin: 0; width: 148px; flex-shrink: 0; }
.skin-layer-options legend { font-size: 13px; font-weight: 600; padding: 0; }
.skin-layer-options label { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-bottom: 6px; padding: 9px 8px; font-size: 12px; border: 1px solid rgba(255,255,255,.07); border-radius: 8px; background: rgba(255,255,255,.025); cursor: pointer; }
.skin-layer-options label:has(input:checked) { border-color: rgba(70,151,255,.22); background: rgba(52,120,230,.07); }
.skin-layer-options label:hover { border-color: rgba(70,151,255,.5); background: rgba(52,120,230,.12); }
.skin-layer-options input { appearance: none; position: relative; width: 30px; height: 18px; flex-shrink: 0; margin: 0; border: 1px solid rgba(255,255,255,.18); border-radius: 9px; background: #182230; box-shadow: inset 0 1px 3px rgba(0,0,0,.4); cursor: pointer; }
.skin-layer-options input::before { content: ''; position: absolute; width: 12px; height: 12px; top: 2px; left: 2px; border-radius: 50%; background: #8595a9; }
.skin-layer-options input:checked { border-color: #4e9af8; background: linear-gradient(180deg,#398df5,#1659c5); box-shadow: inset 0 1px 0 rgba(255,255,255,.18); }
.skin-layer-options input:checked::before { left: 14px; background: #eef6ff; box-shadow: 0 1px 2px rgba(0,0,0,.25); }
.skin-layer-options input:focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
.skin-layer-options small { color: var(--text-secondary); font-size: 11px; line-height: 1.5; }
.skin-stage { flex: 1; min-height: 260px; position: relative; touch-action: none; cursor: grab; user-select: none; border-radius: 16px; background: rgba(0,0,0,.18); contain: layout paint; }
.skin-stage:active { cursor: grabbing; }
.skin-stage:focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
.skin-canvas { width: 100%; height: 100%; display: block; }
.skin-placeholder { position: absolute; inset: 0; display: grid; place-items: center; color: var(--text-secondary); }
.skin-view-controls { display: flex; align-items: center; justify-content: center; gap: 12px; padding-top: 14px; }
.skin-view-controls label { display: flex; gap: 8px; align-items: center; font-size: 12px; }
.skin-view-controls input { width: 110px; accent-color: var(--accent); }
.skin-rotate-hint { text-align: center; font-size: 12px; color: var(--text-secondary); margin: 10px 0 0; }
@media (max-width: 760px) { .skin-stage { min-height: 340px; } }
@media (max-width: 1100px) { .skin-preview-layout { flex-direction: column; } .skin-layer-options { width: auto; display: flex; flex-wrap: wrap; gap: 6px 12px; } .skin-layer-options small { width: 100%; } }
</style>

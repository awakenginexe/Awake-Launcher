<script setup lang="ts">
import { onMounted, onUnmounted, reactive, ref, shallowRef, watch } from 'vue';
import { projectSkin, rasterSkin } from '../features/library/skins.ts';
import type { SkinVariant } from '../features/library/skins.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
const props = defineProps<{ textureUrl: string; variant: SkinVariant; name: string; capeUrl?: string; thumbnail?: boolean; t: (key: MessageKey) => string }>();
const yaw = ref(-25), pitch = ref(-10), zoom = ref(props.thumbnail ? 3 : 9);
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
  if (width < 1 || height < 1) return;
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
  if (props.thumbnail || event.button !== 0) return;
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
    <div ref="stage" :class="thumbnail ? 'skin-thumbnail-stage' : 'skin-stage'" :tabindex="thumbnail ? undefined : 0" :role="thumbnail ? 'img' : 'group'" :aria-label="thumbnail ? name : `${t('skinPreview')}: ${name}. ${t('skinRotateHint')}`"
      @pointerdown="pointerDown" @pointermove="pointerMove" @pointerup="drag = null" @pointercancel="drag = null" @lostpointercapture="drag = null" @keydown="rotate">
      <canvas ref="canvas" :class="thumbnail ? 'skin-thumbnail-canvas' : 'skin-canvas'" aria-hidden="true" />
      <span v-if="!textureUrl" class="skin-placeholder">{{ t('skinNoPreview') }}</span>
    </div>
    <fieldset v-if="!thumbnail" class="skin-layer-options">
      <legend>{{ t('skinParts') }}</legend>
      <button v-for="layer in layers" :key="layer.id" type="button" :aria-pressed="visible[layer.id]" :data-skin-part="layer.id" @click="visible[layer.id] = !visible[layer.id]">{{ t(layer.label) }}</button>
      <small :title="t('skinPartsHint')">{{ t('skinPreviewOnly') }}</small>
    </fieldset>
    </div>
    <div v-if="!thumbnail" class="skin-view-controls">
      <button type="button" class="btn-subtle" :aria-label="t('skinRotateLeft')" @click="yaw -= 30">↶</button>
      <label>{{ t('skinZoom') }}<input v-model.number="zoom" type="range" min="5" max="11" step="0.5" /></label>
      <button type="button" class="btn-subtle" :aria-label="t('skinRotateRight')" @click="yaw += 30">↷</button>
    </div>
    <p v-if="!thumbnail" class="skin-rotate-hint">{{ t('skinRotateHint') }}</p>
  </div>
</template>
<style scoped>
.skin-preview { display: flex; flex-direction: column; flex: 1; min-height: 0; width: 100%; min-width: 0; }
.skin-preview-layout { display: flex; flex: 1; min-height: 0; gap: 12px; }
.skin-layer-options { border: 0; padding: 12px 0; margin: 0; width: 110px; flex-shrink: 0; }
.skin-layer-options legend { font-size: 13px; font-weight: 600; padding: 0; }
.skin-layer-options button { display: block; width: 100%; margin-bottom: 6px; padding: 8px 6px; font: inherit; font-size: 12px; color: var(--text-secondary); border: 1px solid #253246; border-radius: 8px; background: #111b29; cursor: pointer; }
.skin-layer-options button[aria-pressed="true"] { color: #eef6ff; border-color: #448fec; background: linear-gradient(180deg,#193b65,#112742); box-shadow: inset 0 1px 0 #ffffff14, 0 0 10px #318bff30; }
.skin-layer-options button:hover { border-color: #78b4ff; }
.skin-layer-options button:focus-visible { outline: 2px solid var(--accent); outline-offset: 3px; }
.skin-layer-options small { color: var(--text-secondary); font-size: 11px; line-height: 1.5; }
.skin-thumbnail-stage { width: 100%; height: 120px; position: relative; contain: layout paint; }
.skin-thumbnail-canvas { display: block; width: 100%; height: 100%; }
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
@media (max-width: 1100px) { .skin-preview-layout { flex-direction: column; } .skin-layer-options { width: auto; display: flex; flex-wrap: wrap; gap: 6px; } .skin-layer-options button { width: auto; margin: 0; padding-inline: 10px; } .skin-layer-options small { width: 100%; } }
</style>

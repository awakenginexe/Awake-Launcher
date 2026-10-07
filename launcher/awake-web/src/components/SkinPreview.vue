<script setup lang="ts">
import { onMounted, onUnmounted, ref, shallowRef, watch } from 'vue';
import { projectSkin } from '../features/library/skins.ts';
import type { SkinVariant } from '../features/library/skins.ts';
import type { MessageKey } from '../i18n/catalogs.ts';
const props = defineProps<{ textureUrl: string; variant: SkinVariant; name: string; capeUrl?: string; t: (key: MessageKey) => string }>();
const yaw = ref(-25), pitch = ref(-10), zoom = ref(9);
const stage = ref<HTMLElement>(), canvas = ref<HTMLCanvasElement>();
const texture = shallowRef<HTMLImageElement>(), cape = shallowRef<HTMLImageElement>();
let generation = 0, frame = 0;
const observer = new ResizeObserver(scheduleDraw);
function loadImage(url: string): Promise<HTMLImageElement | undefined> {
  if (!url) return Promise.resolve(undefined);
  return new Promise(resolve => {
    const image = new Image();
    image.onload = () => resolve(image); image.onerror = () => resolve(undefined); image.src = url;
  });
}
watch(() => [props.textureUrl, props.capeUrl], async () => {
  const revision = ++generation;
  const images = await Promise.all([loadImage(props.textureUrl), loadImage(props.capeUrl ?? '')]);
  if (revision !== generation) return;
  texture.value = images[0]; cape.value = images[1]; scheduleDraw();
}, { immediate: true });
watch([yaw, pitch, zoom, () => props.variant], scheduleDraw);
function scheduleDraw() { if (!frame) frame = requestAnimationFrame(draw); }
function draw() {
  frame = 0;
  const target = canvas.value, host = stage.value, context = target?.getContext('2d');
  if (!target || !host || !context) return;
  const ratio = window.devicePixelRatio || 1, width = host.clientWidth, height = host.clientHeight;
  if (target.width !== Math.round(width * ratio)) target.width = Math.round(width * ratio);
  if (target.height !== Math.round(height * ratio)) target.height = Math.round(height * ratio);
  context.setTransform(1, 0, 0, 1, 0, 0); context.clearRect(0, 0, target.width, target.height);
  context.setTransform(ratio, 0, 0, ratio, width / 2 * ratio, height / 2 * ratio);
  context.imageSmoothingEnabled = false;
  const faces = projectSkin(props.variant, Boolean(cape.value), yaw.value, pitch.value);
  for (const face of faces) {
    const image = face.part === 'cape' ? cape.value : texture.value;
    if (!image) continue;
    const [u,v,w,h] = face.uv, [a,b,,d] = face.points;
    context.save();
    context.transform((b![0]-a![0])*zoom.value/w, (b![1]-a![1])*zoom.value/w,
      (d![0]-a![0])*zoom.value/h, (d![1]-a![1])*zoom.value/h, a![0]*zoom.value, a![1]*zoom.value);
    context.drawImage(image, u, v, w, h, 0, 0, w, h);
    context.restore();
  }
  target.dataset.yaw = String(yaw.value); target.dataset.variant = props.variant;
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
    <div ref="stage" class="skin-stage" tabindex="0" role="group" :aria-label="`${t('skinPreview')}: ${name}. ${t('skinRotateHint')}`"
      @pointerdown="pointerDown" @pointermove="pointerMove" @pointerup="drag = null" @pointercancel="drag = null" @lostpointercapture="drag = null" @keydown="rotate">
      <canvas ref="canvas" class="skin-canvas" aria-hidden="true" />
      <span v-if="!textureUrl" class="skin-placeholder">{{ t('skinNoPreview') }}</span>
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
</style>

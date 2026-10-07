import { isLocalImage } from './model.ts';
export type SkinVariant = 'CLASSIC' | 'SLIM';
export interface SkinEntry { id: string; name: string; variant: SkinVariant; textureHash: string; textureUrl: string; previewUrl: string }
export interface SkinState { accountId: string; editable: boolean; current: SkinEntry | null; minecraftDefault: SkinEntry | null; preview: SkinEntry | null; defaults: SkinEntry[]; capes: { id: string; name: string; textureUrl: string }[]; capeId: string }
export interface SkinService { state: (id: string) => Promise<unknown>; command: (id: string, command: string, payload?: Record<string, unknown>) => Promise<unknown> }
export interface SkinFace { side: string; width: number; height: number; uv: [number, number, number, number] }
export interface SkinBox { id: string; width: number; height: number; depth: number; x: number; y: number; z: number; atlasHeight: number; faces: SkinFace[] }
export function skinBoxes(variant: SkinVariant, cape: boolean): SkinBox[] {
  function box(id: string, width: number, height: number, depth: number, x: number, y: number, z: number, u: number, v: number, tw = width, th = height, td = depth, atlasHeight = 64): SkinBox {
    return { id, width, height, depth, x, y, z, atlasHeight, faces: [
      { side: 'front', width, height, uv: [u + td, v + td, tw, th] },
      { side: 'back', width, height, uv: [u + tw + td * 2, v + td, tw, th] },
      { side: 'left', width: depth, height, uv: [u, v + td, td, th] },
      { side: 'right', width: depth, height, uv: [u + tw + td, v + td, td, th] },
      { side: 'top', width, height: depth, uv: [u + td, v, tw, td] },
      { side: 'bottom', width, height: depth, uv: [u + td + tw, v, tw, td] },
    ] };
  }
  const arm = variant === 'SLIM' ? 3 : 4;
  const parts = [
    box('head', 8, 8, 8, 0, -12, 0, 0, 0),
    box('body', 8, 12, 4, 0, -2, 0, 16, 16),
    box('right-arm', arm, 12, 4, -4 - arm / 2, -2, 0, 40, 16),
    box('left-arm', arm, 12, 4, 4 + arm / 2, -2, 0, 32, 48),
    box('right-leg', 4, 12, 4, -2, 10, 0, 0, 16),
    box('left-leg', 4, 12, 4, 2, 10, 0, 16, 48),
    box('head-overlay', 9, 9, 9, 0, -12, 0, 32, 0, 8, 8, 8),
    box('body-overlay', 8.5, 12.5, 4.5, 0, -2, 0, 16, 32, 8, 12, 4),
    box('right-arm-overlay', arm + 0.5, 12.5, 4.5, -4 - arm / 2, -2, 0, 40, 32, arm, 12, 4),
    box('left-arm-overlay', arm + 0.5, 12.5, 4.5, 4 + arm / 2, -2, 0, 48, 48, arm, 12, 4),
    box('right-leg-overlay', 4.5, 12.5, 4.5, -2, 10, 0, 0, 32, 4, 12, 4),
    box('left-leg-overlay', 4.5, 12.5, 4.5, 2, 10, 0, 0, 48, 4, 12, 4),
  ];
  if (cape) parts.push(box('cape', 10, 16, 1, 0, 0, -3, 0, 0, 10, 16, 1, 32));
  return parts;
}
export function projectSkin(variant: SkinVariant, cape: boolean, yaw: number, pitch: number, visible: Record<string, boolean> = {}) {
  type Point = [number, number, number];
  const cy = Math.cos(yaw * Math.PI / 180), sy = Math.sin(yaw * Math.PI / 180);
  const cx = Math.cos(pitch * Math.PI / 180), sx = Math.sin(pitch * Math.PI / 180);
  const rotate = ([x, y, z]: Point): Point => {
    const rx = x * cy + z * sy, rz = -x * sy + z * cy;
    return [rx, y * cx - rz * sx, y * sx + rz * cx];
  };
  return skinBoxes(variant, cape).filter(box => visible[box.id] !== false).flatMap(box => {
    const w = box.width / 2, h = box.height / 2, d = box.depth / 2;
    const corners: Record<string, Point[]> = {
      front: [[-w,-h,d],[w,-h,d],[w,h,d],[-w,h,d]],
      back: [[w,-h,-d],[-w,-h,-d],[-w,h,-d],[w,h,-d]],
      left: [[-w,-h,-d],[-w,-h,d],[-w,h,d],[-w,h,-d]],
      right: [[w,-h,d],[w,-h,-d],[w,h,-d],[w,h,d]],
      top: [[-w,-h,-d],[w,-h,-d],[w,-h,d],[-w,-h,d]],
      bottom: [[-w,h,d],[w,h,d],[w,h,-d],[-w,h,-d]],
    };
    return box.faces.flatMap(face => {
      const points = corners[face.side]!.map(([x,y,z]) => {
        if (box.id === 'cape') {
          const tilt = 10.8 * Math.PI / 180, down = y + h;
          x = -x; z = -z;
          const away = z * Math.cos(tilt) - down * Math.sin(tilt);
          y = -h + down * Math.cos(tilt) + z * Math.sin(tilt);
          z = away;
        }
        return rotate([x + box.x, y + box.y, z + box.z]);
      });
      const [a,b,c] = points;
      if ((b![0] - a![0]) * (c![1] - a![1]) - (b![1] - a![1]) * (c![0] - a![0]) <= 0.00001) return [];
      return [{ part: box.id, uv: face.uv, atlasHeight: box.atlasHeight, points, depth: points.reduce((sum, point) => sum + point[2], 0) / 4 }];
    });
  }).sort((a,b) => a.depth - b.depth);
}
export function rasterSkin(faces: ReturnType<typeof projectSkin>, skin: { width: number; height: number; data: Uint8ClampedArray } | undefined,
  cape: typeof skin, width: number, height: number, scale: number): Uint8ClampedArray<ArrayBuffer> {
  const pixels = new Uint8ClampedArray(width * height * 4);
  const depth = new Float32Array(width * height).fill(-Infinity);
  for (const face of faces) {
    const image = face.part === 'cape' ? cape : skin;
    if (!image) continue;
    const [a,b,,d] = face.points;
    const ax = a![0] * scale + width / 2, ay = a![1] * scale + height / 2;
    const bx = (b![0] - a![0]) * scale, by = (b![1] - a![1]) * scale;
    const dx = (d![0] - a![0]) * scale, dy = (d![1] - a![1]) * scale;
    const determinant = bx * dy - by * dx;
    if (Math.abs(determinant) < 0.00001) continue;
    const xs = face.points.map(point => point[0] * scale + width / 2), ys = face.points.map(point => point[1] * scale + height / 2);
    const [u,v,w,h] = face.uv;
    for (let y = Math.max(0, Math.floor(Math.min(...ys))); y < Math.min(height, Math.ceil(Math.max(...ys))); y++) {
      for (let x = Math.max(0, Math.floor(Math.min(...xs))); x < Math.min(width, Math.ceil(Math.max(...xs))); x++) {
        const px = x + 0.5 - ax, py = y + 0.5 - ay;
        const s = (px * dy - py * dx) / determinant, t = (bx * py - by * px) / determinant;
        if (s < 0 || s >= 1 || t < 0 || t >= 1) continue;
        const z = a![2] + s * (b![2] - a![2]) + t * (d![2] - a![2]), index = y * width + x;
        if (z < depth[index]!) continue;
        const tx = Math.floor((u + s * w) * image.width / 64), ty = Math.floor((v + t * h) * image.height / face.atlasHeight);
        const source = (ty * image.width + tx) * 4;
        // Minecraft skin layers use cutout transparency; clear texels must not hide the body.
        if (image.data[source + 3]! < 128) continue;
        depth[index] = z;
        pixels[index * 4] = image.data[source]!;
        pixels[index * 4 + 1] = image.data[source + 1]!;
        pixels[index * 4 + 2] = image.data[source + 2]!;
        pixels[index * 4 + 3] = 255;
      }
    }
  }
  return pixels;
}
export function skinChanged(state: SkinState | null, selected: SkinEntry | null, variant: SkinVariant, capeId: string): boolean {
  if (!state || !selected) return false;
  const current = state.current;
  return !current || (selected.textureHash && current.textureHash ? selected.textureHash !== current.textureHash : selected.id !== current.id) || variant !== current.variant || capeId !== state.capeId;
}
export function parseSkinState(value: unknown): SkinState {
  if (!value || typeof value !== 'object') throw new Error('Invalid skin state');
  const data = value as Record<string, unknown>;
  if (typeof data.accountId !== 'string') throw new Error('Invalid skin account');
  function entry(value: unknown): SkinEntry | null {
    if (!value || typeof value !== 'object') return null;
    const item = value as Record<string, unknown>;
    if (typeof item.id !== 'string' || typeof item.name !== 'string' || typeof item.textureUrl !== 'string' || !isLocalImage(item.textureUrl)) return null;
    return { id: item.id, name: item.name, variant: item.variant === 'SLIM' ? 'SLIM' : 'CLASSIC', textureHash: typeof item.textureHash === 'string' && /^[a-f0-9]{64}$/.test(item.textureHash) ? item.textureHash : '', textureUrl: item.textureUrl, previewUrl: typeof item.previewUrl === 'string' && isLocalImage(item.previewUrl) ? item.previewUrl : '' };
  }
  return {
    accountId: data.accountId, editable: data.editable === true, current: entry(data.current), minecraftDefault: entry(data.minecraftDefault), preview: entry(data.preview),
    defaults: Array.isArray(data.defaults) ? data.defaults.map(entry).filter((item): item is SkinEntry => item !== null) : [],
    capes: Array.isArray(data.capes) ? data.capes.filter((item): item is { id: string; name: string; textureUrl?: string } => Boolean(item && typeof item === 'object' && typeof item.id === 'string' && typeof item.name === 'string')).map(item => ({ id: item.id, name: item.name, textureUrl: typeof item.textureUrl === 'string' && isLocalImage(item.textureUrl) ? item.textureUrl : '' })) : [],
    capeId: typeof data.capeId === 'string' ? data.capeId : '',
  };
}

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
export function projectSkin(variant: SkinVariant, cape: boolean, yaw: number, pitch: number) {
  type Point = [number, number, number];
  const cy = Math.cos(yaw * Math.PI / 180), sy = Math.sin(yaw * Math.PI / 180);
  const cx = Math.cos(pitch * Math.PI / 180), sx = Math.sin(pitch * Math.PI / 180);
  const rotate = ([x, y, z]: Point): Point => {
    const rx = x * cy + z * sy, rz = -x * sy + z * cy;
    return [rx, y * cx - rz * sx, y * sx + rz * cx];
  };
  return skinBoxes(variant, cape).flatMap(box => {
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
        if (box.id === 'cape') { x = -x; z = -z; }
        return rotate([x + box.x, y + box.y, z + box.z]);
      });
      const [a,b,c] = points;
      if ((b![0] - a![0]) * (c![1] - a![1]) - (b![1] - a![1]) * (c![0] - a![0]) <= 0.00001) return [];
      return [{ part: box.id, uv: face.uv, points, depth: points.reduce((sum, point) => sum + point[2], 0) / 4 }];
    });
  }).sort((a,b) => a.depth - b.depth);
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

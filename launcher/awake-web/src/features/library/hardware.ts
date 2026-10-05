export interface GpuSettings {
  ok: boolean;
  supported: boolean;
  mode: string;
  devices: { name: string }[];
  powerSavingName?: string;
  highPerformanceName?: string;
}

export function privateGpuName(name: string, visible: boolean): string {
  if (visible) return name;
  if (/nvidia/i.test(name)) return /geforce/i.test(name) ? 'NVIDIA GeForce -----' : 'NVIDIA -----';
  if (/amd|radeon/i.test(name)) return /radeon/i.test(name) ? 'AMD Radeon -----' : 'AMD -----';
  if (/intel/i.test(name)) return 'Intel Graphics -----';
  return 'GPU -----';
}

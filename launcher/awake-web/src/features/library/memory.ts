export const ramPresets = [2, 4, 6, 8, 12, 16, 20, 24, 32].map(gb => ({ label: `${gb} GB`, mb: gb * 1024 }));

export function installedMemoryMb(totalMb: number): number {
  return Number.isFinite(totalMb) && totalMb > 0 ? totalMb : 0;
}

export function presetFits(mb: number, totalMb: number): boolean {
  return installedMemoryMb(totalMb) > 0 && mb <= installedMemoryMb(totalMb);
}

export function memoryRisk(mb: number, totalMb: number): 'safe' | 'caution' | 'danger' | 'unknown' {
  const total = installedMemoryMb(totalMb);
  if (!total) return 'unknown';
  if (mb >= total * 0.9) return 'danger';
  return mb > total * 0.5 ? 'caution' : 'safe';
}

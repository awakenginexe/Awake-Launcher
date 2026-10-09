import type { PackVersion } from '../../bridge/catalog.ts';

export interface InstalledPack {
  provider: string; name: string; versionId: string; versionName: string;
  reminders: boolean; skippedVersion: string;
}

export function availablePackUpdate(pack: InstalledPack, versions: PackVersion[]): PackVersion | undefined {
  const latest = versions[0];
  if (!pack.reminders || !latest || latest.id === pack.skippedVersion || latest.id === pack.versionId) return;
  let current = versions.findIndex(version => version.id === pack.versionId);
  if (current < 0 && pack.provider === 'modrinth') current = versions.findIndex(version => version.versionNumber === pack.versionName || version.name === pack.versionName);
  if (current > 0) return latest;
  if (current < 0 && pack.provider === 'curseforge' && /^\d+$/.test(pack.versionId) && /^\d+$/.test(latest.id) && BigInt(latest.id) > BigInt(pack.versionId)) return latest;
}

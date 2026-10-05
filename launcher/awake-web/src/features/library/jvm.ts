import type { MessageKey } from '../../i18n/catalogs.ts';

export type JvmPreset = 'compatible' | 'balanced' | 'performance' | 'custom';
export const jvmPresets: { id: JvmPreset; label: MessageKey; description: MessageKey }[] = [
  { id: 'compatible', label: 'jvmCompatible', description: 'jvmCompatibleHint' },
  { id: 'balanced', label: 'jvmBalanced', description: 'jvmBalancedHint' },
  { id: 'performance', label: 'jvmPerformance', description: 'jvmPerformanceHint' },
  { id: 'custom', label: 'jvmCustom', description: 'jvmCustomHint' },
];
export function parseJvmPreset(value: unknown): JvmPreset {
  return jvmPresets.find(p => p.id === value)?.id ?? 'compatible';
}

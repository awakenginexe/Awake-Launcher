export function formatPlaytime(seconds: number | null, locale: string): string {
  if (seconds === null || !Number.isFinite(seconds) || seconds < 0) return '—';
  const total = Math.floor(seconds);
  const units: [string, number][] = [['hour', Math.floor(total / 3600)], ['minute', Math.floor(total / 60) % 60], ['second', total % 60]];
  return units.filter(([unit, value]) => value > 0 || (unit === 'second' && total === 0))
    .map(([unit, value]) => new Intl.NumberFormat(locale, { style: 'unit', unit, unitDisplay: 'narrow' }).format(value)).join(' ');
}

export function formatLastPlayed(timestamp: number, locale: string): string | null {
  if (!Number.isFinite(timestamp) || timestamp <= 0 || !Number.isFinite(new Date(timestamp).getTime())) return null;
  return new Intl.DateTimeFormat(locale, { dateStyle: 'medium', timeStyle: 'short' }).format(timestamp);
}

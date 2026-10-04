import { normalizeLocale } from '../../i18n/catalogs.ts';
import type { Locale } from '../../i18n/catalogs.ts';
import { BridgeError } from '../../bridge/client.ts';

export type SortMode = 'Name' | 'LastLaunch' | 'TotalTimePlayed';
export type Action =
  | 'create'
  | 'import'
  | 'edit'
  | 'folder'
  | 'launch'
  | 'rename'
  | 'changeGroup'
  | 'copy'
  | 'export'
  | 'delete'
  | 'kill'
  | 'accounts'
  | 'settings'
  | 'manage'
  | 'launchOptions'
  | 'application'
  | 'logs'
  | 'legacy'
  | 'addMicrosoft'
  | 'addOffline'
  | 'removeAccount'
  | 'setDefaultAccount'
  | 'refreshAccount'
  | 'createQuick'
  | 'installPack'
  | 'importArchive'
  | 'windowMinimize'
  | 'windowMaximize'
  | 'windowClose'
  | 'openRootFolder'
  | 'openInstancesFolder'
  | 'openModsFolder'
  | 'openLogsFolder'
  | 'openJavaFolder'
  | 'openSkinsFolder'
  | 'checkForUpdates'
  | 'clearMetadata'
  | 'reportBug'
  | 'about'
  | 'discord'
  | 'reddit'
  | 'matrix';

export type PreferenceKey =
  | 'compact'
  | 'reducedMotion'
  | 'sortMode'
  | 'pin'
  | 'language'
  | 'minMem'
  | 'maxMem'
  | 'gameWidth'
  | 'gameHeight'
  | 'maximizeGame'
  | 'closeOnLaunch';

export interface AccountItem {
  id: string;
  name: string;
  type: 'microsoft' | 'offline';
  active: boolean;
  valid: boolean;
}

export interface LauncherSettings {
  language: string;
  minMem: number;
  maxMem: number;
  javaPath: string;
  gameWidth: number;
  gameHeight: number;
  maximizeGame: boolean;
  closeOnLaunch: boolean;
}

export interface Instance {
  id: string; name: string; group: string; minecraftVersion: string; loader: string; loaderVersion: string;
  iconUrl: string; pinned: boolean; canLaunch: boolean; running: boolean; broken: boolean;
  lastLaunch: number; totalTimePlayed: number;
}
export interface Snapshot {
  instances: Instance[]; selectedId: string; locale: Locale; reducedMotion: boolean; compact: boolean;
  sortMode: SortMode; accountName: string; modalActive: boolean;
  accounts: AccountItem[]; launcherSettings: LauncherSettings;
}
export function emptySnapshot(): Snapshot {
  return {
    instances: [], selectedId: '', locale: 'en', reducedMotion: false, compact: false, sortMode: 'Name', accountName: '', modalActive: false,
    accounts: [],
    launcherSettings: {
      language: '', minMem: 1024, maxMem: 4096, javaPath: '', gameWidth: 854, gameHeight: 480, maximizeGame: false, closeOnLaunch: false
    }
  };
}
export function isLocalImage(value: string): boolean {
  if (!value || /(?:^|\/)(?:\.|%2e){2}(?:\/|$)/i.test(value) || /[\\\s]/.test(value)) return false;
  try {
    const url = new URL(value);
    const decoded = decodeURIComponent(url.pathname);
    return url.protocol === 'awake:' && url.hostname === 'ui' && !url.port && !url.username && !url.password && !url.search && !url.hash && !decoded.split('/').some(part => part === '..' || part === '.');
  } catch { return false; }
}
export function parseSnapshot(value: unknown): Snapshot {
  const fail = (): never => { throw new BridgeError('protocol', 'Invalid native library snapshot'); };
  if (!value || typeof value !== 'object') return fail();
  const data = value as Record<string, unknown>;
  if (!Array.isArray(data.instances) || typeof data.selectedId !== 'string' || typeof data.locale !== 'string' || typeof data.accountName !== 'string' || typeof data.reducedMotion !== 'boolean' || typeof data.compact !== 'boolean' || !['Name', 'LastLaunch', 'TotalTimePlayed'].includes(String(data.sortMode))) return fail();
  const ids = new Set<string>();
  const instances = data.instances.map(item => {
    if (!item || typeof item !== 'object') return fail();
    const fields = item as Record<string, unknown>;
    for (const key of ['id', 'name', 'group', 'minecraftVersion', 'loader', 'loaderVersion', 'iconUrl']) if (typeof fields[key] !== 'string') return fail();
    for (const key of ['pinned', 'canLaunch', 'running', 'broken']) if (typeof fields[key] !== 'boolean') return fail();
    for (const key of ['lastLaunch', 'totalTimePlayed']) if (typeof fields[key] !== 'number' || !Number.isFinite(fields[key]) || fields[key] < 0) return fail();
    const instance = fields as unknown as Instance;
    if (!instance.id || ids.has(instance.id)) return fail();
    ids.add(instance.id);
    return { ...instance, iconUrl: isLocalImage(instance.iconUrl) ? instance.iconUrl : '' };
  });

  const accounts: AccountItem[] = Array.isArray(data.accounts)
    ? data.accounts
        .filter((item): item is Record<string, unknown> => Boolean(item && typeof item === 'object'))
        .map(a => ({
          id: String(a.id || ''),
          name: String(a.name || ''),
          type: a.type === 'offline' ? ('offline' as const) : ('microsoft' as const),
          active: Boolean(a.active),
          valid: Boolean(a.valid),
        }))
    : [];

  const rawSettings = (data.launcherSettings || {}) as Record<string, unknown>;
  const launcherSettings: LauncherSettings = {
    language: String(rawSettings.language || ''),
    minMem: Number(rawSettings.minMem) || 1024,
    maxMem: Number(rawSettings.maxMem) || 4096,
    javaPath: String(rawSettings.javaPath || ''),
    gameWidth: Number(rawSettings.gameWidth) || 854,
    gameHeight: Number(rawSettings.gameHeight) || 480,
    maximizeGame: Boolean(rawSettings.maximizeGame),
    closeOnLaunch: Boolean(rawSettings.closeOnLaunch),
  };

  return {
    instances,
    selectedId: ids.has(data.selectedId) ? data.selectedId : '',
    locale: normalizeLocale(data.locale),
    reducedMotion: data.reducedMotion,
    compact: data.compact,
    sortMode: data.sortMode as SortMode,
    accountName: data.accountName,
    modalActive: Boolean(data.modalActive),
    accounts,
    launcherSettings,
  };
}
export function acceptSnapshot(current: Snapshot, response: Snapshot, revision: number, requestedRevision: number): Snapshot {
  return revision === requestedRevision ? response : current;
}
export function presentInstances(items: Instance[], query: string, group: string, pinnedOnly: boolean, sort: SortMode, locale: Locale): Instance[] {
  const needle = query.trim().toLocaleLowerCase(locale);
  const collator = new Intl.Collator(locale, { numeric: true, sensitivity: 'base' });
  return items.filter(item => (!group || item.group === group) && (!pinnedOnly || item.pinned) && (!needle || [item.name, item.group, item.minecraftVersion, item.loader, item.loaderVersion].some(field => field.toLocaleLowerCase(locale).includes(needle)))).sort((a, b) => {
    if (a.pinned !== b.pinned) return a.pinned ? -1 : 1;
    const numeric = sort === 'LastLaunch' ? b.lastLaunch - a.lastLaunch : sort === 'TotalTimePlayed' ? b.totalTimePlayed - a.totalTimePlayed : 0;
    return numeric || collator.compare(a.name, b.name) || collator.compare(a.id, b.id);
  });
}
export class ArtworkSequence {
  private revision = 0;
  private selectedId = '';
  begin(id: string): number { this.selectedId = id; return ++this.revision; }
  current(id: string, revision: number): boolean { return this.selectedId === id && this.revision === revision; }
}

import { normalizeLocale } from '../../i18n/catalogs.ts';
import type { Locale } from '../../i18n/catalogs.ts';
import { BridgeError } from '../../bridge/client.ts';
import { parseJvmPreset } from './jvm.ts';
import type { JvmPreset } from './jvm.ts';

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
  headUrl: string;
  type: 'microsoft' | 'offline';
  active: boolean;
  valid: boolean;
}

export interface LauncherSettings {
  language: string;
  minMem: number;
  maxMem: number;
  javaPath: string;
  jvmArgs: string;
  version: string;
  jvmPreset: JvmPreset;
  gameWidth: number;
  gameHeight: number;
  maximizeGame: boolean;
  closeOnLaunch: boolean;
}

export interface Instance {
  id: string; name: string; group: string; minecraftVersion: string; loader: string; loaderVersion: string;
  iconUrl: string; pinned: boolean; canLaunch: boolean; running: boolean; broken: boolean;
  lastLaunch: number; lastTimePlayed: number | null; totalTimePlayed: number;
}
export interface Snapshot {
  instances: Instance[]; selectedId: string; locale: Locale; reducedMotion: boolean; compact: boolean;
  sortMode: SortMode; accountName: string; modalActive: boolean; totalMemoryMb: number;
  accounts: AccountItem[]; launcherSettings: LauncherSettings;
  updates: UpdateState;
  deletion: { active: boolean; id: string; name: string };
}
export interface UpdateState {
  status: 'unavailable' | 'idle' | 'checking' | 'available' | 'upToDate' | 'error' | 'downloading' | 'installing';
  currentVersion: string; latestVersion: string; notes: string; error: string;
  automatic: boolean; portable: boolean; presentation: number;
  hasSetup: boolean; hasPortable: boolean; hasRelease: boolean;
  canInstall: boolean; progress: number;
}
function parseUpdates(value: unknown): UpdateState {
  const data = value && typeof value === 'object' ? value as Record<string, unknown> : {};
  const statuses = ['idle', 'checking', 'available', 'upToDate', 'error', 'downloading', 'installing'];
  return {
    status: statuses.includes(String(data.status)) ? data.status as UpdateState['status'] : 'unavailable',
    currentVersion: String(data.currentVersion || ''), latestVersion: String(data.latestVersion || ''),
    notes: String(data.notes || ''), error: String(data.error || ''), automatic: data.automatic === true,
    portable: data.portable === true, presentation: typeof data.presentation === 'number' && Number.isSafeInteger(data.presentation) && data.presentation > 0 ? data.presentation : 0,
    hasSetup: Boolean(data.setupUrl), hasPortable: Boolean(data.portableUrl), hasRelease: Boolean(data.releaseUrl),
    canInstall: data.canInstall === true, progress: typeof data.progress === 'number' && Number.isFinite(data.progress) ? Math.max(0, Math.min(100, data.progress)) : 0,
  };
}
export function emptySnapshot(): Snapshot {
  return {
    instances: [], selectedId: '', locale: 'en', reducedMotion: false, compact: false, sortMode: 'Name', accountName: '', modalActive: false,
    accounts: [], totalMemoryMb: 0,
    updates: parseUpdates(null),
    deletion: { active: false, id: '', name: '' },
    launcherSettings: {
      language: '', minMem: 1024, maxMem: 4096, javaPath: '', jvmArgs: '', version: '', jvmPreset: 'compatible', gameWidth: 854, gameHeight: 480, maximizeGame: false, closeOnLaunch: false
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
    if (fields.lastTimePlayed != null && (typeof fields.lastTimePlayed !== 'number' || !Number.isFinite(fields.lastTimePlayed) || fields.lastTimePlayed < 0)) return fail();
    const instance = fields as unknown as Instance;
    if (!instance.id || ids.has(instance.id)) return fail();
    ids.add(instance.id);
    return { ...instance, lastTimePlayed: instance.lastTimePlayed ?? null, iconUrl: isLocalImage(instance.iconUrl) ? instance.iconUrl : '' };
  });

  const accounts: AccountItem[] = Array.isArray(data.accounts)
    ? data.accounts
        .filter((item): item is Record<string, unknown> => Boolean(item && typeof item === 'object'))
        .map(a => ({
          id: String(a.id || ''),
          name: String(a.name || ''),
          headUrl: typeof a.headUrl === 'string' && isLocalImage(a.headUrl) ? a.headUrl : '',
          type: a.type === 'offline' ? ('offline' as const) : ('microsoft' as const),
          active: Boolean(a.active),
          valid: Boolean(a.valid),
        }))
    : [];

  const rawSettings = (data.launcherSettings || {}) as Record<string, unknown>;
  const rawDeletion = (data.deletion && typeof data.deletion === 'object' ? data.deletion : {}) as Record<string, unknown>;
  const launcherSettings: LauncherSettings = {
    language: String(rawSettings.language || ''),
    minMem: Number(rawSettings.minMem) || 1024,
    maxMem: Number(rawSettings.maxMem) || 4096,
    javaPath: String(rawSettings.javaPath || ''),
    jvmArgs: String(rawSettings.jvmArgs || ''),
    version: String(rawSettings.version || ''),
    jvmPreset: parseJvmPreset(rawSettings.jvmPreset),
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
    totalMemoryMb: typeof data.totalMemoryMb === 'number' && Number.isFinite(data.totalMemoryMb) && data.totalMemoryMb > 0 ? data.totalMemoryMb : 0,
    modalActive: Boolean(data.modalActive),
    accounts,
    launcherSettings,
    updates: parseUpdates(data.updates),
    deletion: { active: rawDeletion.active === true, id: String(rawDeletion.id || ''), name: String(rawDeletion.name || '') },
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

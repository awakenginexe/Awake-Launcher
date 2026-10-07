import type { RequestOptions } from './catalog.ts';

export type ModProvider = 'modrinth' | 'curseforge';
export interface ModEntry { id: string; provider: ModProvider; name: string; author: string; description: string; icon: string; website: string }
export interface ModVersion { id: string; name: string; minecraft: string; loader: string; type: string; date: string; filename: string }
export interface ModSelection { provider: ModProvider; projectId: string; versionId: string }
export interface QueuedMod extends ModSelection { name: string; version: string; icon: string }
export interface ModSearch { mods: ModEntry[]; hasMore: boolean; minecraft: string; loader: string; sorts: { id: string; name: string }[] }
export interface ModVersions { project: ModEntry; versions: ModVersion[]; minecraft: string; loader: string; notice?: string }
export interface ReviewItem extends ModSelection { name: string; filename: string; version: string; type: string; requiredBy: string[]; dependency: boolean; maybeInstalled: boolean }
export interface ModReview { reviewId: string; items: ReviewItem[]; warnings: string[] }
export interface ModProgress { current: number; total: number; status: string }
export interface ModInstalled extends ModSelection { name: string }
export interface ModResult { ok: boolean; installed: ModInstalled[]; failed: (ModInstalled & { error: string })[]; warnings: string[]; canceled?: boolean; error?: string }
export interface ModService {
  search(instance: string, provider: ModProvider, query: string, sort: string, offset: number, signal?: AbortSignal): Promise<ModSearch>;
  versions(instance: string, provider: ModProvider, project: string, signal?: AbortSignal): Promise<ModVersions>;
  prepare(instance: string, selections: ModSelection[], signal?: AbortSignal): Promise<ModReview>;
  install(instance: string, review: string, progress: (value: ModProgress) => void, signal?: AbortSignal): Promise<ModResult>;
}
export function createModService(request: <T>(method: string, args: unknown[], options?: RequestOptions) => Promise<T>): ModService {
  return {
    search: (instance, provider, query, sort, offset, signal) => request('modSearch', [instance, provider, query, sort, offset], { signal }),
    versions: (instance, provider, project, signal) => request('modVersions', [instance, provider, project], { signal }),
    prepare: (instance, selections, signal) => request('modPrepare', [instance, selections], { signal }),
    install: (instance, review, progress, signal) => request('modInstall', [instance, review], { signal, timeoutMs: 600_000, acceptFailure: true, progress: value => {
      if (value && typeof value === 'object' && 'current' in value && 'total' in value && 'status' in value && typeof value.current === 'number' && typeof value.total === 'number' && typeof value.status === 'string') progress(value as ModProgress);
    } }),
  };
}

import { BridgeError, callNative, subscribe } from './client.ts';
import type { NativeObject } from './client.ts';

export interface PackEntry {
  id: string; name: string; author: string; description: string; icon: string;
  downloads: string; minecraft: string; loader: string;
}
export interface PackVersion { id: string; name: string; minecraft: string; loader: string; versionNumber?: string }
export interface MinecraftVersion { version: string; released: string; type: string; recommended: boolean }
export interface LocalInstance {
  id: string; path: string; name: string; source: string; minecraft: string; loader: string; loaderVersion: string; error: string;
}
export interface CatalogResult {
  packs: PackEntry[]; versions: PackVersion[]; minecraftVersions: MinecraftVersion[]; hasMore: boolean;
  archiveUrl?: string; fileName?: string;
  localInstances?: LocalInstance[]; canceled?: boolean;
}
export interface RequestOptions {
  timeoutMs?: number; signal?: AbortSignal; progress?: (value: unknown) => void; acceptFailure?: boolean;
}

export class CatalogClient {
  private native: NativeObject;
  private revision = 0;
  private prefix = crypto.randomUUID();
  private pending = new Map<string, { resolve: (result: unknown) => void; reject: (error: Error) => void; cleanup: () => void; options: RequestOptions }>();
  private disconnect: (() => void)[] = [];
  constructor(native: NativeObject) {
    this.native = native;
    this.disconnect.push(subscribe(native, 'catalogFinished', (id, result) => {
      if (typeof id !== 'string') return;
      const request = this.pending.get(id);
      if (!request) return;
      this.pending.delete(id);
      request.cleanup();
      if (!result || typeof result !== 'object' || !('ok' in result)) {
        request.reject(new BridgeError('protocol', 'Invalid catalog response'));
      } else if (!result.ok && !request.options.acceptFailure) {
        request.reject(new BridgeError('operation', 'error' in result ? String(result.error) : 'Provider request failed'));
      } else {
        request.resolve(result);
      }
    }));
    if (native.modProgress) this.disconnect.push(subscribe(native, 'modProgress', (id, value) => {
      if (typeof id === 'string') this.pending.get(id)?.options.progress?.(value);
    }));
  }
  request<T = CatalogResult>(method: string, args: unknown[], options: RequestOptions = {}): Promise<T> {
    const id = `${this.prefix}-${++this.revision}`;
    return new Promise((resolve, reject) => {
      if (options.signal?.aborted) { reject(new BridgeError('operation', 'Provider request canceled')); return; }
      const cancel = () => { if (method.startsWith('mod')) void callNative(this.native, 'modCancel', [id]).catch(() => {}); };
      const timer = setTimeout(() => {
        this.pending.delete(id);
        cleanup(); cancel();
        reject(new BridgeError('timeout', 'Provider request timed out. Please retry.'));
      }, options.timeoutMs ?? 90_000);
      const cleanup = () => { clearTimeout(timer); options.signal?.removeEventListener('abort', cancel); };
      options.signal?.addEventListener('abort', cancel, { once: true });
      this.pending.set(id, { resolve: result => resolve(result as T), reject, cleanup, options });
      void callNative(this.native, method, [id, ...args]).catch(error => {
        const request = this.pending.get(id);
        if (!request) return;
        this.pending.delete(id);
        request.cleanup();
        reject(error);
      });
    });
  }
  dispose() {
    this.disconnect.forEach(disconnect => disconnect());
    for (const request of this.pending.values()) {
      request.cleanup();
      request.reject(new BridgeError('disconnected', 'Catalog disconnected'));
    }
    this.pending.clear();
  }
}

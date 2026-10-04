import { BridgeError, callNative, subscribe } from './client.ts';
import type { NativeObject } from './client.ts';

export interface PackEntry {
  id: string; name: string; author: string; description: string; icon: string;
  downloads: string; minecraft: string; loader: string;
}
export interface PackVersion { id: string; name: string; minecraft: string; loader: string }
export interface MinecraftVersion { version: string; released: string; type: string; recommended: boolean }
export interface CatalogResult {
  packs: PackEntry[]; versions: PackVersion[]; minecraftVersions: MinecraftVersion[]; hasMore: boolean;
  archiveUrl?: string; fileName?: string;
}

export class CatalogClient {
  private native: NativeObject;
  private revision = 0;
  private prefix = crypto.randomUUID();
  private pending = new Map<string, { resolve: (result: CatalogResult) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout> }>();
  private disconnect: () => void;
  constructor(native: NativeObject) {
    this.native = native;
    this.disconnect = subscribe(native, 'catalogFinished', (id, result) => {
      if (typeof id !== 'string') return;
      const request = this.pending.get(id);
      if (!request) return;
      this.pending.delete(id);
      clearTimeout(request.timer);
      if (!result || typeof result !== 'object' || !('ok' in result)) {
        request.reject(new BridgeError('protocol', 'Invalid catalog response'));
      } else if (!result.ok) {
        request.reject(new BridgeError('operation', 'error' in result ? String(result.error) : 'Provider request failed'));
      } else {
        request.resolve(result as unknown as CatalogResult);
      }
    });
  }
  request(method: string, args: unknown[]): Promise<CatalogResult> {
    const id = `${this.prefix}-${++this.revision}`;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new BridgeError('timeout', 'Provider request timed out. Please retry.'));
      }, 90_000);
      this.pending.set(id, { resolve, reject, timer });
      void callNative(this.native, method, [id, ...args]).catch(error => {
        const request = this.pending.get(id);
        if (!request) return;
        this.pending.delete(id);
        clearTimeout(request.timer);
        reject(error);
      });
    });
  }
  dispose() {
    this.disconnect();
    for (const request of this.pending.values()) {
      clearTimeout(request.timer);
      request.reject(new BridgeError('disconnected', 'Catalog disconnected'));
    }
    this.pending.clear();
  }
}

export type ErrorCode = 'disconnected' | 'timeout' | 'protocol' | 'operation' | 'transport';
export class BridgeError extends Error {
  readonly code: ErrorCode;
  readonly detail: string;
  constructor(code: ErrorCode, detail: string) {
    super(detail);
    this.name = 'BridgeError';
    this.code = code;
    this.detail = detail;
  }
}

export type NativeObject = Record<string, unknown>;
export interface NativeSignal {
  connect: (listener: (...args: unknown[]) => void) => void;
  disconnect: (listener: (...args: unknown[]) => void) => void;
}
interface ChannelEnvironment {
  qt?: { webChannelTransport?: unknown };
  QWebChannel?: new (transport: unknown, callback: (channel: { objects: Record<string, NativeObject> }) => void) => unknown;
}
declare global {
  interface Window extends ChannelEnvironment {}
}

export function callNative(native: NativeObject, method: string, args: unknown[] = [], timeoutMs = 30_000): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const fn = native[method];
    if (typeof fn !== 'function') {
      reject(new BridgeError('protocol', `Missing native method: ${method}`));
      return;
    }
    let finished = false;
    const finish = (error?: BridgeError, value?: unknown) => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      if (error) reject(error); else resolve(value);
    };
    const timer = setTimeout(() => finish(new BridgeError('timeout', `${method}: no reply within ${timeoutMs} ms`)), timeoutMs);
    try {
      fn.apply(native, [...args, (value: unknown) => {
        if (method === 'snapshot' || method === 'frontendReady') { finish(undefined, value); return; }
        if (!value || typeof value !== 'object' || !('ok' in value) || typeof value.ok !== 'boolean') {
          finish(new BridgeError('protocol', `${method}: invalid operation result`));
        } else if (!value.ok) {
          finish(new BridgeError('operation', 'error' in value && typeof value.error === 'string' ? value.error : `${method}: operation rejected`));
        } else finish(undefined, value);
      }]);
    } catch (error) {
      finish(new BridgeError('transport', `${method}: ${error instanceof Error ? error.message : String(error)}`));
    }
  });
}

export function connectChannel(environment: ChannelEnvironment, timeoutMs = 8_000): Promise<NativeObject> {
  return new Promise((resolve, reject) => {
    if (!environment.qt?.webChannelTransport || !environment.QWebChannel) {
      reject(new BridgeError('disconnected', 'Qt WebChannel transport or local qwebchannel.js is unavailable'));
      return;
    }
    let finished = false;
    const timer = setTimeout(() => {
      finished = true;
      reject(new BridgeError('timeout', 'QWebChannel initialization timed out'));
    }, timeoutMs);
    try {
      new environment.QWebChannel(environment.qt.webChannelTransport, (channel) => {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        if (!channel.objects.awake) reject(new BridgeError('protocol', 'QWebChannel object awake is unavailable'));
        else resolve(channel.objects.awake);
      });
    } catch (error) {
      clearTimeout(timer);
      finished = true;
      reject(new BridgeError('transport', String(error)));
    }
  });
}

export function subscribe(native: NativeObject, name: string, listener: (...args: unknown[]) => void): () => void {
  const signal = native[name] as NativeSignal | undefined;
  if (!signal || typeof signal.connect !== 'function' || typeof signal.disconnect !== 'function') throw new BridgeError('protocol', `Missing native signal: ${name}`);
  signal.connect(listener);
  return () => signal.disconnect(listener);
}

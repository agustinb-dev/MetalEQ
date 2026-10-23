/*
    Real bridge: talks to the JUCE backend embedded in the plugin.

    We mirror the promise protocol of JUCE's own webview-interop package
    (juce_gui_extra/native/typescript/webview-interop/src/index.ts) instead of
    bundling it, so the packaged zip has no dependency on JUCE's npm tooling:

      JS -> C++ : window.__JUCE__.backend.emitEvent('__juce__invoke',
                  { name, params, resultId })
      C++ -> JS : window.__JUCE__.backend.addEventListener('__juce__complete',
                  { promiseId, result })
      C++ -> JS : window.__JUCE__.backend.addEventListener('metaleq.tick', ...)

    Selection rule: if window.__JUCE__.backend exists we are inside the plugin
    (resource root https://juce.backend/ on Windows); otherwise the app falls
    back to the mock bridge for browser development.
*/

import type {
  MetalEqBridge,
  OpResult,
  SetParamResult,
  SuggestionCurveResult,
  Snapshot,
  TickMessage,
} from './types';
import { PROTOCOL_VERSION } from './types';

interface JuceBackend {
  emitEvent(name: string, payload: unknown): void;
  addEventListener(name: string, fn: (data: unknown) => void): void;
}

declare global {
  interface Window {
    __JUCE__?: {
      backend?: JuceBackend;
      initialisationData?: { __juce__functions?: string[] };
    };
  }
}

export function isJuceBackendAvailable(): boolean {
  return typeof window !== 'undefined' && !!window.__JUCE__?.backend;
}

class PromiseHandler {
  private lastId = 0;
  private readonly pending = new Map<number, { resolve: (v: unknown) => void }>();

  constructor(backend: JuceBackend) {
    backend.addEventListener('__juce__complete', (payload) => {
      const { promiseId, result } = payload as { promiseId: number; result: unknown };
      const entry = this.pending.get(promiseId);
      if (entry) {
        this.pending.delete(promiseId);
        entry.resolve(result);
      }
    });
  }

  invoke(backend: JuceBackend, name: string, args: unknown[]): Promise<unknown> {
    const promiseId = this.lastId++;
    const result = new Promise<unknown>((resolve) => {
      this.pending.set(promiseId, { resolve });
    });
    backend.emitEvent('__juce__invoke', { name, params: args, resultId: promiseId });
    return result;
  }
}

export class JuceBridge implements MetalEqBridge {
  private backend: JuceBackend;
  private promises: PromiseHandler;
  private knownFunctions: Set<string>;

  constructor() {
    const backend = window.__JUCE__?.backend;
    if (!backend) throw new Error('JUCE backend not available');
    this.backend = backend;
    this.promises = new PromiseHandler(backend);
    this.knownFunctions = new Set(window.__JUCE__?.initialisationData?.__juce__functions ?? []);
  }

  private call<T>(name: string, args: unknown[]): Promise<T> {
    if (this.knownFunctions.size > 0 && !this.knownFunctions.has(name)) {
      console.warn(`[MetalEQ] native function '${name}' unknown to the backend`);
    }
    return this.promises.invoke(this.backend, name, args) as Promise<T>;
  }

  async init(handlers: { onTick: (t: TickMessage) => void }): Promise<Snapshot> {
    // Listen before uiReady: the native side suppresses ticks until uiReady
    // resolves, so no event can be missed in between.
    this.backend.addEventListener('metaleq.tick', (payload) => {
      handlers.onTick(payload as TickMessage);
    });

    const snapshot = await this.call<Snapshot>('uiReady', []);
    if (snapshot?.protocolVersion !== PROTOCOL_VERSION) {
      console.warn(
        `[MetalEQ] protocol mismatch: native=${snapshot?.protocolVersion} js=${PROTOCOL_VERSION}`,
      );
    }
    return snapshot;
  }

  setParameter(id: string, value: number): Promise<SetParamResult> {
    return this.call<SetParamResult>('setParameter', [id, value]);
  }

  applyPreset(index: number): Promise<OpResult> {
    return this.call<OpResult>('applyPreset', [index]);
  }

  applyPresetById(presetId: string): Promise<OpResult> {
    return this.call<OpResult>('applyPresetById', [presetId]);
  }

  loadUserPreset(name: string): Promise<OpResult> {
    return this.call<OpResult>('loadUserPreset', [name]);
  }

  saveUserPreset(name: string, overwrite: boolean): Promise<OpResult> {
    return this.call<OpResult>('saveUserPreset', [name, overwrite]);
  }

  renameUserPreset(oldName: string, newName: string): Promise<OpResult> {
    return this.call<OpResult>('renameUserPreset', [oldName, newName]);
  }

  deleteUserPreset(name: string): Promise<OpResult> {
    return this.call<OpResult>('deleteUserPreset', [name]);
  }

  revertToPreset(): Promise<OpResult> {
    return this.call<OpResult>('revertToPreset', []);
  }

  resetToNeutral(): Promise<OpResult> {
    return this.call<OpResult>('resetToNeutral', []);
  }

  setAbSlot(slot: number): Promise<OpResult> {
    return this.call<OpResult>('setAbSlot', [slot]);
  }

  copyAbToOther(): Promise<OpResult> {
    return this.call<OpResult>('copyAbToOther', []);
  }

  swapAbSlots(): Promise<OpResult> {
    return this.call<OpResult>('swapAbSlots', []);
  }

  startAnalyzerCapture(profile: number): Promise<OpResult> {
    return this.call<OpResult>('startAnalyzerCapture', [profile]);
  }

  cancelAnalyzerCapture(): Promise<OpResult> {
    return this.call<OpResult>('cancelAnalyzerCapture', []);
  }

  applyAnalyzerSuggestion(amount: number): Promise<OpResult> {
    return this.call<OpResult>('applyAnalyzerSuggestion', [amount]);
  }

  getSuggestionCurve(amount: number): Promise<SuggestionCurveResult> {
    return this.call<SuggestionCurveResult>('getSuggestionCurve', [amount]);
  }

  setEditorExpanded(expanded: boolean): Promise<OpResult> {
    return this.call<OpResult>('setEditorExpanded', [expanded]);
  }
}

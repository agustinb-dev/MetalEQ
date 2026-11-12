/*
    Single source of truth for the front-end.

    Mirrors the native side's semantics:
    * `params` holds authoritative PLAIN values; every update from a tick or
      from a setParameter() result overwrites local state.
    * While a control is being dragged, `dragging` marks the id so incoming
      ticks don't fight the gesture; the authoritative value lands in `params`
      anyway and the control picks it up when the drag ends.
    * JS is never authoritative: we only send requests.
*/

import { useSyncExternalStore } from 'react';
import type {
  AnalyzerInfo,
  AnalyzerTargetInfo,
  CurveInfo,
  DynamicReductionInfo,
  FactoryPresetInfo,
  MetersInfo,
  PresetInfo,
  ResonanceInfo,
  Snapshot,
  TickMessage,
} from './types';
import { defaultParams, getSpec, snapPlain } from './params';

function normaliseAnalyzer(info?: Partial<AnalyzerInfo> | null): AnalyzerInfo {
  return {
    status: info?.status ?? 0,
    progress: info?.progress ?? 0,
    suggestion: info?.suggestion ?? [0, 0, 0, 0, 0],
    targetIndex: info?.targetIndex ?? 0,
    targetId: info?.targetId,
    targetLabel: info?.targetLabel,
    resultTarget: info?.resultTarget ?? -1,
    hasReport: info?.hasReport ?? false,
    input: info?.input,
    target: info?.target,
    predicted: info?.predicted,
    featureFreqs: info?.featureFreqs,
    correctionFreqs: info?.correctionFreqs,
    inputConfidence: info?.inputConfidence,
    correctionConfidence: info?.correctionConfidence,
    warnings: info?.warnings,
    sub60RelDb: info?.sub60RelDb,
    hfFloorRelDb: info?.hfFloorRelDb,
    tiltDbPerOct: info?.tiltDbPerOct,
    alreadyClose: info?.alreadyClose,
    changed: info?.changed,
  };
}

function normaliseResonance(info?: Partial<ResonanceInfo> | null): ResonanceInfo {
  return {
    surgical: {
      status: info?.surgical?.status ?? 0,
      progress: info?.surgical?.progress ?? 0,
      enoughVariation: info?.surgical?.enoughVariation ?? false,
      candidates: info?.surgical?.candidates ?? [],
      changed: info?.surgical?.changed,
    },
    dynamic: {
      status: info?.dynamic?.status ?? 0,
      progress: info?.dynamic?.progress ?? 0,
      enoughVariation: info?.dynamic?.enoughVariation ?? false,
      candidates: info?.dynamic?.candidates ?? [],
      changed: info?.dynamic?.changed,
    },
  };
}

export interface UiState {
  ready: boolean;
  protocolVersion: number;
  params: Record<string, number>;
  preset: PresetInfo;
  factoryPresets: FactoryPresetInfo[];
  analyzerTargets: AnalyzerTargetInfo[];
  userPresets: string[];
  meters: MetersInfo;
  dynamicReduction: DynamicReductionInfo;
  analyzer: AnalyzerInfo;
  resonance: ResonanceInfo;
  curve: CurveInfo | null;
  /** Curve of the suggested correction (240 dB points), fetched on demand. */
  suggestionCurve: number[] | null;
  /** Curve of the selected target's nine band objectives (240 dB points). */
  targetCurve: number[] | null;
  dragging: Set<string>;
  /** Fatal bridge error, surfaced as a banner (e.g. native call rejected). */
  lastError: string | null;
}

const initialPreset: PresetInfo = {
  index: 0,
  presetId: '',
  name: 'Custom',
  kind: 'custom',
  dirty: false,
  abSlot: 0,
};

const initialState: UiState = {
  ready: false,
  protocolVersion: 0,
  params: defaultParams(),
  preset: initialPreset,
  factoryPresets: [],
  analyzerTargets: [],
  userPresets: [],
  meters: { in: 0, out: 0 },
  dynamicReduction: [0, 0, 0, 0],
  analyzer: normaliseAnalyzer(),
  resonance: normaliseResonance(),
  curve: null,
  suggestionCurve: null,
  targetCurve: null,
  dragging: new Set(),
  lastError: null,
};

type Listener = () => void;

class Store {
  private state: UiState = initialState;
  private listeners = new Set<Listener>();

  subscribe = (listener: Listener): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getState = (): UiState => this.state;

  private set(patch: Partial<UiState>): void {
    this.state = { ...this.state, ...patch };
    for (const l of this.listeners) l();
  }

  applySnapshot(s: Snapshot): void {
    this.set({
      ready: true,
      protocolVersion: s.protocolVersion,
      params: { ...this.state.params, ...s.params },
      preset: s.preset,
      factoryPresets: s.factoryPresets ?? [],
      analyzerTargets: s.analyzerTargets ?? [],
      userPresets: s.userPresets ?? [],
      dynamicReduction: s.dynamicReduction ?? this.state.dynamicReduction,
      analyzer: normaliseAnalyzer({ ...this.state.analyzer, ...(s.analyzer ?? {}) }),
      resonance: s.resonance ? normaliseResonance(s.resonance) : this.state.resonance,
      curve: s.curve ?? this.state.curve,
      suggestionCurve: null,
      targetCurve: null,
    });
  }

  applyTick(t: TickMessage): void {
    const patch: Partial<UiState> = {};

    if (t.params && Object.keys(t.params).length > 0) {
      patch.params = { ...this.state.params, ...t.params };
    }
    if (t.preset) patch.preset = t.preset;
    if (t.userPresets) patch.userPresets = t.userPresets;
    if (t.meters) patch.meters = t.meters;
    if (t.dynamicReduction) patch.dynamicReduction = t.dynamicReduction;
    if (t.analyzer) {
      patch.analyzer = normaliseAnalyzer({ ...this.state.analyzer, ...t.analyzer });
      // Analyzer left "ready": the shown suggestion is no longer valid.
      if (t.analyzer.status !== 4) patch.suggestionCurve = null;
    }
    if (t.curve) patch.curve = t.curve;
    if (t.resonance) patch.resonance = normaliseResonance(t.resonance);

    if (Object.keys(patch).length > 0) this.set(patch);
  }

  /** Optimistic local echo while the native call is in flight. */
  setParamLocal(id: string, value: number): void {
    const spec = getSpec(id);
    const v = spec ? snapPlain(spec, value) : value;
    this.set({ params: { ...this.state.params, [id]: v } });
  }

  /** Authoritative value from a setParameter() result. */
  setParamAuthoritative(id: string, value: number): void {
    this.set({ params: { ...this.state.params, [id]: value } });
  }

  setSuggestionCurve(curve: number[] | null): void {
    this.set({ suggestionCurve: curve });
  }

  setTargetCurve(curve: number[] | null): void {
    this.set({ targetCurve: curve });
  }

  setDragging(id: string | null): void {
    const dragging = new Set(this.state.dragging);
    if (id === null) dragging.clear();
    else dragging.add(id);
    this.set({ dragging });
  }

  setError(message: string | null): void {
    this.set({ lastError: message });
  }
}

export const store = new Store();

/** Subscribe a component to the whole state (used sparingly: graphs, root). */
export function useUiState(): UiState {
  return useSyncExternalStore(store.subscribe, store.getState, store.getState);
}

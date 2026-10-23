/*
    Wire types for the MetalEQ bridge protocol (version 2).

    These mirror Source/ui/MetalEqUiBridge.cpp exactly: the native side is the
    single source of truth, JS only renders what it receives and sends requests.
*/

export const PROTOCOL_VERSION = 2;

export type ParamKind = 'float' | 'choice' | 'bool';

export interface ParamSpec {
  id: string;
  label: string;
  kind: ParamKind;
  min: number;
  max: number;
  /** juce::NormalisableRange skew (1 = linear). */
  skew: number;
  /** Plain-value interval used for snapping (0 = continuous). */
  step: number;
  /** Default plain value; for choices this is the default index. */
  def: number;
  choices?: string[];
}

/** Factory-preset entry as sent in the uiReady snapshot. */
export interface FactoryPresetInfo {
  /** Canonical selector index: 0 is Custom; factory entries are 1..25. */
  index: number;
  /** Stable identity saved with the project. Never derive meaning from index. */
  id: string;
  name: string;
  /** Browser grouping: "Metalcore" | "Thrash" | "Utility". */
  genre: string;
  /** Browser sub-group, e.g. "2000s", "Modern", "Pre-Amp". */
  subgenre: string;
  placement: string; // "PRE-AMP" | "POST-AMP" | "EITHER"
  description: string;
}

export type PresetKind = 'user' | 'factory' | 'custom';

export interface PresetInfo {
  index: number;
  presetId: string;
  name: string;
  kind: PresetKind;
  dirty: boolean;
  /** 0 = A, 1 = B. Output trim stays global across both. */
  abSlot: number;
}

/** Analyzer target with stable identity (table order may change). */
export interface AnalyzerTargetInfo {
  index: number;
  presetId: string;
  label: string;
}

/** Mirrors MetalEQAudioProcessor::AnalyzerStatus. */
export const AnalyzerStatus = {
  idle: 0,
  capturing: 1,
  captureComplete: 2,
  analyzing: 3,
  ready: 4,
  failed: 5,
} as const;

export interface AnalyzerInfo {
  status: number;
  progress: number;
  suggestion: number[];
  changed?: boolean;
}

export interface CurveInfo {
  points: number[]; // 240 dB values, evenly spaced in log-frequency
  minDb: number; // fixed -15 dB graph floor (values below it are clipped)
  maxDb: number; // fixed +15 dB graph ceiling (values above it are clipped)
  sampleRate?: number;
}

export interface MetersInfo {
  in: number;
  out: number;
}

/** One "metaleq.tick" event payload (every field except seq is optional/diffed). */
export interface TickMessage {
  seq: number;
  params?: Record<string, number>;
  preset?: PresetInfo;
  userPresets?: string[];
  meters?: MetersInfo;
  analyzer?: AnalyzerInfo;
  curve?: CurveInfo;
}

/** Result of the uiReady native call: a full snapshot. */
export interface Snapshot {
  protocolVersion: number;
  params: Record<string, number>;
  preset: PresetInfo;
  factoryPresets: FactoryPresetInfo[];
  analyzerTargets: AnalyzerTargetInfo[];
  userPresets: string[];
  analyzer?: AnalyzerInfo;
  curve?: CurveInfo;
}

export interface OpResult {
  ok: boolean;
  error?: string;
}

export interface SetParamResult extends OpResult {
  value?: number;
}

export interface SuggestionCurveResult extends OpResult {
  curve?: number[];
}

/** Every native function exposed by the bridge. */
export interface MetalEqBridge {
  /** Registers the tick listener, calls uiReady and resolves with the snapshot. */
  init(handlers: { onTick: (t: TickMessage) => void }): Promise<Snapshot>;
  setParameter(id: string, value: number): Promise<SetParamResult>;
  /** Canonical index: 0 = Custom, 1..N = factory preset 0..N-1. */
  applyPreset(index: number): Promise<OpResult>;
  /** Project-safe recall that does not depend on table order. */
  applyPresetById(presetId: string): Promise<OpResult>;
  loadUserPreset(name: string): Promise<OpResult>;
  saveUserPreset(name: string, overwrite: boolean): Promise<OpResult>;
  renameUserPreset(oldName: string, newName: string): Promise<OpResult>;
  deleteUserPreset(name: string): Promise<OpResult>;
  revertToPreset(): Promise<OpResult>;
  resetToNeutral(): Promise<OpResult>;
  /** Real A/B slots. */
  setAbSlot(slot: number): Promise<OpResult>;
  copyAbToOther(): Promise<OpResult>;
  swapAbSlots(): Promise<OpResult>;
  startAnalyzerCapture(profile: number): Promise<OpResult>;
  cancelAnalyzerCapture(): Promise<OpResult>;
  applyAnalyzerSuggestion(amount: number): Promise<OpResult>;
  getSuggestionCurve(amount: number): Promise<SuggestionCurveResult>;
  /** Advanced view asks the native editor to grow for the numeric band panel. */
  setEditorExpanded(expanded: boolean): Promise<OpResult>;
}

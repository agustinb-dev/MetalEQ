/*
    Wire types for the MetalEQ bridge protocol (version 1).

    These mirror Source/ui/MetalEqUiBridge.cpp exactly: the native side is the
    single source of truth, JS only renders what it receives and sends requests.
*/

export const PROTOCOL_VERSION = 1;

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
  index: number; // 0 is reserved for Custom; factory entries are 1..16
  name: string;
  placement: string; // "PRE-AMP" | "POST-AMP" | "EITHER"
  description: string;
}

export type PresetKind = 'user' | 'factory' | 'custom';

export interface PresetInfo {
  index: number;
  name: string;
  kind: PresetKind;
  dirty: boolean;
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
  minDb: number;
  maxDb: number;
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
  applyPreset(index: number): Promise<OpResult>;
  loadUserPreset(name: string): Promise<OpResult>;
  saveUserPreset(name: string, overwrite: boolean): Promise<OpResult>;
  renameUserPreset(oldName: string, newName: string): Promise<OpResult>;
  deleteUserPreset(name: string): Promise<OpResult>;
  revertToPreset(): Promise<OpResult>;
  resetToNeutral(): Promise<OpResult>;
  startAnalyzerCapture(profile: number): Promise<OpResult>;
  cancelAnalyzerCapture(): Promise<OpResult>;
  applyAnalyzerSuggestion(amount: number): Promise<OpResult>;
  getSuggestionCurve(amount: number): Promise<SuggestionCurveResult>;
  /** Fine EQ open/close: the native side resizes the host window by +220 px. */
  setEditorExpanded(expanded: boolean): Promise<OpResult>;
}

/*
    Wire types for the MetalEQ bridge protocol (version 7).

    These mirror Source/ui/MetalEqUiBridge.cpp exactly: the native side is the
    single source of truth, JS only renders what it receives and sends requests.
*/

export const PROTOCOL_VERSION = 7;

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

/** One research citation attached to a target model. */
export interface AnalyzerSourceInfo {
  title: string;
  url: string;
  /** Interview/publication/access date as supplied by the native record. */
  date: string;
  /** What this source supports, in the target's own terms. */
  claim: string;
  /** 0 documented, 1 secondary analysis, 2 engineering interpretation, 3 unknown. */
  evidence: number;
  evidenceLabel: string;
}

/** Analyzer target with stable identity and evidence metadata. */
export interface AnalyzerTargetInfo {
  index: number;
  /** Stable id, independent of table order. */
  id: string;
  label: string;
  shortLabel: string;
  /** 0 genre, 1 artist, 2 producer. */
  category: number;
  categoryLabel: string;
  albumPeriod: string;
  objective: string;
  limitations: string;
  confidence: number;
  sources: AnalyzerSourceInfo[];
  /** Isolated-stem provenance for measured targets; empty for estimates. */
  measuredFromStems?: string;
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
  /** Selected destination, authoritative on the native side. */
  targetIndex?: number;
  targetId?: string;
  targetLabel?: string;
  /** Target the current recommendation was computed for, or -1. */
  resultTarget?: number;
  /** True when input/target/predicted arrays are present and usable. */
  hasReport?: boolean;
  /** Nine measured input band levels, dB relative to the 500-2000 Hz region. */
  input?: number[];
  /** Nine target band levels in the same domain. */
  target?: number[];
  /** Input plus the fitted correction, in the same domain. */
  predicted?: number[];
  /** Feature band centres (Hz), sent by the native side. */
  featureFreqs?: number[];
  /** Five correction band centres (Hz). */
  correctionFreqs?: number[];
  inputConfidence?: number;
  correctionConfidence?: number;
  /** Bitmask of the native CaptureWarning flags (1 = leakage, 2 = HF, 4 = spread). */
  warnings?: number;
  /** Sub-60 Hz energy relative to the mid reference, dB. */
  sub60RelDb?: number;
  /** 10-16 kHz energy relative to the mid reference, dB. */
  hfFloorRelDb?: number;
  /** Broad spectral tilt, dB/octave across 175-5600 Hz. */
  tiltDbPerOct?: number;
  alreadyClose?: boolean;
  changed?: boolean;
}

/** Capture-warning bits, mirroring metaleq::tone::CaptureWarning. */
export const CAPTURE_WARNING = {
  lowEndLeakage: 1,
  highEndTruncated: 2,
  transientSpread: 4,
} as const;

/** One stationary resonance suggested by the V3 detector. */
export interface ResonanceCandidateInfo {
  freq: number;
  q: number;
  /** LTAS peak height above the local 1/2-octave envelope. */
  prominence: number;
  /** Harmonic-residual evidence at this frequency. */
  residual: number;
  /** 0..1; the UI uses this for ordering and for a confidence label. */
  confidence: number;
  /** Proposed surgical cut in dB (always <= 0). */
  cutDb: number;
}

/** A complete V3.1 dynamic-band suggestion from the dynamic scan. */
export interface DynamicCandidateInfo {
  /** Index into the dynamic scan's own candidate list. */
  candidateIndex: number;
  freq: number;
  q: number;
  threshold: number;
  ratio: number;
  attack: number;
  release: number;
  maxCut: number;
  confidence: number;
  /** Hot-point (90th percentile) relative excess measured with the live detector. */
  excessDb: number;
}

/** Shared state for one independent resonance capture. */
export interface ResonanceScanInfo {
  status: number;
  progress: number;
  /** False when the capture was too static to separate harmonics from resonances. */
  enoughVariation: boolean;
  changed?: boolean;
}

/** Surgical scan result: stationary peaks for the six fixed cuts. */
export interface SurgicalResonanceInfo extends ResonanceScanInfo {
  candidates: ResonanceCandidateInfo[];
}

/** Dynamic scan result: suggestions for the four dynamic bands. */
export interface DynamicResonanceInfo extends ResonanceScanInfo {
  candidates: DynamicCandidateInfo[];
}

export interface ResonanceInfo {
  surgical: SurgicalResonanceInfo;
  dynamic: DynamicResonanceInfo;
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

/** Positive gain reduction in dB for each V3.1 dynamic band. */
export type DynamicReductionInfo = number[];

/** One "metaleq.tick" event payload (every field except seq is optional/diffed). */
export interface TickMessage {
  seq: number;
  params?: Record<string, number>;
  preset?: PresetInfo;
  userPresets?: string[];
  meters?: MetersInfo;
  dynamicReduction?: DynamicReductionInfo;
  analyzer?: AnalyzerInfo;
  resonance?: ResonanceInfo;
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
  resonance?: ResonanceInfo;
  curve?: CurveInfo;
  dynamicReduction?: DynamicReductionInfo;
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
  /** Selects a target; if a capture is already analysed, the recommendation is recalculated. */
  setAnalyzerTarget(index: number): Promise<OpResult>;
  startAnalyzerCapture(profile: number): Promise<OpResult>;
  cancelAnalyzerCapture(): Promise<OpResult>;
  applyAnalyzerSuggestion(amount: number): Promise<OpResult>;
  getSuggestionCurve(amount: number): Promise<SuggestionCurveResult>;
  /** V3: independent 8 s captures for the surgical and dynamic sections. */
  startResonanceScan(dynamic: boolean): Promise<OpResult>;
  cancelResonanceScan(dynamic: boolean): Promise<OpResult>;
  /** Selected candidate indices, in order; up to six are copied to surgical bands. */
  applyResonanceSelection(indices: number[]): Promise<OpResult>;
  clearSurgicalBands(): Promise<OpResult>;
  /** Selected candidate indices, in order; up to four are copied to dynamic bands. */
  applyResonanceToDynamic(indices: number[]): Promise<OpResult>;
  clearDynamicBands(): Promise<OpResult>;
  /** Advanced view asks the native editor to grow for the numeric band panel. */
  setEditorExpanded(expanded: boolean): Promise<OpResult>;
  /** Opens a research link in the system browser (http/https only). */
  openExternalUrl(url: string): Promise<OpResult>;
}

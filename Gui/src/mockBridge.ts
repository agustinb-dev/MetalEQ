/*
    Mock bridge: full protocol implementation in the browser, used when the
    JUCE backend is absent (`npm run dev`, vite preview, headless screenshots).

    It reproduces the native contract on purpose:
    * same validation/clamping rules as validateParameterWrite()
    * same diff-based tick stream at 20 Hz
    * same response curve math (via dspMirror.ts, a port of MetalEqDsp.h)
    * JS stays non-authoritative in shape: setParameter returns the clamped
      "authoritative" value and the tick carries it back.

    The analyzer is simulated (no audio input in a browser): capture runs for
    8 s with progress, then reports a canned per-profile suggestion.

    NEVER shipped: the plugin only ever loads JuceBridge.
*/

import type {
  AnalyzerInfo,
  CurveInfo,
  MetalEqBridge,
  OpResult,
  PresetInfo,
  SetParamResult,
  SuggestionCurveResult,
  Snapshot,
  TickMessage,
} from './types';
import { PROTOCOL_VERSION } from './types';
import { PARAMS, clampPlain, getSpec, snapPlain, defaultParams } from './params';
import { buildChain, chainResponseDb, NUM_BANDS, type EqSettings, type MacroSettings } from './dspMirror';
import { GRAPH_MAX_DB, GRAPH_MIN_DB } from './graphAxis';
import { ANALYZER_TARGETS, FACTORY_PRESETS, factoryPresetInfos, neutralEq } from './presets';

interface AbSlotSnapshot {
  params: Record<string, number>;
  loadedSnapshot: Record<string, number> | null;
  preset: PresetInfo;
}

const TICK_MS = 1000 / 20; // matches UiBridge::tickHz = 20
const CURVE_POINTS = 240; // matches UiBridge::numCurvePoints
const GRAPH_MIN = 20;
const GRAPH_MAX = 20000;
const CAPTURE_SECONDS = 8;

// Analyzer band centres, mirroring getAnalyzerResponseDb().
const SUGGESTION_FREQS = [120, 350, 1000, 2800, 7000];

// Canned per-profile suggestions (browser has no audio to analyse).
const PROFILE_SUGGESTIONS: number[][] = [
  [-1.8, -2.4, 1.2, -0.9, -2.2], // Metalcore · Tight Rhythm
  [-1.2, -1.6, 0.8, 1.4, -1.8],  // Thrash · Technical Clarity
  [-0.8, -1.1, 2.1, 0.9, -1.4],  // Classic Thrash · Midrange Punch
  [-2.2, -3.0, 0.6, -1.5, -3.4], // Post-Amp · Mix Cleanup
];

interface UserPresetFile {
  name: string;
  params: Record<string, number>;
}

const USER_PRESET_KEY = 'metaleq.mockUserPresets.v1';

function loadUserPresetFiles(): UserPresetFile[] {
  try {
    const raw = localStorage.getItem(USER_PRESET_KEY);
    if (raw) return JSON.parse(raw) as UserPresetFile[];
  } catch {
    /* ignore corrupt storage */
  }
  return [];
}

function saveUserPresetFiles(files: UserPresetFile[]): void {
  try {
    localStorage.setItem(USER_PRESET_KEY, JSON.stringify(files));
  } catch {
    /* storage full / unavailable: mimic a write failure */
  }
}

/** Plain params -> EqSettings for the mirror DSP (mirrors readEqSettings()). */
function paramsToEq(p: Record<string, number>): EqSettings {
  const eq = neutralEq();
  eq.hpFreq = p.hpFreq;
  eq.hpSlope = p.hpSlope;
  eq.lpFreq = p.lpFreq;
  eq.lpSlope = p.lpSlope;
  eq.lpEnabled = p.lpOn >= 0.5;
  eq.lsFreq = p.lsFreq;
  eq.lsGainDb = p.lsGain;
  eq.hsFreq = p.hsFreq;
  eq.hsGainDb = p.hsGain;
  for (let i = 0; i < NUM_BANDS; i++) {
    eq.bands[i] = {
      freq: p[`band${i + 1}Freq`],
      gainDb: p[`band${i + 1}Gain`],
      q: p[`band${i + 1}Q`],
      on: p[`band${i + 1}On`] >= 0.5,
    };
  }
  eq.shapes = [0, 1, 2, 3, 4, 5].map((i) => p[`shape${['Tighten', 'Scoop', 'MidPunch', 'PickAttack', 'FizzTamer', 'Brighten'][i]}On`] >= 0.5);
  eq.trimDb = p.trim;
  return eq;
}

function paramsToMacros(p: Record<string, number>): MacroSettings {
  return { tight: p.tight, body: p.body, scoop: p.scoop, bite: p.bite, fizzCut: p.fizzCut };
}

function buildCurve(p: Record<string, number>, sampleRate: number): CurveInfo {
  const chain = buildChain(paramsToEq(p), paramsToMacros(p), sampleRate);
  const points: number[] = [];
  for (let i = 0; i < CURVE_POINTS; i++) {
    const unit = i / (CURVE_POINTS - 1);
    const freq = GRAPH_MIN * Math.pow(GRAPH_MAX / GRAPH_MIN, unit);
    points.push(chainResponseDb(chain, sampleRate, freq));
  }
  // Fixed graph window, matching the native bridge: values outside the range
  // are clipped by the renderer instead of rescaling the whole graph.
  return { points, minDb: GRAPH_MIN_DB, maxDb: GRAPH_MAX_DB, sampleRate };
}

function nearlyEqual(a: number, b: number): boolean {
  const scale = Math.max(1, Math.abs(b));
  return Math.abs(a - b) <= 1e-6 * scale;
}

/** Mirrors getAnalyzerResponseDb(): base EQ with the 5 suggestion bands swapped in. */
function analyzerResponseDb(
  p: Record<string, number>,
  suggestion: number[],
  amount: number,
  sampleRate: number,
  freq: number,
): number {
  const eq = paramsToEq(p);
  for (let i = 0; i < NUM_BANDS; i++) {
    eq.bands[i] = { freq: SUGGESTION_FREQS[i], gainDb: suggestion[i] * amount, q: 0.8, on: true };
  }
  return chainResponseDb(buildChain(eq, paramsToMacros(p), sampleRate), sampleRate, freq);
}

export class MockBridge implements MetalEqBridge {
  private params: Record<string, number> = defaultParams();
  private preset: PresetInfo = {
    index: 0, presetId: '', name: 'Custom', kind: 'custom', dirty: false, abSlot: 0,
  };
  private userPresets: UserPresetFile[] = loadUserPresetFiles();
  private activeAb = 0;
  private abSlots: Array<AbSlotSnapshot | null> = [null, null];
  private analyzer: AnalyzerInfo = { status: 0, progress: 0, suggestion: [0, 0, 0, 0, 0] };
  private curveDirty = true;
  private analyzerDirty = false;
  private userPresetsDirty = false;
  private fullSync = true;
  private seq = 0;
  private lastSent = new Map<string, number>();
  private sampleRate = 48000;
  private emit: ((t: TickMessage) => void) | null = null;
  private timer: ReturnType<typeof setInterval> | null = null;
  private captureTimer: ReturnType<typeof setInterval> | null = null;

  // Tracks the "loaded" preset snapshot for the dirty flag (like the native
  // loadedPresetState comparison).
  private loadedSnapshot: Record<string, number> | null = null;

  init(handlers: { onTick: (t: TickMessage) => void }): Promise<Snapshot> {
    this.emit = handlers.onTick;
    for (const spec of PARAMS) this.lastSent.set(spec.id, Number.NaN);

    if (this.abSlots[0] == null) {
      const source = this.captureActiveSlot();
      this.abSlots[1] = {
        params: { ...source.params },
        loadedSnapshot: source.loadedSnapshot ? { ...source.loadedSnapshot } : null,
        preset: { ...source.preset },
      };
    }

    if (!this.timer) {
      this.timer = setInterval(() => this.tick(), TICK_MS);
    }

    const snapshot: Snapshot = {
      protocolVersion: PROTOCOL_VERSION,
      params: { ...this.params },
      preset: { ...this.preset },
      factoryPresets: factoryPresetInfos(),
      analyzerTargets: ANALYZER_TARGETS.map((t) => ({ ...t })),
      userPresets: this.userPresets.map((f) => f.name),
      analyzer: { ...this.analyzer },
      curve: this.buildCurve(),
    };
    this.curveDirty = false;
    this.fullSync = true;
    return Promise.resolve(snapshot);
  }

  private buildCurve(): CurveInfo {
    return buildCurve(this.params, this.sampleRate);
  }

  private tick(): void {
    if (!this.emit) return;
    this.seq += 1;

    const msg: TickMessage = { seq: this.seq };
    const params: Record<string, number> = {};
    for (const spec of PARAMS) {
      const value = this.params[spec.id];
      const last = this.lastSent.get(spec.id) ?? Number.NaN;
      if (this.fullSync || !nearlyEqual(value, last)) {
        params[spec.id] = value;
        this.lastSent.set(spec.id, value);
      }
    }
    this.fullSync = false;
    if (Object.keys(params).length > 0) msg.params = params;

    msg.preset = { ...this.preset };

    if (this.userPresetsDirty) {
      msg.userPresets = this.userPresets.map((f) => f.name);
      this.userPresetsDirty = false;
    }

    // Simulated meter: idle noise so the meters breathe in development.
    const t = Date.now() / 1000;
    const idle = 0.08 + 0.05 * Math.sin(t * 2.3);
    msg.meters = { in: Math.max(0, idle), out: Math.max(0, idle * 0.85) };

    if (this.analyzerDirty) {
      msg.analyzer = { ...this.analyzer, changed: true };
      this.analyzerDirty = false;
    }

    if (this.curveDirty) {
      msg.curve = this.buildCurve();
      this.curveDirty = false;
    }

    this.emit(msg);
  }

  //--- helpers ------------------------------------------------------------

  private markDirty(): void {
    this.curveDirty = true;
    // Preset dirty tracking (mirror of isPresetModified()).
    if (this.loadedSnapshot) {
      let dirty = false;
      for (const spec of PARAMS) {
        // Trim is the global level-match control; it never dirties a preset.
        if (spec.id === 'trim' || spec.id === 'bypass' || spec.id === 'abCompare') continue;
        if (!nearlyEqual(this.params[spec.id], this.loadedSnapshot[spec.id] ?? 0)) {
          dirty = true;
          break;
        }
      }
      this.preset = { ...this.preset, dirty };
    }
  }

  private markUserPresetsChanged(): void {
    this.userPresetsDirty = true;
  }

  private allSoundParams(): Record<string, number> {
    const out: Record<string, number> = {};
    for (const spec of PARAMS) {
      if (spec.id === 'bypass' || spec.id === 'abCompare') continue;
      out[spec.id] = this.params[spec.id];
    }
    return out;
  }

  //--- native function handlers -------------------------------------------

  setParameter(id: string, value: number): Promise<SetParamResult> {
    const spec = getSpec(id);
    if (!spec) {
      return Promise.resolve({ ok: false, error: `Unknown parameter id: ${id}` });
    }
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      return Promise.resolve({ ok: false, error: `Value for ${id} must be a finite number.` });
    }

    const plain = clampPlain(spec, value);
    this.params[id] = plain;

    // Native side effect: enabling lpOn while slope is Off forces 24 dB/oct
    // (MetalEQAudioProcessor::setLowPassEnabled).
    if (id === 'lpOn' && plain >= 0.5 && (this.params.lpSlope ?? 0) < 0.5) {
      this.params.lpSlope = 2;
    }

    this.markDirty();
    return Promise.resolve({ ok: true, value: this.params[id] });
  }

  applyPreset(index: number): Promise<OpResult> {
    if (!Number.isFinite(index)) {
      return Promise.resolve({ ok: false, error: 'applyPreset expects a preset index.' });
    }
    const safe = Math.min(FACTORY_PRESETS.length, Math.max(0, Math.round(index)));

    if (safe === 0) {
      this.resetToNeutralInternal();
      return Promise.resolve({ ok: true });
    }

    // Canonical v2 mapping: selector 1 = factory[0].
    const def = FACTORY_PRESETS[safe - 1];
    const eq = def.eq;
    this.params.hpFreq = eq.hpFreq;
    this.params.hpSlope = eq.hpSlope;
    this.params.lpFreq = eq.lpFreq;
    this.params.lpSlope = eq.lpSlope;
    this.params.lpOn = eq.lpEnabled ? 1 : 0;
    this.params.lsFreq = eq.lsFreq;
    this.params.lsGain = eq.lsGainDb;
    this.params.hsFreq = eq.hsFreq;
    this.params.hsGain = eq.hsGainDb;
    for (let i = 0; i < NUM_BANDS; i++) {
      this.params[`band${i + 1}Freq`] = eq.bands[i].freq;
      this.params[`band${i + 1}Gain`] = eq.bands[i].gainDb;
      this.params[`band${i + 1}Q`] = eq.bands[i].q;
      this.params[`band${i + 1}On`] = eq.bands[i].on ? 1 : 0;
    }
    this.params.trim = eq.trimDb;
    for (const id of ['shapeTightenOn', 'shapeScoopOn', 'shapeMidPunchOn',
      'shapePickAttackOn', 'shapeFizzTamerOn', 'shapeBrightenOn']) {
      this.params[id] = 0;
    }
    if (def.setsMacros) {
      this.params.tight = def.macros.tight;
      this.params.body = def.macros.body;
      this.params.scoop = def.macros.scoop;
      this.params.bite = def.macros.bite;
      this.params.fizzCut = def.macros.fizzCut;
    }

    this.preset = {
      index: safe, presetId: def.id, name: def.name, kind: 'factory', dirty: false,
      abSlot: this.activeAb,
    };
    this.loadedSnapshot = { ...this.params };
    this.markDirty();
    this.markUserPresetsChanged();
    return Promise.resolve({ ok: true });
  }

  private resetToNeutralInternal(): void {
    const neutral = neutralEq();
    this.params.hpFreq = neutral.hpFreq;
    this.params.hpSlope = neutral.hpSlope;
    this.params.lpFreq = neutral.lpFreq;
    this.params.lpSlope = neutral.lpSlope;
    this.params.lpOn = neutral.lpEnabled ? 1 : 0;
    this.params.lsFreq = neutral.lsFreq;
    this.params.lsGain = neutral.lsGainDb;
    this.params.hsFreq = neutral.hsFreq;
    this.params.hsGain = neutral.hsGainDb;
    for (let i = 0; i < NUM_BANDS; i++) {
      this.params[`band${i + 1}Freq`] = neutral.bands[i].freq;
      this.params[`band${i + 1}Gain`] = neutral.bands[i].gainDb;
      this.params[`band${i + 1}Q`] = neutral.bands[i].q;
      this.params[`band${i + 1}On`] = 1;
    }
    this.params.trim = 0;
    this.params.tight = 0;
    this.params.body = 0.5;
    this.params.scoop = 0;
    this.params.bite = 0;
    this.params.fizzCut = 0;
    for (const id of ['shapeTightenOn', 'shapeScoopOn', 'shapeMidPunchOn',
      'shapePickAttackOn', 'shapeFizzTamerOn', 'shapeBrightenOn']) {
      this.params[id] = 0;
    }
  }

  resetToNeutral(): Promise<OpResult> {
    this.resetToNeutralInternal();
    // applyPreset(0) semantics: back to Custom.
    this.preset = {
      index: 0, presetId: '', name: 'Custom', kind: 'custom', dirty: false, abSlot: this.activeAb,
    };
    this.loadedSnapshot = { ...this.params };
    this.markDirty();
    return Promise.resolve({ ok: true });
  }

  applyPresetById(presetId: string): Promise<OpResult> {
    const found = FACTORY_PRESETS.find((p) => p.id === presetId);
    if (!found) return Promise.resolve({ ok: false, error: `Unknown factory preset id: ${presetId}` });
    return this.applyPreset(found.index);
  }

  loadUserPreset(name: string): Promise<OpResult> {
    const file = this.userPresets.find((f) => f.name === name);
    if (!file) {
      return Promise.resolve({ ok: false, error: `User preset '${name}' does not exist.` });
    }
    for (const [key, value] of Object.entries(file.params)) {
      const spec = getSpec(key);
      if (spec && key !== 'bypass' && key !== 'abCompare') {
        this.params[key] = snapPlain(spec, value);
      }
    }
    this.preset = {
      index: 0, presetId: '', name, kind: 'user', dirty: false, abSlot: this.activeAb,
    };
    this.loadedSnapshot = { ...this.params };
    this.markDirty();
    return Promise.resolve({ ok: true });
  }

  saveUserPreset(name: string, overwrite: boolean): Promise<OpResult> {
    const trimmed = name.trim();
    if (trimmed.length === 0) {
      return Promise.resolve({ ok: false, error: 'Enter a preset name.' });
    }
    const exists = this.userPresets.some((f) => f.name.toLowerCase() === trimmed.toLowerCase());
    if (exists && !overwrite) {
      return Promise.resolve({ ok: false, error: 'A user preset with that name already exists.' });
    }
    if (exists) {
      const i = this.userPresets.findIndex((f) => f.name.toLowerCase() === trimmed.toLowerCase());
      this.userPresets[i] = { name: trimmed, params: this.allSoundParams() };
    } else {
      this.userPresets.push({ name: trimmed, params: this.allSoundParams() });
    }
    saveUserPresetFiles(this.userPresets);
    this.preset = {
      index: 0, presetId: '', name: trimmed, kind: 'user', dirty: false, abSlot: this.activeAb,
    };
    this.loadedSnapshot = { ...this.params };
    this.markUserPresetsChanged();
    return Promise.resolve({ ok: true });
  }

  renameUserPreset(oldName: string, newName: string): Promise<OpResult> {
    const i = this.userPresets.findIndex((f) => f.name === oldName);
    if (i < 0) return Promise.resolve({ ok: false, error: `User preset '${oldName}' does not exist.` });
    const trimmed = newName.trim();
    if (trimmed.length === 0) return Promise.resolve({ ok: false, error: 'Enter a preset name.' });
    const clash = this.userPresets.some((f, j) => j !== i && f.name.toLowerCase() === trimmed.toLowerCase());
    if (clash) return Promise.resolve({ ok: false, error: 'A user preset with that name already exists.' });
    this.userPresets[i] = { ...this.userPresets[i], name: trimmed };
    saveUserPresetFiles(this.userPresets);
    if (this.preset.kind === 'user' && this.preset.name === oldName) {
      this.preset = { ...this.preset, name: trimmed };
    }
    this.markUserPresetsChanged();
    return Promise.resolve({ ok: true });
  }

  deleteUserPreset(name: string): Promise<OpResult> {
    const before = this.userPresets.length;
    this.userPresets = this.userPresets.filter((f) => f.name !== name);
    if (this.userPresets.length === before) {
      return Promise.resolve({ ok: false, error: `User preset '${name}' does not exist.` });
    }
    saveUserPresetFiles(this.userPresets);
    if (this.preset.kind === 'user' && this.preset.name === name) {
      this.preset = {
        index: 0, presetId: '', name: 'Custom', kind: 'custom', dirty: false, abSlot: this.activeAb,
      };
      this.loadedSnapshot = { ...this.params };
    }
    this.markUserPresetsChanged();
    return Promise.resolve({ ok: true });
  }

  revertToPreset(): Promise<OpResult> {
    if (!this.loadedSnapshot) return Promise.resolve({ ok: true });
    for (const [key, value] of Object.entries(this.loadedSnapshot)) {
      this.params[key] = value;
    }
    this.preset = { ...this.preset, dirty: false };
    this.markDirty();
    return Promise.resolve({ ok: true });
  }

  //--- real A/B slots (mirror the native snapshots) -----------------------

  private captureActiveSlot(): AbSlotSnapshot {
    const params = this.allSoundParams();
    delete params.trim;
    const snapshot: AbSlotSnapshot = {
      params,
      loadedSnapshot: this.loadedSnapshot ? { ...this.loadedSnapshot } : null,
      preset: { ...this.preset, abSlot: this.activeAb },
    };
    this.abSlots[this.activeAb] = snapshot;
    return snapshot;
  }

  private loadSlot(slot: number): void {
    const stored = this.abSlots[slot];
    if (!stored) return;

    const trim = this.params.trim;
    for (const [key, value] of Object.entries(stored.params)) this.params[key] = value;
    this.params.trim = trim; // output trim is global, not per-slot
    this.preset = { ...stored.preset, abSlot: slot };
    this.loadedSnapshot = stored.loadedSnapshot ? { ...stored.loadedSnapshot } : null;
    this.params.abCompare = slot === 1 ? 1 : 0;
    this.markDirty();
  }

  setAbSlot(slot: number): Promise<OpResult> {
    if (!Number.isFinite(slot) || (slot !== 0 && slot !== 1)) {
      return Promise.resolve({ ok: false, error: 'setAbSlot expects 0 (A) or 1 (B).' });
    }
    if (slot === this.activeAb) return Promise.resolve({ ok: true });

    this.captureActiveSlot();
    if (!this.abSlots[slot]) {
      this.abSlots[slot] = this.abSlots[this.activeAb]
        ? {
            params: { ...this.abSlots[this.activeAb]!.params },
            loadedSnapshot: this.abSlots[this.activeAb]!.loadedSnapshot
              ? { ...this.abSlots[this.activeAb]!.loadedSnapshot! }
              : null,
            preset: { ...this.abSlots[this.activeAb]!.preset },
          }
        : null;
    }
    this.activeAb = slot;
    this.loadSlot(slot);
    return Promise.resolve({ ok: true });
  }

  copyAbToOther(): Promise<OpResult> {
    this.captureActiveSlot();
    const other = this.activeAb === 0 ? 1 : 0;
    const source = this.abSlots[this.activeAb];
    this.abSlots[other] = source
      ? {
          params: { ...source.params },
          loadedSnapshot: source.loadedSnapshot ? { ...source.loadedSnapshot } : null,
          preset: { ...source.preset, abSlot: other },
        }
      : null;
    return source ? Promise.resolve({ ok: true })
                  : Promise.resolve({ ok: false, error: 'Could not copy the A/B slot.' });
  }

  swapAbSlots(): Promise<OpResult> {
    this.captureActiveSlot();
    const a = this.abSlots[0];
    const b = this.abSlots[1];
    if (!a || !b) return Promise.resolve({ ok: false, error: 'Could not swap the A/B slots.' });
    this.abSlots[0] = b;
    this.abSlots[1] = a;
    this.loadSlot(this.activeAb);
    return Promise.resolve({ ok: true });
  }

  //--- analyzer (simulated) ----------------------------------------------

  startAnalyzerCapture(profile: number): Promise<OpResult> {
    this.cancelCaptureTimer();
    const target = Math.min(ANALYZER_TARGETS.length - 1, Math.max(0, Math.round(profile)));
    this.analyzer = { status: 1, progress: 0, suggestion: [0, 0, 0, 0, 0] };
    this.analyzerDirty = true;

    const started = Date.now();
    this.captureTimer = setInterval(() => {
      const elapsed = (Date.now() - started) / 1000;
      const progress = Math.min(1, elapsed / CAPTURE_SECONDS);
      if (progress >= 1) {
        this.cancelCaptureTimer();
        this.analyzer = { status: 3, progress: 1, suggestion: [0, 0, 0, 0, 0] };
        this.analyzerDirty = true;
        setTimeout(() => {
          this.analyzer = {
            status: 4,
            progress: 1,
            suggestion: [...PROFILE_SUGGESTIONS[target]],
          };
          this.analyzerDirty = true;
        }, 700);
      } else {
        this.analyzer = { ...this.analyzer, status: 1, progress };
        this.analyzerDirty = true;
      }
    }, 100);

    return Promise.resolve({ ok: true });
  }

  private cancelCaptureTimer(): void {
    if (this.captureTimer) {
      clearInterval(this.captureTimer);
      this.captureTimer = null;
    }
  }

  cancelAnalyzerCapture(): Promise<OpResult> {
    this.cancelCaptureTimer();
    this.analyzer = { status: 0, progress: 0, suggestion: [0, 0, 0, 0, 0] };
    this.analyzerDirty = true;
    return Promise.resolve({ ok: true });
  }

  applyAnalyzerSuggestion(amount: number): Promise<OpResult> {
    if (this.analyzer.status !== 4) return Promise.resolve({ ok: true });
    const a = Math.min(1, Math.max(0, amount));
    for (let i = 0; i < NUM_BANDS; i++) {
      this.params[`band${i + 1}Freq`] = SUGGESTION_FREQS[i];
      this.params[`band${i + 1}Gain`] = snapPlain(getSpec(`band${i + 1}Gain`)!, this.analyzer.suggestion[i] * a);
      this.params[`band${i + 1}Q`] = 0.8;
      this.params[`band${i + 1}On`] = 1;
    }
    this.preset = {
      index: 0, presetId: '', name: 'Custom', kind: 'custom', dirty: false, abSlot: this.activeAb,
    };
    this.loadedSnapshot = { ...this.params };
    this.markDirty();
    return Promise.resolve({ ok: true });
  }

  getSuggestionCurve(amount: number): Promise<SuggestionCurveResult> {
    const a = Math.min(1, Math.max(0, amount));
    const curve: number[] = [];
    for (let i = 0; i < CURVE_POINTS; i++) {
      const unit = i / (CURVE_POINTS - 1);
      const freq = GRAPH_MIN * Math.pow(GRAPH_MAX / GRAPH_MIN, unit);
      curve.push(analyzerResponseDb(this.params, this.analyzer.suggestion, a, this.sampleRate, freq));
    }
    return Promise.resolve({ ok: true, curve });
  }

  setEditorExpanded(_expanded: boolean): Promise<OpResult> {
    // Browser preview: the page simply scrolls; no host window to resize.
    return Promise.resolve({ ok: true });
  }
}

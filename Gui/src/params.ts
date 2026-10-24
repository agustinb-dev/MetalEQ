/*
    Parameter metadata mirroring MetalEQAudioProcessor::createParameterLayout()
    (Source/PluginProcessor.cpp) and metaleq::params (MetalEqParams.h).

    Plain values are what travels over the bridge; the skew math below mirrors
    juce::NormalisableRange::convertTo0to1 / convertFrom0to1 so that knob
    dragging feels identical to the native editor.
*/

import type { ParamSpec } from './types';

export const NUM_BANDS = 5;
export const NUM_SURGICAL_BANDS = 6;
export const NUM_DYNAMIC_BANDS = 4;

const choice = (
  id: string,
  label: string,
  choices: string[],
  def: number,
): ParamSpec => ({
  id,
  label,
  kind: 'choice',
  min: 0,
  max: choices.length - 1,
  skew: 1,
  step: 1,
  def,
  choices,
});

const flag = (id: string, label: string, def: 0 | 1): ParamSpec => ({
  id,
  label,
  kind: 'bool',
  min: 0,
  max: 1,
  skew: 1,
  step: 1,
  def,
});

const slopeChoices = ['Off', '12 dB/oct', '24 dB/oct', '36 dB/oct'];

export const PARAMS: ParamSpec[] = [
  //--- crossovers -----------------------------------------------------------
  { id: 'hpFreq', label: 'High-Pass Freq', kind: 'float', min: 20, max: 180, skew: 0.35, step: 0.01, def: 80 },
  choice('hpSlope', 'High-Pass Slope', slopeChoices, 0),
  { id: 'lpFreq', label: 'Low-Pass Freq', kind: 'float', min: 3000, max: 20000, skew: 0.35, step: 0.01, def: 12000 },
  choice('lpSlope', 'Low-Pass Slope', slopeChoices, 0),
  flag('lpOn', 'Low-Pass Enabled', 0),

  //--- shelves --------------------------------------------------------------
  { id: 'lsFreq', label: 'Low Shelf Freq', kind: 'float', min: 30, max: 400, skew: 0.4, step: 0.01, def: 140 },
  { id: 'lsGain', label: 'Low Shelf Gain', kind: 'float', min: -12, max: 12, skew: 1, step: 0.01, def: 0 },
  { id: 'hsFreq', label: 'High Shelf Freq', kind: 'float', min: 3000, max: 16000, skew: 0.4, step: 0.01, def: 7000 },
  { id: 'hsGain', label: 'High Shelf Gain', kind: 'float', min: -12, max: 12, skew: 1, step: 0.01, def: 0 },

  //--- five parametric bands -------------------------------------------------
  ...[120, 400, 1200, 3500, 8000].flatMap((def, i) => {
    const n = i + 1;
    return [
      { id: `band${n}Freq`, label: `Band ${n} Freq`, kind: 'float' as const, min: 60, max: 16000, skew: 0.4, step: 0.01, def },
      { id: `band${n}Gain`, label: `Band ${n} Gain`, kind: 'float' as const, min: -12, max: 12, skew: 1, step: 0.01, def: 0 },
      { id: `band${n}Q`, label: `Band ${n} Q`, kind: 'float' as const, min: 0.3, max: 6, skew: 1, step: 0.01, def: 1 },
      flag(`band${n}On`, `Band ${n} On`, 1),
    ];
  }),

  //--- V3 surgical resonance bands ------------------------------------------
  ...[250, 620, 1200, 2400, 4000, 6200].flatMap((def, i) => {
    const n = i + 1;
    return [
      { id: `res${n}Freq`, label: `Resonance ${n} Freq`, kind: 'float' as const, min: 80, max: 14000, skew: 0.4, step: 0.01, def },
      { id: `res${n}Gain`, label: `Resonance ${n} Gain`, kind: 'float' as const, min: -12, max: 6, skew: 1, step: 0.01, def: 0 },
      { id: `res${n}Q`, label: `Resonance ${n} Q`, kind: 'float' as const, min: 2, max: 20, skew: 1, step: 0.01, def: 7 },
      flag(`res${n}On`, `Resonance ${n} On`, 0),
    ];
  }),

  //--- V3.1 dynamic resonance bands -----------------------------------------
  ...[250, 700, 3200, 7500].flatMap((def, i) => {
    const n = i + 1;
    return [
      { id: `dyn${n}Freq`, label: `Dynamic ${n} Freq`, kind: 'float' as const, min: 80, max: 14000, skew: 0.4, step: 0.01, def },
      { id: `dyn${n}Q`, label: `Dynamic ${n} Q`, kind: 'float' as const, min: 0.5, max: 6, skew: 1, step: 0.01, def: 1.2 },
      { id: `dyn${n}Threshold`, label: `Dynamic ${n} Threshold`, kind: 'float' as const, min: 0, max: 24, skew: 1, step: 0.1, def: 6 },
      { id: `dyn${n}Ratio`, label: `Dynamic ${n} Ratio`, kind: 'float' as const, min: 1, max: 10, skew: 1, step: 0.1, def: 4 },
      { id: `dyn${n}Attack`, label: `Dynamic ${n} Attack`, kind: 'float' as const, min: 0.5, max: 50, skew: 0.6, step: 0.1, def: 5 },
      { id: `dyn${n}Release`, label: `Dynamic ${n} Release`, kind: 'float' as const, min: 20, max: 1000, skew: 0.5, step: 1, def: 120 },
      { id: `dyn${n}Range`, label: `Dynamic ${n} Max Cut`, kind: 'float' as const, min: 0, max: 12, skew: 1, step: 0.1, def: 6 },
      flag(`dyn${n}On`, `Dynamic ${n} On`, 0),
    ];
  }),

  //--- output trim -----------------------------------------------------------
  { id: 'trim', label: 'Output Trim', kind: 'float', min: -12, max: 6, skew: 1, step: 0.01, def: 0 },

  //--- macros -----------------------------------------------------------------
  { id: 'tight', label: 'TIGHT', kind: 'float', min: 0, max: 1, skew: 1, step: 0.005, def: 0 },
  { id: 'body', label: 'BODY', kind: 'float', min: 0, max: 1, skew: 1, step: 0.005, def: 0.5 },
  { id: 'scoop', label: 'SCOOP', kind: 'float', min: 0, max: 1, skew: 1, step: 0.005, def: 0 },
  { id: 'bite', label: 'BITE', kind: 'float', min: 0, max: 1, skew: 1, step: 0.005, def: 0 },
  { id: 'fizzCut', label: 'FIZZ CUT', kind: 'float', min: 0, max: 1, skew: 1, step: 0.005, def: 0 },

  //--- one-click shapes --------------------------------------------------------
  flag('shapeTightenOn', 'TIGHTEN', 0),
  flag('shapeScoopOn', 'SCOOP Shape', 0),
  flag('shapeMidPunchOn', 'MID PUNCH', 0),
  flag('shapePickAttackOn', 'PICK ATTACK', 0),
  flag('shapeFizzTamerOn', 'FIZZ TAMER', 0),
  flag('shapeBrightenOn', 'BRIGHTEN', 0),

  //--- bypass / A-B (part of the same diff stream, not in allEqParameterIds) ---
  flag('bypass', 'Bypass', 0),
  flag('abCompare', 'A/B Compare', 0),
  flag('resAudition', 'Surgical Resonance Delta Monitor', 0),
  flag('dynAudition', 'Dynamic Resonance Delta Monitor', 0),
];

const byId = new Map<string, ParamSpec>(PARAMS.map((p) => [p.id, p]));

export function getSpec(id: string): ParamSpec | undefined {
  return byId.get(id);
}

/** All ids in the order the native diff stream sends them. */
export function allParamIds(): string[] {
  return PARAMS.map((p) => p.id);
}

/** Default plain-value map (used by the mock bridge before the first snapshot). */
export function defaultParams(): Record<string, number> {
  const out: Record<string, number> = {};
  for (const p of PARAMS) out[p.id] = p.def;
  return out;
}

export const clamp01 = (v: number) => (v < 0 ? 0 : v > 1 ? 1 : v);

/** juce::NormalisableRange::convertTo0to1 (skewed). */
export function toNormalized(spec: ParamSpec, value: number): number {
  const linear = (value - spec.min) / (spec.max - spec.min);
  return clamp01(Math.pow(clamp01(linear), spec.skew));
}

/** juce::NormalisableRange::convertFrom0to1 (skewed). */
export function fromNormalized(spec: ParamSpec, norm: number): number {
  const p = clamp01(norm);
  return spec.min + (spec.max - spec.min) * Math.pow(p, 1 / spec.skew);
}

/** Clamp a plain value to the parameter's range (mirror of validateParameterWrite). */
export function clampPlain(spec: ParamSpec, value: number): number {
  if (!Number.isFinite(value)) return spec.def;
  if (spec.kind === 'bool') return value >= 0.5 ? 1 : 0;
  if (spec.kind === 'choice') {
    return Math.min(spec.max, Math.max(0, Math.round(value)));
  }
  return Math.min(spec.max, Math.max(spec.min, value));
}

/** Snap to the parameter interval, like juce::Slider with an interval. */
export function snapPlain(spec: ParamSpec, value: number): number {
  const v = clampPlain(spec, value);
  if (spec.kind !== 'float' || spec.step <= 0) return v;
  const snapped = spec.min + Math.round((v - spec.min) / spec.step) * spec.step;
  return clampPlain(spec, snapped);
}

//--- value formatting (mirrors the JUCE slider suffixes/decimals) ------------

export function formatFreq(value: number): string {
  return value >= 1000 ? `${(value / 1000).toFixed(2)} kHz` : `${Math.round(value)} Hz`;
}

/** Native sliders show whole Hz with a " Hz" suffix. */
export function formatFreqPlain(value: number): string {
  return `${Math.round(value)} Hz`;
}

export function formatGain(value: number): string {
  const v = Math.abs(value) < 0.05 ? 0 : value;
  return `${v > 0 ? '+' : ''}${v.toFixed(1)} dB`;
}

export function formatQ(value: number): string {
  return value.toFixed(2);
}

export function formatPercent(value: number): string {
  return `${Math.round(value * 100)}%`;
}

export function formatTrim(value: number): string {
  const v = Math.abs(value) < 0.05 ? 0 : value;
  return `${v > 0 ? '+' : ''}${v.toFixed(1)} dB`;
}

export function formatThreshold(value: number): string {
  return `${value.toFixed(1)} dB`;
}

export function formatRatio(value: number): string {
  return `${value.toFixed(1)}:1`;
}

export function formatTimeMs(value: number): string {
  return value < 10 ? `${value.toFixed(1)} ms` : `${Math.round(value)} ms`;
}

export function formatCut(value: number): string {
  return `${value.toFixed(1)} dB`;
}

/** Label for a band card, e.g. "BAND 2". */
export function bandLabel(index: number): string {
  return `BAND ${index + 1}`;
}

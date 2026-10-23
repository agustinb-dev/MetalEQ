/*
    Factory preset table for browser development only — a 1:1 port of
    Source/dsp/MetalEqPresets.cpp (25 presets). The plugin itself gets its
    preset list and stable ids from the native snapshot; this file is never
    compiled into the binary.

    Index semantics match the bridge: factory entries are reported with
    index = i + 1 (0 is reserved for Custom).
*/

import type { AnalyzerTargetInfo, FactoryPresetInfo } from './types';
import type { EqSettings, MacroSettings } from './dspMirror';

export interface FactoryPresetDef {
  index: number; // i + 1
  id: string;
  genre: string;
  subgenre: string;
  name: string;
  placement: string;
  description: string;
  eq: EqSettings;
  setsMacros: boolean;
  macros: MacroSettings;
}

export interface ShapeDef {
  name: string;
  description: string;
}

const NEUTRAL_MACROS: MacroSettings = { tight: 0, body: 0.5, scoop: 0, bite: 0, fizzCut: 0 };

const band = (freq: number, gainDb: number, q: number, on = true) => ({ freq, gainDb, q, on });

/** Default (neutral) EqSettings, mirroring metaleq::EqSettings defaults. */
export function neutralEq(): EqSettings {
  return {
    hpFreq: 80,
    lpFreq: 12000,
    hpSlope: 0,
    lpSlope: 0,
    lpEnabled: false,
    lsFreq: 140,
    lsGainDb: 0,
    hsFreq: 7000,
    hsGainDb: 0,
    bands: [
      { freq: 120, gainDb: 0, q: 1, on: true },
      { freq: 400, gainDb: 0, q: 1, on: true },
      { freq: 1200, gainDb: 0, q: 1, on: true },
      { freq: 3500, gainDb: 0, q: 1, on: true },
      { freq: 8000, gainDb: 0, q: 1, on: true },
    ],
    shapes: [false, false, false, false, false, false],
    trimDb: 0,
  };
}

type EqParts = [
  hpFreq: number, hpSlope: number, lpFreq: number, lpSlope: number,
  lsFreq: number, lsGain: number, hsFreq: number, hsGain: number,
  bands: ReturnType<typeof band>[],
  trim: number,
];

function makeEq(p: EqParts): EqSettings {
  const e = neutralEq();
  e.hpFreq = p[0]; e.hpSlope = p[1]; e.lpFreq = p[2]; e.lpSlope = p[3];
  e.lpEnabled = p[3] > 0;
  e.lsFreq = p[4]; e.lsGainDb = p[5]; e.hsFreq = p[6]; e.hsGainDb = p[7];
  e.bands = [...p[8]];
  e.trimDb = p[9];
  return e;
}

type PresetRow = [
  id: string, genre: string, subgenre: string, name: string,
  placement: string, description: string,
  eq: EqSettings, setsMacros: boolean, macros: MacroSettings,
];

const rows: PresetRow[] = [
  //--- Metalcore / 2000s -----------------------------------------------------
  ['metalcore-2000s-tight-rhythm', 'Metalcore', '2000s', '2000s Metalcore: Tight Rhythm', 'POST-AMP',
    'Post-amp. Tight low end, controlled low mids, aggressive attack and restrained fizz. The safe starting point for palm-muted rhythm.',
    makeEq([85, 2, 12000, 2, 120, -2.5, 7000, -1.5,
      [band(240, -3.5, 1), band(420, -2, 1.4), band(2200, 3, 0.9), band(5200, -2, 1.2), band(8500, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['metalcore-2000s-double-track', 'Metalcore', '2000s', '2000s Metalcore: Double Track', 'POST-AMP',
    'Post-amp. Keeps doubled rhythm guitars from turning into a midrange pile-up while preserving the pick attack that cuts through drums.',
    makeEq([90, 2, 14000, 2, 130, -2, 7200, -1,
      [band(260, -3, 1), band(500, -2.5, 1.3), band(2100, 2, 0.9), band(4800, -2, 1.1), band(9000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['metalcore-2000s-lead', 'Metalcore', '2000s', '2000s Metalcore: Lead', 'POST-AMP',
    'Post-amp. A focused lead voice with enough upper mids to sit on top of a dense rhythm section without becoming brittle.',
    makeEq([95, 2, 16000, 1, 150, -1.5, 7500, 0.5,
      [band(280, -3, 1), band(650, -2, 1.2), band(2600, 4, 0.8), band(5500, 1, 1.2), band(9500, -2, 0.7)],
      0]), false, NEUTRAL_MACROS],

  //--- Metalcore / Modern ----------------------------------------------------
  ['metalcore-modern-drop-tuning', 'Metalcore', 'Modern', 'Modern Metalcore: Drop Tuning', 'POST-AMP',
    'Post-amp. A 36 dB/oct high-pass at 70 Hz keeps the low notes of a drop tuning separate instead of rumbling into each other.',
    makeEq([70, 3, 11000, 2, 100, -3.5, 6000, -2,
      [band(180, -4, 0.9), band(350, -3, 1.3), band(1900, 3.5, 0.8), band(4500, -3, 1), band(8000, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['metalcore-modern-aggressive-mids', 'Metalcore', 'Modern', 'Modern Metalcore: Aggressive Mids', 'POST-AMP',
    'Post-amp. A forward, angry midrange for riffs that need to punch through a busy mix. Scoop the midrange back if it feels crowded.',
    makeEq([80, 2, 13500, 2, 110, -2, 7200, -2.5,
      [band(220, -3, 1), band(450, -1.5, 1), band(1600, 4, 0.8), band(3200, 2, 1), band(8000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['metalcore-modern-wide-rhythm', 'Metalcore', 'Modern', 'Modern Metalcore: Wide Rhythm', 'POST-AMP',
    'Post-amp. Broad low-mid shaping for wall-of-guitar rhythm tones. Use one guitar per side rather than stacking several bright takes.',
    makeEq([105, 3, 9000, 2, 100, -4, 6500, -2,
      [band(180, -4.5, 0.8), band(380, -3.5, 1.2), band(1600, 2.5, 0.7), band(3500, -1, 1), band(7500, -3, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Metalcore / Melodic ---------------------------------------------------
  ['metalcore-melodic-cut-through', 'Metalcore', 'Melodic', 'Melodic Metalcore: Cut Through', 'POST-AMP',
    'Post-amp. Articulate chords with enough midrange to stay audible when two rhythm guitars are doubled.',
    makeEq([95, 2, 14000, 2, 150, -1.5, 8000, -2,
      [band(260, -2.5, 1), band(700, -1.5, 1.2), band(2400, 3.5, 0.8), band(6000, -3, 1), band(10000, -2, 0.7)],
      0]), false, NEUTRAL_MACROS],
  ['metalcore-melodic-lead-presence', 'Metalcore', 'Melodic', 'Melodic Metalcore: Lead Presence', 'POST-AMP',
    'Post-amp. Smooth, singing lead tone with a controlled top end for harmonised melodies and sustained notes.',
    makeEq([95, 2, 16000, 1, 150, -1, 6000, 1.5,
      [band(280, -2, 1), band(700, -2.5, 1.1), band(2800, 3.5, 0.8), band(5500, 1.5, 1.1), band(9500, -1.5, 0.7)],
      0]), false, NEUTRAL_MACROS],

  //--- Metalcore / Progressive ----------------------------------------------
  ['metalcore-progressive-djent-tight', 'Metalcore', 'Progressive', 'Progressive Metalcore: Djent Tight', 'POST-AMP',
    'Post-amp. Fast, percussive low-end response for palm-muted syncopation. The high-pass is intentionally steep.',
    makeEq([120, 3, 10000, 2, 90, -4, 6500, -1.5,
      [band(160, -5, 0.8), band(300, -3, 1.1), band(1200, 1.5, 0.8), band(2800, 3.5, 0.7), band(6500, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['metalcore-progressive-open-clean', 'Metalcore', 'Progressive', 'Progressive Metalcore: Open Clean', 'POST-AMP',
    'Post-amp. An open, ambient clean foundation for layered passages. Use the output trim to match the level of your distortion tone.',
    makeEq([70, 1, 18000, 1, 160, 1, 8000, 2,
      [band(220, -1.5, 1), band(500, 1, 1), band(1800, 2, 0.8), band(4000, 1.5, 0.9), band(10000, 1, 0.7)],
      0]), false, NEUTRAL_MACROS],

  //--- Thrash / Classic ------------------------------------------------------
  ['thrash-classic-midrange-punch', 'Thrash', 'Classic', 'Classic Thrash: Midrange Punch', 'POST-AMP',
    'Post-amp. The old-school recipe: tight lows, a strong midrange push and enough top-end bite for fast alternate picking.',
    makeEq([75, 2, 15000, 2, 130, -2, 8000, -1,
      [band(250, -3.5, 1), band(700, -2, 1.1), band(1800, 4, 0.8), band(3500, 1.5, 1), band(8000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['thrash-classic-deep-scoop', 'Thrash', 'Classic', 'Classic Thrash: Deep Scoop', 'POST-AMP',
    'Post-amp. A wide, scooped rhythm tone. Keep some midrange if you need the guitar to survive a dense mix.',
    makeEq([80, 2, 16000, 2, 100, -3, 6500, -2,
      [band(220, -3, 1), band(500, -5, 1.2), band(1400, -2, 0.9), band(3200, 2, 1), band(8500, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['thrash-classic-bright-attack', 'Thrash', 'Classic', 'Classic Thrash: Bright Attack', 'POST-AMP',
    'Post-amp. A bright, immediate attack for fast riffs. Back the high shelf down if the amp is already bright.',
    makeEq([70, 2, 19000, 1, 180, -1, 7500, 2.5,
      [band(300, -2, 1), band(800, -1, 1), band(1800, 4, 0.8), band(4500, 2.5, 1.1), band(9000, -1, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Thrash / Technical ----------------------------------------------------
  ['thrash-technical-clarity', 'Thrash', 'Technical', 'Thrash: Technical Clarity', 'POST-AMP',
    'Post-amp. Separates fast, intricate picking from chord work by cleaning the low mids and adding definition around 3 kHz.',
    makeEq([90, 2, 16000, 2, 120, -2, 7000, 0,
      [band(200, -3, 1), band(500, -3, 1.2), band(2800, 4, 0.8), band(5000, 1, 1.1), band(9000, -1.5, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['thrash-technical-fast-picking', 'Thrash', 'Technical', 'Thrash: Fast Picking', 'POST-AMP',
    'Post-amp. A percussive attack with a tight low end for tremolo and alternate picking at speed.',
    makeEq([100, 3, 14000, 2, 110, -2, 7500, -1.5,
      [band(200, -3, 1), band(600, -1.5, 1), band(2600, 4, 0.8), band(4800, -2, 1.1), band(8500, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Thrash / Modern -------------------------------------------------------
  ['thrash-modern-tight-rhythm', 'Thrash', 'Modern', 'Modern Thrash: Tight Rhythm', 'POST-AMP',
    'Post-amp. Modern tightness without losing the midrange snarl that makes thrash riffs read.',
    makeEq([85, 3, 13000, 2, 120, -2.5, 7000, -1,
      [band(240, -4, 1), band(450, -2, 1.2), band(2000, 3, 0.8), band(4000, 0.5, 1), band(8000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['thrash-modern-precision-bite', 'Thrash', 'Modern', 'Modern Thrash: Precision and Bite', 'POST-AMP',
    'Post-amp. A dry, precise attack for layered rhythm parts. The low-pass keeps high-gain fizz under control.',
    makeEq([95, 2, 12000, 2, 110, -2.5, 8000, 0.5,
      [band(220, -3.5, 1), band(500, -1, 1), band(2200, 3.5, 0.8), band(4500, 1.5, 1), band(8500, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Utility / Pre-Amp -----------------------------------------------------
  ['utility-preamp-overdrive-tightener', 'Utility', 'Pre-Amp', 'Pre-Amp: Overdrive Tightener', 'PRE-AMP',
    'Place BEFORE your amp/cab. Keeps the low end out of the preamp, lifts the upper mids and takes the edge off above 5 kHz.',
    makeEq([40, 1, 20000, 0, 120, -4, 5500, -3.5,
      [band(300, -2, 1), band(720, 4.5, 0.8), band(1200, -1, 1.2), band(3000, 1.5, 1), band(9000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['utility-preamp-clean-boost', 'Utility', 'Pre-Amp', 'Pre-Amp: Clean Boost', 'PRE-AMP',
    'Place BEFORE your amp/cab. A mostly flat level lift with a little upper-mid push; use the output trim to drive the amp input.',
    makeEq([30, 1, 20000, 0, 90, -1, 5000, 1.5,
      [band(250, -2, 1), band(700, 3, 0.9), band(1200, -0.5, 1), band(2500, 2, 1), band(7000, -1, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Utility / Post-Amp ----------------------------------------------------
  ['utility-postamp-mix-cleanup', 'Utility', 'Post-Amp', 'Post-Amp: Mix Cleanup', 'POST-AMP',
    'Post-amp. The general cleanup starting point: rumble out with the high-pass, mud out around 300-550 Hz, attack back around 2.4 kHz.',
    makeEq([75, 2, 15000, 2, 120, -1.5, 7000, -1,
      [band(300, -2.5, 1), band(550, -2, 1.2), band(2400, 2, 0.9), band(4500, -1, 1), band(9000, -1.5, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['utility-postamp-fizz-control', 'Utility', 'Post-Amp', 'Post-Amp: Fizz Control', 'POST-AMP',
    'Post-amp. A corrective curve for fizzy high-gain tones: tame the harsh region without dulling the pick attack.',
    makeEq([70, 2, 10000, 2, 130, -1, 6500, -3.5,
      [band(280, -2, 1), band(700, -1, 1), band(2200, 1.5, 0.9), band(5200, -3, 1.1), band(8500, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Utility / Reference ---------------------------------------------------
  ['utility-low-tuning-extended-range', 'Utility', 'Reference', 'Low Tuning: Extended Range', 'POST-AMP',
    'Post-amp. A starting point for baritone and extended-range guitars: a low high-pass, tight subs and a controlled top end.',
    makeEq([55, 3, 11000, 2, 90, -3, 6000, -2,
      [band(140, -4, 0.9), band(300, -3, 1.1), band(1600, 3, 0.8), band(3800, -2, 1), band(7500, -3, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['utility-reference-dark-cab', 'Utility', 'Reference', 'Reference: Dark Cab', 'POST-AMP',
    'Post-amp. A dark, mid-focused reference curve for comparing a bright amp or IR without harshness.',
    makeEq([80, 1, 9000, 2, 140, 0, 6000, -3,
      [band(180, -2, 1), band(400, -1, 1), band(1200, 1, 1), band(3000, -1, 1), band(7000, -3, 0.8)],
      0]), false, NEUTRAL_MACROS],
  ['utility-reference-bright-amp', 'Utility', 'Reference', 'Reference: Bright Amp', 'POST-AMP',
    'Post-amp. A bright, open reference curve for comparing a dark amp or recording chain.',
    makeEq([90, 2, 16000, 1, 120, -1, 8000, 3,
      [band(250, -2, 1), band(600, -1, 1), band(2200, 2, 0.8), band(4500, 1, 1), band(9000, 2, 0.8)],
      0]), false, NEUTRAL_MACROS],

  //--- Utility / Neutral -----------------------------------------------------
  ['utility-neutral-flat', 'Utility', 'Neutral', 'Neutral: Flat EQ', 'EITHER',
    'A completely neutral EQ: no filters, no trim, macros centred or off. Use it as a clean slate or a level-matched bypass reference.',
    neutralEq(), true, NEUTRAL_MACROS],
];

export const FACTORY_PRESETS: FactoryPresetDef[] = rows.map((r, i) => ({
  index: i + 1,
  id: r[0],
  genre: r[1],
  subgenre: r[2],
  name: r[3],
  placement: r[4],
  description: r[5],
  eq: r[6],
  setsMacros: r[7],
  macros: r[8],
}));

/** Mirrors metaleq::analyzerTargets. */
export const ANALYZER_TARGETS: AnalyzerTargetInfo[] = [
  { index: 0, presetId: 'metalcore-2000s-tight-rhythm', label: '2000s Metalcore: Tight Rhythm' },
  { index: 1, presetId: 'thrash-technical-clarity', label: 'Thrash: Technical Clarity' },
  { index: 2, presetId: 'thrash-classic-midrange-punch', label: 'Classic Thrash: Midrange Punch' },
  { index: 3, presetId: 'utility-postamp-mix-cleanup', label: 'Post-Amp: Mix Cleanup' },
];

export const SHAPES: ShapeDef[] = [
  { name: 'TIGHTEN', description: 'Tightens excess lows and clears low-mid looseness.' },
  { name: 'SCOOP', description: 'Applies a broad midrange scoop; use gently in dense mixes.' },
  { name: 'MID PUNCH', description: 'Adds forward midrange body and authority.' },
  { name: 'PICK ATTACK', description: 'Adds pick definition around the attack region.' },
  { name: 'FIZZ TAMER', description: 'Reduces fizz and harshness while retaining pick attack.' },
  { name: 'BRIGHTEN', description: 'Adds controlled presence to a dark guitar tone.' },
];

/** Convenience for components that only need snapshot-shaped metadata. */
export function factoryPresetInfos(): FactoryPresetInfo[] {
  return FACTORY_PRESETS.map(({ index, id, name, genre, subgenre, placement, description }) => ({
    index, id, name, genre, subgenre, placement, description,
  }));
}

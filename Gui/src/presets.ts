/*
    Factory preset table for browser development only — a 1:1 port of
    Source/dsp/MetalEqPresets.cpp (16 presets + 7 shapes). The plugin itself
    gets its preset list from the native snapshot; this file is never compiled
    into the binary.

    Index semantics match the bridge: factory entries are reported with
    index = i + 1 (0 is reserved for Custom).
*/

import type { EqSettings, MacroSettings } from './dspMirror';

export interface FactoryPresetDef {
  index: number; // i + 1
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
    lpEnabled: true,
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
  e.lsFreq = p[4]; e.lsGainDb = p[5]; e.hsFreq = p[6]; e.hsGainDb = p[7];
  e.bands = [...p[8]];
  e.trimDb = p[9];
  return e;
}

type PresetRow = [
  name: string, placement: string, description: string,
  eq: EqSettings, setsMacros: boolean, macros: MacroSettings,
];

const rows: PresetRow[] = [
  ['2000s Metalcore - Tight Rhythm', 'POST-AMP',
    'Post-amp. Tight low end, controlled low mids, aggressive attack and restrained fizz. The safe starting point for palm-muted rhythm.',
    makeEq([85, 2, 12000, 2, 120, -2.5, 7000, -1.5,
      [band(240, -3.5, 1), band(420, -2, 1.4), band(2200, 3, 0.9), band(5200, -2, 1.2), band(8500, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],

  ['Melodic Metalcore - Cut Through', 'POST-AMP',
    'Post-amp. Articulate chords with enough midrange to stay audible when two rhythm guitars are doubled.',
    makeEq([95, 2, 14000, 2, 150, -1.5, 8000, -2,
      [band(260, -2.5, 1), band(700, -1.5, 1.2), band(2400, 3.5, 0.8), band(6000, -3, 1), band(10000, -2, 0.7)],
      0]), false, NEUTRAL_MACROS],

  ['Modern Metalcore - Drop Tuning', 'POST-AMP',
    'Post-amp. A 36 dB/oct high-pass at 70 Hz keeps the low notes of a drop tuning separate instead of rumbling into each other.',
    makeEq([70, 3, 11000, 2, 100, -3.5, 6000, -2,
      [band(180, -4, 0.9), band(350, -3, 1.3), band(1900, 3.5, 0.8), band(4500, -3, 1), band(8000, -2.5, 0.8)],
      0]), false, NEUTRAL_MACROS],

  ['Sneap-Inspired - Precision', 'POST-AMP',
    'Post-amp. Inspired by the general clarity of modern tight high-gain mixing, not by any specific proprietary setting.',
    makeEq([100, 2, 13000, 2, 130, -3, 7500, -2.5,
      [band(250, -3, 1.1), band(500, -2.5, 1.5), band(2100, 2.5, 1), band(5500, -2.5, 1.1), band(9000, -2.5, 0.7)],
      0]), false, NEUTRAL_MACROS],

  ['Richardson-Inspired - Aggressive Mids', 'POST-AMP',
    "Post-amp. Inspired by the general 'forward and aggressive' metal mixing style, not by any specific proprietary setting.",
    makeEq([95, 2, 15000, 1, 160, -2, 8000, 0,
      [band(300, -4.5, 0.9), band(700, -2, 1.2), band(1800, 4, 0.8), band(3200, 1.5, 1), band(7000, -3, 1)],
      0]), false, NEUTRAL_MACROS],

  ['Metalcore Lead - Presence', 'POST-AMP',
    'Post-amp. A scooped lead tone: the low mids are out of the way so the note is audible, and the top end is open rather than harsh.',
    makeEq([90, 2, 16000, 1, 140, -2, 6000, 1.5,
      [band(300, -3, 1), band(650, -3.5, 0.9), band(2800, 3, 0.9), band(5500, 1.5, 1), band(9000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],

  ['Thrash - Technical Clarity', 'POST-AMP',
    'Post-amp. Fast, precise picking with every note separated and a controlled low end.',
    makeEq([90, 2, 15000, 2, 120, -2.5, 7500, -2,
      [band(220, -3, 1), band(550, -2, 1.3), band(2500, 3.5, 0.7), band(6000, -2, 1), band(9500, -2.5, 0.7)],
      0]), false, NEUTRAL_MACROS],

  ['Classic Thrash - Midrange Punch', 'POST-AMP',
    'Post-amp. Aggressive and mid-forward with the top left open: a 1980s-style rhythm starting point.',
    makeEq([75, 2, 18000, 1, 180, -1.5, 7000, -1,
      [band(280, -2, 1), band(620, -1.5, 1.2), band(1600, 4.5, 0.7), band(3600, 2, 1), band(7500, -2.5, 1)],
      0]), false, NEUTRAL_MACROS],

  ['Deep Scoop - Thrash', 'POST-AMP',
    'Post-amp. A pronounced broad midrange dip (driven by the SCOOP macro so you can widen or close it) with enough low-end body and upper-mid attack to stay a rhythm tone.',
    makeEq([80, 2, 14000, 2, 200, -2, 7000, -2,
      [band(200, -2.5, 0.9), band(750, -2, 1), band(2000, 3.5, 0.8), band(5000, -2, 1), band(8500, -2.5, 0.8)],
      0]), true, { tight: 0, body: 0.5, scoop: 0.85, bite: 0, fizzCut: 0 }],

  ['Thrash - Tight Modern Rhythm', 'POST-AMP',
    'Post-amp. Disciplined low end, sharp articulation and highs that stay under control at high gain.',
    makeEq([110, 2, 12000, 2, 130, -3.5, 6500, -2.5,
      [band(240, -4, 0.9), band(450, -2.5, 1.4), band(2300, 3, 0.8), band(5500, -2.5, 1), band(9000, -2.5, 0.7)],
      0]), false, NEUTRAL_MACROS],

  ['Megadeth-Inspired - Precision and Bite', 'POST-AMP',
    'Post-amp. Inspired by the general definition and upper-mid bite of that style, not by any specific proprietary setting.',
    makeEq([100, 2, 16000, 1, 140, -2, 7500, -2.5,
      [band(260, -3.5, 1), band(480, -2, 1.2), band(2600, 4, 0.8), band(5200, -1.5, 1.1), band(9000, -3, 0.7)],
      0]), false, NEUTRAL_MACROS],

  ['Metallica-Inspired - Heavy Rhythm', 'POST-AMP',
    'Post-amp. Inspired by the general full, mid-forward rhythm guitar sound of that era, not by any specific proprietary setting.',
    makeEq([70, 2, 17000, 1, 190, -1, 6500, -1.5,
      [band(300, -2.5, 0.9), band(800, 0, 1), band(1500, 3.5, 0.8), band(2800, 1.5, 1), band(5500, -2, 1)],
      0]), false, NEUTRAL_MACROS],

  ['Pre-Amp - Overdrive Tightener', 'PRE-AMP',
    'PRE-AMP: place this BEFORE your amp/cab. It keeps low end for the preamp to distort, cuts the bottom, lifts around 700 Hz and takes the edge off above 5 kHz - a classic overdrive contour.',
    makeEq([40, 1, 20000, 0, 120, -4, 5500, -3.5,
      [band(300, -2, 1), band(720, 4.5, 0.8), band(1200, -1, 1.2), band(3000, 1.5, 1), band(9000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],

  ['Post-Amp - Mix Cleanup', 'POST-AMP',
    'Post-amp. The general cleanup starting point: rumble out with the high-pass, muddiness out around 300-550 Hz, attack back around 2.4 kHz and the harsh 5-9 kHz region tamed.',
    makeEq([90, 2, 12000, 2, 130, -2.5, 7000, -2,
      [band(300, -4, 0.9), band(550, -2, 1.3), band(2400, 2, 0.9), band(5500, -3, 1), band(9000, -2, 0.8)],
      0]), false, NEUTRAL_MACROS],

  ['Fizz Control', 'POST-AMP',
    'Post-amp. For amp/cabinet combinations that are too harsh. Cuts the 5-11 kHz region where fizz lives while leaving the attack at 2-3 kHz alone.',
    makeEq([90, 2, 15000, 1, 150, -2, 6500, -4.5,
      [band(250, -2, 1), band(450, -2, 1.2), band(5000, -3.5, 1), band(7000, -3, 0.8), band(10000, -2, 0.7)],
      0]), false, NEUTRAL_MACROS],

  ['Neutral - Flat EQ', 'EITHER',
    'Fully transparent: no filters, no trim, macros at neutral. Useful for A/B-ing any other preset or for a known-good starting point.',
    neutralEq(), true, NEUTRAL_MACROS],
];

export const FACTORY_PRESETS: FactoryPresetDef[] = rows.map((r, i) => ({
  index: i + 1,
  name: r[0],
  placement: r[1],
  description: r[2],
  eq: r[3],
  setsMacros: r[4],
  macros: r[5],
}));

export const SHAPES: ShapeDef[] = [
  { name: 'TIGHTEN', description: 'Removes low-end and low-mid looseness: raises the high-pass, takes weight out below 150 Hz and clears mud around 250-500 Hz.' },
  { name: 'SCOOP', description: 'A broad, controllable midrange reduction centred around 550 Hz so the guitar sits behind a wall of guitars. Restore attack at 1.8 kHz.' },
  { name: 'MID PUNCH', description: 'Restores the midrange that gives a guitar body and authority: clears boxiness around 320 Hz and pushes 1.6 kHz forward.' },
  { name: 'PICK ATTACK', description: 'Emphasises articulation and string definition around 2.6 kHz with a little air, while clearing low mids that blur the pick.' },
  { name: 'FIZZ TAMER', description: 'Reduces unpleasant high-frequency harshness in the 5-11 kHz region without touching the 2-3 kHz attack.' },
  { name: 'BRIGHTEN', description: 'Adds controlled presence when the tone is too dark: a high-shelf lift plus a little around 3 kHz and 10 kHz.' },
  { name: 'RESET', description: 'Back to a completely neutral EQ: no filters, no trim, macros centred or off. This is the audible bypass of the EQ itself.' },
];

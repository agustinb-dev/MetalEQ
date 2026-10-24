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
    surgical: [
      { freq: 250, gainDb: 0, q: 7, on: false },
      { freq: 620, gainDb: 0, q: 7, on: false },
      { freq: 1200, gainDb: 0, q: 7, on: false },
      { freq: 2400, gainDb: 0, q: 7, on: false },
      { freq: 4000, gainDb: 0, q: 7, on: false },
      { freq: 6200, gainDb: 0, q: 7, on: false },
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

/**
 * Browser-preview mirror of metaleq::tone::getToneTarget(). The native side is
 * authoritative in the plugin; this table only feeds the mock bridge and keeps
 * the Vite preview honest about categories and provenance.
 */
export const ANALYZER_TARGETS: AnalyzerTargetInfo[] = [
  {
    index: 0, id: 'genre-metalcore-2000s', label: '2000s Metalcore — Tight Rhythm',
    shortLabel: '2000s Metalcore', category: 0, categoryLabel: 'Genres',
    albumPeriod: 'Killswitch Engage, The End of Heartache (2004); Bullet for My Valentine, The Poison (2005)',
    objective: 'Tight lows, a deliberate low-mid scoop, aggressive upper-mid bite and controlled fizz.',
    limitations: 'Representative records, not a genre average. The exact album EQ is undocumented; values are estimates.',
    confidence: 0.72,
    sources: [{
      title: "Killswitch Engage's 8 tips on mastering metalcore (MusicRadar)",
      url: 'https://www.musicradar.com/news/guitars/killswitch-engages-8-tips-on-mastering-metalcore-594489',
      date: '2014-02-14',
      claim: 'Adam D/Joel describe a mid-range-forward tone with extra low end and an 808 to remove flubbiness.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 1, id: 'genre-metalcore-modern', label: 'Modern Metalcore — Dense & Defined',
    shortLabel: 'Modern Metalcore', category: 0, categoryLabel: 'Genres',
    albumPeriod: 'Architects, Holy Hell (2018); Spiritbox, Eternal Blue (2021)',
    objective: 'Tight, defined lows, more low-mid body than the 2000s scoop, controlled upper-mids and a smoother top.',
    limitations: 'The selected records differ substantially. This is a conservative intersection, not a claim that all modern metalcore matches.',
    confidence: 0.65,
    sources: [{
      title: "Spiritbox guitarist Mike Stringer's 13 tips for using guitar plugins (MusicRadar)",
      url: 'https://www.musicradar.com/news/spiritbox-mike-stringer-plugins',
      date: '2021-09-17',
      claim: 'Barely any gain; roll off lows but keep weight; bright, clear top; double-tracked plugin guitars.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 2, id: 'genre-metalcore-lead', label: 'Lead Metalcore — Cut Through',
    shortLabel: 'Lead Metalcore', category: 0, categoryLabel: 'Genres',
    albumPeriod: 'Bullet for My Valentine, The Poison (2005); Trivium, Ascendancy (2005)',
    objective: 'More low-mid body and upper-mid presence than a rhythm target, with a controlled top for articulation.',
    limitations: 'No isolated lead-guitar reference was available; this is an arrangement-space interpretation, not a measured lead tone.',
    confidence: 0.45,
    sources: [{
      title: 'Joey Sturgis: The Real "Secret Sauce" (URM Academy)',
      url: 'https://urm.academy/ep150-dear-joey/',
      date: '2017-07-24',
      claim: 'Heavily distorted guitars become linear; low lead notes need automation/EQ so they do not fight bass and kick.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 3, id: 'genre-thrash-black-album', label: 'Classic Thrash — The Black Album (1991)',
    shortLabel: 'Black Album', category: 0, categoryLabel: 'Genres',
    albumPeriod: 'Metallica, Metallica (The Black Album, 1991)',
    objective: 'Thick, controlled low end, real low-mid weight, a broad mid scoop in the setup and a smooth top.',
    limitations: 'A multi-guitar, multi-cabinet production, not a single EQ curve; values are estimates.',
    confidence: 0.70,
    sources: [{
      title: "Classic Tracks: Metallica's \"Enter Sandman\" (Mix)",
      url: 'https://www.mixonline.com/recording/classic-tracks-metallicas-enter-sandman-366232',
      date: '2010-10-25',
      claim: 'Hetfield used a Mesa-Boogie plus other amps with a scooped tone, 9-11 cabinets, room mics and uncompressed guitars.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 4, id: 'genre-thrash-megadeth', label: 'Classic Thrash — Megadeth, Rust in Peace (1990)',
    shortLabel: 'Megadeth', category: 0, categoryLabel: 'Genres',
    albumPeriod: 'Megadeth, Rust in Peace (1990)',
    objective: 'Tight, dry, articulate technical thrash: lean lows, controlled low-mids, strong upper-mid attack.',
    limitations: 'Direct tonal documentation is sparse; the profile is derived from production credits and context, with low confidence.',
    confidence: 0.40,
    sources: [{
      title: 'Rust in Peace (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/Rust_in_Peace',
      date: 'accessed 2026-10-24',
      claim: 'Produced by Dave Mustaine and Mike Clink, mixed by Max Norman, recorded at Rumbo Recorders.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 5, id: 'genre-thrash-ajfa', label: 'Classic Thrash — Metallica, ...And Justice for All (1988)',
    shortLabel: '…And Justice for All', category: 0, categoryLabel: 'Genres',
    albumPeriod: 'Metallica, ...And Justice for All (1988)',
    objective: 'Dry, tight and in-your-face: lean lows, controlled low-mids, strong upper-mid/treble attack and no room.',
    limitations: 'The guitar sound is documented only qualitatively; specific frequencies are not. The bass was deliberately buried.',
    confidence: 0.60,
    sources: [{
      title: "Metallica's 'And Justice For All' (Steve Thompson Productions)",
      url: 'https://stevethompsonproductions.com/news/metallicas-and-justice-for-all',
      date: '2018-11-08',
      claim: 'The band was meticulous and tight; mixed on an SSL G series straight to tape, with no Pro Tools.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 6, id: 'artist-bullet-for-my-valentine', label: 'Bullet for My Valentine — The Poison (2005)',
    shortLabel: 'Bullet for My Valentine', category: 1, categoryLabel: 'Artists',
    albumPeriod: 'Bullet for My Valentine, The Poison (2005)',
    objective: 'Tight British metalcore rhythm guitars with a strong upper-mid/presence signature and a 5150 bite.',
    limitations: "Only The Poison; the band's sound changed substantially after this period.",
    confidence: 0.55,
    sources: [{
      title: 'The Poison (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/The_Poison_(Bullet_for_My_Valentine_album)',
      date: 'accessed 2026-10-24',
      claim: 'Produced and mixed by Colin Richardson at The Chapel; engineered by Dan Turner.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 7, id: 'artist-trivium', label: 'Trivium — Ascendancy (2005)',
    shortLabel: 'Trivium', category: 1, categoryLabel: 'Artists',
    albumPeriod: 'Trivium, Ascendancy (2005)',
    objective: 'Tight, articulate thrash-influenced metalcore with controlled low-mids and strong upper-mid cut.',
    limitations: 'No direct Ascendancy guitar-tone interview was obtained; based on credits and documented mix approach.',
    confidence: 0.45,
    sources: [{
      title: 'Ascendancy (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/Ascendancy_(album)',
      date: 'accessed 2026-10-24',
      claim: 'Produced by Jason Suecof and Matt Heafy; mixed and mastered by Andy Sneap.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 8, id: 'artist-as-i-lay-dying', label: 'As I Lay Dying — An Ocean Between Us (2007)',
    shortLabel: 'As I Lay Dying', category: 1, categoryLabel: 'Artists',
    albumPeriod: 'As I Lay Dying, An Ocean Between Us (2007)',
    objective: 'Tight low end, controlled low-mid scoop, sharp upper-mid attack and a darker top than brighter 2000s records.',
    limitations: 'A collaboration between Adam Dutkiewicz, Colin Richardson and Andy Sneap; this describes the record, not one producer.',
    confidence: 0.50,
    sources: [{
      title: 'An Ocean Between Us (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/An_Ocean_Between_Us',
      date: 'accessed 2026-10-24',
      claim: 'Produced by Adam Dutkiewicz and AILD; mixed by Colin Richardson; guitar re-amping by Andy Sneap.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 9, id: 'artist-miss-may-i', label: 'Miss May I — Monument (2010)',
    shortLabel: 'Miss May I', category: 1, categoryLabel: 'Artists',
    albumPeriod: 'Miss May I, Monument (2010)',
    objective: 'A polished in-the-box metalcore rhythm sound: tight, scooped lows, bright upper-mids and a modern top.',
    limitations: 'The Monument era only; no direct tonal interview from Sturgis about this album was found.',
    confidence: 0.50,
    sources: [{
      title: 'Monument (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/Monument_(Miss_May_I_album)',
      date: 'accessed 2026-10-24',
      claim: 'Produced, engineered, mixed and mastered by Joey Sturgis.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 10, id: 'producer-andy-sneap', label: 'Andy Sneap — Tight & Transparent',
    shortLabel: 'Andy Sneap', category: 2, categoryLabel: 'Producers',
    albumPeriod: 'Testament, The Formation of Damnation (2008); Megadeth, Endgame (2009); Killswitch Engage mix work',
    objective: 'Tight controlled lows, a purposeful low-mid scoop, articulate upper-mid attack and a smooth top.',
    limitations: 'A producer profile, not one curve. Specific EQ moves come from secondary production analysis.',
    confidence: 0.60,
    sources: [{
      title: 'Andy Sneap: The More Things Change... (Tape Op #144)',
      url: 'https://tapeop.com/interviews/144/andy-sneap',
      date: '2021-07',
      claim: "Sneap's mixes are described as tight, snarling and clear, with the playing fully audible.",
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 11, id: 'producer-colin-richardson', label: 'Colin Richardson — Midrange Authority',
    shortLabel: 'Colin Richardson', category: 2, categoryLabel: 'Producers',
    albumPeriod: 'Bullet for My Valentine, The Poison (2005); As I Lay Dying, An Ocean Between Us (2007)',
    objective: 'A midrange-forward, attack-led metal sound: tight lows, controlled fizz, strong 700 Hz body and upper-mid cut.',
    limitations: 'An interpretation across several records; not a claim that he used the same EQ on every album.',
    confidence: 0.55,
    sources: [{
      title: 'How Colin Richardson Defined The Sound of Modern Metal (Nail The Mix)',
      url: 'https://www.nailthemix.com/colin-richardson',
      date: '2025-08-06',
      claim: 'Secondary analysis: midrange authority and attack, less gain, HPF, controlled fizz and an attack boost.',
      evidence: 1, evidenceLabel: 'secondary analysis',
    }],
  },
  {
    index: 12, id: 'producer-bob-rock', label: 'Bob Rock — Weight & Smooth Top',
    shortLabel: 'Bob Rock', category: 2, categoryLabel: 'Producers',
    albumPeriod: 'Metallica, The Black Album (1991); Mötley Crüe, Dr. Feelgood (1989) as low-end context',
    objective: 'Generous low-end weight, present low-mids, a warmer midrange and a smooth, uncompressed top.',
    limitations: 'Evidence centres on The Black Album, which is also a genre target; Dr. Feelgood is low-end context only.',
    confidence: 0.65,
    sources: [{
      title: 'Metallica The Black Album track-by-track interview with Bob Rock (MusicRadar)',
      url: 'https://www.musicradar.com/news/bob-rock-metallica-black-album-interview',
      date: '2021-08',
      claim: 'The record added a rich low-end punch; the band tracked live and tuned down to D for Sad But True.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 13, id: 'producer-flemming-rasmussen', label: 'Flemming Rasmussen — Mid-Focused Thrash',
    shortLabel: 'Flemming Rasmussen', category: 2, categoryLabel: 'Producers',
    albumPeriod: 'Metallica, Master of Puppets (1986)',
    objective: 'Mid-focused, tight thrash: solid midrange body, controlled lows, articulate upper-mids and a smooth top.',
    limitations: 'Gear, mic technique and a studio EQ insert are documented, but not its frequencies.',
    confidence: 0.60,
    sources: [{
      title: "How Flemming Rasmussen produced Metallica's classic Master Of Puppets (MusicRadar)",
      url: 'https://www.musicradar.com/artists/producers-engineers/all-the-albums-i-did-with-metallica-were-recorded-on-24-track-analogue-tape-theres-not-a-computer-in-sight-how-flemming-rasmussen-produced-metallicas-classic-master-of-puppets',
      date: '2025-05-01',
      claim: 'Mesa/Boogie Mark II C+ into a Marshall power amp; two SM57s plus DPA room mics and AKG Gold tubes; a studio parametric EQ insert.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 14, id: 'producer-mike-clink', label: 'Mike Clink — Raw Hard-Rock Midrange',
    shortLabel: 'Mike Clink', category: 2, categoryLabel: 'Producers',
    albumPeriod: "Guns N' Roses, Appetite for Destruction (1987)",
    objective: 'Raw, midrange-forward hard rock: moderate gain, tight lows, strong 700 Hz body and a smooth top.',
    limitations: 'A hard-rock profile, not a metalcore one; no direct Clink EQ interview was found.',
    confidence: 0.45,
    sources: [{
      title: 'Appetite for Destruction (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/Appetite_for_Destruction',
      date: 'accessed 2026-10-24',
      claim: 'Produced by Mike Clink; Slash used a Les Paul copy with Seymour Duncan Alnico II pickups into a Marshall.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
  {
    index: 15, id: 'producer-max-norman', label: 'Max Norman — Polished Thrash Weight',
    shortLabel: 'Max Norman', category: 2, categoryLabel: 'Producers',
    albumPeriod: 'Megadeth, Countdown to Extinction (1992); Ozzy Osbourne, Diary of a Madman (1981) as guitar-layering context',
    objective: 'A polished, weighty metal sound: fuller lows than Rust in Peace, present low-mids, clear upper-mids and a smooth top.',
    limitations: 'Evidence is thin and spans two different productions; this is a low-confidence interpretation.',
    confidence: 0.40,
    sources: [{
      title: 'Countdown to Extinction (Wikipedia, credits adapted from liner notes)',
      url: 'https://en.wikipedia.org/wiki/Countdown_to_Extinction',
      date: 'accessed 2026-10-24',
      claim: 'Produced and mixed by Max Norman; a more groove/melody-oriented Megadeth record.',
      evidence: 0, evidenceLabel: 'documented',
    }],
  },
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

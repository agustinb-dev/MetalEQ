/*
    Browser-only mirror of the DSP chain in Source/dsp/MetalEqDsp.h.

    THIS FILE IS NEVER PART OF THE PLUGIN BINARY. It exists so the mock bridge
    (browser development via `npm run dev`, and headless screenshots) can draw
    the same response curve the native side would compute. The shipped plugin
    always receives its curve from the native bridge, which uses the real C++
    metaleq::buildChain + chainResponseDb.

    Ported 1:1: same clamps, same filter order, same crossover compensation.
*/

export const GRAPH_MIN_FREQ = 20;
export const GRAPH_MAX_FREQ = 20000;
export const NUM_BANDS = 5;

const K_PI = 3.14159265358979323846;
const BUTTERWORTH_Q = 0.7071067811865475244;

export type FilterType = 'highPass' | 'lowPass' | 'lowShelf' | 'highShelf' | 'peak';

export interface FilterSpec {
  type: FilterType;
  freq: number;
  q: number;
  gainDb: number;
}

export interface ChainSpec {
  filters: FilterSpec[];
  trimDb: number;
}

export interface BandSettings {
  freq: number;
  gainDb: number;
  q: number;
  on: boolean;
}

export interface EqSettings {
  hpFreq: number;
  lpFreq: number;
  hpSlope: number;
  lpSlope: number;
  lpEnabled: boolean;
  lsFreq: number;
  lsGainDb: number;
  hsFreq: number;
  hsGainDb: number;
  bands: BandSettings[];
  shapes: boolean[]; // TIGHTEN, SCOOP, MID PUNCH, PICK ATTACK, FIZZ TAMER, BRIGHTEN
  trimDb: number;
}

export interface MacroSettings {
  tight: number;
  body: number;
  scoop: number;
  bite: number;
  fizzCut: number;
}

interface BiquadCoeffs {
  b0: number; b1: number; b2: number; a1: number; a2: number;
}

const identityCoeffs = (): BiquadCoeffs => ({ b0: 1, b1: 0, b2: 0, a1: 0, a2: 0 });

const clamp = (v: number, lo: number, hi: number) => (v < lo ? lo : v > hi ? hi : v);
const finite = (v: number) => Number.isFinite(v);

function designBiquad(
  type: FilterType,
  sampleRate: number,
  freq: number,
  q: number,
  gainDb: number,
): BiquadCoeffs {
  if (!(finite(sampleRate) && finite(freq) && finite(q) && finite(gainDb))) return identityCoeffs();
  if (sampleRate < 1000 || freq <= 0) return identityCoeffs();

  const nyquist = sampleRate * 0.5;
  freq = clamp(freq, 1, nyquist * 0.98);
  q = clamp(q, 0.05, 40);
  gainDb = clamp(gainDb, -48, 48);

  // A flat shelf or peak is a straight-through.
  if (type !== 'highPass' && type !== 'lowPass' && Math.abs(gainDb) < 1e-4) return identityCoeffs();

  const w0 = 2 * K_PI * freq / sampleRate;
  const cosW0 = Math.cos(w0);
  const sinW0 = Math.sin(w0);
  const alpha = sinW0 / (2 * q);

  let b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

  switch (type) {
    case 'highPass':
      b0 = (1 + cosW0) * 0.5;
      b1 = -(1 + cosW0);
      b2 = b0;
      a0 = 1 + alpha;
      a1 = -2 * cosW0;
      a2 = 1 - alpha;
      break;
    case 'lowPass':
      b0 = (1 - cosW0) * 0.5;
      b1 = 1 - cosW0;
      b2 = b0;
      a0 = 1 + alpha;
      a1 = -2 * cosW0;
      a2 = 1 - alpha;
      break;
    case 'peak': {
      const A = Math.pow(10, gainDb / 40);
      b0 = 1 + alpha * A;
      b1 = -2 * cosW0;
      b2 = 1 - alpha * A;
      a0 = 1 + alpha / A;
      a1 = -2 * cosW0;
      a2 = 1 - alpha / A;
      break;
    }
    case 'lowShelf': {
      const A = Math.pow(10, gainDb / 40);
      const sqrtA = Math.sqrt(A);
      const tsa = 2 * sqrtA * alpha;
      b0 = A * ((A + 1) - (A - 1) * cosW0 + tsa);
      b1 = 2 * A * ((A - 1) - (A + 1) * cosW0);
      b2 = A * ((A + 1) - (A - 1) * cosW0 - tsa);
      a0 = (A + 1) + (A - 1) * cosW0 + tsa;
      a1 = -2 * ((A - 1) + (A + 1) * cosW0);
      a2 = (A + 1) + (A - 1) * cosW0 - tsa;
      break;
    }
    case 'highShelf': {
      const A = Math.pow(10, gainDb / 40);
      const sqrtA = Math.sqrt(A);
      const tsa = 2 * sqrtA * alpha;
      b0 = A * ((A + 1) + (A - 1) * cosW0 + tsa);
      b1 = -2 * A * ((A - 1) + (A + 1) * cosW0);
      b2 = A * ((A + 1) + (A - 1) * cosW0 - tsa);
      a0 = (A + 1) - (A - 1) * cosW0 + tsa;
      a1 = 2 * ((A - 1) - (A + 1) * cosW0);
      a2 = (A + 1) - (A - 1) * cosW0 - tsa;
      break;
    }
  }

  if (!finite(a0) || Math.abs(a0) < 1e-12) return identityCoeffs();

  const invA0 = 1 / a0;
  const c: BiquadCoeffs = { b0: b0 * invA0, b1: b1 * invA0, b2: b2 * invA0, a1: a1 * invA0, a2: a2 * invA0 };
  if (!(finite(c.b0) && finite(c.b1) && finite(c.b2) && finite(c.a1) && finite(c.a2))) {
    return identityCoeffs();
  }
  return c;
}

function magnitudeDb(c: BiquadCoeffs, sampleRate: number, freq: number): number {
  if (sampleRate <= 0 || !finite(freq) || freq <= 0) return 0;

  const w = 2 * K_PI * freq / sampleRate;
  const cw = Math.cos(w);
  const c2 = Math.cos(2 * w);

  const num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2
    + 2 * (c.b0 * c.b1 + c.b1 * c.b2) * cw
    + 2 * c.b0 * c.b2 * c2;
  const den = 1 + c.a1 * c.a1 + c.a2 * c.a2
    + 2 * (c.a1 + c.a1 * c.a2) * cw
    + 2 * c.a2 * c2;

  if (den < 1e-12 || num <= 0 || !finite(num / den)) return -120;
  return 10 * Math.log10(num / den);
}

function crossoverCompensation(stages: number): number {
  if (stages === 2) return 1.5572;
  if (stages === 3) return 1.993;
  return 1;
}

function addCrossover(
  chain: ChainSpec,
  type: FilterType,
  freq: number,
  slopeChoice: number,
  sampleRate: number,
): void {
  if (slopeChoice <= 0) return;
  const stages = clamp(slopeChoice, 1, 3);
  const f = clamp(freq * crossoverCompensation(stages), 1, sampleRate * 0.49);
  for (let i = 0; i < stages; ++i) chain.filters.push({ type, freq: f, q: BUTTERWORTH_Q, gainDb: 0 });
}

/** Mirror of metaleq::buildChain. */
export function buildChain(eq: EqSettings, macros: MacroSettings, sampleRate: number): ChainSpec {
  const chain: ChainSpec = { filters: [], trimDb: 0 };

  const tight = clamp(macros.tight, 0, 1);
  const body = clamp(macros.body, 0, 1);
  const scoop = clamp(macros.scoop, 0, 1);
  const bite = clamp(macros.bite, 0, 1);
  const fizzCut = clamp(macros.fizzCut, 0, 1);

  addCrossover(chain, 'highPass', eq.hpFreq, eq.hpSlope, sampleRate);
  chain.filters.push({ type: 'lowShelf', freq: eq.lsFreq, q: BUTTERWORTH_Q, gainDb: eq.lsGainDb });

  // TIGHT: low-end weight and low-mid mud.
  if (tight > 1e-3) {
    chain.filters.push({ type: 'lowShelf', freq: 130, q: BUTTERWORTH_Q, gainDb: -6 * tight });
    chain.filters.push({ type: 'peak', freq: 300, q: 0.85, gainDb: -3.5 * tight });
  }

  // BODY: bipolar around 0.5.
  if (Math.abs(body - 0.5) > 5e-3) {
    chain.filters.push({ type: 'lowShelf', freq: 300, q: BUTTERWORTH_Q, gainDb: (body - 0.5) * 10 });
  }

  for (let i = 0; i < NUM_BANDS; ++i) {
    const b = eq.bands[i];
    if (b && b.on && Math.abs(b.gainDb) > 1e-3) {
      chain.filters.push({ type: 'peak', freq: b.freq, q: b.q, gainDb: b.gainDb });
    }
  }

  if (scoop > 1e-3) chain.filters.push({ type: 'peak', freq: 520, q: 0.5, gainDb: -7.5 * scoop });
  if (bite > 1e-3) chain.filters.push({ type: 'peak', freq: 3000, q: 0.8, gainDb: 4.5 * bite });

  chain.filters.push({ type: 'highShelf', freq: eq.hsFreq, q: BUTTERWORTH_Q, gainDb: eq.hsGainDb });

  if (fizzCut > 1e-3) {
    chain.filters.push({ type: 'highShelf', freq: 7000, q: BUTTERWORTH_Q, gainDb: -5.5 * fizzCut });
  }

  // Independent tonal-shape layers.
  if (eq.shapes[0]) {
    chain.filters.push({ type: 'lowShelf', freq: 130, q: BUTTERWORTH_Q, gainDb: -3 });
    chain.filters.push({ type: 'peak', freq: 300, q: 0.85, gainDb: -3 });
    chain.filters.push({ type: 'peak', freq: 1800, q: 1, gainDb: 1 });
  }
  if (eq.shapes[1]) {
    chain.filters.push({ type: 'peak', freq: 550, q: 0.55, gainDb: -4.5 });
    chain.filters.push({ type: 'peak', freq: 1800, q: 0.9, gainDb: 1.5 });
  }
  if (eq.shapes[2]) {
    chain.filters.push({ type: 'peak', freq: 320, q: 0.9, gainDb: -3 });
    chain.filters.push({ type: 'peak', freq: 1600, q: 0.75, gainDb: 4 });
    chain.filters.push({ type: 'peak', freq: 3000, q: 1, gainDb: 1.5 });
  }
  if (eq.shapes[3]) {
    chain.filters.push({ type: 'peak', freq: 350, q: 1, gainDb: -2 });
    chain.filters.push({ type: 'peak', freq: 2600, q: 0.7, gainDb: 4 });
    chain.filters.push({ type: 'peak', freq: 5000, q: 1, gainDb: 1.5 });
  }
  if (eq.shapes[4]) {
    chain.filters.push({ type: 'highShelf', freq: 7000, q: BUTTERWORTH_Q, gainDb: -2 });
    chain.filters.push({ type: 'peak', freq: 5200, q: 1, gainDb: -3.5 });
    chain.filters.push({ type: 'peak', freq: 8500, q: 0.8, gainDb: -2.5 });
  }
  if (eq.shapes[5]) {
    chain.filters.push({ type: 'highShelf', freq: 8000, q: BUTTERWORTH_Q, gainDb: 3 });
    chain.filters.push({ type: 'peak', freq: 3000, q: 0.9, gainDb: 2 });
    chain.filters.push({ type: 'peak', freq: 10000, q: 0.7, gainDb: 1.5 });
  }

  if (eq.lpEnabled) addCrossover(chain, 'lowPass', eq.lpFreq, eq.lpSlope, sampleRate);

  chain.trimDb = eq.trimDb;
  return chain;
}

export function chainResponseDb(chain: ChainSpec, sampleRate: number, freq: number): number {
  let total = 0;
  for (const spec of chain.filters) {
    const c = designBiquad(spec.type, sampleRate, spec.freq, spec.q, spec.gainDb);
    total += magnitudeDb(c, sampleRate, freq);
  }
  return total + chain.trimDb;
}

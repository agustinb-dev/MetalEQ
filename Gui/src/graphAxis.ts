/*
    Mirror of Source/dsp/MetalEqGraphAxis.h — log-frequency mapping and the
    non-overlapping label selection algorithm. The native EQGraph and this SVG
    graph must place identical landmarks at identical positions.
*/

import { GRAPH_MIN_FREQ, GRAPH_MAX_FREQ } from './dspMirror';

export const GUITAR_LANDMARKS = [
  20, 40, 60, 80, 100, 150, 200, 300, 400, 500, 800, 1000, 1500, 2000, 3000,
  4000, 5000, 6000, 8000, 10000, 12000, 16000, 20000,
] as const;

const clamp = (v: number, lo: number, hi: number) => (v < lo ? lo : v > hi ? hi : v);

export function frequencyToUnit(frequency: number): number {
  const f = clamp(frequency, GRAPH_MIN_FREQ, GRAPH_MAX_FREQ);
  return Math.log(f / GRAPH_MIN_FREQ) / Math.log(GRAPH_MAX_FREQ / GRAPH_MIN_FREQ);
}

export function unitToFrequency(unit: number): number {
  const u = clamp(unit, 0, 1);
  return GRAPH_MIN_FREQ * Math.pow(GRAPH_MAX_FREQ / GRAPH_MIN_FREQ, u);
}

export function dbToUnit(db: number, minDb: number, maxDb: number): number {
  return clamp((db - minDb) / (maxDb - minDb), 0, 1);
}

/** Select a non-overlapping subset of frequency labels for the available width. */
export function visibleLabelIndices(width: number, minLabelSpacing: number): number[] {
  const selected: number[] = [];
  if (width <= 0 || minLabelSpacing <= 0) return selected;

  let lastX = 0;
  selected.push(0);
  for (let i = 0; i < GUITAR_LANDMARKS.length; i++) {
    const f = GUITAR_LANDMARKS[i];
    if (f < GRAPH_MIN_FREQ || f > GRAPH_MAX_FREQ) continue;
    if (f === GRAPH_MIN_FREQ || f === GRAPH_MAX_FREQ) continue;
    const x = frequencyToUnit(f) * width;
    if (x - lastX >= minLabelSpacing && x <= width - minLabelSpacing) {
      selected.push(i);
      lastX = x;
    }
  }
  const last = GUITAR_LANDMARKS.length - 1;
  if (width - lastX < minLabelSpacing && selected.length > 1) selected.pop();
  if (width >= minLabelSpacing) selected.push(last);
  return selected;
}

export function frequencyLabel(frequency: number): string {
  const table: Record<number, string> = {
    20: '20', 40: '40', 60: '60', 80: '80', 100: '100', 150: '150', 200: '200',
    300: '300', 400: '400', 500: '500', 800: '800', 1000: '1k', 1500: '1.5k',
    2000: '2k', 3000: '3k', 4000: '4k', 5000: '5k', 6000: '6k', 8000: '8k',
    10000: '10k', 12000: '12k', 16000: '16k', 20000: '20k',
  };
  return table[frequency] ?? '';
}

/** Major landmark gridlines (same set as the native EQGraph). */
export function isMajorLandmark(hz: number): boolean {
  return hz === 20 || hz === 100 || hz === 500 || hz === 1000 || hz === 3000
      || hz === 8000 || hz === 20000;
}

/** Guitar-EQ regions as subtle backdrop cues (same table as the native graph). */
export const REGIONS: ReadonlyArray<{ from: number; to: number; label: string; fill: string }> = [
  { from: 20, to: 80, label: 'RUMBLE', fill: '#23272d' },
  { from: 80, to: 200, label: 'LOW END', fill: '#20262b' },
  { from: 200, to: 400, label: 'BODY / MUD', fill: '#24252b' },
  { from: 400, to: 1500, label: 'CHARACTER', fill: '#20252b' },
  { from: 1500, to: 4000, label: 'ATTACK / BITE', fill: '#24262b' },
  { from: 4000, to: 7000, label: 'PRESENCE', fill: '#20252b' },
  { from: 7000, to: 20000, label: 'FIZZ / TOP', fill: '#242329' },
];

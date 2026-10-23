/*
    Horizontal slider used by the HP/LP rows, the numeric band/filter
    panels and the analyzer AMOUNT control. Mirrors the native juce::Slider
    LinearHorizontal style: track left, value box right, skew-aware mapping
    identical to Knob.tsx (juce::NormalisableRange power curve).
*/

import { useCallback, useEffect, useRef, useState } from 'react';
import { clamp01 } from '../params';

interface SliderProps {
  /** Row label shown left of the track; pass '' when the row has its own label. */
  label: string;
  value: number;
  min: number;
  max: number;
  /** juce::NormalisableRange skew (1 = linear). */
  skew?: number;
  format: (v: number) => string;
  tip?: string;
  /** Called on gesture start/end so the store can suppress tick overwrites. */
  onDragStateChange?: (dragging: boolean) => void;
  onChange: (v: number) => void;
}

export function Slider({ label, value, min, max, skew = 1, format, tip, onDragStateChange, onChange }: SliderProps) {
  const [localValue, setLocalValue] = useState<number | null>(null);
  const draggingRef = useRef(false);
  const shown = localValue ?? value;

  // Drop the local override once the authoritative value arrives.
  useEffect(() => {
    if (!draggingRef.current && localValue !== null) setLocalValue(null);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [value]);

  const toNorm = useCallback(
    (v: number) => Math.pow(clamp01((v - min) / (max - min)), skew),
    [min, max, skew],
  );
  const fromNorm = useCallback(
    (p: number) => min + (max - min) * Math.pow(clamp01(p), 1 / skew),
    [min, max, skew],
  );

  return (
    <div className="slider-row" title={tip}>
      {label !== '' && <span className="slider-label">{label}</span>}
      <input
        className="slider-track"
        type="range"
        min={0}
        max={1000}
        step={1}
        aria-label={label !== '' ? label : undefined}
        value={Math.round(toNorm(shown) * 1000)}
        onPointerDown={() => {
          draggingRef.current = true;
          onDragStateChange?.(true);
        }}
        onPointerUp={() => {
          if (!draggingRef.current) return;
          draggingRef.current = false;
          setLocalValue(null);
          onDragStateChange?.(false);
        }}
        onPointerCancel={() => {
          if (!draggingRef.current) return;
          draggingRef.current = false;
          setLocalValue(null);
          onDragStateChange?.(false);
        }}
        onChange={(e) => {
          const next = fromNorm(Number(e.target.value) / 1000);
          setLocalValue(next);
          onChange(next);
        }}
      />
      <span className="slider-value">{format(shown)}</span>
    </div>
  );
}

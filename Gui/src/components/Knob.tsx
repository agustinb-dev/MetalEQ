/*
    Rotary knob with JUCE-compatible value mapping.

    Drag model mirrors juce::Slider rotary behaviour:
    * vertical drag over ~180 px covers the full normalised range
    * Shift = fine adjustment (10x slower)
    * double-click returns to the parameter default (like
      setDoubleClickReturnValue on the native macro knobs)
    * the normalised <-> plain conversion uses the NormalisableRange skew so
      the feel matches the native editor exactly.
*/

import { useCallback, useEffect, useRef, useState } from 'react';
import type { ParamSpec } from '../types';
import { fromNormalized, snapPlain, toNormalized } from '../params';

const FULL_SWEEP_PX = 180;
const ARC_START_DEG = -135;
const ARC_SWEEP_DEG = 270;

function polar(cx: number, cy: number, r: number, deg: number): [number, number] {
  const rad = ((deg - 90) * Math.PI) / 180;
  return [cx + r * Math.cos(rad), cy + r * Math.sin(rad)];
}

function arcPath(cx: number, cy: number, r: number, fromDeg: number, toDeg: number): string {
  const [x1, y1] = polar(cx, cy, r, fromDeg);
  const [x2, y2] = polar(cx, cy, r, toDeg);
  const large = Math.abs(toDeg - fromDeg) > 180 ? 1 : 0;
  return `M ${x1.toFixed(2)} ${y1.toFixed(2)} A ${r} ${r} 0 ${large} 1 ${x2.toFixed(2)} ${y2.toFixed(2)}`;
}

interface KnobProps {
  spec: ParamSpec;
  value: number;
  /** Formatted value text (e.g. "75%", "+3.0 dB"). */
  format: (v: number) => string;
  title: string;
  tip?: string;
  onChange: (v: number) => void;
  /** Called on gesture start/end so the store can suppress tick overwrites. */
  onDragStateChange?: (dragging: boolean) => void;
}

export function Knob({ spec, value, format, title, tip, onChange, onDragStateChange }: KnobProps) {
  const [localValue, setLocalValue] = useState<number | null>(null);
  const dragRef = useRef<{ startY: number; startNorm: number } | null>(null);
  const knobRef = useRef<HTMLDivElement>(null);

  const shown = localValue ?? value;

  // After the drag ends, drop the local override only once the authoritative
  // tick value has arrived (value prop changes), otherwise keep showing ours.
  useEffect(() => {
    if (dragRef.current === null && localValue !== null) {
      setLocalValue(null);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [value]);

  const handlePointerDown = useCallback(
    (e: React.PointerEvent) => {
      if (e.button !== 0) return;
      e.preventDefault();
      (e.target as Element).setPointerCapture(e.pointerId);
      dragRef.current = { startY: e.clientY, startNorm: toNormalized(spec, value) };
      onDragStateChange?.(true);
    },
    [spec, value, onDragStateChange],
  );

  const handlePointerMove = useCallback(
    (e: React.PointerEvent) => {
      const drag = dragRef.current;
      if (!drag) return;
      const fine = e.shiftKey ? 0.1 : 1;
      const delta = ((drag.startY - e.clientY) / FULL_SWEEP_PX) * fine;
      const norm = Math.min(1, Math.max(0, drag.startNorm + delta));
      const next = snapPlain(spec, fromNormalized(spec, norm));
      setLocalValue(next);
      onChange(next);
    },
    [spec, onChange],
  );

  const handlePointerUp = useCallback(
    (e: React.PointerEvent) => {
      if (!dragRef.current) return;
      dragRef.current = null;
      (e.target as Element).releasePointerCapture?.(e.pointerId);
      onDragStateChange?.(false);
    },
    [onDragStateChange],
  );

  const handleDoubleClick = useCallback(() => {
    const next = snapPlain(spec, spec.def);
    setLocalValue(next);
    onChange(next);
    // Let the next authoritative tick clear the local override.
    window.setTimeout(() => setLocalValue(null), 150);
  }, [spec, onChange]);

  const norm = toNormalized(spec, shown);
  const cx = 34;
  const cy = 34;
  const r = 26;
  const valueDeg = ARC_START_DEG + ARC_SWEEP_DEG * norm;

  return (
    <div className="knob" ref={knobRef} title={tip ?? title}>
      <div
        className="knob-dial"
        role="slider"
        aria-label={title}
        aria-valuemin={spec.min}
        aria-valuemax={spec.max}
        aria-valuenow={Math.round(shown * 1000) / 1000}
        tabIndex={0}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onDoubleClick={handleDoubleClick}
        onKeyDown={(e) => {
          const step = (spec.max - spec.min) * (e.shiftKey ? 0.005 : 0.05);
          if (e.key === 'ArrowUp' || e.key === 'ArrowRight') {
            e.preventDefault();
            onChange(snapPlain(spec, value + step));
          } else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') {
            e.preventDefault();
            onChange(snapPlain(spec, value - step));
          }
        }}
      >
        <svg width="68" height="68" viewBox="0 0 68 68">
          <path className="knob-track" d={arcPath(cx, cy, r, ARC_START_DEG, ARC_START_DEG + ARC_SWEEP_DEG)} />
          <path
            className="knob-fill"
            d={arcPath(cx, cy, r, ARC_START_DEG, Math.max(ARC_START_DEG + 0.01, valueDeg))}
          />
          <circle className="knob-cap" cx={cx} cy={cy} r={18} />
          <line
            className="knob-pointer"
            x1={cx}
            y1={cy - 6}
            x2={polar(cx, cy, 16, valueDeg)[0]}
            y2={polar(cx, cy, 16, valueDeg)[1]}
          />
        </svg>
      </div>
      <div className="knob-title">{title}</div>
      <div className="knob-value">{format(shown)}</div>
    </div>
  );
}

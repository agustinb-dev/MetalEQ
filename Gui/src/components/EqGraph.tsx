/*
    SVG frequency-response graph — the visual mirror of the native EQGraph,
    with direct manipulation:

      * drag a numbered band node to change its frequency and gain
      * drag the HP/LP -3 dB markers to change their cutoff
      * everything remains available as numeric controls in Advanced view

    The dB range comes from the native curve ({minDb, maxDb}) instead of being
    fixed, so stacked boosts are never clipped.
*/

import { useMemo, useRef, useState } from 'react';
import {
  GUITAR_LANDMARKS,
  REGIONS,
  dbToUnit,
  frequencyLabel,
  frequencyToUnit,
  isMajorLandmark,
  unitToFrequency,
  visibleLabelIndices,
} from '../graphAxis';
import type { CurveInfo, ParamSpec } from '../types';

// Plot margins mirror the native EQGraph paint().
const PLOT_MARGIN = { left: 42, right: 42, top: 26, bottom: 43 };

interface EqGraphProps {
  width: number;
  height: number;
  curve: CurveInfo | null;
  params: Record<string, number>;
  suggestionCurve: number[] | null;
  /** Enables the draggable band and crossover handles. */
  editable?: boolean;
  selectedBand?: number | null;
  onSelectBand?: (index: number) => void;
  onBandChange?: (index: number, freq: number, gainDb: number) => void;
  onCrossoverChange?: (kind: 'hp' | 'lp', freq: number) => void;
}

interface MarkerSpec {
  freqParam: ParamSpec;
  slopeParam: ParamSpec;
  id: 'hp' | 'lp';
  enabled: boolean;
  color: string;
  radius: number;
}

type DragState =
  | { kind: 'band'; index: number; pointerId: number }
  | { kind: 'crossover'; id: 'hp' | 'lp'; pointerId: number }
  | null;

export function EqGraph({
  width,
  height,
  curve,
  params,
  suggestionCurve,
  editable = false,
  selectedBand = null,
  onSelectBand,
  onBandChange,
  onCrossoverChange,
}: EqGraphProps) {
  const svgRef = useRef<SVGSVGElement>(null);
  const dragRef = useRef<DragState>(null);
  const [dragging, setDragging] = useState<DragState>(null);

  const plot = useMemo(() => {
    const w = width - PLOT_MARGIN.left - PLOT_MARGIN.right;
    const h = height - PLOT_MARGIN.top - PLOT_MARGIN.bottom;
    return { x: PLOT_MARGIN.left, y: PLOT_MARGIN.top, w, h };
  }, [width, height]);

  const minDb = curve?.minDb ?? -18;
  const maxDb = curve?.maxDb ?? 12;

  if (plot.w < 80 || plot.h < 60 || !curve) {
    return <svg className="eq-graph" width={width} height={height} />;
  }

  const freqToX = (hz: number) => plot.x + plot.w * frequencyToUnit(hz);
  const dbToY = (db: number) => plot.y + plot.h * (1 - dbToUnit(db, minDb, maxDb));

  const localPoint = (event: React.PointerEvent<SVGGElement>) => {
    const rect = svgRef.current?.getBoundingClientRect();
    if (!rect) return null;
    return {
      x: ((event.clientX - rect.left) * width) / Math.max(1, rect.width),
      y: ((event.clientY - rect.top) * height) / Math.max(1, rect.height),
    };
  };

  const applyDrag = (event: React.PointerEvent<SVGGElement>) => {
    const state = dragRef.current;
    if (!state) return;
    const point = localPoint(event);
    if (!point) return;

    if (state.kind === 'crossover') {
      const unit = (point.x - plot.x) / plot.w;
      onCrossoverChange?.(state.id, unitToFrequency(unit));
      return;
    }

    const freqUnit = (point.x - plot.x) / plot.w;
    const gainUnit = 1 - (point.y - plot.y) / plot.h;
    const freq = unitToFrequency(freqUnit);
    const gain = minDb + Math.max(0, Math.min(1, gainUnit)) * (maxDb - minDb);
    onBandChange?.(state.index, freq, Math.max(-12, Math.min(12, gain)));
  };

  const beginBandDrag = (event: React.PointerEvent<SVGGElement>, index: number) => {
    if (!editable) return;
    event.preventDefault();
    event.stopPropagation();
    event.currentTarget.setPointerCapture?.(event.pointerId);
    const next: DragState = { kind: 'band', index, pointerId: event.pointerId };
    dragRef.current = next;
    setDragging(next);
    onSelectBand?.(index);
  };

  const beginCrossoverDrag = (
    event: React.PointerEvent<SVGGElement>,
    id: 'hp' | 'lp',
  ) => {
    if (!editable) return;
    event.preventDefault();
    event.stopPropagation();
    event.currentTarget.setPointerCapture?.(event.pointerId);
    const next: DragState = { kind: 'crossover', id, pointerId: event.pointerId };
    dragRef.current = next;
    setDragging(next);
  };

  const endDrag = (event: React.PointerEvent<SVGGElement>) => {
    if (dragRef.current == null) return;
    event.currentTarget.releasePointerCapture?.(dragRef.current.pointerId);
    dragRef.current = null;
    setDragging(null);
  };

  // dB grid: native draws -18..12 step 6; extend when the native range grows.
  const dbLines: number[] = [];
  const startDb = Math.ceil(minDb / 6) * 6;
  for (let db = startDb; db <= maxDb + 1e-9; db += 6) dbLines.push(db);
  if (!dbLines.includes(0) && 0 >= minDb && 0 <= maxDb) dbLines.push(0);

  const responsePath =
    curve.points.length > 1
      ? curve.points
          .map((db, i) => {
            const unit = i / (curve.points.length - 1);
            const x = freqToX(20 * Math.pow(20000 / 20, unit));
            return `${i === 0 ? 'M' : 'L'} ${x.toFixed(2)} ${dbToY(db).toFixed(2)}`;
          })
          .join(' ')
      : '';

  const suggestionPath =
    suggestionCurve && suggestionCurve.length > 1
      ? suggestionCurve
          .map((db, i) => {
            const unit = i / (suggestionCurve.length - 1);
            const x = freqToX(20 * Math.pow(20000 / 20, unit));
            return `${i === 0 ? 'M' : 'L'} ${x.toFixed(2)} ${dbToY(db).toFixed(2)}`;
          })
          .join(' ')
      : '';

  const markers: MarkerSpec[] = [];
  if (params.hpSlope > 0) {
    markers.push({
      freqParam: { id: 'hpFreq', label: '', kind: 'float', min: 0, max: 1, skew: 1, step: 0, def: 0 },
      slopeParam: { id: 'hpSlope', label: '', kind: 'choice', min: 0, max: 1, skew: 1, step: 1, def: 0 },
      id: 'hp',
      enabled: true,
      color: 'rgba(155, 180, 208, 0.7)',
      radius: editable ? 6 : 3.5,
    });
  }
  if (params.lpSlope > 0) {
    markers.push({
      freqParam: { id: 'lpFreq', label: '', kind: 'float', min: 0, max: 1, skew: 1, step: 0, def: 0 },
      slopeParam: { id: 'lpSlope', label: '', kind: 'choice', min: 0, max: 1, skew: 1, step: 1, def: 0 },
      id: 'lp',
      enabled: params.lpOn >= 0.5,
      color: params.lpOn >= 0.5 ? 'rgba(105, 214, 166, 0.85)' : '#737b85',
      radius: editable ? 6 : 4,
    });
  }

  const labels = visibleLabelIndices(plot.w, 45);
  const bandNodes = Array.from({ length: 5 }, (_, i) => {
    const n = i + 1;
    return {
      index: i,
      freq: params[`band${n}Freq`] ?? 1000,
      gain: params[`band${n}Gain`] ?? 0,
      on: (params[`band${n}On`] ?? 1) >= 0.5,
    };
  });

  return (
    <svg
      ref={svgRef}
      className={editable ? 'eq-graph editable' : 'eq-graph'}
      width={width}
      height={height}
      role="img"
      aria-label="EQ response curve"
    >
      {/* regions */}
      {REGIONS.map((r) => {
        const x1 = freqToX(r.from);
        const x2 = freqToX(r.to);
        const zoneWidth = x2 - x1;
        return (
          <g key={r.label}>
            <rect x={x1} y={plot.y} width={zoneWidth} height={plot.h} fill={r.fill} />
            {zoneWidth > 55 && (
              <text x={x1 + zoneWidth / 2} y={plot.y + 12} textAnchor="middle" className="graph-region">
                {r.label}
              </text>
            )}
          </g>
        );
      })}

      {/* dB grid + labels */}
      {dbLines.map((db) => {
        const y = Math.round(dbToY(db));
        const zero = db === 0;
        return (
          <g key={`db${db}`}>
            <line
              x1={plot.x}
              x2={plot.x + plot.w}
              y1={y}
              y2={y}
              stroke={zero ? '#78818b' : '#363c44'}
              strokeWidth={1}
            />
            <text x={2} y={y - 7 + 7} textAnchor="start" dominantBaseline="middle" className={zero ? 'graph-db-label zero' : 'graph-db-label'}>
              {db} dB
            </text>
          </g>
        );
      })}

      {/* frequency grid */}
      {GUITAR_LANDMARKS.map((hz) => {
        const x = Math.round(freqToX(hz));
        const major = isMajorLandmark(hz);
        return (
          <line
            key={`f${hz}`}
            x1={x}
            x2={x}
            y1={plot.y}
            y2={plot.y + plot.h}
            stroke={major ? '#535a63' : '#333940'}
            strokeWidth={1}
          />
        );
      })}

      {/* frequency labels */}
      {labels.map((index) => {
        const hz = GUITAR_LANDMARKS[index];
        const x = freqToX(hz);
        const anchor = hz === 20 ? 'start' : hz === 20000 ? 'end' : 'middle';
        const tx = hz === 20 ? Math.max(40, x) : hz === 20000 ? Math.min(width - 4, x) : x;
        return (
          <text key={`l${hz}`} x={tx} y={height - 18} textAnchor={anchor} className="graph-freq-label">
            {frequencyLabel(hz)}
          </text>
        );
      })}

      {/* response curve */}
      <path d={responsePath} className="graph-response" />

      {/* HP / LP -3 dB markers; draggable horizontally when editable */}
      {markers.map((m) => {
        const freq = params[m.freqParam.id] ?? 0;
        const x = freqToX(freq);
        const y = dbToY(-3);
        return (
          <g
            key={`m${m.id}`}
            opacity={m.enabled ? 1 : 0.9}
            className={editable ? 'graph-crossover-handle' : undefined}
            onPointerDown={editable ? (event) => beginCrossoverDrag(event, m.id) : undefined}
            onPointerMove={editable ? applyDrag : undefined}
            onPointerUp={editable ? endDrag : undefined}
            onPointerCancel={editable ? endDrag : undefined}
          >
            <line x1={x} x2={x} y1={plot.y} y2={plot.y + plot.h} stroke={m.color} strokeWidth={1} />
            <circle cx={x} cy={y} r={m.radius} fill={m.color} />
            {editable && <title>{`Drag to change the ${m.id === 'hp' ? 'high-pass' : 'low-pass'} cutoff`}</title>}
          </g>
        );
      })}

      {/* direct-edit band handles */}
      {editable && bandNodes.filter((band) => band.on).map((band) => {
        const cx = freqToX(band.freq);
        const cy = dbToY(band.gain);
        const selected = selectedBand === band.index;
        const isDragging = dragging?.kind === 'band' && dragging.index === band.index;
        return (
          <g
            key={`band-handle-${band.index}`}
            className={`graph-band-handle${selected ? ' selected' : ''}${isDragging ? ' dragging' : ''}`}
            role="slider"
            tabIndex={0}
            aria-label={`Band ${band.index + 1}`}
            aria-valuetext={`${Math.round(band.freq)} Hz, ${band.gain.toFixed(1)} dB`}
            onPointerDown={(event) => beginBandDrag(event, band.index)}
            onPointerMove={applyDrag}
            onPointerUp={endDrag}
            onPointerCancel={endDrag}
            onFocus={() => onSelectBand?.(band.index)}
          >
            <circle cx={cx} cy={cy} r={selected || isDragging ? 11 : 9} className="band-handle-hit" />
            <circle cx={cx} cy={cy} r={selected || isDragging ? 7 : 5.5} className="band-handle-dot" />
            <text x={cx} y={cy + 0.5} textAnchor="middle" dominantBaseline="middle" className="band-handle-label">
              {band.index + 1}
            </text>
            <title>{`Band ${band.index + 1}: ${Math.round(band.freq)} Hz, ${band.gain.toFixed(1)} dB`}</title>
          </g>
        );
      })}

      {/* suggested correction */}
      {suggestionPath && <path d={suggestionPath} className="graph-suggestion" />}

      {/* legends */}
      <text x={48} y={14} className="graph-legend accent">
        CURRENT EQ
      </text>
      {suggestionPath && (
        <text x={146} y={14} className="graph-legend suggestion">
          SUGGESTED
        </text>
      )}
      {editable && (
        <text x={width - 46} y={14} textAnchor="end" className="graph-legend">
          DRAG BANDS
        </text>
      )}
    </svg>
  );
}

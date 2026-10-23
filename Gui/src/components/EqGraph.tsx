/*
    SVG frequency-response graph — the visual mirror of the native EQGraph,
    with direct manipulation:

      * drag a numbered band node to change its frequency and gain
      * drag the HP/LP -3 dB markers to change their cutoff
      * everything remains available as numeric controls in Advanced view

    The vertical range is fixed at -15..+15 dB so steep high-pass/low-pass
    rolloff and stacked boosts cannot shrink the useful EQ shapes. Response
    outside that window is clipped at the graph edge.
*/

import { useMemo, useRef, useState } from 'react';
import {
  GRAPH_DB_LINES,
  GRAPH_MAX_DB,
  GRAPH_MIN_DB,
  GUITAR_LANDMARKS,
  REGIONS,
  dbToUnit,
  frequencyLabel,
  frequencyToUnit,
  isMajorLandmark,
  unitToFrequency,
  visibleLabelIndices,
} from '../graphAxis';
import type { CurveInfo, ParamSpec, ResonanceCandidateInfo } from '../types';

// Plot margins mirror the native EQGraph paint().
const PLOT_MARGIN = { left: 42, right: 42, top: 26, bottom: 43 };

interface EqGraphProps {
  width: number;
  height: number;
  curve: CurveInfo | null;
  params: Record<string, number>;
  suggestionCurve: number[] | null;
  /** V3 scan candidates, drawn as vertical markers. */
  resonanceCandidates?: ResonanceCandidateInfo[];
  selectedCandidates?: number[];
  hoveredCandidate?: number | null;
  onToggleCandidate?: (index: number) => void;
  onHoverCandidate?: (index: number | null) => void;
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
  resonanceCandidates = [],
  selectedCandidates = [],
  hoveredCandidate = null,
  onToggleCandidate,
  onHoverCandidate,
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

  if (plot.w < 80 || plot.h < 60 || !curve) {
    return <svg className="eq-graph" width={width} height={height} />;
  }

  const freqToX = (hz: number) => plot.x + plot.w * frequencyToUnit(hz);
  const dbToY = (db: number) => plot.y + plot.h * (1 - dbToUnit(db));

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
    const gain = GRAPH_MIN_DB + Math.max(0, Math.min(1, gainUnit)) * (GRAPH_MAX_DB - GRAPH_MIN_DB);
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

  const dbLines: readonly number[] = GRAPH_DB_LINES;

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
            <text x={2} y={y} textAnchor="start" dominantBaseline="middle" className={zero ? 'graph-db-label zero' : 'graph-db-label'}>
              {db > 0 ? `+${db}` : db} dB
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

      {/* V3 resonance candidates: selectable vertical markers */}
      {resonanceCandidates.map((candidate, index) => {
        const x = freqToX(candidate.freq);
        const selected = selectedCandidates.includes(index);
        const hovered = hoveredCandidate === index;
        const markerY = dbToY(0);
        return (
          <g
            key={`resonance-${index}`}
            className={`resonance-marker${selected ? ' selected' : ''}${hovered ? ' hovered' : ''}`}
            role="button"
            tabIndex={0}
            aria-pressed={selected}
            aria-label={`Resonance candidate ${index + 1}: ${Math.round(candidate.freq)} Hz`}
            onPointerDown={(event) => event.stopPropagation()}
            onClick={(event) => {
              event.stopPropagation();
              onToggleCandidate?.(index);
            }}
            onKeyDown={(event) => {
              if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                onToggleCandidate?.(index);
              }
            }}
            onPointerEnter={() => onHoverCandidate?.(index)}
            onPointerLeave={() => onHoverCandidate?.(null)}
            onFocus={() => onHoverCandidate?.(index)}
            onBlur={() => onHoverCandidate?.(null)}
          >
            <line x1={x} x2={x} y1={plot.y} y2={plot.y + plot.h} className="resonance-marker-line" />
            <circle cx={x} cy={markerY} r={selected || hovered ? 6 : 4.5} className="resonance-marker-dot" />
            <text x={x} y={markerY - 11} textAnchor="middle" className="resonance-marker-label">
              {Math.round(candidate.freq)}
            </text>
            <title>{`${Math.round(candidate.freq)} Hz · Q ${candidate.q.toFixed(1)} · ${candidate.cutDb.toFixed(1)} dB · ${Math.round(candidate.confidence * 100)}% confidence`}</title>
          </g>
        );
      })}

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

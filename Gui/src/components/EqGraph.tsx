/*
    SVG frequency-response graph — the visual mirror of
    MetalEQNativeFallbackEditor::EQGraph::paint() with one improvement: the dB
    range comes from the native curve ({minDb, maxDb}) instead of being fixed,
    so stacked boosts are never clipped.

    Layer order matches native: regions -> dB grid -> frequency grid ->
    response curve -> HP/LP -3 dB markers -> suggestion curve -> legends.
*/

import { useMemo } from 'react';
import {
  GUITAR_LANDMARKS,
  REGIONS,
  dbToUnit,
  frequencyLabel,
  frequencyToUnit,
  isMajorLandmark,
  visibleLabelIndices,
} from '../graphAxis';
import type { CurveInfo, ParamSpec } from '../types';

// Plot margins mirror MetalEQNativeFallbackEditor::EQGraph::paint():
// reduced(42, 20), then removeFromBottom(23) and removeFromTop(6).
const PLOT_MARGIN = { left: 42, right: 42, top: 26, bottom: 43 };

interface EqGraphProps {
  width: number;
  height: number;
  curve: CurveInfo | null;
  params: Record<string, number>;
  suggestionCurve: number[] | null;
}

interface MarkerSpec {
  freqParam: ParamSpec;
  slopeParam: ParamSpec;
  enabled: boolean;
  color: string;
  radius: number;
}

export function EqGraph({ width, height, curve, params, suggestionCurve }: EqGraphProps) {
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

  // dB grid: native draws -18..12 step 6; when the native range expands we
  // extend the same 6 dB stepping to cover it.
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

  // HP / LP -3 dB markers (native draws them when slope > 0).
  const markers: MarkerSpec[] = [];
  if (params.hpSlope > 0) {
    markers.push({
      freqParam: { id: 'hpFreq', label: '', kind: 'float', min: 0, max: 1, skew: 1, step: 0, def: 0 },
      slopeParam: { id: 'hpSlope', label: '', kind: 'choice', min: 0, max: 1, skew: 1, step: 1, def: 0 },
      enabled: true,
      color: 'rgba(155, 180, 208, 0.7)',
      radius: 3.5,
    });
  }
  if (params.lpSlope > 0) {
    markers.push({
      freqParam: { id: 'lpFreq', label: '', kind: 'float', min: 0, max: 1, skew: 1, step: 0, def: 0 },
      slopeParam: { id: 'lpSlope', label: '', kind: 'choice', min: 0, max: 1, skew: 1, step: 1, def: 0 },
      enabled: params.lpOn >= 0.5,
      color: params.lpOn >= 0.5 ? 'rgba(105, 214, 166, 0.85)' : '#737b85',
      radius: 4,
    });
  }

  const labels = visibleLabelIndices(plot.w, 45);

  return (
    <svg className="eq-graph" width={width} height={height} role="img" aria-label="EQ response curve">
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
        const anchor =
          hz === 20 ? 'start' : hz === 20000 ? 'end' : 'middle';
        const tx = hz === 20 ? Math.max(40, x) : hz === 20000 ? Math.min(width - 4, x) : x;
        return (
          <text key={`l${hz}`} x={tx} y={height - 18} textAnchor={anchor} className="graph-freq-label">
            {frequencyLabel(hz)}
          </text>
        );
      })}

      {/* response curve */}
      <path d={responsePath} className="graph-response" />

      {/* HP / LP markers */}
      {markers.map((m, i) => {
        const freq = params[m.freqParam.id] ?? 0;
        const x = freqToX(freq);
        const y = dbToY(-3);
        return (
          <g key={`m${i}`} opacity={m.enabled ? 1 : 0.9}>
            <line x1={x} x2={x} y1={plot.y} y2={plot.y + plot.h} stroke={m.color} strokeWidth={1} />
            <circle cx={x} cy={y} r={m.radius} fill={m.color} />
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
    </svg>
  );
}

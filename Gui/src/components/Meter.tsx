/*
    Peak meter bar. Mirrors the native drawMeter(): a track with a fill whose
    width is peak (0..1), green below 0.95 and red above (native uses
    0xffef5b5b above 0.95).
*/

interface MeterProps {
  title: string;
  peak: number;
}

export function Meter({ title, peak }: MeterProps) {
  const clamped = Math.min(1, Math.max(0, peak));
  return (
    <div className="meter" title={`${title}: ${(peak * 100).toFixed(0)}%`}>
      <div className="meter-title">{title}</div>
      <div className="meter-track">
        <div
          className={`meter-fill ${peak > 0.95 ? 'meter-hot' : ''}`}
          style={{ width: `${clamped * 100}%` }}
        />
      </div>
    </div>
  );
}

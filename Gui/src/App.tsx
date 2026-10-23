/*
    MetalEQ WebView UI - root component.

    Two levels of control, sharing one bridge and one EQ engine:

      Simple    the default. Pick a genre/subgenre tone, shape it with macros
                and the fixed character switches, and drag the graph directly.
      Advanced  every numeric parameter, including the five parametric bands
                and the filters, plus the analyzer.

    The choice is remembered after the user makes it. The first run always
    starts in Simple.

    Every control sends requests through the bridge; nothing here is
    authoritative. Authoritative values come back via metaleq.tick and the
    setParameter() result and overwrite local state.
*/

import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { isJuceBackendAvailable, JuceBridge } from './bridge';
import { MockBridge } from './mockBridge';
import { store, useUiState } from './store';
import { AnalyzerStatus, PROTOCOL_VERSION } from './types';
import type {
  FactoryPresetInfo,
  MetalEqBridge,
  ParamSpec,
  PresetInfo,
  ResonanceInfo,
} from './types';
import {
  NUM_BANDS,
  NUM_DYNAMIC_BANDS,
  NUM_SURGICAL_BANDS,
  formatCut,
  formatFreqPlain,
  formatGain,
  formatPercent,
  formatQ,
  formatRatio,
  formatThreshold,
  formatTimeMs,
  formatTrim,
  getSpec,
  snapPlain,
} from './params';
import { EqGraph } from './components/EqGraph';
import { Knob } from './components/Knob';
import { Meter } from './components/Meter';
import { Modal } from './components/Modal';
import { Select } from './components/Select';
import type { SelectOption } from './components/Select';
import { Slider } from './components/Slider';
import { Toggle } from './components/Toggle';

const SLOPE_CHOICES = [
  { value: '0', label: 'Off' },
  { value: '1', label: '12 dB/oct' },
  { value: '2', label: '24 dB/oct' },
  { value: '3', label: '36 dB/oct' },
];

const MACRO_KNOBS: Array<{ id: string; title: string; tip: string }> = [
  { id: 'tight', title: 'TIGHT', tip: 'Controls low-end looseness and low-mid buildup.' },
  { id: 'body', title: 'BODY', tip: 'Adds or reduces low-mid fullness. Centre is neutral.' },
  { id: 'scoop', title: 'SCOOP', tip: 'Broad midrange reduction. Heavy scooping can reduce mix clarity.' },
  { id: 'bite', title: 'BITE', tip: 'Controls upper-mid pick attack and aggression.' },
  { id: 'fizzCut', title: 'FIZZ CUT', tip: 'Tames upper-frequency harshness while preserving useful attack.' },
  { id: 'trim', title: 'OUTPUT', tip: 'Output level after EQ; useful for level-matched A/B.' },
];

const SHAPES: Array<{ id: string; label: string; tip: string }> = [
  { id: 'shapeTightenOn', label: 'TIGHTEN', tip: 'Tightens excess lows and clears low-mid looseness.' },
  { id: 'shapeScoopOn', label: 'SCOOP', tip: 'Applies a broad midrange scoop; use gently in dense mixes.' },
  { id: 'shapeMidPunchOn', label: 'MID PUNCH', tip: 'Adds forward midrange body and authority.' },
  { id: 'shapePickAttackOn', label: 'PICK ATTACK', tip: 'Adds pick definition around the attack region.' },
  { id: 'shapeFizzTamerOn', label: 'FIZZ TAMER', tip: 'Reduces fizz and harshness while retaining pick attack.' },
  { id: 'shapeBrightenOn', label: 'BRIGHTEN', tip: 'Adds controlled presence to a dark guitar tone.' },
];

const VIEW_STORAGE_KEY = 'metaleq.editorView';

type ViewMode = 'simple' | 'advanced';

type PresetChoice =
  | { kind: 'custom' }
  | { kind: 'factory'; presetId: string }
  | { kind: 'user'; name: string };

function specOf(id: string): ParamSpec {
  const spec = getSpec(id);
  if (spec == null) throw new Error(`missing param spec: ${id}`);
  return spec;
}

function initialView(): ViewMode {
  if (typeof window === 'undefined') return 'simple';

  // Browser preview / screenshot aid. Never stored: only an explicit click
  // on the Advanced toggle persists a preference.
  const query = new URLSearchParams(window.location.search);
  if (query.get('view') === 'advanced' || query.has('advanced') || query.has('fine')) return 'advanced';

  try {
    return window.localStorage.getItem(VIEW_STORAGE_KEY) === 'advanced' ? 'advanced' : 'simple';
  } catch {
    return 'simple';
  }
}

function rememberView(view: ViewMode): void {
  try {
    window.localStorage.setItem(VIEW_STORAGE_KEY, view);
  } catch {
    // Storage can be unavailable in a restricted host; the in-memory choice
    // still applies for this session.
  }
}

/** Mirror of the native preset state label (FACTORY / USER / CUSTOM + dirty *). */
function presetStateText(preset: PresetInfo): string {
  const kind = preset.kind === 'user' ? 'USER' : preset.kind === 'factory' ? 'FACTORY' : 'CUSTOM';
  return preset.dirty ? `${kind} *` : kind;
}

/** Mirror of the native updateAnalyzerUi() strings. */
function analyzerStatusText(status: number, progress: number, suggestion: number[]): string {
  switch (status) {
    case AnalyzerStatus.capturing:
      return `Capturing input: ${Math.round(progress * 100)}% · play through the host.`;
    case AnalyzerStatus.analyzing:
      return 'Analyzing captured average spectrum…';
    case AnalyzerStatus.ready:
      return `Suggested gains: ${suggestion.map((g) => g.toFixed(1)).join(' / ')} dB`;
    case AnalyzerStatus.failed:
      return 'No usable audio was captured. Play audio and try again.';
    default:
      return 'Ready · analyzer listens only when you start capture.';
  }
}

//----------------------------------------------------------------------------
// Small presentational pieces

function confidenceLabel(value: number): string {
  if (value >= 0.6) return 'HIGH';
  if (value >= 0.4) return 'MEDIUM';
  return 'LOW';
}

/** Plain-language scan status. The detector is advisory, so the copy never
    promises that a candidate is definitely a problem.
*/
function resonanceStatusText(info: ResonanceInfo): string {
  switch (info.status) {
    case AnalyzerStatus.capturing:
      return `Listening: ${Math.round(info.progress * 100)}% · play varied riffs and let notes ring.`;
    case AnalyzerStatus.analyzing:
      return 'Separating stationary peaks from moving harmonics…';
    case AnalyzerStatus.ready:
      return info.candidates.length === 0
        ? 'No narrow stationary peaks stood out. Nothing will be changed.'
        : `${info.candidates.length} candidate${info.candidates.length === 1 ? '' : 's'} found. Audition before applying.`;
    case AnalyzerStatus.failed:
      return 'No usable audio was captured. Play audio and scan again.';
    default:
      return 'Finds narrow, stationary peaks that make a tone harsh or stuck. Capture is temporary.';
  }
}

function ResonancePanel({
  info,
  selected,
  hovered,
  surgicalActive,
  dynamicActive,
  audition,
  onScan,
  onCancel,
  onToggleCandidate,
  onHoverCandidate,
  onApply,
  onClear,
  onAudition,
}: {
  info: ResonanceInfo;
  selected: number[];
  hovered: number | null;
  surgicalActive: boolean;
  dynamicActive: boolean;
  audition: boolean;
  onScan: () => void;
  onCancel: () => void;
  onToggleCandidate: (index: number) => void;
  onHoverCandidate: (index: number | null) => void;
  onApply: () => void;
  onClear: () => void;
  onAudition: (enabled: boolean) => void;
}) {
  const capturing = info.status === AnalyzerStatus.capturing;
  const analyzing = info.status === AnalyzerStatus.analyzing;
  const ready = info.status === AnalyzerStatus.ready;
  const canSelect = ready || info.status === AnalyzerStatus.idle || info.status === AnalyzerStatus.failed;

  return (
    <section className="resonance-section">
      <div className="section-heading">
        <span className="section-title">Resonance scan</span>
        <span className="section-hint">find narrow stationary peaks, audition, then cut surgically</span>
      </div>

      <div className="resonance-toolbar">
        <button
          type="button"
          className="btn capture-btn"
          onClick={capturing ? onCancel : onScan}
          disabled={analyzing}
          title="Capture 8 seconds of playing. The scan looks for peaks that stay at the same frequency across notes."
        >
          {capturing ? 'Stop scan' : analyzing ? 'Analyzing…' : 'Scan 8 seconds'}
        </button>
        <span className={`resonance-status${ready && info.candidates.length > 0 ? ' ready' : ''}`}>
          {resonanceStatusText(info)}
        </span>
        <div className="resonance-toolbar-spacer" />
        <Toggle
          label="HEAR REMOVED"
          checked={audition}
          disabled={!surgicalActive && !dynamicActive}
          variant="accent"
          onChange={onAudition}
          tip="Monitor the static and dynamic resonance cuts in isolation. This listening aid is never saved with the preset."
        />
        <button
          type="button"
          className="btn primary"
          disabled={!ready || selected.length === 0}
          onClick={onApply}
          title="Copy the selected candidates into the six surgical bands."
        >
          Apply selected
        </button>
        <button
          type="button"
          className="btn"
          disabled={!surgicalActive}
          onClick={onClear}
          title="Switch off and zero every surgical resonance band."
        >
          Clear cuts
        </button>
      </div>

      {ready && !info.enoughVariation && (
        <div className="resonance-warning">
          The capture was too static to separate harmonics from resonances. Play across several
          notes, with bends or vibrato, for a more reliable scan.
        </div>
      )}

      {info.status === AnalyzerStatus.ready && info.candidates.length > 0 && (
        <div className="resonance-list" role="list" aria-label="Resonance candidates">
          {info.candidates.map((candidate, index) => {
            const isSelected = selected.includes(index);
            const isHovered = hovered === index;
            return (
              <label
                key={`${candidate.freq}-${index}`}
                className={`resonance-candidate${isSelected ? ' selected' : ''}${isHovered ? ' hovered' : ''}`}
                onPointerEnter={() => onHoverCandidate(index)}
                onPointerLeave={() => onHoverCandidate(null)}
              >
                <input
                  type="checkbox"
                  checked={isSelected}
                  disabled={!canSelect}
                  onChange={() => onToggleCandidate(index)}
                  aria-label={`Select ${Math.round(candidate.freq)} Hz resonance`}
                />
                <span className="resonance-rank">{index + 1}</span>
                <span className="resonance-frequency">{formatFreqPlain(candidate.freq)}</span>
                <span className={`resonance-confidence ${confidenceLabel(candidate.confidence).toLowerCase()}`}>
                  {confidenceLabel(candidate.confidence)}
                </span>
                <span className="resonance-cut">{candidate.cutDb.toFixed(1)} dB cut</span>
                <span className="resonance-detail">
                  Q {candidate.q.toFixed(1)} · peak +{candidate.prominence.toFixed(1)} dB · residual +{candidate.residual.toFixed(1)} dB
                </span>
              </label>
            );
          })}
        </div>
      )}

      <div className="resonance-disclaimer">
        Suggestions, not automatic fixes. Compare bypass on/off and keep only the cuts that improve
        the tone. Surgical bands are separate from the five musical bands.
      </div>
    </section>
  );
}

function SurgicalPanel({
  params,
  onParam,
  onDrag,
}: {
  params: Record<string, number>;
  onParam: (id: string, value: number) => void;
  onDrag: (id: string, dragging: boolean) => void;
}) {
  return (
    <div className="surgical-panel" role="group" aria-label="Surgical resonance bands">
      {Array.from({ length: NUM_SURGICAL_BANDS }, (_, i) => {
        const n = i + 1;
        const freqId = `res${n}Freq`;
        const gainId = `res${n}Gain`;
        const qId = `res${n}Q`;
        const onId = `res${n}On`;
        const freqSpec = specOf(freqId);
        const gainSpec = specOf(gainId);
        const qSpec = specOf(qId);
        const enabled = (params[onId] ?? 0) >= 0.5;
        return (
          <div className={`surgical-card${enabled ? ' enabled' : ''}`} key={n}>
            <div className="surgical-card-head">
              <span className="surgical-index">{n}</span>
              <div className="surgical-title">SURGICAL {n}</div>
              <Toggle
                label="ON"
                checked={enabled}
                onChange={(value) => onParam(onId, value ? 1 : 0)}
                tip="Enable this narrow resonance cut."
              />
            </div>
            <Slider
              label="FREQ"
              value={params[freqId] ?? freqSpec.def}
              min={freqSpec.min}
              max={freqSpec.max}
              skew={freqSpec.skew}
              format={formatFreqPlain}
              tip="Narrow cut centre. Scan candidates fill this automatically."
              onDragStateChange={(dragging) => onDrag(freqId, dragging)}
              onChange={(value) => onParam(freqId, snapPlain(freqSpec, value))}
            />
            <Slider
              label="CUT"
              value={params[gainId] ?? gainSpec.def}
              min={gainSpec.min}
              max={gainSpec.max}
              format={formatGain}
              tip="Cut depth. Deeper is not better: remove only the harsh peak."
              onDragStateChange={(dragging) => onDrag(gainId, dragging)}
              onChange={(value) => onParam(gainId, snapPlain(gainSpec, value))}
            />
            <Slider
              label="Q"
              value={params[qId] ?? qSpec.def}
              min={qSpec.min}
              max={qSpec.max}
              format={formatQ}
              tip="High Q is extremely narrow; lower Q removes more around the centre."
              onDragStateChange={(dragging) => onDrag(qId, dragging)}
              onChange={(value) => onParam(qId, snapPlain(qSpec, value))}
            />
          </div>
        );
      })}
    </div>
  );
}

function DynamicPanel({
  params,
  reductions,
  onParam,
  onDrag,
}: {
  params: Record<string, number>;
  reductions: number[];
  onParam: (id: string, value: number) => void;
  onDrag: (id: string, dragging: boolean) => void;
}) {
  return (
    <div className="dynamic-panel" role="group" aria-label="Dynamic resonance bands">
      {Array.from({ length: NUM_DYNAMIC_BANDS }, (_, i) => {
        const n = i + 1;
        const freqId = `dyn${n}Freq`;
        const qId = `dyn${n}Q`;
        const thresholdId = `dyn${n}Threshold`;
        const ratioId = `dyn${n}Ratio`;
        const attackId = `dyn${n}Attack`;
        const releaseId = `dyn${n}Release`;
        const rangeId = `dyn${n}Range`;
        const onId = `dyn${n}On`;
        const freqSpec = specOf(freqId);
        const qSpec = specOf(qId);
        const thresholdSpec = specOf(thresholdId);
        const ratioSpec = specOf(ratioId);
        const attackSpec = specOf(attackId);
        const releaseSpec = specOf(releaseId);
        const rangeSpec = specOf(rangeId);
        const enabled = (params[onId] ?? 0) >= 0.5;
        const reduction = Math.max(0, reductions[i] ?? 0);
        const maxCut = params[rangeId] ?? rangeSpec.def;
        const meterPercent = maxCut > 0 ? Math.min(1, reduction / maxCut) : 0;
        return (
          <div className={`dynamic-card${enabled ? ' enabled' : ''}`} key={n}>
            <div className="dynamic-card-head">
              <span className="dynamic-index">{n}</span>
              <div className="dynamic-title">DYNAMIC {n}</div>
              <Toggle
                label="ON"
                checked={enabled}
                onChange={(value) => onParam(onId, value ? 1 : 0)}
                tip="Enable this zero-latency dynamic resonance cut."
              />
            </div>
            <Slider
              label="FREQ"
              value={params[freqId] ?? freqSpec.def}
              min={freqSpec.min}
              max={freqSpec.max}
              skew={freqSpec.skew}
              format={formatFreqPlain}
              tip="Centre of the band that will be cut when it runs hotter than its neighbours."
              onDragStateChange={(dragging) => onDrag(freqId, dragging)}
              onChange={(value) => onParam(freqId, snapPlain(freqSpec, value))}
            />
            <Slider
              label="Q"
              value={params[qId] ?? qSpec.def}
              min={qSpec.min}
              max={qSpec.max}
              format={formatQ}
              tip="Width of the dynamic cut. Lower Q removes a broader region."
              onDragStateChange={(dragging) => onDrag(qId, dragging)}
              onChange={(value) => onParam(qId, snapPlain(qSpec, value))}
            />
            <Slider
              label="THRESH"
              value={params[thresholdId] ?? thresholdSpec.def}
              min={thresholdSpec.min}
              max={thresholdSpec.max}
              format={formatThreshold}
              tip="How far this band must exceed the adjacent half-octave bands before it cuts. Relative, so playing louder does not trigger it."
              onDragStateChange={(dragging) => onDrag(thresholdId, dragging)}
              onChange={(value) => onParam(thresholdId, snapPlain(thresholdSpec, value))}
            />
            <Slider
              label="RATIO"
              value={params[ratioId] ?? ratioSpec.def}
              min={ratioSpec.min}
              max={ratioSpec.max}
              format={formatRatio}
              tip="How strongly excess energy is reduced. 1:1 is off; higher ratios cut closer to the maximum."
              onDragStateChange={(dragging) => onDrag(ratioId, dragging)}
              onChange={(value) => onParam(ratioId, snapPlain(ratioSpec, value))}
            />
            <Slider
              label="ATTACK"
              value={params[attackId] ?? attackSpec.def}
              min={attackSpec.min}
              max={attackSpec.max}
              skew={attackSpec.skew}
              format={formatTimeMs}
              tip="How quickly the cut engages after excess energy appears."
              onDragStateChange={(dragging) => onDrag(attackId, dragging)}
              onChange={(value) => onParam(attackId, snapPlain(attackSpec, value))}
            />
            <Slider
              label="RELEASE"
              value={params[releaseId] ?? releaseSpec.def}
              min={releaseSpec.min}
              max={releaseSpec.max}
              skew={releaseSpec.skew}
              format={formatTimeMs}
              tip="How quickly the cut recovers after the excess energy stops."
              onDragStateChange={(dragging) => onDrag(releaseId, dragging)}
              onChange={(value) => onParam(releaseId, snapPlain(releaseSpec, value))}
            />
            <Slider
              label="MAX CUT"
              value={params[rangeId] ?? rangeSpec.def}
              min={rangeSpec.min}
              max={rangeSpec.max}
              format={formatCut}
              tip="Ceiling for this band's gain reduction. The engine only ever cuts."
              onDragStateChange={(dragging) => onDrag(rangeId, dragging)}
              onChange={(value) => onParam(rangeId, snapPlain(rangeSpec, value))}
            />
            <div className="dynamic-gr" title="Live gain reduction for this band.">
              <span className="dynamic-gr-label">GR</span>
              <div className="dynamic-gr-track">
                <div className="dynamic-gr-fill" style={{ width: `${meterPercent * 100}%` }} />
              </div>
              <span className="dynamic-gr-value">
                {reduction > 0.05 ? `-${reduction.toFixed(1)} dB` : '0.0 dB'}
              </span>
            </div>
          </div>
        );
      })}
    </div>
  );
}

function ViewToggle({ view, onChange }: { view: ViewMode; onChange: (view: ViewMode) => void }) {
  return (
    <div className="view-toggle" role="group" aria-label="Editor view">
      <button
        type="button"
        className={`view-btn${view === 'simple' ? ' active' : ''}`}
        aria-pressed={view === 'simple'}
        onClick={() => onChange('simple')}
        title="Simple view: presets, macros, shapes and direct graph editing."
      >
        Simple
      </button>
      <button
        type="button"
        className={`view-btn${view === 'advanced' ? ' active' : ''}`}
        aria-pressed={view === 'advanced'}
        onClick={() => onChange('advanced')}
        title="Advanced view: numeric bands, filters and the analyzer."
      >
        Advanced
      </button>
    </div>
  );
}

function AbControls({
  slot,
  onSlot,
  onCopy,
  onSwap,
}: {
  slot: number;
  onSlot: (slot: number) => void;
  onCopy: () => void;
  onSwap: () => void;
}) {
  return (
    <div className="ab-controls" title="A/B holds two complete EQ setups. Output trim stays global for level-matched comparison.">
      <span className="ab-label">A/B</span>
      <div className="ab-segment" role="group" aria-label="A/B slot">
        <button
          type="button"
          className={`ab-btn${slot === 0 ? ' active' : ''}`}
          aria-pressed={slot === 0}
          onClick={() => onSlot(0)}
          title="Recall slot A"
        >
          A
        </button>
        <button
          type="button"
          className={`ab-btn${slot === 1 ? ' active' : ''}`}
          aria-pressed={slot === 1}
          onClick={() => onSlot(1)}
          title="Recall slot B"
        >
          B
        </button>
      </div>
      <button type="button" className="btn compact" onClick={onCopy} title="Copy the active slot to the other slot">
        Copy
      </button>
      <button type="button" className="btn compact" onClick={onSwap} title="Swap the complete A and B setups">
        Swap
      </button>
    </div>
  );
}

interface PresetBrowserProps {
  factoryPresets: FactoryPresetInfo[];
  userPresets: string[];
  current: PresetInfo;
  onChoose: (choice: PresetChoice) => void;
  onSaveAs: () => void;
  onClose: () => void;
}

function PresetBrowser({
  factoryPresets,
  userPresets,
  current,
  onChoose,
  onSaveAs,
  onClose,
}: PresetBrowserProps) {
  const [filter, setFilter] = useState('All');
  const [query, setQuery] = useState('');

  const groups = useMemo(() => {
    const byGenre = new Map<string, Map<string, FactoryPresetInfo[]>>();
    for (const preset of factoryPresets) {
      const subs = byGenre.get(preset.genre) ?? new Map<string, FactoryPresetInfo[]>();
      const list = subs.get(preset.subgenre) ?? [];
      list.push(preset);
      subs.set(preset.subgenre, list);
      byGenre.set(preset.genre, subs);
    }
    return Array.from(byGenre, ([genre, subs]) => ({
      genre,
      subs: Array.from(subs, ([subgenre, presets]) => ({ subgenre, presets })),
    }));
  }, [factoryPresets]);

  const normalizedQuery = query.trim().toLowerCase();
  const matches = useCallback(
    (preset: FactoryPresetInfo) =>
      normalizedQuery.length === 0
      || [preset.name, preset.description, preset.genre, preset.subgenre, preset.placement]
        .some((text) => text.toLowerCase().includes(normalizedQuery)),
    [normalizedQuery],
  );

  const visibleGroups = groups
    .filter((group) => filter === 'All' || filter === group.genre)
    .map((group) => ({
      ...group,
      subs: group.subs
        .map((sub) => ({ ...sub, presets: sub.presets.filter(matches) }))
        .filter((sub) => sub.presets.length > 0),
    }))
    .filter((group) => group.subs.length > 0);

  const showUsers = filter === 'All' || filter === 'User';
  const visibleUsers = showUsers
    ? userPresets.filter((name) => normalizedQuery.length === 0 || name.toLowerCase().includes(normalizedQuery))
    : [];
  const showCustom =
    filter === 'All'
    && (normalizedQuery.length === 0
      || 'custom neutral'.includes(normalizedQuery)
      || 'custom / neutral'.includes(normalizedQuery));

  const currentFactory = current.presetId.length > 0
    ? factoryPresets.find((preset) => preset.id === current.presetId)
    : current.kind === 'factory'
      ? factoryPresets.find((preset) => preset.index === current.index)
      : undefined;

  const genreTabs = ['All', ...groups.map((group) => group.genre), 'User'];

  return (
    <div
      className="browser-backdrop"
      onPointerDown={(event) => event.target === event.currentTarget && onClose()}
    >
      <div
        className="preset-browser"
        role="dialog"
        aria-modal="true"
        aria-label="Tone library"
        onKeyDown={(event) => {
          if (event.key === 'Escape') {
            event.preventDefault();
            onClose();
          }
        }}
      >
        <div className="browser-header">
          <div>
            <div className="browser-title">Tone library</div>
            <div className="browser-subtitle">
              Start with a complete sound, then shape it with the graph, macros and switches.
            </div>
          </div>
          <button type="button" className="btn" onClick={onClose} aria-label="Close tone library">
            Close
          </button>
        </div>

        <div className="browser-search-row">
          <input
            className="browser-search"
            type="search"
            autoFocus
            value={query}
            placeholder="Search tones (e.g. drop tuning, thrash, fizz)…"
            onChange={(event) => setQuery(event.target.value)}
          />
        </div>

        <div className="browser-tabs" role="tablist" aria-label="Tone category">
          {genreTabs.map((genre) => (
            <button
              key={genre}
              type="button"
              role="tab"
              aria-selected={filter === genre}
              className={`browser-tab${filter === genre ? ' active' : ''}`}
              onClick={() => setFilter(genre)}
            >
              {genre}
              {genre === 'User' && userPresets.length > 0 ? ` (${userPresets.length})` : ''}
            </button>
          ))}
        </div>

        <div className="browser-list">
          {showCustom && (
            <section className="browser-group">
              <div className="browser-group-title">Start from scratch</div>
              <button
                type="button"
                className={`preset-card custom${current.kind === 'custom' ? ' current' : ''}`}
                onClick={() => onChoose({ kind: 'custom' })}
              >
                <span className="preset-card-main">
                  <span className="preset-card-name">Custom / Neutral</span>
                  <span className="preset-card-meta">Flat EQ · no filters · no output trim</span>
                </span>
                <span className="preset-card-badge">CUSTOM</span>
              </button>
            </section>
          )}

          {visibleGroups.map((group) => (
            <section className="browser-group" key={group.genre}>
              <div className="browser-group-title">{group.genre}</div>
              {group.subs.map((sub) => (
                <div className="browser-subgroup" key={`${group.genre}:${sub.subgenre}`}>
                  <div className="browser-subgroup-title">{sub.subgenre}</div>
                  <div className="preset-card-grid">
                    {sub.presets.map((preset) => {
                      const isCurrent = currentFactory?.id === preset.id;
                      return (
                        <button
                          key={preset.id}
                          type="button"
                          className={`preset-card${isCurrent ? ' current' : ''}`}
                          onClick={() => onChoose({ kind: 'factory', presetId: preset.id })}
                        >
                          <span className="preset-card-main">
                            <span className="preset-card-name">{preset.name}</span>
                            <span className="preset-card-meta">
                              {preset.genre} · {preset.subgenre} · {preset.placement}
                            </span>
                            <span className="preset-card-description">{preset.description}</span>
                          </span>
                          <span className={`preset-card-badge${isCurrent ? ' active' : ''}`}>
                            {isCurrent ? 'LOADED' : 'FACTORY'}
                          </span>
                        </button>
                      );
                    })}
                  </div>
                </div>
              ))}
            </section>
          ))}

          {visibleUsers.length > 0 && (
            <section className="browser-group">
              <div className="browser-group-title">User presets</div>
              <div className="preset-card-grid">
                {visibleUsers.map((name) => (
                  <button
                    key={name}
                    type="button"
                    className={`preset-card${current.kind === 'user' && current.name === name ? ' current' : ''}`}
                    onClick={() => onChoose({ kind: 'user', name })}
                  >
                    <span className="preset-card-main">
                      <span className="preset-card-name">{name}</span>
                      <span className="preset-card-meta">Saved user preset</span>
                    </span>
                    <span className={`preset-card-badge${current.kind === 'user' && current.name === name ? ' active' : ''}`}>
                      {current.kind === 'user' && current.name === name ? 'LOADED' : 'USER'}
                    </span>
                  </button>
                ))}
              </div>
            </section>
          )}

          {filter === 'User' && visibleUsers.length === 0 && (
            <div className="browser-empty">
              No user presets yet. Dial in a sound, then use Save As to keep it here.
            </div>
          )}
          {filter !== 'User' && visibleGroups.length === 0 && visibleUsers.length === 0 && !showCustom && (
            <div className="browser-empty">No tones match that search.</div>
          )}
        </div>

        <div className="browser-footer">
          <span className="browser-footer-note">
            Factory tones are read-only. User presets are stored with your project/application data.
          </span>
          <button type="button" className="btn primary" onClick={onSaveAs}>
            Save current as…
          </button>
        </div>
      </div>
    </div>
  );
}

function FilterPanel({
  params,
  onParam,
  onDrag,
}: {
  params: Record<string, number>;
  onParam: (id: string, value: number) => void;
  onDrag: (id: string, dragging: boolean) => void;
}) {
  const hpFreq = specOf('hpFreq');
  const lpFreq = specOf('lpFreq');

  return (
    <div className="filter-card-grid">
      <div className="filter-card">
        <div className="filter-card-head">
          <span>HIGH-PASS</span>
          <span className="filter-hint">tighten the low end</span>
        </div>
        <div className="filter-controls">
          <Slider
            label="CUTOFF"
            value={params.hpFreq ?? 80}
            min={hpFreq.min}
            max={hpFreq.max}
            skew={hpFreq.skew}
            format={formatFreqPlain}
            tip="High-pass cutoff. Removes rumble and excess sub-bass; use a lower cutoff on drop tunings."
            onDragStateChange={(dragging) => onDrag('hpFreq', dragging)}
            onChange={(value) => onParam('hpFreq', snapPlain(hpFreq, value))}
          />
          <Select
            className="slope-select"
            value={String(Math.round(params.hpSlope ?? 0))}
            options={SLOPE_CHOICES}
            onChange={(value) => onParam('hpSlope', Number(value))}
            ariaLabel="High-pass slope"
            tip="Steeper slopes remove more low-frequency energy below cutoff."
          />
        </div>
      </div>

      <div className="filter-card">
        <div className="filter-card-head">
          <span>LOW-PASS / HIGH CUT</span>
          <span className="filter-hint">tame fizz and harshness</span>
        </div>
        <div className="filter-controls">
          <Toggle
            label="Enabled"
            checked={(params.lpOn ?? 0) >= 0.5}
            onChange={(enabled) => onParam('lpOn', enabled ? 1 : 0)}
            tip="Enable or bypass the high-frequency low-pass filter."
          />
          <Slider
            label="CUTOFF"
            value={params.lpFreq ?? 12000}
            min={lpFreq.min}
            max={lpFreq.max}
            skew={lpFreq.skew}
            format={formatFreqPlain}
            tip="For post-amp guitars, audition around 8–12 kHz when the top end is fizzy."
            onDragStateChange={(dragging) => onDrag('lpFreq', dragging)}
            onChange={(value) => onParam('lpFreq', snapPlain(lpFreq, value))}
          />
          <Select
            className="slope-select"
            value={String(Math.round(params.lpSlope ?? 0))}
            options={SLOPE_CHOICES}
            onChange={(value) => onParam('lpSlope', Number(value))}
            ariaLabel="Low-pass slope"
            tip="Steeper slopes remove more high frequencies above the cutoff."
          />
        </div>
      </div>
    </div>
  );
}

function ShelfPanel({
  params,
  onParam,
  onDrag,
}: {
  params: Record<string, number>;
  onParam: (id: string, value: number) => void;
  onDrag: (id: string, dragging: boolean) => void;
}) {
  const lsFreq = specOf('lsFreq');
  const lsGain = specOf('lsGain');
  const hsFreq = specOf('hsFreq');
  const hsGain = specOf('hsGain');

  return (
    <div className="shelf-panel">
      <div className="shelf-group">
        <div className="shelf-title">
          <span>LOW SHELF</span>
          <span className="filter-hint">broad weight in the low end</span>
        </div>
        <Slider
          label="FREQ"
          value={params.lsFreq ?? lsFreq.def}
          min={lsFreq.min}
          max={lsFreq.max}
          skew={lsFreq.skew}
          format={formatFreqPlain}
          tip="Low-shelf corner frequency. Boost or cut everything below it as one broad move."
          onDragStateChange={(dragging) => onDrag('lsFreq', dragging)}
          onChange={(value) => onParam('lsFreq', snapPlain(lsFreq, value))}
        />
        <Slider
          label="GAIN"
          value={params.lsGain ?? lsGain.def}
          min={lsGain.min}
          max={lsGain.max}
          format={formatGain}
          tip="Low-shelf gain. Positive adds weight; negative tightens the low end."
          onDragStateChange={(dragging) => onDrag('lsGain', dragging)}
          onChange={(value) => onParam('lsGain', snapPlain(lsGain, value))}
        />
      </div>

      <div className="shelf-group">
        <div className="shelf-title">
          <span>HIGH SHELF</span>
          <span className="filter-hint">broad top-end balance</span>
        </div>
        <Slider
          label="FREQ"
          value={params.hsFreq ?? hsFreq.def}
          min={hsFreq.min}
          max={hsFreq.max}
          skew={hsFreq.skew}
          format={formatFreqPlain}
          tip="High-shelf corner frequency. Adjusts the overall brightness above it."
          onDragStateChange={(dragging) => onDrag('hsFreq', dragging)}
          onChange={(value) => onParam('hsFreq', snapPlain(hsFreq, value))}
        />
        <Slider
          label="GAIN"
          value={params.hsGain ?? hsGain.def}
          min={hsGain.min}
          max={hsGain.max}
          format={formatGain}
          tip="High-shelf gain. Positive adds air and presence; negative darkens the tone."
          onDragStateChange={(dragging) => onDrag('hsGain', dragging)}
          onChange={(value) => onParam('hsGain', snapPlain(hsGain, value))}
        />
      </div>
    </div>
  );
}

function BandPanel({
  params,
  selectedBand,
  onSelectBand,
  onParam,
  onDrag,
}: {
  params: Record<string, number>;
  selectedBand: number;
  onSelectBand: (index: number) => void;
  onParam: (id: string, value: number) => void;
  onDrag: (id: string, dragging: boolean) => void;
}) {
  return (
    <div className="band-panel" role="group" aria-label="Parametric EQ bands">
      {Array.from({ length: NUM_BANDS }, (_, i) => {
        const n = i + 1;
        const freqId = `band${n}Freq`;
        const gainId = `band${n}Gain`;
        const qId = `band${n}Q`;
        const onId = `band${n}On`;
        const freqSpec = specOf(freqId);
        const gainSpec = specOf(gainId);
        const qSpec = specOf(qId);
        return (
          <div
            className={`band-card${selectedBand === i ? ' selected' : ''}`}
            key={n}
            onPointerDown={() => onSelectBand(i)}
          >
            <div className="band-card-head">
              <button
                type="button"
                className={`band-selector${selectedBand === i ? ' active' : ''}`}
                onClick={() => onSelectBand(i)}
                title="Select this band on the graph"
              >
                {n}
              </button>
              <div className="band-title">BAND {n}</div>
              <Toggle
                label="ON"
                checked={(params[onId] ?? 1) >= 0.5}
                onChange={(enabled) => onParam(onId, enabled ? 1 : 0)}
                tip="Enable or bypass this parametric EQ band."
              />
            </div>
            <Slider
              label="FREQ"
              value={params[freqId] ?? freqSpec.def}
              min={freqSpec.min}
              max={freqSpec.max}
              skew={freqSpec.skew}
              format={formatFreqPlain}
              tip="Band centre frequency. Logarithmic range from 60 Hz to 16 kHz."
              onDragStateChange={(dragging) => onDrag(freqId, dragging)}
              onChange={(value) => onParam(freqId, snapPlain(freqSpec, value))}
            />
            <Slider
              label="GAIN"
              value={params[gainId] ?? gainSpec.def}
              min={gainSpec.min}
              max={gainSpec.max}
              format={formatGain}
              tip="Boost or cut this band in decibels."
              onDragStateChange={(dragging) => onDrag(gainId, dragging)}
              onChange={(value) => onParam(gainId, snapPlain(gainSpec, value))}
            />
            <Slider
              label="Q"
              value={params[qId] ?? qSpec.def}
              min={qSpec.min}
              max={qSpec.max}
              format={formatQ}
              tip="Bandwidth: lower Q is broad, higher Q is narrow."
              onDragStateChange={(dragging) => onDrag(qId, dragging)}
              onChange={(value) => onParam(qId, snapPlain(qSpec, value))}
            />
          </div>
        );
      })}
    </div>
  );
}

//----------------------------------------------------------------------------

type ModalState =
  | { kind: 'saveAs' }
  | { kind: 'overwrite'; name: string }
  | { kind: 'rename' }
  | { kind: 'delete'; name: string }
  | { kind: 'error'; message: string };

export function App(): React.JSX.Element {
  const state = useUiState();

  // The JUCE backend exists only inside the plugin; anywhere else (npm run
  // dev, vite preview, headless screenshots) we run the same protocol
  // against the browser mock.
  const [bridge] = useState<MetalEqBridge>(() =>
    isJuceBackendAvailable() ? new JuceBridge() : new MockBridge(),
  );
  const isMock = useMemo(() => !isJuceBackendAvailable(), []);

  const [view, setView] = useState<ViewMode>(initialView);
  // Mock-only screenshot aid: ?browser=1 opens the tone library on load.
  const [browserOpen, setBrowserOpen] = useState(
    () => !isJuceBackendAvailable()
      && new URLSearchParams(window.location.search).has('browser'),
  );
  const [modal, setModal] = useState<ModalState | null>(null);
  const [manageMenuOpen, setManageMenuOpen] = useState(false);
  const [amount, setAmount] = useState(0.75);
  const [profile, setProfile] = useState(0);
  const [selectedBand, setSelectedBand] = useState(0);
  const [selectedCandidates, setSelectedCandidates] = useState<number[]>([]);
  const [hoveredCandidate, setHoveredCandidate] = useState<number | null>(null);
  const manageRootRef = useRef<HTMLDivElement>(null);
  const graphSlotRef = useRef<HTMLDivElement>(null);
  const [graphSize, setGraphSize] = useState({ width: 924, height: 292 });

  const analyzerReady = state.analyzer.status === AnalyzerStatus.ready;
  const capturing = state.analyzer.status === AnalyzerStatus.capturing;
  const resonance = state.resonance;
  const surgicalActive = useMemo(() => {
    for (let i = 0; i < NUM_SURGICAL_BANDS; i++) {
      const n = i + 1;
      if ((state.params[`res${n}On`] ?? 0) >= 0.5 && Math.abs(state.params[`res${n}Gain`] ?? 0) > 1e-3) {
        return true;
      }
    }
    return false;
  }, [state.params]);

  const dynamicActive = useMemo(() => {
    for (let i = 0; i < NUM_DYNAMIC_BANDS; i++) {
      const n = i + 1;
      if ((state.params[`dyn${n}On`] ?? 0) >= 0.5
        && (state.params[`dyn${n}Range`] ?? 0) > 1e-3
        && (state.params[`dyn${n}Ratio`] ?? 0) > 1.000001) {
        return true;
      }
    }
    return false;
  }, [state.params]);

  // A new scan invalidates the previous selection. Keep indices in range if a
  // tick ever arrives with a shorter candidate list.
  useEffect(() => {
    if (resonance.status !== AnalyzerStatus.ready) {
      setSelectedCandidates([]);
      setHoveredCandidate(null);
    }
  }, [resonance.status]);

  useEffect(() => {
    setSelectedCandidates((previous) => previous.filter((index) => index < resonance.candidates.length));
  }, [resonance.candidates.length]);

  const analyzerTargets = state.analyzerTargets;
  const targetOptions = useMemo<SelectOption[]>(
    () => analyzerTargets.map((target) => ({ value: String(target.index), label: target.label })),
    [analyzerTargets],
  );

  //----------------------------------------------------------------------
  // Bridge lifecycle: uiReady() -> snapshot, then metaleq.tick stream.
  useEffect(() => {
    let cancelled = false;
    bridge
      .init({ onTick: (tick) => store.applyTick(tick) })
      .then((snapshot) => {
        if (cancelled) return;
        store.applySnapshot(snapshot);
        if (snapshot.protocolVersion !== PROTOCOL_VERSION) {
          store.setError(
            `UI/native protocol mismatch (native v${snapshot.protocolVersion}, UI v${PROTOCOL_VERSION}).`,
          );
        }

        // Mock-only QA aid: ?preset=N applies a canonical preset, ?capture=1
        // starts the simulated analyzer. These never trigger in the plugin.
        if (isMock) {
          const query = new URLSearchParams(window.location.search);
          const presetParam = query.get('preset');
          if (presetParam != null) {
            const index = Number(presetParam);
            if (Number.isFinite(index)) void bridge.applyPreset(index);
          }
          if (query.has('capture')) void bridge.startAnalyzerCapture(0);
        }
      })
      .catch((error: unknown) => {
        if (!cancelled) store.setError(`Failed to initialise the UI bridge: ${String(error)}`);
      });
    return () => {
      cancelled = true;
    };
  }, [bridge, isMock]);

  // Tell the native editor once whether this session starts in Advanced.
  // The toggle path calls setEditorExpanded directly on every change.
  const didApplyInitialSize = useRef(false);
  useEffect(() => {
    if (!state.ready || didApplyInitialSize.current) return;
    didApplyInitialSize.current = true;
    void bridge.setEditorExpanded(view === 'advanced');
  }, [bridge, state.ready, view]);

  // Graph slot tracks its container so the SVG fills whatever the host
  // window gives us (native editor is resizable 780..1440 px wide).
  useLayoutEffect(() => {
    const slot = graphSlotRef.current;
    if (slot == null) return;
    const measure = () =>
      setGraphSize({ width: Math.max(320, slot.clientWidth), height: Math.max(200, slot.clientHeight) });
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(slot);
    return () => observer.disconnect();
  }, [state.ready]);

  // Close the Manage menu on outside click / Escape.
  useEffect(() => {
    if (!manageMenuOpen) return;
    const onDown = (event: PointerEvent) => {
      if (!manageRootRef.current?.contains(event.target as Node)) setManageMenuOpen(false);
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setManageMenuOpen(false);
    };
    document.addEventListener('pointerdown', onDown);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('pointerdown', onDown);
      document.removeEventListener('keydown', onKey);
    };
  }, [manageMenuOpen]);

  useEffect(() => {
    if (targetOptions.length === 0) {
      if (profile !== 0) setProfile(0);
      return;
    }
    if (profile >= targetOptions.length) setProfile(0);
  }, [profile, targetOptions.length]);

  // Suggested-correction curve: fetched whenever the analyzer is ready and
  // the amount changes (debounced so dragging the slider stays cheap).
  useEffect(() => {
    if (!analyzerReady) {
      store.setSuggestionCurve(null);
      return;
    }
    const timer = window.setTimeout(() => {
      void bridge.getSuggestionCurve(amount).then((result) => {
        if (result.ok && result.curve != null) store.setSuggestionCurve(result.curve);
      });
    }, 150);
    return () => window.clearTimeout(timer);
  }, [bridge, analyzerReady, amount]);

  //----------------------------------------------------------------------
  // Parameter write: optimistic local echo, then the authoritative value.
  const setParam = useCallback(
    (id: string, value: number) => {
      store.setParamLocal(id, value);
      void bridge.setParameter(id, value).then((result) => {
        if (result.ok && typeof result.value === 'number') store.setParamAuthoritative(id, result.value);
        else if (!result.ok) store.setError(result.error ?? `setParameter(${id}) failed.`);
      });
    },
    [bridge],
  );

  const knobChange = useCallback((id: string, value: number) => setParam(id, value), [setParam]);
  const knobDrag = useCallback((id: string, dragging: boolean) => store.setDragging(dragging ? id : null), []);

  const changeView = useCallback(
    (next: ViewMode) => {
      setView(next);
      rememberView(next);
      void bridge.setEditorExpanded(next === 'advanced');
      if (next === 'advanced') setBrowserOpen(false);
    },
    [bridge],
  );

  // Direct graph editing. Snap exactly like the numeric controls so dragging
  // and typing produce the same values.
  const onBandChange = useCallback(
    (index: number, freq: number, gainDb: number) => {
      const n = index + 1;
      const freqSpec = specOf(`band${n}Freq`);
      const gainSpec = specOf(`band${n}Gain`);
      setParam(`band${n}Freq`, snapPlain(freqSpec, freq));
      setParam(`band${n}Gain`, snapPlain(gainSpec, gainDb));
      setSelectedBand(index);
    },
    [setParam],
  );

  const onCrossoverChange = useCallback(
    (kind: 'hp' | 'lp', freq: number) => {
      const spec = specOf(kind === 'hp' ? 'hpFreq' : 'lpFreq');
      setParam(spec.id, snapPlain(spec, freq));
    },
    [setParam],
  );

  //----------------------------------------------------------------------
  // Preset flows (mirror the native comboBox + AlertWindow behaviour).
  const showError = useCallback((message: string) => setModal({ kind: 'error', message }), []);

  const userPresetExists = useCallback(
    (name: string) => state.userPresets.some((n) => n.toLowerCase() === name.toLowerCase()),
    [state.userPresets],
  );

  const recallPreset = useCallback(
    (choice: PresetChoice) => {
      setBrowserOpen(false);
      const operation =
        choice.kind === 'custom'
          ? bridge.applyPreset(0)
          : choice.kind === 'factory'
            ? bridge.applyPresetById(choice.presetId)
            : bridge.loadUserPreset(choice.name);
      void operation.then((result) => {
        if (!result.ok) showError(result.error ?? 'Preset operation failed.');
      });
    },
    [bridge, showError],
  );

  const savePresetNamed = useCallback(
    (name: string, overwrite: boolean) => {
      void bridge.saveUserPreset(name, overwrite).then((result) => {
        if (!result.ok) showError(result.error ?? 'Saving the user preset failed.');
      });
    },
    [bridge, showError],
  );

  const openSaveAs = useCallback(() => {
    setManageMenuOpen(false);
    setBrowserOpen(false);
    setModal({ kind: 'saveAs' });
  }, []);

  const onSaveAsConfirmed = useCallback(
    (text: string | undefined) => {
      const name = (text ?? '').trim();
      setModal(null);
      if (name.length === 0) {
        showError('Enter a preset name.');
        return;
      }
      if (userPresetExists(name)) setModal({ kind: 'overwrite', name });
      else savePresetNamed(name, false);
    },
    [savePresetNamed, showError, userPresetExists],
  );

  const onSaveClicked = useCallback(() => {
    if (state.preset.kind !== 'user') openSaveAs();
    else setModal({ kind: 'overwrite', name: state.preset.name });
  }, [openSaveAs, state.preset]);

  const onRenameConfirmed = useCallback(
    (text: string | undefined) => {
      const oldName = state.preset.kind === 'user' ? state.preset.name : '';
      const newName = (text ?? '').trim();
      setModal(null);
      if (oldName.length === 0) {
        showError('Select or load a user preset first. Factory presets are read-only.');
        return;
      }
      if (newName.length === 0) {
        showError('Enter a preset name.');
        return;
      }
      void bridge.renameUserPreset(oldName, newName).then((result) => {
        if (!result.ok) showError(result.error ?? 'Renaming the user preset failed.');
      });
    },
    [bridge, showError, state.preset],
  );

  const onDeleteConfirmed = useCallback(() => {
    const name = state.preset.kind === 'user' ? state.preset.name : '';
    setModal(null);
    if (name.length === 0) {
      showError('Select or load a user preset first. Factory presets are read-only.');
      return;
    }
    void bridge.deleteUserPreset(name).then((result) => {
      if (!result.ok) showError(result.error ?? 'Deleting the user preset failed.');
    });
  }, [bridge, showError, state.preset]);

  const onRevert = useCallback(() => {
    setManageMenuOpen(false);
    void bridge.revertToPreset().then((result) => {
      if (!result.ok) showError(result.error ?? 'Reverting failed.');
    });
  }, [bridge, showError]);

  const onReset = useCallback(() => {
    void bridge.resetToNeutral().then((result) => {
      if (!result.ok) showError(result.error ?? 'Reset failed.');
    });
  }, [bridge, showError]);

  const onAbSlot = useCallback(
    (slot: number) => {
      void bridge.setAbSlot(slot).then((result) => {
        if (!result.ok) showError(result.error ?? 'Could not switch A/B slot.');
      });
    },
    [bridge, showError],
  );

  const onAbCopy = useCallback(() => {
    void bridge.copyAbToOther().then((result) => {
      if (!result.ok) showError(result.error ?? 'Could not copy the A/B slot.');
    });
  }, [bridge, showError]);

  const onAbSwap = useCallback(() => {
    void bridge.swapAbSlots().then((result) => {
      if (!result.ok) showError(result.error ?? 'Could not swap the A/B slots.');
    });
  }, [bridge, showError]);

  const onCaptureClicked = useCallback(() => {
    if (targetOptions.length === 0) return;
    const operation = capturing
      ? bridge.cancelAnalyzerCapture()
      : bridge.startAnalyzerCapture(profile);
    void operation.then((result) => {
      if (!result.ok) showError(result.error ?? 'Analyzer operation failed.');
    });
  }, [bridge, capturing, profile, showError, targetOptions.length]);

  const onApplySuggestion = useCallback(() => {
    void bridge.applyAnalyzerSuggestion(amount).then((result) => {
      if (!result.ok) showError(result.error ?? 'Applying the suggestion failed.');
    });
  }, [bridge, amount, showError]);

  const onResonanceScan = useCallback(() => {
    void bridge.startResonanceScan().then((result) => {
      if (!result.ok) showError(result.error ?? 'Could not start the resonance scan.');
    });
  }, [bridge, showError]);

  const onResonanceCancel = useCallback(() => {
    void bridge.cancelResonanceScan().then((result) => {
      if (!result.ok) showError(result.error ?? 'Could not stop the resonance scan.');
    });
  }, [bridge, showError]);

  const onToggleCandidate = useCallback(
    (index: number) => {
      const alreadySelected = selectedCandidates.includes(index);
      if (!alreadySelected && selectedCandidates.length >= NUM_SURGICAL_BANDS) {
        showError(`The scan can apply at most ${NUM_SURGICAL_BANDS} candidates. Deselect one first.`);
        return;
      }
      setSelectedCandidates((previous) => {
        if (previous.includes(index)) return previous.filter((value) => value !== index);
        return [...previous, index].sort((a, b) => a - b);
      });
      setHoveredCandidate(index);
    },
    [selectedCandidates, showError],
  );

  const onApplyResonance = useCallback(() => {
    if (selectedCandidates.length === 0) return;
    void bridge.applyResonanceSelection(selectedCandidates).then((result) => {
      if (!result.ok) showError(result.error ?? 'Applying the resonance selection failed.');
    });
  }, [bridge, selectedCandidates, showError]);

  const onClearSurgical = useCallback(() => {
    setParam('resAudition', 0);
    void bridge.clearSurgicalBands().then((result) => {
      if (!result.ok) showError(result.error ?? 'Could not clear the surgical bands.');
    });
  }, [bridge, setParam, showError]);

  const onAuditionCuts = useCallback(
    (enabled: boolean) => setParam('resAudition', enabled ? 1 : 0),
    [setParam],
  );

  const activeFactory = useMemo(() => {
    if (state.preset.presetId.length > 0) {
      return state.factoryPresets.find((preset) => preset.id === state.preset.presetId);
    }
    return state.preset.kind === 'factory'
      ? state.factoryPresets.find((preset) => preset.index === state.preset.index)
      : undefined;
  }, [state.factoryPresets, state.preset]);

  const presetMeta = activeFactory != null
    ? `${activeFactory.genre} · ${activeFactory.subgenre} · ${activeFactory.placement}`
    : state.preset.kind === 'user'
      ? 'User preset · saved on this machine'
      : 'Custom starting point · no factory identity';

  const presetDescription = activeFactory?.description
    ?? (state.preset.kind === 'user'
      ? 'Your saved EQ setup. Save changes to keep them, or use Revert to return to the last saved version.'
      : 'A blank slate for building a tone from scratch. Start with the graph, macros, or the tone library.');

  //----------------------------------------------------------------------
  if (!state.ready) {
    return (
      <div className="metaleq-app loading">
        <div className="loading-text">Connecting to MetalEQ…</div>
      </div>
    );
  }

  const macroKnobs = MACRO_KNOBS.map((knob) => {
    const spec = specOf(knob.id);
    const isTrim = knob.id === 'trim';
    return (
      <div className="macro-cell" key={knob.id}>
        <div className="macro-title">{knob.title}</div>
        <Knob
          spec={spec}
          value={state.params[knob.id] ?? spec.def}
          format={isTrim ? formatTrim : formatPercent}
          title={knob.title}
          tip={knob.tip}
          onChange={(value) => knobChange(knob.id, value)}
          onDragStateChange={(dragging) => knobDrag(knob.id, dragging)}
        />
      </div>
    );
  });

  const shapeToggles = (
    <div className="shapes-row">
      {SHAPES.map((shape) => (
        <Toggle
          key={shape.id}
          label={shape.label}
          tip={shape.tip}
          checked={(state.params[shape.id] ?? 0) >= 0.5}
          onChange={(enabled) => setParam(shape.id, enabled ? 1 : 0)}
        />
      ))}
      <button
        type="button"
        className="btn reset-btn"
        title="Reset all EQ and macros to neutral. Shape toggles are switched off."
        onClick={onReset}
      >
        RESET EQ
      </button>
    </div>
  );

  const analyzerPanel = (
    <section className="analyzer-section">
      <div className="section-heading">
        <span className="section-title">Analyzer</span>
        <span className="section-hint">match a captured spectrum to a reference tone</span>
      </div>
      <div className="analyzer-row">
        <span className="row-label target-label">TARGET</span>
        <Select
          className="target-select"
          value={String(profile)}
          options={targetOptions}
          onChange={(value) => setProfile(Number(value))}
          ariaLabel="Analyzer target profile"
          tip="The analyzer compares the captured spectrum with this target; it does not detect the source genre."
        />
        <button
          type="button"
          className="btn capture-btn"
          onClick={onCaptureClicked}
          disabled={targetOptions.length === 0}
        >
          {capturing ? 'Stop capture' : 'Capture 8 seconds'}
        </button>
        <span className="row-label amount-label">AMOUNT</span>
        <Slider
          label=""
          value={amount}
          min={0}
          max={1}
          format={formatPercent}
          onDragStateChange={(dragging) => knobDrag('__amount__', dragging)}
          onChange={setAmount}
        />
        <div className="analyzer-spacer" />
        <button type="button" className="btn primary apply-btn" disabled={!analyzerReady} onClick={onApplySuggestion}>
          Apply suggestion
        </button>
      </div>
      <div className="analyzer-status">
        {analyzerStatusText(state.analyzer.status, state.analyzer.progress, state.analyzer.suggestion)}
      </div>
      <div className="analyzer-disclaimer">
        Average-spectrum suggestion only. Capture is temporary and never saved in presets.
      </div>
    </section>
  );

  return (
    <div className={`metaleq-app ${view}`}>
      {isMock && <div className="mock-badge">UI preview · mock data (not the plugin)</div>}
      {state.lastError != null && (
        <div className="error-banner" role="alert">
          <span>{state.lastError}</span>
          <button type="button" className="btn" onClick={() => store.setError(null)}>
            Dismiss
          </button>
        </div>
      )}

      {/*------------------------------------------------ header */}
      <div className="header-row">
        <span className="app-title">MetalEQ</span>
        <span className="app-subtitle">Zero-latency guitar EQ · before or after an amp</span>
        <div className="header-spacer" />
        <Meter title="INPUT PEAK" peak={state.meters.in} />
        <Meter title="OUTPUT PEAK" peak={state.meters.out} />
        <ViewToggle view={view} onChange={changeView} />
      </div>

      {/*------------------------------------------------ tone identity / actions */}
      <section className="tone-hero">
        <div className="tone-identity">
          <div className="tone-eyebrow">
            <span>{view === 'simple' ? 'SIMPLE TONE' : 'ADVANCED EDITOR'}</span>
            <span className={`preset-state ${state.preset.dirty ? 'dirty' : ''}`}>
              {presetStateText(state.preset)}
            </span>
          </div>
          <div className="tone-name-row">
            <h1 className="tone-name">{state.preset.name}</h1>
            <span className="tone-ab-badge">{state.preset.abSlot === 1 ? 'B' : 'A'}</span>
          </div>
          <div className="tone-meta">{presetMeta}</div>
          <p className="tone-description">{presetDescription}</p>
        </div>

        <div className="tone-actions">
          <div className="tone-action-row">
            <button
              type="button"
              className="btn primary browse-btn"
              onClick={() => setBrowserOpen(true)}
              title="Browse factory tones by genre and subgenre, or load a user preset."
            >
              Browse tones
            </button>
            <button
              type="button"
              className="btn"
              onClick={onSaveClicked}
              title="Overwrite the selected user preset after confirmation, or save this sound as a new user preset."
            >
              Save
            </button>
            <div className="manage-root" ref={manageRootRef}>
              <button
                type="button"
                className={`btn ${manageMenuOpen ? 'active' : ''}`}
                title="Manage user presets and revert the loaded preset."
                onClick={() => setManageMenuOpen((open) => !open)}
              >
                Manage
              </button>
              {manageMenuOpen && (
                <div className="manage-menu" role="menu">
                  <button type="button" role="menuitem" onClick={openSaveAs}>
                    Save As…
                  </button>
                  <button
                    type="button"
                    role="menuitem"
                    disabled={state.preset.kind !== 'user'}
                    onClick={() => {
                      setManageMenuOpen(false);
                      setModal({ kind: 'rename' });
                    }}
                  >
                    Rename…
                  </button>
                  <button
                    type="button"
                    role="menuitem"
                    disabled={state.preset.kind !== 'user'}
                    onClick={() => {
                      setManageMenuOpen(false);
                      setModal({ kind: 'delete', name: state.preset.name });
                    }}
                  >
                    Delete…
                  </button>
                  <div className="manage-separator" />
                  <button type="button" role="menuitem" disabled={!state.preset.dirty} onClick={onRevert}>
                    Revert to loaded preset
                  </button>
                </div>
              )}
            </div>
          </div>

          <div className="tone-action-row">
            <AbControls
              slot={state.preset.abSlot}
              onSlot={onAbSlot}
              onCopy={onAbCopy}
              onSwap={onAbSwap}
            />
            <Toggle
              label="Bypass"
              checked={(state.params.bypass ?? 0) >= 0.5}
              onChange={(enabled) => setParam('bypass', enabled ? 1 : 0)}
              variant="accent"
              tip="Bypass the EQ without changing any settings."
            />
          </div>
        </div>
      </section>

      {/*------------------------------------------------ EQ graph */}
      <div className="graph-slot" ref={graphSlotRef}>
        <EqGraph
          width={graphSize.width}
          height={graphSize.height}
          curve={state.curve}
          params={state.params}
          suggestionCurve={analyzerReady ? state.suggestionCurve : null}
          resonanceCandidates={resonance.status === AnalyzerStatus.ready ? resonance.candidates : []}
          selectedCandidates={selectedCandidates}
          hoveredCandidate={hoveredCandidate}
          onToggleCandidate={onToggleCandidate}
          onHoverCandidate={setHoveredCandidate}
          editable
          selectedBand={selectedBand}
          onSelectBand={setSelectedBand}
          onBandChange={onBandChange}
          onCrossoverChange={onCrossoverChange}
        />
      </div>

      {/*------------------------------------------------ macro knobs */}
      <div className="knobs-row">{macroKnobs}</div>

      {/*------------------------------------------------ shapes */}
      {shapeToggles}

      {view === 'simple' ? (
        <>
          <FilterPanel params={state.params} onParam={setParam} onDrag={knobDrag} />
          <div className="simple-tip">
            <strong>Quick start:</strong> pick a preset from the tone library, then use TIGHT, BODY and
            BITE to fit it to your guitar. Drag a numbered node on the graph for precise frequency and gain.
          </div>
        </>
      ) : (
        <>
          <section className="parameter-section">
            <div className="section-heading">
              <span className="section-title">Parametric bands</span>
              <span className="section-hint">drag a band on the graph, or use the numeric controls below</span>
            </div>
            <BandPanel
              params={state.params}
              selectedBand={selectedBand}
              onSelectBand={setSelectedBand}
              onParam={setParam}
              onDrag={knobDrag}
            />
          </section>

          <section className="parameter-section">
            <div className="section-heading">
              <span className="section-title">Filters &amp; shelves</span>
              <span className="section-hint">remove rumble, tame fizz, and balance the broad extremes</span>
            </div>
            <FilterPanel params={state.params} onParam={setParam} onDrag={knobDrag} />
            <ShelfPanel params={state.params} onParam={setParam} onDrag={knobDrag} />
          </section>

          <ResonancePanel
            info={resonance}
            selected={selectedCandidates}
            hovered={hoveredCandidate}
            surgicalActive={surgicalActive}
            dynamicActive={dynamicActive}
            audition={(state.params.resAudition ?? 0) >= 0.5}
            onScan={onResonanceScan}
            onCancel={onResonanceCancel}
            onToggleCandidate={onToggleCandidate}
            onHoverCandidate={setHoveredCandidate}
            onApply={onApplyResonance}
            onClear={onClearSurgical}
            onAudition={onAuditionCuts}
          />

          <section className="parameter-section">
            <div className="section-heading">
              <span className="section-title">Surgical resonance cuts</span>
              <span className="section-hint">
                six narrow, independent bands · scan suggestions never overwrite the five musical bands
              </span>
            </div>
            <SurgicalPanel params={state.params} onParam={setParam} onDrag={knobDrag} />
          </section>

          <section className="parameter-section dynamic-section">
            <div className="section-heading">
              <span className="section-title">Dynamic resonance cuts</span>
              <span className="section-hint">
                four zero-latency bands · cut only when a band exceeds its neighbouring half-octaves
              </span>
            </div>
            <DynamicPanel
              params={state.params}
              reductions={state.dynamicReduction}
              onParam={setParam}
              onDrag={knobDrag}
            />
          </section>

          {analyzerPanel}
        </>
      )}

      <div className="app-footer">
        <span>MetalEQ · zero added latency · output trim is global for A/B</span>
        <span>Factory tones are read-only; user presets live in your application data.</span>
      </div>

      {/*------------------------------------------------ modals */}
      {modal?.kind === 'saveAs' && (
        <Modal
          title="Save user preset"
          message="Choose a name for this EQ sound."
          initialText="My Guitar Tone"
          confirmLabel="Save"
          onConfirm={onSaveAsConfirmed}
          onClose={() => setModal(null)}
        />
      )}
      {modal?.kind === 'overwrite' && (
        <Modal
          title="Overwrite user preset?"
          message={`Replace ‘${modal.name}’ with the current settings?`}
          confirmLabel="Overwrite"
          onConfirm={() => {
            const name = modal.name;
            setModal(null);
            savePresetNamed(name, true);
          }}
          onClose={() => setModal(null)}
        />
      )}
      {modal?.kind === 'rename' && (
        <Modal
          title="Rename user preset"
          message="Enter a new preset name."
          initialText={state.preset.kind === 'user' ? state.preset.name : ''}
          confirmLabel="Rename"
          onConfirm={onRenameConfirmed}
          onClose={() => setModal(null)}
        />
      )}
      {modal?.kind === 'delete' && (
        <Modal
          title="Delete user preset?"
          message={`Permanently delete ‘${modal.name}’?`}
          confirmLabel="Delete"
          onConfirm={onDeleteConfirmed}
          onClose={() => setModal(null)}
        />
      )}
      {modal?.kind === 'error' && (
        <Modal
          title="MetalEQ preset"
          message={modal.message}
          confirmLabel="OK"
          cancelLabel=""
          onConfirm={() => undefined}
          onClose={() => setModal(null)}
        />
      )}

      {browserOpen && (
        <PresetBrowser
          factoryPresets={state.factoryPresets}
          userPresets={state.userPresets}
          current={state.preset}
          onChoose={recallPreset}
          onSaveAs={openSaveAs}
          onClose={() => setBrowserOpen(false)}
        />
      )}
    </div>
  );
}

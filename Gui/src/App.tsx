/*
    MetalEQ WebView UI - root component.

    Layout mirrors Source/native/MetalEQNativeFallbackEditor.cpp resized():
    header (title + peak meters), preset row, EQ graph, macro knobs, shape
    toggles, high-pass row, low-pass row, Fine EQ toggle (+ 220 px band
    panel), analyzer target row, status line and disclaimer.

    Every control sends requests through the bridge; nothing here is
    authoritative. Authoritative values come back via metaleq.tick and the
    setParameter() result and overwrite local state.
*/

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { isJuceBackendAvailable, JuceBridge } from './bridge';
import { MockBridge } from './mockBridge';
import { store, useUiState } from './store';
import { AnalyzerStatus, PROTOCOL_VERSION } from './types';
import type { MetalEqBridge, ParamSpec, PresetInfo } from './types';
import {
  NUM_BANDS,
  formatFreqPlain,
  formatGain,
  formatPercent,
  formatQ,
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

// Target profiles, mirroring targetCombo in the native editor. The analyzer
// compares the captured spectrum with the selected profile's target curve.
const TARGET_PROFILES = [
  'Metalcore · Tight Rhythm',
  'Thrash · Technical Clarity',
  'Classic Thrash · Midrange Punch',
  'Post-Amp · Mix Cleanup',
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

function specOf(id: string): ParamSpec {
  const spec = getSpec(id);
  if (spec == null) throw new Error(`missing param spec: ${id}`);
  return spec;
}

type ModalState =
  | { kind: 'saveAs' }
  | { kind: 'overwrite'; name: string }
  | { kind: 'rename' }
  | { kind: 'delete'; name: string }
  | { kind: 'error'; message: string };

/** Mirror of the native preset state label (FACTORY / USER / CUSTOM + dirty *). */
function presetStateText(preset: PresetInfo): string {
  const kind = preset.kind === 'user' ? 'USER' : preset.kind === 'factory' ? 'FACTORY' : 'CUSTOM';
  return preset.dirty ? `${kind} *` : kind;
}

/** Mirror of MetalEQNativeFallbackEditor::updateAnalyzerUi() strings. */
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

export function App(): React.JSX.Element {
  const state = useUiState();

  // The JUCE backend exists only inside the plugin; anywhere else (npm run
  // dev, vite preview, headless screenshots) we run the same protocol
  // against the browser mock.
  const [bridge] = useState<MetalEqBridge>(() =>
    isJuceBackendAvailable() ? new JuceBridge() : new MockBridge(),
  );
  const isMock = useMemo(() => !isJuceBackendAvailable(), []);

  const [amount, setAmount] = useState(0.75);
  const [profile, setProfile] = useState(0);
  // Mock-only screenshot/QA aid: ?fine=1 opens the Fine EQ panel at startup,
  // ?preset=N applies factory preset N (index as reported by the bridge).
  // The plugin always serves the fixed index.html, so these never trigger
  // inside the real editor.
  const [fineOpen, setFineOpen] = useState(
    () => isMock && new URLSearchParams(window.location.search).has('fine'),
  );
  const [modal, setModal] = useState<ModalState | null>(null);
  const [manageMenuOpen, setManageMenuOpen] = useState(false);
  const manageRootRef = useRef<HTMLDivElement>(null);
  const graphSlotRef = useRef<HTMLDivElement>(null);
  const [graphSize, setGraphSize] = useState({ width: 924, height: 292 });

  const analyzerReady = state.analyzer.status === AnalyzerStatus.ready;
  const capturing = state.analyzer.status === AnalyzerStatus.capturing;

  //----------------------------------------------------------------------
  // Bridge lifecycle: uiReady() -> snapshot, then metaleq.tick stream.
  useEffect(() => {
    let cancelled = false;
    bridge
      .init({ onTick: (t) => store.applyTick(t) })
      .then((snapshot) => {
        if (cancelled) return;
        store.applySnapshot(snapshot);
        if (snapshot.protocolVersion !== PROTOCOL_VERSION) {
          store.setError(
            `UI/native protocol mismatch (native v${snapshot.protocolVersion}, UI v${PROTOCOL_VERSION}).`,
          );
        }
        // Mock-only QA aid (see fineOpen): ?preset=N applies a factory preset
        // so screenshots can capture a non-flat response curve, and
        // ?capture=1 starts the simulated analyzer (with headless virtual
        // time the 8 s capture completes and the suggestion curve renders).
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
  }, [bridge]);

  // Graph slot tracks its container so the SVG fills whatever the host
  // window gives us (native editor is resizable 780..1440 px wide).
  useEffect(() => {
    const slot = graphSlotRef.current;
    if (slot == null) return;
    const measure = () =>
      setGraphSize({ width: Math.max(320, slot.clientWidth), height: Math.max(200, slot.clientHeight) });
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(slot);
    return () => observer.disconnect();
  }, []);

  // Close the Manage menu on outside click / Escape.
  useEffect(() => {
    if (!manageMenuOpen) return;
    const onDown = (e: PointerEvent) => {
      if (!manageRootRef.current?.contains(e.target as Node)) setManageMenuOpen(false);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setManageMenuOpen(false);
    };
    document.addEventListener('pointerdown', onDown);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('pointerdown', onDown);
      document.removeEventListener('keydown', onKey);
    };
  }, [manageMenuOpen]);

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

  //----------------------------------------------------------------------
  // Preset flows (mirror the native comboBox + AlertWindow behaviour).
  const showError = useCallback((message: string) => setModal({ kind: 'error', message }), []);

  const presetValue = useMemo(() => {
    if (state.preset.kind === 'user') return `user:${state.preset.name}`;
    if (state.preset.kind === 'factory') return `f${state.preset.index}`;
    return 'custom';
  }, [state.preset]);

  const presetOptions = useMemo<SelectOption[]>(() => {
    const options: SelectOption[] = [{ value: 'custom', label: 'Custom / Neutral' }];
    for (const p of state.factoryPresets) options.push({ value: `f${p.index}`, label: p.name });
    if (state.userPresets.length > 0) {
      options.push({ value: '__separator__', label: '──────────', disabled: true });
      for (const name of state.userPresets) options.push({ value: `user:${name}`, label: `User · ${name}` });
    }
    return options;
  }, [state.factoryPresets, state.userPresets]);

  const onPresetSelected = useCallback(
    (value: string) => {
      const op =
        value === 'custom'
          ? bridge.applyPreset(0)
          : value.startsWith('user:')
            ? bridge.loadUserPreset(value.slice('user:'.length))
            : bridge.applyPreset(Number(value.slice(1)));
      void op.then((result) => {
        if (!result.ok) showError(result.error ?? 'Preset operation failed.');
      });
    },
    [bridge, showError],
  );

  const userPresetExists = useCallback(
    (name: string) => state.userPresets.some((n) => n.toLowerCase() === name.toLowerCase()),
    [state.userPresets],
  );

  const savePresetNamed = useCallback(
    (name: string, overwrite: boolean) => {
      void bridge.saveUserPreset(name, overwrite).then((result) => {
        if (!result.ok) showError(result.error ?? 'Saving the user preset failed.');
      });
    },
    [bridge, showError],
  );

  const openSaveAs = useCallback(() => setModal({ kind: 'saveAs' }), []);

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

  //----------------------------------------------------------------------
  // Fine EQ: mirrors the native FINE EQ + / - button; the native side grows
  // the host window by 220 px via setEditorExpanded.
  const toggleFine = useCallback(() => {
    setFineOpen((open) => {
      const next = !open;
      void bridge.setEditorExpanded(next);
      return next;
    });
  }, [bridge]);

  const onCaptureClicked = useCallback(() => {
    const op = capturing ? bridge.cancelAnalyzerCapture() : bridge.startAnalyzerCapture(profile);
    void op.then((result) => {
      if (!result.ok) showError(result.error ?? 'Analyzer operation failed.');
    });
  }, [bridge, capturing, profile, showError]);

  const onApplySuggestion = useCallback(() => {
    void bridge.applyAnalyzerSuggestion(amount).then((result) => {
      if (!result.ok) showError(result.error ?? 'Applying the suggestion failed.');
    });
  }, [bridge, amount, showError]);

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
          onChange={(v) => knobChange(knob.id, v)}
          onDragStateChange={(d) => knobDrag(knob.id, d)}
        />
      </div>
    );
  });

  return (
    <div className="metaleq-app">
      {isMock && <div className="mock-badge">UI preview · mock data (not the plugin)</div>}
      {state.lastError != null && (
        <div className="error-banner" role="alert">
          <span>{state.lastError}</span>
          <button type="button" className="btn" onClick={() => store.setError(null)}>
            Dismiss
          </button>
        </div>
      )}

      {/*------------------------------------------------ header (34 px) */}
      <div className="header-row">
        <span className="app-title">MetalEQ</span>
        <span className="app-subtitle">Zero-latency guitar EQ · before or after an amp</span>
        <div className="header-spacer" />
        <Meter title="INPUT PEAK" peak={state.meters.in} />
        <Meter title="OUTPUT PEAK" peak={state.meters.out} />
      </div>

      {/*------------------------------------------------ preset row (36 px) */}
      <div className="preset-row">
        <span className="row-label preset-label">PRESET</span>
        <Select
          className="preset-select"
          value={presetValue}
          options={presetOptions}
          onChange={onPresetSelected}
          ariaLabel="Preset"
          tip="Factory presets are read-only. User presets are stored in your Windows app-data folder."
        />
        <span className={`preset-state ${state.preset.dirty ? 'dirty' : ''}`}>
          {presetStateText(state.preset)}
        </span>
        <button
          type="button"
          className="btn"
          title="Overwrite the selected user preset after confirmation, or open Save As for a factory or custom sound."
          onClick={onSaveClicked}
        >
          Save
        </button>
        <div className="manage-root" ref={manageRootRef}>
          <button
            type="button"
            className={`btn ${manageMenuOpen ? 'active' : ''}`}
            title="Manage user presets: Save As creates a preset and asks before overwriting; Rename and Delete apply to the selected user preset; Revert restores the loaded preset."
            onClick={() => setManageMenuOpen((open) => !open)}
          >
            Manage
          </button>
          {manageMenuOpen && (
            <div className="manage-menu" role="menu">
              <button
                type="button"
                role="menuitem"
                onClick={() => {
                  setManageMenuOpen(false);
                  openSaveAs();
                }}
              >
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
              <button
                type="button"
                role="menuitem"
                disabled={!state.preset.dirty}
                onClick={() => {
                  setManageMenuOpen(false);
                  void bridge.revertToPreset().then((result) => {
                    if (!result.ok) showError(result.error ?? 'Reverting failed.');
                  });
                }}
              >
                Revert to loaded preset
              </button>
            </div>
          )}
        </div>
        <Toggle
          label="Bypass"
          checked={(state.params.bypass ?? 0) >= 0.5}
          onChange={(v) => setParam('bypass', v ? 1 : 0)}
          variant="accent"
        />
        <Toggle
          label="A/B"
          checked={(state.params.abCompare ?? 0) >= 0.5}
          onChange={(v) => setParam('abCompare', v ? 1 : 0)}
          tip="Compare against flat EQ at the same output trim."
        />
      </div>

      {/*------------------------------------------------ EQ graph */}
      <div className="graph-slot" ref={graphSlotRef}>
        <EqGraph
          width={graphSize.width}
          height={graphSize.height}
          curve={state.curve}
          params={state.params}
          suggestionCurve={analyzerReady ? state.suggestionCurve : null}
        />
      </div>

      {/*------------------------------------------------ macro knobs (140 px) */}
      <div className="knobs-row">{macroKnobs}</div>

      {/*------------------------------------------------ shapes row (42 px) */}
      <div className="shapes-row">
        {SHAPES.map((shape) => (
          <Toggle
            key={shape.id}
            label={shape.label}
            tip={shape.tip}
            checked={(state.params[shape.id] ?? 0) >= 0.5}
            onChange={(v) => setParam(shape.id, v ? 1 : 0)}
          />
        ))}
        <button
          type="button"
          className="btn reset-btn"
          title="Reset all EQ and macros to neutral. Shape toggles are switched off."
          onClick={() => void bridge.resetToNeutral()}
        >
          RESET EQ
        </button>
      </div>

      {/*------------------------------------------------ high-pass row (46 px) */}
      <div className="filter-row">
        <span className="row-label hp-label" title="Removes low-frequency rumble; use a lower cutoff on drop tunings.">
          HIGH-PASS
        </span>
        <Slider
          label=""
          value={state.params.hpFreq ?? 80}
          min={specOf('hpFreq').min}
          max={specOf('hpFreq').max}
          skew={specOf('hpFreq').skew}
          format={formatFreqPlain}
          tip="High-pass cutoff. Tightens low end; avoid cutting the fundamental of low-tuned notes."
          onDragStateChange={(d) => knobDrag('hpFreq', d)}
          onChange={(v) => setParam('hpFreq', snapPlain(specOf('hpFreq'), v))}
        />
        <Select
          className="slope-select"
          value={String(Math.round(state.params.hpSlope ?? 0))}
          options={SLOPE_CHOICES}
          onChange={(v) => setParam('hpSlope', Number(v))}
          ariaLabel="High-pass slope"
          tip="Steeper slopes remove more low-frequency energy below cutoff."
        />
      </div>

      {/*------------------------------------------------ low-pass row (50 px) */}
      <div className="filter-row lp-row">
        <span className="row-label lp-label">LOW-PASS / HIGH CUT</span>
        <Toggle
          label="Enabled"
          checked={(state.params.lpOn ?? 1) >= 0.5}
          onChange={(v) => setParam('lpOn', v ? 1 : 0)}
          tip="Enable or bypass the high-frequency low-pass filter. This reduces top-end content; it is not a noise gate."
        />
        <span className="row-label cutoff-label">CUTOFF</span>
        <Slider
          label=""
          value={state.params.lpFreq ?? 12000}
          min={specOf('lpFreq').min}
          max={specOf('lpFreq').max}
          skew={specOf('lpFreq').skew}
          format={formatFreqPlain}
          tip="Cutoff frequency. For post-amp guitars, audition around 8–12 kHz when the top end is fizzy."
          onDragStateChange={(d) => knobDrag('lpFreq', d)}
          onChange={(v) => setParam('lpFreq', snapPlain(specOf('lpFreq'), v))}
        />
        <Select
          className="slope-select"
          value={String(Math.round(state.params.lpSlope ?? 0))}
          options={SLOPE_CHOICES}
          onChange={(v) => setParam('lpSlope', Number(v))}
          ariaLabel="Low-pass slope"
          tip="Steeper slopes remove more high frequencies above the cutoff."
        />
      </div>

      {/*------------------------------------------------ fine EQ toggle + panel */}
      <button
        type="button"
        className="btn fine-btn"
        title="Open five precise parametric bands with frequency, gain, Q and bypass controls."
        onClick={toggleFine}
      >
        {fineOpen ? 'FINE EQ −' : 'FINE EQ +'}
      </button>

      {fineOpen && (
        <div className="fine-panel">
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
              <div className="band-card" key={n}>
                <Toggle
                  label="ON"
                  checked={(state.params[onId] ?? 1) >= 0.5}
                  onChange={(v) => setParam(onId, v ? 1 : 0)}
                  tip="Enable or bypass this parametric EQ band."
                />
                <div className="band-title">BAND {n}</div>
                <Slider
                  label="FREQ"
                  value={state.params[freqId] ?? freqSpec.def}
                  min={freqSpec.min}
                  max={freqSpec.max}
                  skew={freqSpec.skew}
                  format={formatFreqPlain}
                  tip="Band centre frequency. Logarithmic range from 60 Hz to 16 kHz."
                  onDragStateChange={(d) => knobDrag(freqId, d)}
                  onChange={(v) => setParam(freqId, snapPlain(freqSpec, v))}
                />
                <Slider
                  label="GAIN"
                  value={state.params[gainId] ?? gainSpec.def}
                  min={gainSpec.min}
                  max={gainSpec.max}
                  format={formatGain}
                  tip="Boost or cut this band in decibels."
                  onDragStateChange={(d) => knobDrag(gainId, d)}
                  onChange={(v) => setParam(gainId, snapPlain(gainSpec, v))}
                />
                <Slider
                  label="Q"
                  value={state.params[qId] ?? qSpec.def}
                  min={qSpec.min}
                  max={qSpec.max}
                  format={formatQ}
                  tip="Bandwidth: lower Q is broad, higher Q is narrow."
                  onDragStateChange={(d) => knobDrag(qId, d)}
                  onChange={(v) => setParam(qId, snapPlain(qSpec, v))}
                />
              </div>
            );
          })}
        </div>
      )}

      {/*------------------------------------------------ analyzer target row (34 px) */}
      <div className="analyzer-row">
        <span className="row-label target-label">TARGET</span>
        <Select
          className="target-select"
          value={String(profile)}
          options={TARGET_PROFILES.map((name, i) => ({ value: String(i), label: name }))}
          onChange={(v) => setProfile(Number(v))}
          ariaLabel="Analyzer target profile"
          tip="The analyzer compares the captured spectrum with this target; it does not detect the source genre."
        />
        <button type="button" className="btn capture-btn" onClick={onCaptureClicked}>
          {capturing ? 'Stop capture' : 'Capture 8 seconds'}
        </button>
        <span className="row-label amount-label">AMOUNT</span>
        <Slider
          label=""
          value={amount}
          min={0}
          max={1}
          format={formatPercent}
          onDragStateChange={(d) => knobDrag('__amount__', d)}
          onChange={setAmount}
        />
        <div className="analyzer-spacer" />
        <button type="button" className="btn primary apply-btn" disabled={!analyzerReady} onClick={onApplySuggestion}>
          Apply suggestion
        </button>
      </div>

      <div className="analyzer-status">{analyzerStatusText(state.analyzer.status, state.analyzer.progress, state.analyzer.suggestion)}</div>
      <div className="analyzer-disclaimer">
        Average-spectrum suggestion only. Capture is temporary and never saved in presets.
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
    </div>
  );
}

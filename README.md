# MetalEQ

MetalEQ 3.2.0 is a zero added-latency VST3 equalizer for high-gain guitar. It combines editable filters and tone macros with a 25-preset metalcore, thrash and utility library. A simple view gets a player to a usable rhythm or lead sound quickly; an advanced view exposes every filter and parametric band. The optional analyzer captures up to eight seconds of live input and compares the average spectrum with a selected target profile. V3 adds an offline resonance scan that finds narrow, stationary harshness, lets you audition each suggestion, and applies only the cuts you select to six dedicated surgical bands. V3.1 adds four zero-latency dynamic resonance bands that cut only when a band runs disproportionately hot relative to its neighbours. V3.2 replaces the old four-preset analyzer with sixteen research-backed destinations across genres, artists and producers: the captured input is measured in eight broad bands, compared with a structured target model, and only the meaningful differences are turned into a limited five-band correction. Target switching recalculates the recommendation from the same capture, and the UI shows input, destination and correction separately alongside the research record.

## Build

Requirements: CMake 3.22+, Git, and a network connection for the first JUCE 9.0.4 fetch. The WebView editor is built with Node.js/npm; if npm is unavailable the plugin still builds with its native fallback editor.

Windows (Visual Studio 2022):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The VST3 is written to `build/MetalEQ_artefacts/Release/VST3/MetalEQ.vst3`. Build Debug with `cmake --build build --config Debug`.

Linux (tested locally with GCC 13 and Ninja): install the JUCE Linux dependencies for WebKitGTK 4.1, GTK 3, ALSA and X11, plus Node.js/npm, then build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

The Linux VST3 is written to `build-release/MetalEQ_artefacts/Release/VST3/MetalEQ.vst3`. This repository's local release build uses that path.

## Controls

- **Simple and Advanced:** the editor starts in Simple on first run. Simple keeps presets, macros, shape switches, filters and the editable graph in one screen. Advanced adds the five numeric parametric bands, the analyzer, the resonance scan, the six surgical bands and the four dynamic bands. An explicit view choice is remembered.
- **Tone library:** factory tones are grouped by genre and subgenre: Metalcore (2000s, Modern, Melodic, Progressive), Thrash (Classic, Technical, Modern) and Utility (Pre-Amp, Post-Amp, Reference, Neutral). User presets appear in the same browser. Factory presets have stable identities, so recalling by id is project-safe.
- **User preset storage:** files are stored under the platform application-data folder (`%APPDATA%\MetalEQ\Presets` on Windows, typically `~/.config/MetalEQ/Presets` on Linux) in a versioned `.metaleq` format. Use Save As to create one; Save on a user preset asks before overwriting. Rename and Delete operate only on user presets. Revert restores the last loaded preset, and the dirty marker indicates tone edits since loading or saving. Preset files exclude analyzer captures. DAW project state is stored independently, so saved sessions still restore when preset files are moved or deleted.
- **Tone shapes:** Tighten, Scoop, Mid Punch, Pick Attack, Fizz Tamer, and Brighten are independent toggles. Enabling one adds its fixed EQ contribution; disabling it removes only that contribution, preserving macros and detailed EQ edits. Reset EQ is a separate action.
- **Macros:** Tight, Body, Scoop, Bite, Fizz Cut, and Output Trim.
- **Filters and shelves:** high-pass and low-pass each have cutoff and slope controls. The low-pass is a conventional high-cut used to tame excessive top-end fizz; it is not a noise gate. Its slope selector doubles as the bypass: **Off** is transparent, while 12, 24, and 36 dB/oct enable the filter. Advanced also exposes the low and high shelves' corner frequency and gain for broad weight and brightness moves.
- **Graph:** logarithmic frequency scale with guitar-focused landmarks, adaptive labels, gain grid, active filter cutoffs, and the combined response. Drag a numbered band node to set frequency and gain, or drag the HP/LP markers to move a cutoff. The advanced numeric controls remain available for precise edits. Input and output peak meters are polled by the UI.
- **Bypass and real A/B:** bypass leaves the signal untouched. A and B each hold a complete EQ, macro and shape snapshot. Copy sends the active slot to the other slot; Swap exchanges them. Output trim is deliberately global, so A/B remains level-matched. Host automation of the A/B parameter switches the same slots.
- **Analyzer (V3.2):** choose one of sixteen destinations organised as **Genres**, **Artists** and **Producers** — 2000s and modern metalcore, lead metalcore, the Black Album, *Rust in Peace*, *…And Justice for All*, Bullet for My Valentine, Trivium, As I Lay Dying, Miss May I, and the Andy Sneap, Colin Richardson, Bob Rock, Flemming Rasmussen, Mike Clink and Max Norman profiles. Each target is a structured tonal model (eight broad band objectives with tolerances, weights, confidence and a source record), not a renamed factory EQ curve. Capture eight seconds while the host sends audio; the analyzer measures the input relative to its own 500–2000 Hz region, compares it with the selected model and fits up to ±12 dB onto the five musical bands (individual feature caps remain tighter). A 0.5 dB deadband on the key differentiators leaves regions that already match alone, and a 0.8–1.0 confidence scale keeps low-confidence targets conservative. Switching target after a capture recalculates the recommendation immediately. The panel shows input, target and predicted balances, the five-band correction, the target's references/limitations, and never presents the target curve as the correction curve. It does not identify the source genre, and no reference album audio was measured: the numeric models are documented engineering estimates. See [Docs/AnalyzerResearch.md](Docs/AnalyzerResearch.md) for the full provenance record.
- **Resonance scan (V3, Advanced view):** capture eight seconds while playing varied riffs and letting notes ring. The detector combines long-term spectrum prominence with a harmonic-residual map, so it favours narrow peaks that stay at a fixed frequency across notes instead of naturally strong, moving harmonics. It reports a ranked, conservative candidate list; nothing is applied automatically. Select candidates, compare them on the graph, then use **Apply selected** to copy them into the six surgical bands. If the capture is too static, the scan warns that harmonics cannot be separated reliably.
- **Surgical resonance cuts:** six narrow bands with frequency, gain, Q and enable controls, independent of the five musical bands. Scan suggestions never overwrite the musical EQ. **Clear cuts** switches every surgical band off and returns it to flat. **Hear removed** temporarily monitors only the six surgical cuts in isolation (their delta signal); it is a listening aid and is never saved. As with any resonance tool, compare bypass on/off and keep only the cuts that improve the tone.
- **Dynamic resonance cuts (V3.1, Advanced view):** four independent zero-latency bands with frequency, Q, threshold, ratio, attack, release, maximum cut and enable controls. Each band compares its own energy with two adjacent half-octave reference bands and cuts only when the relative excess exceeds the threshold, so turning up the playing level does not by itself change the amount of cut. Q sets the width of the audible cut; detection uses a separate fixed narrow analysis bandwidth. Detection is stereo-linked, the live GR bar shows positive gain reduction, and there is no lookahead or oversampling. All four bands default off.
- **Dynamic suggestions (V3.1, Advanced view):** the dynamic section runs its own independent eight-second scan. It measures each candidate with the live dynamic detector's relative-excess statistic and lists the peaks that show a usable hot-point excess and a real spread over their neighbours, with the suggested threshold, Q and maximum cut. The surgical and dynamic scans keep separate status, progress and results: scanning one section never replaces the suggestions shown by the other, although only one capture runs at a time. Selecting suggestions never changes the tone; **Apply to dynamic** copies up to four of them into the dynamic bands, replacing the current dynamic setup and switching unused bands off. If no candidate produces enough relative excess, the panel says so and leaves the dynamic section untouched. **Clear dynamic** zeroes and disables all four bands, and **Hear removed** monitors only the four dynamic cuts. The surgical and dynamic monitors are separate: enabling one switches the other off, so each section can be judged on its own.

The audio callback uses preallocated capture storage and fixed-size DSP state. FFT analysis runs on a background worker after capture completes. Capture adds no signal path latency. If the host pauses, capture progress pauses too; resume playback to finish or cancel the capture.

## Compatibility and migration

MetalEQ 3.2.0 saves DAW session state with schema version 4 and user presets with schema version 3. The six surgical bands and four dynamic bands are part of the tone, A/B slots and user presets; the two temporary **Hear removed** delta-monitor switches are deliberately excluded from both. A session written before V3.1 loads with the dynamic bands at their neutral defaults and disabled; a session written before V3 also starts the surgical bands flat and off instead of inheriting whatever happened to be in the plugin instance. Older user presets load normally and receive only the missing surgical or dynamic defaults, so existing musical EQ, macro and shape edits are preserved. The UI bridge protocol is version 7, which adds the sixteen-target catalogue with categories, provenance and confidence, the separated input/target/predicted arrays, and the `setAnalyzerTarget` command. Analyzer targets are runtime UI state only: they are not part of the tone, A/B slots, user presets or DAW state, so existing sessions and presets load unchanged. The React front-end hides each section's scan results unless that section's current scan is ready, so stale candidates cannot be applied after a new capture starts.

## Verification

Build and run the dependency-light DSP, resonance and state/preset tests with:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Linux, use the build directory from the Build section, for example `ctest --test-dir build-release --output-on-failure`.

The tests cover logarithmic graph mapping and label density, shape composition and removal, low-pass bypass/slopes and legacy migration at representative sample rates, chain retirement, factory preset validity and stable ids, user-preset save/load/overwrite/rename/delete, canonical preset recall, real A/B snapshots, bridge protocol v7, DAW state schema 4 round-trip, V2/V3 session migration, V1 preset migration, the V3 resonance detector on synthetic and neutral captures, dynamic-suggestion bounds and measured-excess eligibility, the surgical delta-monitor chain response and processor audio, section-monitor exclusivity, and the V3.1 dynamic engine's relative detection, level independence, broadband rejection, zero-latency first sample, stereo linking, maximum-cut clamp and release. The V3.2 tests additionally cover the sixteen-target table and its source records, eight-band input analysis, already-matching inputs, input adaptation (muddy/thin/bright), correction caps and EQ validity, and the distinction between genre, artist and producer destinations; the analyzer model is deterministic and runs without JUCE. REAPER and pluginval were unavailable in the build environment; scan/instantiation, GUI interaction, host automation during playback, audible transition quality, and in-host project recall still require validation in REAPER.

## License

This repository's source and the selected open-source JUCE build route are licensed under **GNU AGPL version 3.0 only**; see [LICENSE](LICENSE). JUCE 9.0.4 is dual-licensed under AGPLv3 or the JUCE commercial license. This project selects AGPLv3. Closed-source distribution requires an appropriate JUCE commercial license and a separate review of third-party terms. The JUCE license does not become MIT because the VST3 SDK is MIT-licensed.

The bundled VST3 SDK notice and JUCE framework license notice are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). JUCE's SPDX bill of materials covers its other bundled dependencies; see the pinned [JUCE 9.0.4 SPDX file](https://github.com/juce-framework/JUCE/blob/9.0.4/JUCE.spdx.json). Review the JUCE 9 [EULA](https://juce.com/legal/juce-9-licence/) and [licensing FAQ](https://juce.com/get-juce/#licensing-faq) for the current terms.

# MetalEQ

MetalEQ is a zero added-latency VST3 equalizer for high-gain guitar. It combines editable filters and tone macros with a 25-preset metalcore, thrash and utility library. A simple view gets a player to a usable rhythm or lead sound quickly; an advanced view exposes every filter and parametric band. The optional analyzer captures up to eight seconds of live input and compares the average spectrum with a selected target profile.

## Build

Requirements: CMake 3.22+, Git, Visual Studio 2022, and a network connection for the first JUCE 9.0.4 fetch.

From the project directory:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The VST3 is written to `build/MetalEQ_artefacts/Release/VST3/MetalEQ.vst3`.
Build Debug with `cmake --build build --config Debug`.

## Controls

- **Simple and Advanced:** the editor starts in Simple on first run. Simple keeps presets, macros, shape switches, filters and the editable graph in one screen. Advanced adds the five numeric parametric bands and the analyzer. An explicit view choice is remembered.
- **Tone library:** factory tones are grouped by genre and subgenre: Metalcore (2000s, Modern, Melodic, Progressive), Thrash (Classic, Technical, Modern) and Utility (Pre-Amp, Post-Amp, Reference, Neutral). User presets appear in the same browser. Factory presets have stable identities, so recalling by id is project-safe.
- **User preset storage:** files are stored under `%APPDATA%\MetalEQ\Presets` in a versioned `.metaleq` format. Use Save As to create one; Save on a user preset asks before overwriting. Rename and Delete operate only on user presets. Revert restores the last loaded preset, and the dirty marker indicates tone edits since loading or saving. Preset files exclude analyzer captures. DAW project state is stored independently, so saved sessions still restore when preset files are moved or deleted.
- **Tone shapes:** Tighten, Scoop, Mid Punch, Pick Attack, Fizz Tamer, and Brighten are independent toggles. Enabling one adds its fixed EQ contribution; disabling it removes only that contribution, preserving macros and detailed EQ edits. Reset EQ is a separate action.
- **Macros:** Tight, Body, Scoop, Bite, Fizz Cut, and Output Trim.
- **Filters and shelves:** high-pass and low-pass each have enable, cutoff, and slope controls. The low-pass is a conventional high-cut used to tame excessive top-end fizz; it is not a noise gate. It defaults to Off; enabling it while the slope is Off selects 24 dB/oct. Effective low-pass filtering requires both Enabled and a non-Off slope. Advanced also exposes the low and high shelves' corner frequency and gain for broad weight and brightness moves.
- **Graph:** logarithmic frequency scale with guitar-focused landmarks, adaptive labels, gain grid, active filter cutoffs, and the combined response. Drag a numbered band node to set frequency and gain, or drag the HP/LP markers to move a cutoff. The advanced numeric controls remain available for precise edits. Input and output peak meters are polled by the UI.
- **Bypass and real A/B:** bypass leaves the signal untouched. A and B each hold a complete EQ, macro and shape snapshot. Copy sends the active slot to the other slot; Swap exchanges them. Output trim is deliberately global, so A/B remains level-matched. Host automation of the A/B parameter switches the same slots.
- **Analyzer:** choose a target from the stable analyzer list, capture while the host sends audio, inspect the suggested curve, adjust its amount, and apply it to the five parametric bands. It compares the average captured spectrum with the selected target; it does not identify the source genre.

The audio callback uses preallocated capture storage and fixed-size DSP state. FFT analysis runs on a background worker after capture completes. Capture adds no signal path latency. If the host pauses, capture progress pauses too; resume playback to finish or cancel the capture.

## Verification

Build and run the dependency-light DSP and state/preset tests with:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The tests cover logarithmic graph mapping and label density, shape composition and removal, low-pass bypass/slopes and legacy migration at representative sample rates, chain retirement, factory preset validity and stable ids, user-preset save/load/overwrite/rename/delete, canonical preset recall, real A/B snapshots, bridge protocol v2 and DAW state schema v2 round-trip. REAPER and pluginval were unavailable in the build environment; scan/instantiation, GUI interaction, host automation during playback, audible transition quality, and in-host project recall still require validation in REAPER.

## License

This repository's source and the selected open-source JUCE build route are licensed under **GNU AGPL version 3.0 only**; see [LICENSE](LICENSE). JUCE 9.0.4 is dual-licensed under AGPLv3 or the JUCE commercial license. This project selects AGPLv3. Closed-source distribution requires an appropriate JUCE commercial license and a separate review of third-party terms. The JUCE license does not become MIT because the VST3 SDK is MIT-licensed.

The bundled VST3 SDK notice and JUCE framework license notice are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). JUCE's SPDX bill of materials covers its other bundled dependencies; see the pinned [JUCE 9.0.4 SPDX file](https://github.com/juce-framework/JUCE/blob/9.0.4/JUCE.spdx.json). Review the JUCE 9 [EULA](https://juce.com/legal/juce-9-licence/) and [licensing FAQ](https://juce.com/get-juce/#licensing-faq) for the current terms.

# MetalEQ

MetalEQ is a zero added-latency VST3 equalizer for high-gain guitar. It combines editable filters and tone macros with metalcore and thrash factory presets. Its optional analyzer captures up to eight seconds of live input and compares the average spectrum with a selected target profile.

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

- **Factory and user presets:** all factory profiles are available in the selector. Use Save As to create a user preset; Save on a user preset asks before overwriting. Rename and Delete operate only on user presets. Revert restores the last loaded preset, and the dirty marker indicates edits since loading or saving.
- **User preset storage:** files are stored under `%APPDATA%\MetalEQ\Presets` in a versioned `.metaleq` format. Preset files exclude analyzer captures. DAW project state is stored independently, so saved sessions still restore when preset files are moved or deleted.
- **Tone shapes:** Tighten, Scoop, Mid Punch, Pick Attack, Fizz Tamer, and Brighten are independent toggles. Enabling one adds its EQ contribution; disabling it removes only that contribution, preserving macros and detailed EQ edits. Reset EQ is a separate action.
- **Macros:** Tight, Body, Scoop, Bite, Fizz Cut, and Output Trim.
- **Filters and fine EQ:** high-pass and low-pass each have enable, cutoff, and slope controls. The low-pass is a conventional high-cut used to tame excessive top-end fizz; it is not a noise gate. Expand Fine EQ to edit the five bell bands' frequency, gain, Q, and enable state.
- **Graph:** logarithmic frequency scale with guitar-focused landmarks, adaptive labels, gain grid, active filter cutoffs, and the combined response. Input and output peak meters are polled by the UI.
- **Bypass and A/B:** bypass leaves the signal untouched; A/B compares the EQ with a neutral curve while preserving output trim.
- **Analyzer:** choose a metalcore or thrash target, capture while the host sends audio, inspect the suggested curve, adjust its amount, and apply it to the five parametric bands. It compares the average captured spectrum with the selected target; it does not identify the source genre.

The audio callback uses preallocated capture storage and fixed-size DSP state. FFT analysis runs on a background worker after capture completes. Capture adds no signal path latency. If the host pauses, capture progress pauses too; resume playback to finish or cancel the capture.

## Verification

Build and run the dependency-light DSP and state/preset tests with:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The tests cover logarithmic graph mapping and label density, shape composition and removal, low-pass bypass/slopes at representative sample rates, factory preset validity, user-preset save/load/overwrite/rename/delete, and DAW state round-trip. REAPER and pluginval were unavailable in the build environment; scan/instantiation, GUI interaction, host automation during playback, audible transition quality, and in-host project recall still require validation in REAPER.

## License

This repository's source and the selected open-source JUCE build route are licensed under **GNU AGPL version 3.0 only**; see [LICENSE](LICENSE). JUCE 9.0.4 is dual-licensed under AGPLv3 or the JUCE commercial license. This project selects AGPLv3. Closed-source distribution requires an appropriate JUCE commercial license and a separate review of third-party terms. The JUCE license does not become MIT because the VST3 SDK is MIT-licensed.

The bundled VST3 SDK notice and JUCE framework license notice are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). JUCE's SPDX bill of materials covers its other bundled dependencies; see the pinned [JUCE 9.0.4 SPDX file](https://github.com/juce-framework/JUCE/blob/9.0.4/JUCE.spdx.json). Review the JUCE 9 [EULA](https://juce.com/legal/juce-9-licence/) and [licensing FAQ](https://juce.com/get-juce/#licensing-faq) for the current terms.

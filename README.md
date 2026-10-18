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

- **Factory presets:** the complete preset table from the DSP source, plus Custom.
- **Tone shapes:** Tighten, Scoop, Mid Punch, Pick Attack, Fizz Tamer, Brighten, and Reset. Each applies editable EQ settings.
- **Macros:** Tight, Body, Scoop, Bite, Fizz Cut, and Output Trim.
- **Bypass and A/B:** bypass leaves the signal untouched; A/B compares the EQ with a neutral curve while preserving output trim.
- **Analyzer:** choose a metalcore or thrash target, capture while the host sends audio, inspect the suggested curve, adjust its amount, and apply it to the five parametric bands. It compares the average captured spectrum with the selected target; it does not identify the source genre.

The audio callback uses preallocated capture storage and fixed-size DSP state. FFT analysis runs on a background worker after capture completes. Capture adds no signal path latency. If the host pauses, capture progress pauses too; resume playback to finish or cancel the capture.

## Verification

Release and Debug VST3 builds were verified with CMake. The project has no automated test target. REAPER and pluginval were unavailable in the build environment, so host validation remains to be done.

## License

This repository's source and the selected open-source JUCE build route are licensed under **GNU AGPL version 3.0 only**; see [LICENSE](LICENSE). JUCE 9.0.4 is dual-licensed under AGPLv3 or the JUCE commercial license. This project selects AGPLv3. Closed-source distribution requires an appropriate JUCE commercial license and a separate review of third-party terms. The JUCE license does not become MIT because the VST3 SDK is MIT-licensed.

The bundled VST3 SDK notice and JUCE framework license notice are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). JUCE's SPDX bill of materials covers its other bundled dependencies; see the pinned [JUCE 9.0.4 SPDX file](https://github.com/juce-framework/JUCE/blob/9.0.4/JUCE.spdx.json). Review the JUCE 9 [EULA](https://juce.com/legal/juce-9-licence/) and [licensing FAQ](https://juce.com/get-juce/#licensing-faq) for the current terms.
/*
    Stable parameter identifiers and value ranges.

    These strings are part of the plugin's saved state and automation map: they
    must never change, otherwise old sessions and host automation breaks.

    Kept free of JUCE so that the test target can validate presets against the
    same numbers the plugin uses.
*/

#pragma once

#include <array>
#include <cstdint>

#include "MetalEqDsp.h"

namespace metaleq::params
{

//==============================================================================
inline constexpr std::array<const char*, 8> idStrings =
{
    "hpFreq", "hpSlope", "lpFreq", "lpSlope", "lsFreq", "lsGain", "hsFreq", "hsGain"
};

//==============================================================================
inline constexpr double hpFreqMin = 20.0,   hpFreqMax = 180.0,   hpFreqDefault = 80.0;
inline constexpr double lpFreqMin = 3000.0, lpFreqMax = 20000.0, lpFreqDefault = 12000.0;
inline constexpr double lsFreqMin = 30.0,   lsFreqMax = 400.0,   lsFreqDefault = 140.0;
inline constexpr double hsFreqMin = 3000.0, hsFreqMax = 16000.0, hsFreqDefault = 7000.0;

inline constexpr double shelfGainMin = -12.0, shelfGainMax = 12.0;

inline constexpr double bandFreqMin = 60.0,  bandFreqMax = 16000.0;
inline constexpr double bandGainMin = -12.0, bandGainMax = 12.0;
inline constexpr double bandQMin    = 0.3,   bandQMax    = 6.0;

// Narrow surgical resonance bands (V3). Cuts are deliberately allowed to go
// deeper than the musical bands, while boosts stay small: the scan only ever
// proposes cuts, and a surgical boost is easy to overdo.
inline constexpr double surgicalFreqMin = 80.0,   surgicalFreqMax = 14000.0;
inline constexpr double surgicalGainMin = -12.0,  surgicalGainMax = 6.0;
inline constexpr double surgicalQMin    = 2.0,    surgicalQMax    = 20.0;

inline constexpr double trimMin = -12.0, trimMax = 6.0;

//==============================================================================
// Cross-check a preset/shape against the declared parameter ranges. Used by the
// test target so that a typo in the preset table cannot ship.
//==============================================================================
inline bool isValidEqSettings (const EqSettings& e) noexcept
{
    auto inRange = [] (double v, double lo, double hi) { return isFiniteValue (v) && v >= lo - 1.0e-6 && v <= hi + 1.0e-6; };

    if (! inRange (e.hpFreq, hpFreqMin, hpFreqMax)) return false;
    if (! inRange (e.lpFreq, lpFreqMin, lpFreqMax)) return false;
    if (e.hpSlope < 0 || e.hpSlope > 3) return false;
    if (e.lpSlope < 0 || e.lpSlope > 3) return false;
    if (! inRange (e.lsFreq, lsFreqMin, lsFreqMax)) return false;
    if (! inRange (e.hsFreq, hsFreqMin, hsFreqMax)) return false;
    if (! inRange (e.lsGainDb, shelfGainMin, shelfGainMax)) return false;
    if (! inRange (e.hsGainDb, shelfGainMin, shelfGainMax)) return false;
    if (! inRange (e.trimDb, trimMin, trimMax)) return false;

    for (const auto& b : e.bands)
    {
        if (! inRange (b.freq, bandFreqMin, bandFreqMax)) return false;
        if (! inRange (b.gainDb, bandGainMin, bandGainMax)) return false;
        if (! inRange (b.q, bandQMin, bandQMax)) return false;
    }

    for (const auto& b : e.surgical)
    {
        if (! inRange (b.freq, surgicalFreqMin, surgicalFreqMax)) return false;
        if (! inRange (b.gainDb, surgicalGainMin, surgicalGainMax)) return false;
        if (! inRange (b.q, surgicalQMin, surgicalQMax)) return false;
    }

    return true;
}

inline bool isValidMacroSettings (const MacroSettings& m) noexcept
{
    auto inRange = [] (double v) { return isFiniteValue (v) && v >= -1.0e-6 && v <= 1.0 + 1.0e-6; };

    return inRange (m.tight) && inRange (m.body) && inRange (m.scoop)
        && inRange (m.bite) && inRange (m.fizzCut);
}

} // namespace metaleq::params

/*
    Factory preset library.

    Everything here is plain data compiled into the plugin: no file I/O, no
    network, no runtime discovery. Selecting a preset is a sequence of ordinary
    parameter changes, so it is automatable, saved with the session and undone
    exactly like any other edit.

    Preset identity
    ---------------
    Every factory preset has a stable, human-readable id. Saved projects store
    that id (not a table index), so reordering or inserting presets in a later
    build cannot silently recall the wrong tone. The APVTS preset choice remains
    index based for host automation: 0 = Custom, 1..N = factory[0..N-1].
*/

#pragma once

#include <array>

#include "MetalEqDsp.h"

namespace metaleq
{

/** Where in the signal chain a preset is meant to be used. */
enum class Placement
{
    postAmp,   // after an amp / cab simulation
    preAmp,    // before an amp simulation (shapes what the preamp distorts)
    either
};

struct PresetDefinition
{
    /** Stable identity. Never reuse or rename an existing id. */
    const char*    id          = "";
    /** Browser grouping: "Metalcore", "Thrash", "Utility". */
    const char*    genre       = "";
    /** Browser sub-group: "2000s", "Modern", "Pre-Amp", "Reference", ... */
    const char*    subgenre    = "";
    const char*    name        = "";
    Placement      placement   = Placement::postAmp;
    const char*    description = "";
    EqSettings     eq {};
    /** When true the preset also sets the macro controls; otherwise the
        guitarist's macro settings are left alone.
    */
    bool           setsMacros  = false;
    MacroSettings  macros {};
};

/** Analyzer comparison targets, exposed to the UI by label and identified by
    stable preset id so table reordering can never change their meaning.
*/
struct AnalyzerTarget
{
    const char* presetId = "";
    const char* label    = "";
};

inline constexpr std::array<AnalyzerTarget, 4> analyzerTargets {{
    { "metalcore-2000s-tight-rhythm", "2000s Metalcore: Tight Rhythm" },
    { "thrash-technical-clarity",     "Thrash: Technical Clarity" },
    { "thrash-classic-midrange-punch", "Classic Thrash: Midrange Punch" },
    { "utility-postamp-mix-cleanup",  "Post-Amp: Mix Cleanup" }
}};

int getNumPresets() noexcept;
const PresetDefinition& getPreset (int index) noexcept;

/** Factory index (0-based) for a stable id, or -1. */
int findPresetById (const char* id) noexcept;

/** Factory index (0-based) for a display name, case-insensitive, or -1. */
int findPresetByName (const char* name) noexcept;

int getNumAnalyzerTargets() noexcept;
const AnalyzerTarget& getAnalyzerTarget (int index) noexcept;

const char* placementToString (Placement p) noexcept;

} // namespace metaleq

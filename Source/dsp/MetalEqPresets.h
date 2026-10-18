/*
    Factory preset library and one-click tonal shapes.

    Everything here is plain data compiled into the plugin: no file I/O, no
    network, no runtime discovery. Selecting a preset or a shape is a sequence
    of ordinary parameter changes, so it is automatable, saved with the session
    and undone exactly like any other edit.
*/

#pragma once

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
    const char*    name        = "";
    Placement      placement   = Placement::postAmp;
    const char*    description = "";
    EqSettings     eq {};
    /** When true the preset also sets the macro controls, otherwise the
        guitarist's macro settings are left alone.
    */
    bool           setsMacros  = false;
    MacroSettings  macros {};
};

struct ShapeDefinition
{
    const char*    name     = "";
    const char*    tooltip  = "";
    EqSettings     eq {};
    /** Shapes that change the crossover points as well as the bands. */
    bool           setsCrossovers = true;
    bool           setsMacros     = false;
    MacroSettings  macros {};
};

//==============================================================================
int getNumPresets() noexcept;
const PresetDefinition& getPreset (int index) noexcept;

/** Preset lookup by name, case insensitive. Returns -1 when not found. */
int findPresetByName (const char* name) noexcept;

int getNumShapes() noexcept;
const ShapeDefinition& getShape (int index) noexcept;

const char* placementToString (Placement p) noexcept;

} // namespace metaleq

#include "MetalEqPresets.h"

#include <cstring>
#include <iterator>

namespace metaleq
{
namespace
{

//==============================================================================
BandSettings band (double freq, double gainDb, double q, bool on = true) noexcept
{
    return BandSettings { freq, gainDb, q, on };
}

/** A neutral band: present, audible position, but doing nothing until moved. */
BandSettings flatBand (double freq) noexcept
{
    return BandSettings { freq, 0.0, 1.0, true };
}

struct EqParts
{
    double hpFreq;
    int    hpSlope;
    double lpFreq;
    int    lpSlope;
    double lsFreq;
    double lsGain;
    double hsFreq;
    double hsGain;
    std::array<BandSettings, kNumBands> bands;
    double trim;
};

EqSettings makeEq (const EqParts& p) noexcept
{
    EqSettings e;
    e.hpFreq = p.hpFreq;   e.hpSlope = p.hpSlope;
    e.lpFreq = p.lpFreq;   e.lpSlope = p.lpSlope;
    e.lsFreq = p.lsFreq;   e.lsGainDb = p.lsGain;
    e.hsFreq = p.hsFreq;   e.hsGainDb = p.hsGain;
    e.bands  = p.bands;
    e.trimDb = p.trim;
    return e;
}

/** The neutral starting point: no filters, no trim, macros centred/off. */
EqSettings neutralEq() noexcept
{
    EqSettings e;
    e.hpSlope = 0;
    e.lpSlope = 0;
    e.bands[0] = flatBand (120.0);
    e.bands[1] = flatBand (400.0);
    e.bands[2] = flatBand (1200.0);
    e.bands[3] = flatBand (3500.0);
    e.bands[4] = flatBand (8000.0);
    return e;
}

constexpr MacroSettings neutralMacros { 0.0, 0.5, 0.0, 0.0, 0.0 };

//==============================================================================
// Preset table. Frequencies in Hz, gains in dB, Q dimensionless.
//
// Post-amp profiles work on an already distorted signal: they clean up rumble,
// muddiness, boxiness and fizz and add attack where the guitar needs it.
// Pre-amp profiles shape what enters the preamp, which changes how it
// distorts, so they deliberately keep low end and boost rather than cut mids.
//==============================================================================
const PresetDefinition presets[] =
{
    //--- 0 -------------------------------------------------------------------
    {
        "2000s Metalcore - Tight Rhythm",
        Placement::postAmp,
        "Post-amp. Tight low end, controlled low mids, aggressive attack and "
        "restrained fizz. The safe starting point for palm-muted rhythm.",
        makeEq ({ 85.0, 2, 12000.0, 2, 120.0, -2.5, 7000.0, -1.5,
                  { band (240.0, -3.5, 1.0), band (420.0, -2.0, 1.4),
                    band (2200.0, 3.0, 0.9), band (5200.0, -2.0, 1.2),
                    band (8500.0, -2.5, 0.8) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 1 -------------------------------------------------------------------
    {
        "Melodic Metalcore - Cut Through",
        Placement::postAmp,
        "Post-amp. Articulate chords with enough midrange to stay audible when "
        "two rhythm guitars are doubled.",
        makeEq ({ 95.0, 2, 14000.0, 2, 150.0, -1.5, 8000.0, -2.0,
                  { band (260.0, -2.5, 1.0), band (700.0, -1.5, 1.2),
                    band (2400.0, 3.5, 0.8), band (6000.0, -3.0, 1.0),
                    band (10000.0, -2.0, 0.7) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 2 -------------------------------------------------------------------
    {
        "Modern Metalcore - Drop Tuning",
        Placement::postAmp,
        "Post-amp. A 36 dB/oct high-pass at 70 Hz keeps the low notes of a drop "
        "tuning separate instead of rumbling into each other.",
        makeEq ({ 70.0, 3, 11000.0, 2, 100.0, -3.5, 6000.0, -2.0,
                  { band (180.0, -4.0, 0.9), band (350.0, -3.0, 1.3),
                    band (1900.0, 3.5, 0.8), band (4500.0, -3.0, 1.0),
                    band (8000.0, -2.5, 0.8) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 3 -------------------------------------------------------------------
    {
        "Sneap-Inspired - Precision",
        Placement::postAmp,
        "Post-amp. Inspired by the general clarity of modern tight high-gain "
        "mixing, not by any specific proprietary setting.",
        makeEq ({ 100.0, 2, 13000.0, 2, 130.0, -3.0, 7500.0, -2.5,
                  { band (250.0, -3.0, 1.1), band (500.0, -2.5, 1.5),
                    band (2100.0, 2.5, 1.0), band (5500.0, -2.5, 1.1),
                    band (9000.0, -2.5, 0.7) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 4 -------------------------------------------------------------------
    {
        "Richardson-Inspired - Aggressive Mids",
        Placement::postAmp,
        "Post-amp. Inspired by the general 'forward and aggressive' metal "
        "mixing style, not by any specific proprietary setting.",
        makeEq ({ 95.0, 2, 15000.0, 1, 160.0, -2.0, 8000.0, 0.0,
                  { band (300.0, -4.5, 0.9), band (700.0, -2.0, 1.2),
                    band (1800.0, 4.0, 0.8), band (3200.0, 1.5, 1.0),
                    band (7000.0, -3.0, 1.0) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 5 -------------------------------------------------------------------
    {
        "Metalcore Lead - Presence",
        Placement::postAmp,
        "Post-amp. A scooped lead tone: the low mids are out of the way so the "
        "note is audible, and the top end is open rather than harsh.",
        makeEq ({ 90.0, 2, 16000.0, 1, 140.0, -2.0, 6000.0, 1.5,
                  { band (300.0, -3.0, 1.0), band (650.0, -3.5, 0.9),
                    band (2800.0, 3.0, 0.9), band (5500.0, 1.5, 1.0),
                    band (9000.0, -2.0, 0.8) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 6 -------------------------------------------------------------------
    {
        "Thrash - Technical Clarity",
        Placement::postAmp,
        "Post-amp. Fast, precise picking with every note separated and a "
        "controlled low end.",
        makeEq ({ 90.0, 2, 15000.0, 2, 120.0, -2.5, 7500.0, -2.0,
                  { band (220.0, -3.0, 1.0), band (550.0, -2.0, 1.3),
                    band (2500.0, 3.5, 0.7), band (6000.0, -2.0, 1.0),
                    band (9500.0, -2.5, 0.7) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 7 -------------------------------------------------------------------
    {
        "Classic Thrash - Midrange Punch",
        Placement::postAmp,
        "Post-amp. Aggressive and mid-forward with the top left open: a "
        "1980s-style rhythm starting point.",
        makeEq ({ 75.0, 2, 18000.0, 1, 180.0, -1.5, 7000.0, -1.0,
                  { band (280.0, -2.0, 1.0), band (620.0, -1.5, 1.2),
                    band (1600.0, 4.5, 0.7), band (3600.0, 2.0, 1.0),
                    band (7500.0, -2.5, 1.0) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 8 -------------------------------------------------------------------
    {
        "Deep Scoop - Thrash",
        Placement::postAmp,
        "Post-amp. A pronounced broad midrange dip (driven by the SCOOP macro "
        "so you can widen or close it) with enough low-end body and upper-mid "
        "attack to stay a rhythm tone.",
        makeEq ({ 80.0, 2, 14000.0, 2, 200.0, -2.0, 7000.0, -2.0,
                  { band (200.0, -2.5, 0.9), band (750.0, -2.0, 1.0),
                    band (2000.0, 3.5, 0.8), band (5000.0, -2.0, 1.0),
                    band (8500.0, -2.5, 0.8) },
                  0.0 }),
        true, MacroSettings { 0.0, 0.5, 0.85, 0.0, 0.0 }
    },

    //--- 9 -------------------------------------------------------------------
    {
        "Thrash - Tight Modern Rhythm",
        Placement::postAmp,
        "Post-amp. Disciplined low end, sharp articulation and highs that stay "
        "under control at high gain.",
        makeEq ({ 110.0, 2, 12000.0, 2, 130.0, -3.5, 6500.0, -2.5,
                  { band (240.0, -4.0, 0.9), band (450.0, -2.5, 1.4),
                    band (2300.0, 3.0, 0.8), band (5500.0, -2.5, 1.0),
                    band (9000.0, -2.5, 0.7) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 10 ------------------------------------------------------------------
    {
        "Megadeth-Inspired - Precision and Bite",
        Placement::postAmp,
        "Post-amp. Inspired by the general definition and upper-mid bite of that "
        "style, not by any specific proprietary setting.",
        makeEq ({ 100.0, 2, 16000.0, 1, 140.0, -2.0, 7500.0, -2.5,
                  { band (260.0, -3.5, 1.0), band (480.0, -2.0, 1.2),
                    band (2600.0, 4.0, 0.8), band (5200.0, -1.5, 1.1),
                    band (9000.0, -3.0, 0.7) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 11 ------------------------------------------------------------------
    {
        "Metallica-Inspired - Heavy Rhythm",
        Placement::postAmp,
        "Post-amp. Inspired by the general full, mid-forward rhythm guitar "
        "sound of that era, not by any specific proprietary setting.",
        makeEq ({ 70.0, 2, 17000.0, 1, 190.0, -1.0, 6500.0, -1.5,
                  { band (300.0, -2.5, 0.9), flatBand (800.0),
                    band (1500.0, 3.5, 0.8), band (2800.0, 1.5, 1.0),
                    band (5500.0, -2.0, 1.0) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 12 ------------------------------------------------------------------
    {
        "Pre-Amp - Overdrive Tightener",
        Placement::preAmp,
        "PRE-AMP: place this BEFORE your amp/cab. It keeps low end for the "
        "preamp to distort, cuts the bottom, lifts around 700 Hz and takes the "
        "edge off above 5 kHz - a classic overdrive contour.",
        makeEq ({ 40.0, 1, 20000.0, 0, 120.0, -4.0, 5500.0, -3.5,
                  { band (300.0, -2.0, 1.0), band (720.0, 4.5, 0.8),
                    band (1200.0, -1.0, 1.2), band (3000.0, 1.5, 1.0),
                    band (9000.0, -2.0, 0.8) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 13 ------------------------------------------------------------------
    {
        "Post-Amp - Mix Cleanup",
        Placement::postAmp,
        "Post-amp. The general cleanup starting point: rumble out with the "
        "high-pass, muddiness out around 300-550 Hz, attack back around 2.4 kHz "
        "and the harsh 5-9 kHz region tamed.",
        makeEq ({ 90.0, 2, 12000.0, 2, 130.0, -2.5, 7000.0, -2.0,
                  { band (300.0, -4.0, 0.9), band (550.0, -2.0, 1.3),
                    band (2400.0, 2.0, 0.9), band (5500.0, -3.0, 1.0),
                    band (9000.0, -2.0, 0.8) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 14 ------------------------------------------------------------------
    {
        "Fizz Control",
        Placement::postAmp,
        "Post-amp. For amp/cabinet combinations that are too harsh. Cuts the "
        "5-11 kHz region where fizz lives while leaving the attack at 2-3 kHz "
        "alone.",
        makeEq ({ 90.0, 2, 15000.0, 1, 150.0, -2.0, 6500.0, -4.5,
                  { band (250.0, -2.0, 1.0), band (450.0, -2.0, 1.2),
                    band (5000.0, -3.5, 1.0), band (7000.0, -3.0, 0.8),
                    band (10000.0, -2.0, 0.7) },
                  0.0 }),
        false, neutralMacros
    },

    //--- 15 ------------------------------------------------------------------
    {
        "Neutral - Flat EQ",
        Placement::either,
        "Fully transparent: no filters, no trim, macros at neutral. Useful for "
        "A/B-ing any other preset or for a known-good starting point.",
        neutralEq(),
        true, neutralMacros
    }
};

//==============================================================================
// One-click tonal shapes. Each one applies a complete, intentional curve made of
// several parameters - not a single knob move - and every part of it stays
// editable with the bands and macros afterwards.
//==============================================================================
const ShapeDefinition shapes[] =
{
    {
        "TIGHTEN",
        "Removes low-end and low-mid looseness: raises the high-pass, takes "
        "weight out below 150 Hz and clears mud around 250-500 Hz.",
        makeEq ({ 90.0, 2, 12000.0, 0, 120.0, -3.0, 7000.0, 0.0,
                  { band (250.0, -3.5, 1.0), band (480.0, -2.0, 1.3),
                    band (1800.0, 1.5, 1.0), flatBand (4000.0),
                    flatBand (8000.0) },
                  0.0 }),
        true, false, neutralMacros
    },

    {
        "SCOOP",
        "A broad, controllable midrange reduction centred around 550 Hz so the "
        "guitar sits behind a wall of guitars. Restore attack at 1.8 kHz.",
        makeEq ({ 80.0, 2, 14000.0, 0, 170.0, -1.0, 7000.0, 0.0,
                  { band (300.0, -1.5, 0.9), band (550.0, -4.5, 0.55),
                    band (1800.0, 1.5, 0.9), flatBand (4000.0),
                    flatBand (8000.0) },
                  0.0 }),
        true, false, neutralMacros
    },

    {
        "MID PUNCH",
        "Restores the midrange that gives a guitar body and authority: clears "
        "boxiness around 320 Hz and pushes 1.6 kHz forward.",
        makeEq ({ 80.0, 0, 16000.0, 0, 150.0, -1.0, 7000.0, 0.0,
                  { band (320.0, -3.0, 0.9), band (1600.0, 4.0, 0.75),
                    band (3000.0, 1.5, 1.0), flatBand (5000.0),
                    flatBand (9000.0) },
                  0.0 }),
        false, false, neutralMacros
    },

    {
        "PICK ATTACK",
        "Emphasises articulation and string definition around 2.6 kHz with a "
        "little air, while clearing low mids that blur the pick.",
        makeEq ({ 90.0, 0, 18000.0, 0, 140.0, -1.5, 8000.0, 1.0,
                  { band (350.0, -2.0, 1.0), band (2600.0, 4.5, 0.7),
                    band (5000.0, 2.0, 1.0), band (9500.0, 1.5, 0.7),
                    flatBand (15000.0) },
                  0.0 }),
        false, false, neutralMacros
    },

    {
        "FIZZ TAMER",
        "Reduces unpleasant high-frequency harshness in the 5-11 kHz region "
        "without touching the 2-3 kHz attack.",
        makeEq ({ 90.0, 0, 16000.0, 0, 150.0, -1.5, 7000.0, -2.0,
                  { flatBand (300.0), band (5200.0, -4.0, 1.0),
                    band (7500.0, -3.5, 0.8), band (11000.0, -2.5, 0.7),
                    flatBand (1500.0) },
                  0.0 }),
        false, false, neutralMacros
    },

    {
        "BRIGHTEN",
        "Adds controlled presence when the tone is too dark: a high-shelf lift "
        "plus a little around 3 kHz and 10 kHz.",
        makeEq ({ 80.0, 0, 20000.0, 0, 150.0, 0.0, 8000.0, 3.0,
                  { flatBand (300.0), flatBand (800.0),
                    band (3000.0, 2.5, 0.9), flatBand (5000.0),
                    band (10000.0, 2.0, 0.7) },
                  0.0 }),
        false, false, neutralMacros
    },

    {
        "RESET",
        "Back to a completely neutral EQ: no filters, no trim, macros centred "
        "or off. This is the audible bypass of the EQ itself.",
        neutralEq(),
        true, true, neutralMacros
    }
};

static_assert (std::size (presets) == 16, "Expected 16 factory presets");
static_assert (std::size (shapes)  == 7,  "Expected 7 one-click shapes");

} // anonymous namespace

//==============================================================================
int getNumPresets() noexcept { return static_cast<int> (std::size (presets)); }

const PresetDefinition& getPreset (int index) noexcept
{
    return presets[clampValue (index, 0, getNumPresets() - 1)];
}

int findPresetByName (const char* name) noexcept
{
    if (name == nullptr)
        return -1;

    for (int i = 0; i < getNumPresets(); ++i)
    {
        const char* a = presets[i].name;
        const char* b = name;

        while (*a != 0 && *b != 0)
        {
            const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char> (*a - 'A' + 'a') : *a;
            const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char> (*b - 'A' + 'a') : *b;

            if (ca != cb)
                break;

            ++a;
            ++b;
        }

        if (*a == 0 && *b == 0)
            return i;
    }

    return -1;
}

int getNumShapes() noexcept { return static_cast<int> (std::size (shapes)); }

const ShapeDefinition& getShape (int index) noexcept
{
    return shapes[clampValue (index, 0, getNumShapes() - 1)];
}

const char* placementToString (Placement p) noexcept
{
    switch (p)
    {
        case Placement::preAmp:   return "PRE-AMP";
        case Placement::postAmp:  return "POST-AMP";
        case Placement::either:   return "EITHER";
    }

    return "";
}

} // namespace metaleq

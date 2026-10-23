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
    // A preset that specifies a non-zero LP slope means an audible low-pass.
    // Slope 0 is the transparent default; lpOn is never left contradictory.
    e.lpEnabled = p.lpSlope > 0;
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
    e.lpEnabled = false;
    e.bands[0] = flatBand (120.0);
    e.bands[1] = flatBand (400.0);
    e.bands[2] = flatBand (1200.0);
    e.bands[3] = flatBand (3500.0);
    e.bands[4] = flatBand (8000.0);
    return e;
}

constexpr MacroSettings neutralMacros { 0.0, 0.5, 0.0, 0.0, 0.0 };

PresetDefinition preset (const char* id, const char* genre, const char* subgenre,
                         const char* name, Placement placement, const char* description,
                         const EqParts& parts, bool setsMacros = false,
                         MacroSettings macros = neutralMacros) noexcept
{
    PresetDefinition p;
    p.id = id;
    p.genre = genre;
    p.subgenre = subgenre;
    p.name = name;
    p.placement = placement;
    p.description = description;
    p.eq = makeEq (parts);
    p.setsMacros = setsMacros;
    p.macros = macros;
    return p;
}

//==============================================================================
// Preset table. Ordered by browsing taxonomy; ids are stable forever.
//
// Post-amp profiles work on an already distorted signal: they clean up rumble,
// muddiness, boxiness and fizz and add attack where the guitar needs it.
// Pre-amp profiles shape what enters the preamp, which changes how it
// distorts, so they deliberately keep low end and boost rather than cut mids.
//==============================================================================
const std::array<PresetDefinition, 25> presets {{
    //--- Metalcore / 2000s ---------------------------------------------------
    preset ("metalcore-2000s-tight-rhythm", "Metalcore", "2000s",
            "2000s Metalcore: Tight Rhythm", Placement::postAmp,
            "Post-amp. Tight low end, controlled low mids, aggressive attack and "
            "restrained fizz. The safe starting point for palm-muted rhythm.",
            { 85.0, 2, 12000.0, 2, 120.0, -2.5, 7000.0, -1.5,
              { band (240.0, -3.5, 1.0), band (420.0, -2.0, 1.4),
                band (2200.0, 3.0, 0.9), band (5200.0, -2.0, 1.2),
                band (8500.0, -2.5, 0.8) }, 0.0 }),

    preset ("metalcore-2000s-double-track", "Metalcore", "2000s",
            "2000s Metalcore: Double Track", Placement::postAmp,
            "Post-amp. Keeps doubled rhythm guitars from turning into a midrange "
            "pile-up while preserving the pick attack that cuts through drums.",
            { 90.0, 2, 14000.0, 2, 130.0, -2.0, 7200.0, -1.0,
              { band (260.0, -3.0, 1.0), band (500.0, -2.5, 1.3),
                band (2100.0, 2.0, 0.9), band (4800.0, -2.0, 1.1),
                band (9000.0, -2.0, 0.8) }, 0.0 }),

    preset ("metalcore-2000s-lead", "Metalcore", "2000s",
            "2000s Metalcore: Lead", Placement::postAmp,
            "Post-amp. A focused lead voice with enough upper mids to sit on top "
            "of a dense rhythm section without becoming brittle.",
            { 95.0, 2, 16000.0, 1, 150.0, -1.5, 7500.0, 0.5,
              { band (280.0, -3.0, 1.0), band (650.0, -2.0, 1.2),
                band (2600.0, 4.0, 0.8), band (5500.0, 1.0, 1.2),
                band (9500.0, -2.0, 0.7) }, 0.0 }),

    //--- Metalcore / Modern --------------------------------------------------
    preset ("metalcore-modern-drop-tuning", "Metalcore", "Modern",
            "Modern Metalcore: Drop Tuning", Placement::postAmp,
            "Post-amp. A 36 dB/oct high-pass at 70 Hz keeps the low notes of a "
            "drop tuning separate instead of rumbling into each other.",
            { 70.0, 3, 11000.0, 2, 100.0, -3.5, 6000.0, -2.0,
              { band (180.0, -4.0, 0.9), band (350.0, -3.0, 1.3),
                band (1900.0, 3.5, 0.8), band (4500.0, -3.0, 1.0),
                band (8000.0, -2.5, 0.8) }, 0.0 }),

    preset ("metalcore-modern-aggressive-mids", "Metalcore", "Modern",
            "Modern Metalcore: Aggressive Mids", Placement::postAmp,
            "Post-amp. A forward, angry midrange for riffs that need to punch "
            "through a busy mix. Scoop the midrange back if it feels crowded.",
            { 80.0, 2, 13500.0, 2, 110.0, -2.0, 7200.0, -2.5,
              { band (220.0, -3.0, 1.0), band (450.0, -1.5, 1.0),
                band (1600.0, 4.0, 0.8), band (3200.0, 2.0, 1.0),
                band (8000.0, -2.0, 0.8) }, 0.0 }),

    preset ("metalcore-modern-wide-rhythm", "Metalcore", "Modern",
            "Modern Metalcore: Wide Rhythm", Placement::postAmp,
            "Post-amp. Broad low-mid shaping for wall-of-guitar rhythm tones. "
            "Use one guitar per side rather than stacking several bright takes.",
            { 105.0, 3, 9000.0, 2, 100.0, -4.0, 6500.0, -2.0,
              { band (180.0, -4.5, 0.8), band (380.0, -3.5, 1.2),
                band (1600.0, 2.5, 0.7), band (3500.0, -1.0, 1.0),
                band (7500.0, -3.0, 0.8) }, 0.0 }),

    //--- Metalcore / Melodic -------------------------------------------------
    preset ("metalcore-melodic-cut-through", "Metalcore", "Melodic",
            "Melodic Metalcore: Cut Through", Placement::postAmp,
            "Post-amp. Articulate chords with enough midrange to stay audible "
            "when two rhythm guitars are doubled.",
            { 95.0, 2, 14000.0, 2, 150.0, -1.5, 8000.0, -2.0,
              { band (260.0, -2.5, 1.0), band (700.0, -1.5, 1.2),
                band (2400.0, 3.5, 0.8), band (6000.0, -3.0, 1.0),
                band (10000.0, -2.0, 0.7) }, 0.0 }),

    preset ("metalcore-melodic-lead-presence", "Metalcore", "Melodic",
            "Melodic Metalcore: Lead Presence", Placement::postAmp,
            "Post-amp. Smooth, singing lead tone with a controlled top end for "
            "harmonised melodies and sustained notes.",
            { 95.0, 2, 16000.0, 1, 150.0, -1.0, 6000.0, 1.5,
              { band (280.0, -2.0, 1.0), band (700.0, -2.5, 1.1),
                band (2800.0, 3.5, 0.8), band (5500.0, 1.5, 1.1),
                band (9500.0, -1.5, 0.7) }, 0.0 }),

    //--- Metalcore / Progressive ---------------------------------------------
    preset ("metalcore-progressive-djent-tight", "Metalcore", "Progressive",
            "Progressive Metalcore: Djent Tight", Placement::postAmp,
            "Post-amp. Fast, percussive low-end response for palm-muted "
            "syncopation. The high-pass is intentionally steep.",
            { 120.0, 3, 10000.0, 2, 90.0, -4.0, 6500.0, -1.5,
              { band (160.0, -5.0, 0.8), band (300.0, -3.0, 1.1),
                band (1200.0, 1.5, 0.8), band (2800.0, 3.5, 0.7),
                band (6500.0, -2.5, 0.8) }, 0.0 }),

    preset ("metalcore-progressive-open-clean", "Metalcore", "Progressive",
            "Progressive Metalcore: Open Clean", Placement::postAmp,
            "Post-amp. An open, ambient clean foundation for layered passages. "
            "Use the output trim to match the level of your distortion tone.",
            { 70.0, 1, 18000.0, 1, 160.0, 1.0, 8000.0, 2.0,
              { band (220.0, -1.5, 1.0), band (500.0, 1.0, 1.0),
                band (1800.0, 2.0, 0.8), band (4000.0, 1.5, 0.9),
                band (10000.0, 1.0, 0.7) }, 0.0 }),

    //--- Thrash / Classic ----------------------------------------------------
    preset ("thrash-classic-midrange-punch", "Thrash", "Classic",
            "Classic Thrash: Midrange Punch", Placement::postAmp,
            "Post-amp. The old-school recipe: tight lows, a strong midrange push "
            "and enough top-end bite for fast alternate picking.",
            { 75.0, 2, 15000.0, 2, 130.0, -2.0, 8000.0, -1.0,
              { band (250.0, -3.5, 1.0), band (700.0, -2.0, 1.1),
                band (1800.0, 4.0, 0.8), band (3500.0, 1.5, 1.0),
                band (8000.0, -2.0, 0.8) }, 0.0 }),

    preset ("thrash-classic-deep-scoop", "Thrash", "Classic",
            "Classic Thrash: Deep Scoop", Placement::postAmp,
            "Post-amp. A wide, scooped rhythm tone. Keep some midrange if you "
            "need the guitar to survive a dense mix.",
            { 80.0, 2, 16000.0, 2, 100.0, -3.0, 6500.0, -2.0,
              { band (220.0, -3.0, 1.0), band (500.0, -5.0, 1.2),
                band (1400.0, -2.0, 0.9), band (3200.0, 2.0, 1.0),
                band (8500.0, -2.5, 0.8) },
            0.0 }),

    preset ("thrash-classic-bright-attack", "Thrash", "Classic",
            "Classic Thrash: Bright Attack", Placement::postAmp,
            "Post-amp. A bright, immediate attack for fast riffs. Back the high "
            "shelf down if the amp is already bright.",
            { 70.0, 2, 19000.0, 1, 180.0, -1.0, 7500.0, 2.5,
              { band (300.0, -2.0, 1.0), band (800.0, -1.0, 1.0),
                band (1800.0, 4.0, 0.8), band (4500.0, 2.5, 1.1),
                band (9000.0, -1.0, 0.8) }, 0.0 }),

    //--- Thrash / Technical --------------------------------------------------
    preset ("thrash-technical-clarity", "Thrash", "Technical",
            "Thrash: Technical Clarity", Placement::postAmp,
            "Post-amp. Separates fast, intricate picking from chord work by "
            "cleaning the low mids and adding definition around 3 kHz.",
            { 90.0, 2, 16000.0, 2, 120.0, -2.0, 7000.0, 0.0,
              { band (200.0, -3.0, 1.0), band (500.0, -3.0, 1.2),
                band (2800.0, 4.0, 0.8), band (5000.0, 1.0, 1.1),
                band (9000.0, -1.5, 0.8) }, 0.0 }),

    preset ("thrash-technical-fast-picking", "Thrash", "Technical",
            "Thrash: Fast Picking", Placement::postAmp,
            "Post-amp. A percussive attack with a tight low end for tremolo and "
            "alternate picking at speed.",
            { 100.0, 3, 14000.0, 2, 110.0, -2.0, 7500.0, -1.5,
              { band (200.0, -3.0, 1.0), band (600.0, -1.5, 1.0),
                band (2600.0, 4.0, 0.8), band (4800.0, -2.0, 1.1),
                band (8500.0, -2.0, 0.8) }, 0.0 }),

    //--- Thrash / Modern -----------------------------------------------------
    preset ("thrash-modern-tight-rhythm", "Thrash", "Modern",
            "Modern Thrash: Tight Rhythm", Placement::postAmp,
            "Post-amp. Modern tightness without losing the midrange snarl that "
            "makes thrash riffs read.",
            { 85.0, 3, 13000.0, 2, 120.0, -2.5, 7000.0, -1.0,
              { band (240.0, -4.0, 1.0), band (450.0, -2.0, 1.2),
                band (2000.0, 3.0, 0.8), band (4000.0, 0.5, 1.0),
                band (8000.0, -2.0, 0.8) }, 0.0 }),

    preset ("thrash-modern-precision-bite", "Thrash", "Modern",
            "Modern Thrash: Precision and Bite", Placement::postAmp,
            "Post-amp. A dry, precise attack for layered rhythm parts. The "
            "low-pass keeps high-gain fizz under control.",
            { 95.0, 2, 12000.0, 2, 110.0, -2.5, 8000.0, 0.5,
              { band (220.0, -3.5, 1.0), band (500.0, -1.0, 1.0),
                band (2200.0, 3.5, 0.8), band (4500.0, 1.5, 1.0),
                band (8500.0, -2.5, 0.8) }, 0.0 }),

    //--- Utility / Pre-Amp ---------------------------------------------------
    preset ("utility-preamp-overdrive-tightener", "Utility", "Pre-Amp",
            "Pre-Amp: Overdrive Tightener", Placement::preAmp,
            "Place BEFORE your amp/cab. Keeps the low end out of the preamp, "
            "lifts the upper mids and takes the edge off above 5 kHz.",
            { 40.0, 1, 20000.0, 0, 120.0, -4.0, 5500.0, -3.5,
              { band (300.0, -2.0, 1.0), band (720.0, 4.5, 0.8),
                band (1200.0, -1.0, 1.2), band (3000.0, 1.5, 1.0),
                band (9000.0, -2.0, 0.8) }, 0.0 }),

    preset ("utility-preamp-clean-boost", "Utility", "Pre-Amp",
            "Pre-Amp: Clean Boost", Placement::preAmp,
            "Place BEFORE your amp/cab. A mostly flat level lift with a little "
            "upper-mid push; use the output trim to drive the amp input.",
            { 30.0, 1, 20000.0, 0, 90.0, -1.0, 5000.0, 1.5,
              { band (250.0, -2.0, 1.0), band (700.0, 3.0, 0.9),
                band (1200.0, -0.5, 1.0), band (2500.0, 2.0, 1.0),
                band (7000.0, -1.0, 0.8) }, 0.0 }),

    //--- Utility / Post-Amp --------------------------------------------------
    preset ("utility-postamp-mix-cleanup", "Utility", "Post-Amp",
            "Post-Amp: Mix Cleanup", Placement::postAmp,
            "Post-amp. The general cleanup starting point: rumble out with the "
            "high-pass, mud out around 300-550 Hz, attack back around 2.4 kHz.",
            { 75.0, 2, 15000.0, 2, 120.0, -1.5, 7000.0, -1.0,
              { band (300.0, -2.5, 1.0), band (550.0, -2.0, 1.2),
                band (2400.0, 2.0, 0.9), band (4500.0, -1.0, 1.0),
                band (9000.0, -1.5, 0.8) }, 0.0 }),

    preset ("utility-postamp-fizz-control", "Utility", "Post-Amp",
            "Post-Amp: Fizz Control", Placement::postAmp,
            "Post-amp. A corrective curve for fizzy high-gain tones: tame the "
            "harsh region without dulling the pick attack.",
            { 70.0, 2, 10000.0, 2, 130.0, -1.0, 6500.0, -3.5,
              { band (280.0, -2.0, 1.0), band (700.0, -1.0, 1.0),
                band (2200.0, 1.5, 0.9), band (5200.0, -3.0, 1.1),
                band (8500.0, -2.5, 0.8) }, 0.0 }),

    //--- Utility / Reference -------------------------------------------------
    preset ("utility-low-tuning-extended-range", "Utility", "Reference",
            "Low Tuning: Extended Range", Placement::postAmp,
            "Post-amp. A starting point for baritone and extended-range guitars: "
            "a low high-pass, tight subs and a controlled top end.",
            { 55.0, 3, 11000.0, 2, 90.0, -3.0, 6000.0, -2.0,
              { band (140.0, -4.0, 0.9), band (300.0, -3.0, 1.1),
                band (1600.0, 3.0, 0.8), band (3800.0, -2.0, 1.0),
                band (7500.0, -3.0, 0.8) }, 0.0 }),

    preset ("utility-reference-dark-cab", "Utility", "Reference",
            "Reference: Dark Cab", Placement::postAmp,
            "Post-amp. A dark, mid-focused reference curve for comparing a "
            "bright amp or IR without harshness.",
            { 80.0, 1, 9000.0, 2, 140.0, 0.0, 6000.0, -3.0,
              { band (180.0, -2.0, 1.0), band (400.0, -1.0, 1.0),
                band (1200.0, 1.0, 1.0), band (3000.0, -1.0, 1.0),
                band (7000.0, -3.0, 0.8) }, 0.0 }),

    preset ("utility-reference-bright-amp", "Utility", "Reference",
            "Reference: Bright Amp", Placement::postAmp,
            "Post-amp. A bright, open reference curve for comparing a dark amp "
            "or recording chain.",
            { 90.0, 2, 16000.0, 1, 120.0, -1.0, 8000.0, 3.0,
              { band (250.0, -2.0, 1.0), band (600.0, -1.0, 1.0),
                band (2200.0, 2.0, 0.8), band (4500.0, 1.0, 1.0),
                band (9000.0, 2.0, 0.8) }, 0.0 }),

    //--- Utility / Neutral ---------------------------------------------------
    preset ("utility-neutral-flat", "Utility", "Neutral",
            "Neutral: Flat EQ", Placement::either,
            "A completely neutral EQ: no filters, no trim, macros centred or "
            "off. Use it as a clean slate or a level-matched bypass reference.",
            { 80.0, 0, 12000.0, 0, 140.0, 0.0, 7000.0, 0.0,
              { flatBand (120.0), flatBand (400.0), flatBand (1200.0),
                flatBand (3500.0), flatBand (8000.0) }, 0.0 },
            true, neutralMacros)
}};

static_assert (std::size (presets) == 25, "Expected 25 factory presets");

bool equalsIgnoreCase (const char* a, const char* b) noexcept
{
    if (a == nullptr || b == nullptr)
        return false;

    while (*a != 0 && *b != 0)
    {
        const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char> (*a - 'A' + 'a') : *a;
        const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char> (*b - 'A' + 'a') : *b;

        if (ca != cb)
            return false;

        ++a;
        ++b;
    }

    return *a == 0 && *b == 0;
}

} // anonymous namespace

//==============================================================================
int getNumPresets() noexcept { return static_cast<int> (std::size (presets)); }

const PresetDefinition& getPreset (int index) noexcept
{
    return presets[static_cast<size_t> (clampValue (index, 0, getNumPresets() - 1))];
}

int findPresetById (const char* id) noexcept
{
    if (id == nullptr || *id == 0)
        return -1;

    for (int i = 0; i < getNumPresets(); ++i)
        if (equalsIgnoreCase (presets[static_cast<size_t> (i)].id, id))
            return i;

    return -1;
}

int findPresetByName (const char* name) noexcept
{
    if (name == nullptr)
        return -1;

    for (int i = 0; i < getNumPresets(); ++i)
        if (equalsIgnoreCase (presets[static_cast<size_t> (i)].name, name))
            return i;

    return -1;
}

int getNumAnalyzerTargets() noexcept { return static_cast<int> (analyzerTargets.size()); }

const AnalyzerTarget& getAnalyzerTarget (int index) noexcept
{
    return analyzerTargets[static_cast<size_t> (
        clampValue (index, 0, getNumAnalyzerTargets() - 1))];
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

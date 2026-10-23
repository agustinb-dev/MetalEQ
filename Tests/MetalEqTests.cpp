#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include "../Source/dsp/MetalEqDsp.h"
#include "../Source/dsp/MetalEqGraphAxis.h"
#include "../Source/dsp/MetalEqParams.h"
#include "../Source/dsp/MetalEqPresets.h"

namespace
{
int failures = 0;

void check (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void near (double actual, double expected, double tolerance, const char* message)
{
    check (std::abs (actual - expected) <= tolerance, message);
}

void testGraphAxis()
{
    near (metaleq::graph::frequencyToUnit (20.0), 0.0, 1.0e-12, "20 Hz maps to graph start");
    near (metaleq::graph::frequencyToUnit (20000.0), 1.0, 1.0e-12, "20 kHz maps to graph end");
    near (metaleq::graph::frequencyToUnit (1000.0), std::log (50.0) / std::log (1000.0),
          1.0e-12, "1 kHz uses logarithmic placement");
    near (metaleq::graph::unitToFrequency (metaleq::graph::frequencyToUnit (3500.0)),
          3500.0, 1.0e-8, "frequency graph mapping round-trips");
    near (metaleq::graph::dbToUnit (-15.0), 0.0, 1.0e-12, "-15 dB maps to graph bottom");
    near (metaleq::graph::dbToUnit (0.0), 0.5, 1.0e-12, "0 dB vertical mapping");
    near (metaleq::graph::dbToUnit (15.0), 1.0, 1.0e-12, "+15 dB maps to graph top");
    near (metaleq::graph::dbToUnit (-60.0), 0.0, 1.0e-12, "response below -15 dB clips at graph bottom");
    near (metaleq::graph::dbToUnit (60.0), 1.0, 1.0e-12, "response above +15 dB clips at graph top");
    check (metaleq::graph::visibleLabelIndices (900.0, 52.0).size()
           < metaleq::graph::visibleLabelIndices (1800.0, 52.0).size(),
           "narrow graph uses fewer frequency labels");
}

void testNonDestructiveShapes()
{
    check (metaleq::getNumShapes() == 6, "six fixed tonal shapes are available");

    metaleq::EqSettings eq;
    metaleq::MacroSettings macros;
    eq.bands[1] = { 437.0, -2.75, 1.4, true };

    metaleq::ChainSpec baseline, shaped, repeated;
    metaleq::buildChain (baseline, eq, macros, 48000.0);

    for (size_t i = 0; i < eq.shapes.size(); ++i)
    {
        eq.shapes[i] = true;
        metaleq::buildChain (shaped, eq, macros, 48000.0);
        const int onceCount = shaped.numFilters;
        check (onceCount > baseline.numFilters, "enabling a shape adds at least one fixed filter");
        metaleq::buildChain (repeated, eq, macros, 48000.0);
        check (repeated.numFilters == onceCount, "rebuilding enabled shape does not accumulate filters");
        check (eq.bands[1].freq == 437.0 && eq.bands[1].gainDb == -2.75,
               "shape leaves manual EQ band values unchanged");

        eq.shapes[i] = false;
        metaleq::buildChain (repeated, eq, macros, 48000.0);
        check (repeated.numFilters == baseline.numFilters,
               "turning one shape off restores the base chain size");
        near (metaleq::chainResponseDb (repeated, 48000.0, 900.0),
              metaleq::chainResponseDb (baseline, 48000.0, 900.0), 1.0e-10,
              "turning one shape off removes only its contribution");
    }

    eq.shapes[0] = true;
    eq.shapes[4] = true;
    metaleq::buildChain (shaped, eq, macros, 48000.0);
    eq.shapes[0] = false;
    metaleq::buildChain (repeated, eq, macros, 48000.0);
    check (repeated.numFilters < shaped.numFilters,
           "disabling one shape leaves another active");
    check (repeated.numFilters <= metaleq::kMaxChainSize, "combined shapes fit fixed DSP capacity");

    // Shape metadata drives both the UI and DSP, so every entry must be valid.
    for (int i = 0; i < metaleq::getNumShapes(); ++i)
    {
        const auto& definition = metaleq::getShapeDefinition (i);
        check (definition.id != nullptr && *definition.id != 0, "shape has a stable id");
        check (definition.name != nullptr && *definition.name != 0, "shape has a display name");
        check (definition.numFilters > 0 && definition.numFilters
               <= static_cast<int> (definition.filters.size()), "shape filter count is in range");
    }
}

void testLowPass()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int slope : { 1, 2, 3 })
        {
            metaleq::EqSettings eq;
            eq.lpEnabled = true;
            eq.lpFreq = 5000.0;
            eq.lpSlope = slope;
            metaleq::ChainSpec chain;
            metaleq::buildChain (chain, eq, metaleq::MacroSettings {}, rate);
            const double atCutoff = metaleq::chainResponseDb (chain, rate, 5000.0);
            const double above = metaleq::chainResponseDb (chain, rate, 18000.0);
            check (std::isfinite (atCutoff) && std::isfinite (above), "LPF response stays finite at supported sample rates");
            check (above < atCutoff, "LPF attenuates high frequencies");

            if (slope == 1)
                near (atCutoff, -3.0103, 0.15, "12 dB/oct LPF cutoff is near -3 dB");
            if (slope > 1)
            {
                metaleq::EqSettings gentler = eq;
                gentler.lpSlope = slope - 1;
                metaleq::ChainSpec gentlerChain;
                metaleq::buildChain (gentlerChain, gentler, metaleq::MacroSettings {}, rate);
                check (metaleq::chainResponseDb (chain, rate, 18000.0)
                       < metaleq::chainResponseDb (gentlerChain, rate, 18000.0),
                       "steeper LPF slope gives stronger upper-frequency attenuation");
            }
        }

        metaleq::EqSettings off;
        off.lpEnabled = false;
        off.lpSlope = 3;
        metaleq::ChainSpec bypassChain;
        metaleq::buildChain (bypassChain, off, metaleq::MacroSettings {}, rate);
        near (metaleq::chainResponseDb (bypassChain, rate, 16000.0), 0.0, 1.0e-8,
              "disabled LPF is transparent");

        // Enabled but with no slope is the transparent default. This is the
        // legacy state that must never become an accidental filter.
        metaleq::EqSettings enabledNoSlope;
        enabledNoSlope.lpEnabled = true;
        enabledNoSlope.lpSlope = 0;
        metaleq::ChainSpec transparent;
        metaleq::buildChain (transparent, enabledNoSlope, metaleq::MacroSettings {}, rate);
        near (metaleq::chainResponseDb (transparent, rate, 16000.0), 0.0, 1.0e-8,
              "enabled low-pass with Off slope is transparent");
    }
}

void testChainRetirement()
{
    metaleq::EqEngine engine;
    engine.prepare (48000.0);

    metaleq::EqSettings eq;
    eq.hpSlope = 2;
    eq.lpEnabled = true;
    eq.lpSlope = 2;
    eq.lpFreq = 6000.0;
    eq.bands[0] = { 120.0, 6.0, 1.0, true };

    metaleq::ChainSpec chain;
    metaleq::buildChain (chain, eq, metaleq::MacroSettings {}, 48000.0);
    engine.setChain (chain, 1);
    check (engine.getNumActiveFilters() == chain.numFilters, "engine installs the target chain");

    metaleq::ChainSpec empty;
    engine.setChain (empty, 64);
    check (engine.getNumActiveFilters() == 0, "engine reports the new, empty target chain");

    std::array<float, 512> samples {};
    float* channels[] = { samples.data() };
    engine.process (channels, 1, static_cast<int> (samples.size()));

    for (float sample : samples)
        check (std::isfinite (sample), "chain retirement never produces non-finite samples");

    // A second block runs entirely after the ramp; it must be transparent.
    samples.fill (0.0f);
    samples[0] = 1.0f;
    engine.process (channels, 1, static_cast<int> (samples.size()));
    near (samples[0], 1.0, 1.0e-7, "retired chain is transparent");
}

void testFactorySettings()
{
    check (metaleq::getNumPresets() == 25, "all factory profiles remain available");

    std::set<std::string> ids;
    std::set<std::string> names;
    std::set<std::string> genres;

    for (int i = 0; i < metaleq::getNumPresets(); ++i)
    {
        const auto& preset = metaleq::getPreset (i);

        check (metaleq::params::isValidEqSettings (preset.eq), "factory preset parameters are valid");
        check (preset.id != nullptr && *preset.id != 0, "factory preset has a stable id");
        check (preset.genre != nullptr && *preset.genre != 0, "factory preset has a genre");
        check (preset.subgenre != nullptr && *preset.subgenre != 0, "factory preset has a subgenre");
        check (preset.name != nullptr && *preset.name != 0, "factory preset has a name");
        check (ids.insert (preset.id).second, "factory preset ids are unique");
        check (names.insert (preset.name).second, "factory preset names are unique");
        genres.insert (preset.genre);

        check (! (preset.eq.lpEnabled && preset.eq.lpSlope == 0),
               "factory preset has no contradictory low-pass state");

        check (metaleq::findPresetById (preset.id) == i, "stable id resolves to the same preset");
        check (metaleq::findPresetByName (preset.name) == i, "display name resolves to the same preset");
    }

    check (genres.count ("Metalcore") == 1, "Metalcore genre is present");
    check (genres.count ("Thrash") == 1, "Thrash genre is present");
    check (genres.count ("Utility") == 1, "Utility genre is present");

    check (metaleq::findPresetById ("no-such-preset") == -1, "unknown stable id is rejected");
    check (metaleq::findPresetByName ("No Such Preset") == -1, "unknown name is rejected");

    const int neutralIndex = metaleq::findPresetById ("utility-neutral-flat");
    check (neutralIndex >= 0, "neutral reference preset exists");
    if (neutralIndex >= 0)
    {
        const auto& neutral = metaleq::getPreset (neutralIndex);
        check (neutral.setsMacros, "neutral preset resets macros");
        check (neutral.macros.tight == 0.0 && neutral.macros.body == 0.5
                   && neutral.macros.scoop == 0.0 && neutral.macros.bite == 0.0
                   && neutral.macros.fizzCut == 0.0,
               "neutral preset uses centred/off macros");
    }

    for (int i = 0; i < metaleq::getNumAnalyzerTargets(); ++i)
    {
        const auto& target = metaleq::getAnalyzerTarget (i);
        check (target.label != nullptr && *target.label != 0, "analyzer target has a label");
        check (metaleq::findPresetById (target.presetId) >= 0,
               "analyzer target points at an existing stable preset id");
    }
}
} // namespace

int main()
{
    testGraphAxis();
    testNonDestructiveShapes();
    testLowPass();
    testChainRetirement();
    testFactorySettings();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "MetalEQ DSP and graph tests passed.\n";
    return EXIT_SUCCESS;
}

#include <cmath>
#include <cstdlib>
#include <iostream>

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
    near (metaleq::graph::dbToUnit (0.0), 0.6, 1.0e-12, "0 dB vertical mapping");
    check (metaleq::graph::visibleLabelIndices (900.0, 52.0).size()
           < metaleq::graph::visibleLabelIndices (1800.0, 52.0).size(),
           "narrow graph uses fewer frequency labels");
}

void testNonDestructiveShapes()
{
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
    }
}

void testFactorySettings()
{
    check (metaleq::getNumPresets() == 16, "all factory profiles remain available");
    for (int i = 0; i < metaleq::getNumPresets(); ++i)
        check (metaleq::params::isValidEqSettings (metaleq::getPreset (i).eq), "factory preset parameters are valid");
}
}

int main()
{
    testGraphAxis();
    testNonDestructiveShapes();
    testLowPass();
    testFactorySettings();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "MetalEQ DSP and graph tests passed.\n";
    return EXIT_SUCCESS;
}

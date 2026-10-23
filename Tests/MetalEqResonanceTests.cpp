/*
    V3 resonance tests.

    The detector is judged on two deliberately opposite fixtures:
      * a varied chromatic performance with three narrow peaks added, which it
        should find at approximately the right frequencies;
      * the same performance without the peaks, which must stay silent.

    The surgical-chain test pins down the separation that makes the feature
    assistive: scan cuts live in their own narrow filters, the delta-monitor
    chain contains only those filters, and the musical bands are untouched.

    This target includes the JUCE-free DSP headers directly, so it does not
    need an audio device, JUCE message loop or plugin instantiation.
*/

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../Source/dsp/MetalEqAnalysis.h"

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

void checkNear (double actual, double expected, double tolerance, const char* message)
{
    if (std::abs (actual - expected) > tolerance)
    {
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
        ++failures;
    }
}

constexpr double sampleRate = 48000.0;

/** A deterministic, varied guitar-like fixture. Each 0.55 s segment is a
    different chromatic note with decaying harmonics and a little noise; the
    variation is what lets the harmonic-residual test separate a fixed
    resonance from note harmonics.
*/
std::vector<float> makeCapture (bool withResonances)
{
    constexpr double durationSeconds = 8.0;
    constexpr double segmentSeconds = 0.55;
    const int numSamples = static_cast<int> (sampleRate * durationSeconds);
    std::vector<float> samples (static_cast<size_t> (numSamples), 0.0f);

    static constexpr double notes[] = {
        82.41, 92.50, 103.83, 116.54, 130.81, 146.83,
        164.81, 185.00, 207.65, 233.08, 261.63, 293.66
    };

    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> noise (-1.0, 1.0);
    constexpr double twoPi = 6.283185307179586;

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const int segment = static_cast<int> (t / segmentSeconds);
        const double f0 = notes[segment % 12];
        const double local = t - static_cast<double> (segment) * segmentSeconds;
        const double envelope = std::max (0.0, std::min (std::min (1.0, local / 0.01),
                                                         std::min (1.0, (segmentSeconds - local) / 0.04)));

        double harmonicSum = 0.0;
        for (int harmonic = 1; harmonic * f0 < sampleRate * 0.45; ++harmonic)
            harmonicSum += std::sin (twoPi * harmonic * f0 * t + 0.3 * harmonic)
                         / static_cast<double> (harmonic);

        samples[static_cast<size_t> (i)] = static_cast<float> (
            0.2 * envelope * std::tanh (7.0 * harmonicSum) + 0.004 * noise (rng));
    }

    const auto applyPeak = [&samples] (double freq, double q, double gainDb)
    {
        const auto coefficients = metaleq::designBiquad (
            metaleq::FilterType::peak, sampleRate, freq, q, gainDb);
        double z1 = 0.0;
        double z2 = 0.0;
        for (auto& sample : samples)
        {
            const double input = static_cast<double> (sample);
            const double output = coefficients.b0 * input + z1;
            z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
            z2 = coefficients.b2 * input - coefficients.a2 * output;
            sample = static_cast<float> (output);
        }
    };

    if (withResonances)
    {
        applyPeak (733.0, 7.0, 6.0);
        applyPeak (2450.0, 10.0, 6.0);
        applyPeak (4170.0, 12.0, 4.5);
    }

    return samples;
}

/** Index of the candidate closest to `freq` in log-frequency terms, or -1. */
int closestCandidate (const metaleq::ResonanceAnalysisResult& result, double freq)
{
    int best = -1;
    double bestDistance = 0.0;

    for (int i = 0; i < result.numCandidates; ++i)
    {
        const auto candidate = result.candidates[static_cast<size_t> (i)].freq;
        if (candidate <= 0.0)
            continue;

        const double distance = std::abs (std::log2 (candidate / freq));
        if (best < 0 || distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }

    return bestDistance <= std::log2 (1.06) ? best : -1;
}

void testDetectorFindsStationaryPeaks()
{
    const auto capture = makeCapture (true);
    metaleq::ResonanceAnalysisResult result;
    metaleq::analyseResonances (capture.data(), static_cast<int> (capture.size()), sampleRate, result);

    check (result.framesAnalysed > 0, "synthetic capture produces analysis frames");
    check (result.enoughVariation, "synthetic chromatic capture has enough variation");
    check (result.numCandidates >= 3, "detector reports all three synthetic resonances");
    check (result.numCandidates <= metaleq::kMaxReportedResonances,
           "detector respects the reported-candidate limit");

    const double expected[] = { 733.0, 2450.0, 4170.0 };
    for (const double freq : expected)
    {
        const int index = closestCandidate (result, freq);
        check (index >= 0, "detector finds a candidate near each injected resonance");
        if (index < 0)
            continue;

        const auto& candidate = result.candidates[static_cast<size_t> (index)];
        check (candidate.q >= 3.0 && candidate.q <= 12.0,
               "suggested resonance Q stays narrow and musically usable");
        check (candidate.suggestedCutDb <= 0.0 && candidate.suggestedCutDb >= -6.0,
               "suggested cut is conservative and always a cut");
        check (candidate.confidence > 0.2 && candidate.confidence <= 1.0,
               "candidate confidence is bounded");
        check (candidate.prominenceDb > 2.0, "candidate clears the LTAS prominence gate");
        check (candidate.residualDb > 1.5, "candidate clears the harmonic-residual gate");
    }
}

void testDetectorRejectsNeutralCapture()
{
    const auto capture = makeCapture (false);
    metaleq::ResonanceAnalysisResult result;
    metaleq::analyseResonances (capture.data(), static_cast<int> (capture.size()), sampleRate, result);

    check (result.framesAnalysed > 0, "neutral capture produces analysis frames");
    check (result.numCandidates == 0, "neutral varied capture produces no resonance candidates");
}

void testDetectorHandlesUnusableInput()
{
    metaleq::ResonanceAnalysisResult result;
    metaleq::analyseResonances (nullptr, 0, sampleRate, result);
    check (result.numCandidates == 0, "null input returns no candidates");

    std::vector<float> silence (static_cast<size_t> (sampleRate), 0.0f);
    metaleq::analyseResonances (silence.data(), static_cast<int> (silence.size()), sampleRate, result);
    check (result.numCandidates == 0, "one second of silence returns no candidates");
}

double surgicalResponse (const metaleq::EqSettings& eq, double freq)
{
    metaleq::ChainSpec chain;
    metaleq::buildSurgicalChain (chain, eq, sampleRate);
    return metaleq::chainResponseDb (chain, sampleRate, freq);
}

void testSurgicalChain()
{
    metaleq::EqSettings neutral;
    metaleq::MacroSettings macros;

    const double neutralAtPeak = metaleq::responseDb (neutral, macros, sampleRate, 733.0);
    checkNear (neutralAtPeak, 0.0, 0.01, "neutral chain is flat at the test frequency");

    neutral.surgical[0] = { 733.0, -6.0, 9.0, true };
    const double cutAtPeak = metaleq::responseDb (neutral, macros, sampleRate, 733.0);
    check (cutAtPeak < -5.0, "surgical band cuts its centre frequency");
    checkNear (surgicalResponse (neutral, 733.0), cutAtPeak, 1.0e-9,
               "full chain and delta-monitor chain agree at the cut centre");
    checkNear (surgicalResponse (neutral, 400.0), 0.0, 0.05,
               "surgical chain is transparent away from its cut");
    check (metaleq::responseDb (neutral, macros, sampleRate, 400.0) > -0.1,
           "surgical cut does not act as a broad tonal filter");

    // Moving a musical band must not leak into the delta-monitor path.
    const double monitorAtBand = surgicalResponse (neutral, 1000.0);
    neutral.bands[0] = { 1000.0, 9.0, 2.0, true };
    checkNear (surgicalResponse (neutral, 1000.0), monitorAtBand, 1.0e-9,
               "delta monitor ignores the five musical bands");
    check (metaleq::responseDb (neutral, macros, sampleRate, 1000.0) > 6.0,
           "the main chain still contains the musical band");

    // The delta monitor has no output trim: it is a surgical residual only.
    const double monitorBeforeTrim = surgicalResponse (neutral, 400.0);
    neutral.trimDb = 6.0;
    checkNear (surgicalResponse (neutral, 400.0), monitorBeforeTrim, 1.0e-9,
               "delta monitor ignores output trim");

    // Disabled and flat bands are inert.
    metaleq::EqSettings disabled = neutral;
    disabled.surgical[0].on = false;
    checkNear (surgicalResponse (disabled, 733.0), 0.0, 1.0e-9,
               "disabled surgical band has no effect");

    metaleq::EqSettings flat = neutral;
    flat.surgical[0].gainDb = 0.0;
    checkNear (surgicalResponse (flat, 733.0), 0.0, 1.0e-9,
               "flat surgical band has no effect");

    // A narrower Q removes less of the neighbouring tone.
    metaleq::EqSettings wide = neutral;
    wide.surgical[0] = { 733.0, -6.0, 2.0, true };
    metaleq::EqSettings narrow = neutral;
    narrow.surgical[0] = { 733.0, -6.0, 14.0, true };
    const double shoulder = 733.0 * std::pow (2.0, 0.25);
    check (surgicalResponse (narrow, shoulder) > surgicalResponse (wide, shoulder) + 0.5,
           "higher Q removes less of the surrounding tone");
}

} // namespace

int main()
{
    testDetectorFindsStationaryPeaks();
    testDetectorRejectsNeutralCapture();
    testDetectorHandlesUnusableInput();
    testSurgicalChain();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "MetalEQ resonance detector and surgical-chain tests passed.\n";
    return EXIT_SUCCESS;
}

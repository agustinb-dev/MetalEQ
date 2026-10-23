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

/** Index into result.dynamicSuggestions closest to `freq`, or -1. */
int closestDynamicSuggestion (const metaleq::ResonanceAnalysisResult& result, double freq)
{
    int best = -1;
    double bestDistance = 0.0;

    for (int i = 0; i < result.numDynamicSuggestions; ++i)
    {
        const auto candidate = result.dynamicSuggestions[static_cast<size_t> (i)].band.freq;
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

    check (result.numDynamicSuggestions >= 1,
           "detector proposes at least one dynamic band from the capture");
    check (closestDynamicSuggestion (result, 733.0) >= 0,
           "detector proposes a dynamic band for the strongest resonance");

    for (int i = 0; i < result.numDynamicSuggestions; ++i)
    {
        const auto& suggestion = result.dynamicSuggestions[static_cast<size_t> (i)];
        check (suggestion.candidateIndex >= 0
               && suggestion.candidateIndex < result.numCandidates,
               "dynamic suggestion points back at a valid scan candidate");
        if (suggestion.candidateIndex < 0 || suggestion.candidateIndex >= result.numCandidates)
            continue;

        const auto& candidate = result.candidates[static_cast<size_t> (suggestion.candidateIndex)];
        check (suggestion.measuredExcessDb >= 3.0,
               "dynamic suggestion is based on a measurable hot-point excess");
        check (suggestion.band.freq == candidate.freq,
               "dynamic suggestion keeps the candidate centre frequency");
        check (suggestion.band.q >= 1.2 && suggestion.band.q <= 3.0,
               "dynamic suggestion uses a broad, usable cut Q");
        check (suggestion.band.ratio == 4.0 && suggestion.band.attackMs == 5.0
               && suggestion.band.releaseMs == 120.0,
               "dynamic suggestion uses the standard dynamic timing and ratio");
        check (suggestion.band.thresholdDb >= 0.0 && suggestion.band.thresholdDb <= 10.0,
               "dynamic threshold stays in a musical range");
        check (suggestion.band.maxCutDb >= 2.0 && suggestion.band.maxCutDb <= 6.0,
               "dynamic maximum cut stays conservative");
        check (suggestion.band.on, "dynamic suggestion is enabled");
    }
}

void testDetectorRejectsNeutralCapture()
{
    const auto capture = makeCapture (false);
    metaleq::ResonanceAnalysisResult result;
    metaleq::analyseResonances (capture.data(), static_cast<int> (capture.size()), sampleRate, result);

    check (result.framesAnalysed > 0, "neutral capture produces analysis frames");
    check (result.numCandidates == 0, "neutral varied capture produces no resonance candidates");
    check (result.numDynamicSuggestions == 0,
           "neutral varied capture produces no dynamic-band suggestions");
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

//==============================================================================
// V3.1 dynamic resonance section
//==============================================================================

std::vector<float> makeSine (double freq, double amplitude, double seconds)
{
    const int numSamples = static_cast<int> (sampleRate * seconds);
    std::vector<float> samples (static_cast<size_t> (numSamples), 0.0f);
    constexpr double twoPi = 6.283185307179586;

    for (int i = 0; i < numSamples; ++i)
        samples[static_cast<size_t> (i)] = static_cast<float> (
            amplitude * std::sin (twoPi * freq * static_cast<double> (i) / sampleRate
                                  + 1.5707963267948966));

    return samples;
}

double rmsRange (const std::vector<float>& samples, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double> (samples[i]) * static_cast<double> (samples[i]);

    return std::sqrt (sum / static_cast<double> (to - from));
}

struct DynamicRunResult
{
    std::vector<float> output;
    double gainReductionDb = 0.0;
};

DynamicRunResult runDynamicBand (const std::vector<float>& input,
                                 const metaleq::DynamicBandSettings& band,
                                 int tailSilenceSamples = 0,
                                 bool reportMaximum = false)
{
    metaleq::DynamicResonanceEngine engine;
    engine.prepare (sampleRate);

    std::array<metaleq::DynamicBandSettings, metaleq::kNumDynamicBands> settings {};
    settings[0] = band;
    engine.setSettings (settings, 1);

    std::vector<float> data = input;
    data.resize (data.size() + static_cast<size_t> (tailSilenceSamples), 0.0f);

    double maximum = 0.0;
    constexpr int blockSize = 256;
    for (int start = 0; start < static_cast<int> (data.size()); start += blockSize)
    {
        const int count = std::min (blockSize, static_cast<int> (data.size()) - start);
        float* pointer = data.data() + start;
        engine.process (&pointer, 1, count);
        maximum = std::max (maximum, engine.getGainReductionDb (0));
    }

    return { std::move (data), reportMaximum ? maximum : engine.getGainReductionDb (0) };
}

metaleq::DynamicBandSettings makeTestDynamicBand()
{
    metaleq::DynamicBandSettings band;
    band.freq        = 733.0;
    band.q           = 1.2;
    band.thresholdDb = 6.0;
    band.ratio       = 4.0;
    band.attackMs    = 5.0;
    band.releaseMs   = 120.0;
    band.maxCutDb    = 6.0;
    band.on          = true;
    return band;
}

void testDynamicSuggestionMeasurement()
{
    const auto sine = makeSine (733.0, 0.25, 2.0);
    const auto stats = metaleq::detail::measureDynamicExcess (
        sine.data(), static_cast<int> (sine.size()), sampleRate, 733.0);

    check (stats.values >= 16, "relative-excess measurement collects enough gated points");
    check (stats.activeFraction > 0.5, "a sustained tone keeps the dynamic detector active");
    check (stats.medianDb > 3.0,
           "a band-centred tone measures a positive relative excess");

    const auto capture = makeCapture (true);
    metaleq::ResonanceAnalysisResult result;
    metaleq::analyseResonances (capture.data(), static_cast<int> (capture.size()), sampleRate, result);

    const int suggestionIndex = closestDynamicSuggestion (result, 733.0);
    check (suggestionIndex >= 0, "analyzer produces a dynamic suggestion for the 733 Hz peak");
    if (suggestionIndex < 0)
        return;

    const auto& suggestion = result.dynamicSuggestions[static_cast<size_t> (suggestionIndex)];
    const auto run = runDynamicBand (capture, suggestion.band, 0, true);
    check (run.gainReductionDb > 0.2,
           "a scan-suggested dynamic band actually engages on the captured program");
    check (run.gainReductionDb <= suggestion.band.maxCutDb + 1.0e-6,
           "a scan-suggested dynamic band respects its suggested maximum cut");
}

void testDynamicEngineTransparency()
{
    metaleq::DynamicBandSettings off;
    off.on = false;

    std::vector<float> noise (4800);
    for (int i = 0; i < static_cast<int> (noise.size()); ++i)
        noise[static_cast<size_t> (i)] = 0.05f * std::sin (0.37f * static_cast<float> (i));

    const auto disabled = runDynamicBand (noise, off);
    check (disabled.output == noise, "disabled dynamic section is bit-transparent");
    checkNear (disabled.gainReductionDb, 0.0, 1.0e-9, "disabled dynamic section reports no reduction");

    metaleq::DynamicBandSettings flat = makeTestDynamicBand();
    flat.maxCutDb = 0.0;
    const auto noRange = runDynamicBand (noise, flat);
    check (noRange.output == noise, "zero maximum cut leaves the dynamic section transparent");

    metaleq::DynamicBandSettings unity = makeTestDynamicBand();
    unity.ratio = 1.0;
    const auto noRatio = runDynamicBand (noise, unity);
    check (noRatio.output == noise, "1:1 dynamic ratio leaves the section transparent");
}

void testDynamicEngineCutsExcess()
{
    const auto band = makeTestDynamicBand();
    const auto sine = makeSine (733.0, 0.25, 2.0);
    const auto loud = runDynamicBand (sine, band);
    const double inputRms = rmsRange (sine, sine.size() - 24000, sine.size());
    const double outputRms = rmsRange (loud.output, loud.output.size() - 24000, loud.output.size());

    check (loud.gainReductionDb > 1.5, "a sustained band-centred tone engages the dynamic cut");
    check (loud.gainReductionDb <= band.maxCutDb + 1.0e-6, "dynamic reduction respects its maximum cut");
    check (outputRms < inputRms * 0.8, "the dynamic cut lowers the band-centre output");
    check (outputRms > inputRms * 0.6, "the dynamic cut is not an unconditional mute");
    checkNear (20.0 * std::log10 (outputRms / inputRms), -loud.gainReductionDb, 0.3,
               "steady-state output reduction matches the reported gain reduction");

    // The detector is relative, so a much quieter copy of the same signal
    // must produce the same reduction. Only the absolute level changes.
    const auto quietSine = makeSine (733.0, 0.01, 2.0);
    const auto quiet = runDynamicBand (quietSine, band);
    checkNear (quiet.gainReductionDb, loud.gainReductionDb, 0.1,
               "dynamic detection is independent of absolute input level");

    // Broadband noise has no narrow peak over its neighbours, so it must not
    // trigger a cut even though it is louder than the quiet sine.
    std::mt19937 rng (42);
    std::uniform_real_distribution<double> noise (-1.0, 1.0);
    std::vector<float> white (static_cast<size_t> (sampleRate * 2.0));
    for (auto& sample : white)
        sample = static_cast<float> (0.1 * noise (rng));

    const auto broadband = runDynamicBand (white, band);
    check (broadband.gainReductionDb < 0.05, "broadband energy does not trigger a dynamic cut");
}

void testDynamicEngineZeroLatencyAndClamp()
{
    auto band = makeTestDynamicBand();
    band.thresholdDb = 0.0;
    band.ratio = 10.0;
    band.maxCutDb = 12.0;

    const auto sine = makeSine (733.0, 0.25, 2.0);
    const auto result = runDynamicBand (sine, band);

    // Sample 0 is processed with sample 0. The attack smoother starts at
    // unity gain, so the very first sample must be very close to the input.
    check (std::abs (result.output[0] - sine[0]) < std::abs (sine[0]) * 0.01,
           "dynamic section has no lookahead or sample delay");

    band.maxCutDb = 3.0;
    const auto clamped = runDynamicBand (sine, band);
    check (clamped.gainReductionDb <= 3.0 + 1.0e-6, "dynamic maximum cut clamps the reduction");
    check (clamped.gainReductionDb > 2.5, "a hot signal reaches the maximum-cut ceiling");
}

void testDynamicEngineRelease()
{
    const auto band = makeTestDynamicBand();
    const auto sine = makeSine (733.0, 0.25, 2.0);
    const auto sustained = runDynamicBand (sine, band);
    const auto released = runDynamicBand (sine, band, static_cast<int> (sampleRate * 0.1));

    check (released.gainReductionDb < sustained.gainReductionDb - 0.5,
           "dynamic gain reduction releases after the excess energy stops");
    check (released.gainReductionDb > 0.2,
           "the release control does not snap the cut to zero");
}

void testDynamicEngineStereoLink()
{
    const auto band = makeTestDynamicBand();
    const auto leftInput = makeSine (733.0, 0.25, 2.0);
    const auto rightInput = makeSine (733.0, 0.125, 2.0);
    std::vector<float> left = leftInput;
    std::vector<float> right = rightInput;

    metaleq::DynamicResonanceEngine engine;
    engine.prepare (sampleRate);
    std::array<metaleq::DynamicBandSettings, metaleq::kNumDynamicBands> settings {};
    settings[0] = band;
    engine.setSettings (settings, 1);

    constexpr int blockSize = 256;
    for (int start = 0; start < static_cast<int> (left.size()); start += blockSize)
    {
        const int count = std::min (blockSize, static_cast<int> (left.size()) - start);
        float* pointers[2] = { left.data() + start, right.data() + start };
        engine.process (pointers, 2, count);
    }

    const double leftRatio = rmsRange (left, left.size() - 24000, left.size())
                           / rmsRange (leftInput, leftInput.size() - 24000, leftInput.size());
    const double rightRatio = rmsRange (right, right.size() - 24000, right.size())
                            / rmsRange (rightInput, rightInput.size() - 24000, rightInput.size());

    checkNear (20.0 * std::log10 (leftRatio), 20.0 * std::log10 (rightRatio), 0.05,
               "stereo-linked detection applies the same gain to both channels");
}

void testDynamicActiveHelper()
{
    metaleq::EqSettings eq;
    check (! metaleq::hasActiveDynamicCuts (eq), "neutral settings report no active dynamic cuts");

    eq.dynamic[0] = makeTestDynamicBand();
    check (metaleq::hasActiveDynamicCuts (eq), "an enabled dynamic band reports an active cut");

    eq.dynamic[0].maxCutDb = 0.0;
    check (! metaleq::hasActiveDynamicCuts (eq), "a zero maximum cut is not active");

    eq.dynamic[0] = makeTestDynamicBand();
    eq.dynamic[0].ratio = 1.0;
    check (! metaleq::hasActiveDynamicCuts (eq), "a 1:1 dynamic band is not active");

    eq.dynamic[0] = makeTestDynamicBand();
    eq.dynamic[0].on = false;
    check (! metaleq::hasActiveDynamicCuts (eq), "a disabled dynamic band is not active");
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
    testDynamicSuggestionMeasurement();
    testDynamicEngineTransparency();
    testDynamicEngineCutsExcess();
    testDynamicEngineZeroLatencyAndClamp();
    testDynamicEngineRelease();
    testDynamicEngineStereoLink();
    testDynamicActiveHelper();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "MetalEQ resonance detector, surgical-chain and dynamic-section tests passed.\n";
    return EXIT_SUCCESS;
}

/*
    V3.2 analyzer model tests.

    These tests are deliberately independent of JUCE and of the audio device:
    they exercise the research-backed target table, the nine-band input
    analysis, and the adaptive correction solver directly.

    What is asserted here is the *behaviour* the Analyzer promises:
      * every target is well-formed and traceable to sources;
      * the same input yields different destinations for different targets;
      * different inputs yield different corrections for the same target;
      * an input that already matches its target is left essentially alone;
      * no fitted gain is extreme or invalid.

    The numeric target values are engineering estimates; these tests check
    internal consistency and safety, not historical accuracy.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../Source/dsp/MetalEqParams.h"
#include "../Source/dsp/MetalEqToneTargets.h"
#include "../Tools/SpectrumProfiler/fixtures/synthetic_expected.h"

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

metaleq::tone::InputProfile makeInput (const std::array<double, metaleq::tone::kNumFeatures>& shape,
                                       double confidence = 0.9)
{
    metaleq::tone::InputProfile input;
    input.relativeDb = shape;
    input.confidence = confidence;
    input.valid = true;
    return input;
}

constexpr double sampleRate = 48000.0;

/** Broadband deterministic fixture: each feature band contains a cluster of
    log-spaced tones. Per-tone amplitude is scaled by sqrt(bandwidth) so the
    band's average power spectral density is what `relativeDb` describes. A
    single sine per band would not be meaningful for a wide-band analyzer: its
    average PSD falls as the band gets wider.
*/
std::vector<float> makeBandTones (const std::array<double, metaleq::tone::kNumFeatures>& relativeDb,
                                  double seconds = 2.0)
{
    const int numSamples = static_cast<int> (sampleRate * seconds);
    std::vector<float> samples (static_cast<size_t> (numSamples), 0.0f);

    constexpr int tonesPerBand = 12;
    for (int band = 0; band < metaleq::tone::kNumFeatures; ++band)
    {
        const auto& spec = metaleq::tone::kFeatures[static_cast<size_t> (band)];
        const double bandwidth = spec.highHz - spec.lowHz;
        const double amplitudeScale = 0.0018 * std::sqrt (bandwidth / static_cast<double> (tonesPerBand))
                                    * std::pow (10.0, relativeDb[static_cast<size_t> (band)] / 20.0);

        for (int tone = 0; tone < tonesPerBand; ++tone)
        {
            const double unit = (static_cast<double> (tone) + 0.5) / static_cast<double> (tonesPerBand);
            const double freq = spec.lowHz * std::pow (spec.highHz / spec.lowHz, unit);
            const double phase = 0.37 * static_cast<double> (band * tonesPerBand + tone);

            for (int i = 0; i < numSamples; ++i)
                samples[static_cast<size_t> (i)] += static_cast<float> (
                    amplitudeScale * std::sin (2.0 * metaleq::kPi * freq
                                               * static_cast<double> (i) / sampleRate + phase));
        }
    }

    return samples;
}

void testTargetTable()
{
    using namespace metaleq::tone;

    check (getNumToneTargets() == 16, "there are 16 target profiles");
    check (kGlobalMaxGainDb == 12.0, "the global correction cap is 12 dB");
    check (getNumSources() >= 30, "the research record has a substantial source list");

    std::set<std::string> ids;
    std::array<int, 3> categoryCounts {};

    for (int i = 0; i < getNumToneTargets(); ++i)
    {
        const auto& target = getToneTarget (i);

        check (target.id != nullptr && *target.id != 0, "target has a stable id");
        check (target.label != nullptr && *target.label != 0, "target has a label");
        check (target.shortLabel != nullptr && *target.shortLabel != 0, "target has a short label");
        check (target.albumPeriod != nullptr && *target.albumPeriod != 0, "target names its album/period");
        check (target.objective != nullptr && *target.objective != 0, "target has an objective");
        check (target.limitations != nullptr && *target.limitations != 0, "target records its limitations");
        check (target.confidence >= 0.0 && target.confidence <= 1.0, "target confidence is in 0..1");
        check (target.numSources >= 3 && target.numSources <= 5, "target has 3-5 source references");

        ids.insert (target.id);
        categoryCounts[static_cast<size_t> (static_cast<int> (target.category))] += 1;

        for (int s = 0; s < target.numSources; ++s)
        {
            const auto& ref = target.sources[static_cast<size_t> (s)];
            check (ref.sourceIndex >= 0 && ref.sourceIndex < getNumSources(),
                   "target source index is valid");
            check (ref.claim != nullptr && *ref.claim != 0, "target source has a claim");

            const auto& source = getSource (ref.sourceIndex);
            check (source.title != nullptr && *source.title != 0, "source has a title");
            check (source.url != nullptr && std::string (source.url).rfind ("http", 0) == 0,
                   "source has an http(s) URL");
            check (source.date != nullptr && *source.date != 0, "source has a date or access date");
        }

        for (int f = 0; f < kNumFeatures; ++f)
        {
            const auto& feature = target.features[static_cast<size_t> (f)];
            check (std::isfinite (feature.relativeDb), "feature target is finite");
            check (std::abs (feature.relativeDb) <= 12.0, "feature target is within a musical range");
            check (feature.toleranceDb >= 0.0 && feature.toleranceDb <= 3.0,
                   "feature tolerance is sane");
            check (feature.weight >= 0.0 && feature.weight <= 1.0, "feature weight is in 0..1");
            check (feature.maxCorrectionDb >= 0.0 && feature.maxCorrectionDb <= 6.0,
                   "feature correction cap is sane");
            check (feature.rationale != nullptr && *feature.rationale != 0,
                   "feature records a rationale");
        }
    }

    check (ids.size() == 16, "target ids are unique");
    check (categoryCounts[0] == 6, "there are six genre targets");
    check (categoryCounts[1] == 4, "there are four artist targets");
    check (categoryCounts[2] == 6, "there are six producer targets");

    // The key differentiators share a 0.5 dB deadband across all profiles.
    for (const int feature : { 0, 1, 2, 4, 7 })
        for (int t = 0; t < getNumToneTargets(); ++t)
            checkNear (getToneTarget (t).features[static_cast<size_t> (feature)].toleranceDb,
                       0.5, 1.0e-9, "key-differentiator deadband is 0.5 dB");


    check (findToneTargetById ("genre-thrash-ajfa") >= 0, "AJFA target is addressable by stable id");
    check (findToneTargetById ("producer-andy-sneap") >= 0, "Sneap target is addressable by stable id");
    check (findToneTargetById ("nonexistent") < 0, "unknown target id is rejected");

    check (std::string (categoryLabel (Category::genre)) == "Genres", "genre category label");
    check (std::string (categoryLabel (Category::artist)) == "Artists", "artist category label");
    check (std::string (categoryLabel (Category::producer)) == "Producers", "producer category label");
}

void testInputAnalysis()
{
    using namespace metaleq::tone;

    std::array<double, kNumFeatures> flat {};
    const auto flatCapture = makeBandTones (flat);
    const auto flatProfile = analyseInput (flatCapture.data(), static_cast<int> (flatCapture.size()), sampleRate);

    check (flatProfile.valid, "a two-second band-tone capture is valid");
    check (flatProfile.frames > 10, "the capture produces enough analysis frames");
    check (flatProfile.confidence > 0.2 && flatProfile.confidence <= 1.0,
           "input confidence is in a useful range");

    double flatMin = flatProfile.relativeDb[0];
    double flatMax = flatProfile.relativeDb[0];
    for (int i = 1; i < kNumFeatures; ++i)
    {
        flatMin = std::min (flatMin, flatProfile.relativeDb[static_cast<size_t> (i)]);
        flatMax = std::max (flatMax, flatProfile.relativeDb[static_cast<size_t> (i)]);
    }
    check (flatMax - flatMin < 0.5,
           "equally loud analysis bands read as a flat relative shape");

    std::array<double, kNumFeatures> shaped {};
    shaped[2] = 6.0;   // 350 Hz body
    shaped[8] = -6.0;  // 9.5 kHz fizz cut
    const auto shapedCapture = makeBandTones (shaped);
    const auto shapedProfile = analyseInput (shapedCapture.data(), static_cast<int> (shapedCapture.size()), sampleRate);

    check (shapedProfile.valid, "shaped capture is valid");
    checkNear (shapedProfile.relativeDb[2] - flatProfile.relativeDb[2], 6.0, 1.0,
               "350 Hz emphasis is measured");
    checkNear (shapedProfile.relativeDb[8] - flatProfile.relativeDb[8], -6.0, 1.0,
               "9.5 kHz cut is measured");

    std::vector<float> silence (static_cast<size_t> (sampleRate), 0.0f);
    check (! analyseInput (silence.data(), static_cast<int> (silence.size()), sampleRate).valid,
           "silence is rejected");

    std::vector<float> tooShort (100, 0.1f);
    check (! analyseInput (tooShort.data(), static_cast<int> (tooShort.size()), sampleRate).valid,
           "too-short capture is rejected");
}

/** Phase 3: capture diagnostics. A clean full-band capture carries no
    warnings; sub-60 Hz bleed and a missing top octave are recognised; the
    per-band median survives loud/quiet swings and is preferred by the solver.
*/
void testCaptureDiagnostics()
{
    using namespace metaleq::tone;

    const auto hasWarning = [] (const InputProfile& profile, CaptureWarning warning)
    {
        return (profile.warnings & static_cast<unsigned> (warning)) != 0;
    };

    std::array<double, kNumFeatures> flat {};
    const auto cleanCapture = makeBandTones (flat);
    const auto clean = analyseInput (cleanCapture.data(), static_cast<int> (cleanCapture.size()), sampleRate);
    check (clean.valid && clean.hasMedian, "a clean capture produces a median profile");
    check (! hasWarning (clean, CaptureWarning::lowEndLeakage),
           "a clean full-band capture has no leakage warning");
    check (! hasWarning (clean, CaptureWarning::highEndTruncated),
           "a clean full-band capture has no high-end warning");
    check (clean.tiltDbPerOct > -1.0 && clean.tiltDbPerOct < 1.0,
           "a flat capture has a near-zero spectral tilt");

    // Kick/bass bleed: a strong 40 Hz component sits inside the sub-60 band.
    auto leakyCapture = cleanCapture;
    for (size_t i = 0; i < leakyCapture.size(); ++i)
        leakyCapture[i] += static_cast<float> (
            0.05 * std::sin (2.0 * metaleq::kPi * 40.0 * static_cast<double> (i) / sampleRate));
    const auto leaky = analyseInput (leakyCapture.data(), static_cast<int> (leakyCapture.size()), sampleRate);
    check (leaky.valid, "the leaky capture is still analysable");
    check (leaky.sub60RelDb > kLeakageWarningSub60Db,
           "sub-60 Hz bleed is measured above the warning threshold");
    check (hasWarning (leaky, CaptureWarning::lowEndLeakage),
           "sub-60 Hz bleed raises the leakage warning");

    // Codec-style truncation: no energy in the 9.5 kHz band at all, so the
    // 10-16 kHz floor sits far below 5.6 kHz.
    std::array<double, kNumFeatures> dark {};
    dark[8] = -100.0;
    const auto darkCapture = makeBandTones (dark);
    const auto truncated = analyseInput (darkCapture.data(), static_cast<int> (darkCapture.size()), sampleRate);
    check (truncated.valid, "the dark capture is still analysable");
    check (hasWarning (truncated, CaptureWarning::highEndTruncated),
           "a missing top octave raises the high-end warning");

    // Loud/quiet alternation: the linear mean follows the loud half, the
    // median reports the middle of the take.
    std::array<double, kNumFeatures> quietShape {};
    std::array<double, kNumFeatures> loudShape {};
    loudShape[2] = 12.0; // 350 Hz much louder in the loud half
    auto quietHalf = makeBandTones (quietShape, 2.0);
    auto loudHalf = makeBandTones (loudShape, 2.0);
    for (auto& sample : loudHalf)
        sample *= 3.0f; // +9.5 dB, so the mean is loud-half dominated

    std::vector<float> alternated;
    alternated.reserve (quietHalf.size() + loudHalf.size());
    alternated.insert (alternated.end(), quietHalf.begin(), quietHalf.end());
    alternated.insert (alternated.end(), loudHalf.begin(), loudHalf.end());

    const auto medianProfile = analyseInput (alternated.data(), static_cast<int> (alternated.size()), sampleRate);
    check (medianProfile.valid && medianProfile.hasMedian, "alternating take has a median profile");
    check (medianProfile.bandMaxRelDb[2] - medianProfile.bandMinRelDb[2] > 6.0,
           "the alternation is visible in the per-band range");
    check (medianProfile.relativeDbMedian[2] < medianProfile.relativeDb[2] - 2.0,
           "the median is not dragged up by the loud half");
    check (medianProfile.relativeDbMedian[2] > quietShape[2] + 2.0,
           "the median still reflects both halves of the take");

    // The solver must fit the median, not the loud-half-dominated mean.
    const auto& target = getToneTarget (findToneTargetById ("genre-thrash-early-80s"));
    InputProfile preferred = makeInput (targetShape (target), 1.0);
    preferred.relativeDb[2] += 6.0; // misleading mean
    preferred.relativeDbMedian = targetShape (target);
    preferred.hasMedian = true;

    const auto medianFit = computeCorrection (preferred, target, sampleRate);
    preferred.hasMedian = false;
    const auto meanFit = computeCorrection (preferred, target, sampleRate);
    check (medianFit.maxAbsGainDb + 0.2 < meanFit.maxAbsGainDb,
           "the correction prefers the median take shape over the mean");
}

void testCorrectionSafety()
{
    using namespace metaleq::tone;

    const std::array<std::array<double, kNumFeatures>, 6> inputs {{
        {{ -2.0, -0.5,  0.0, 0.5,  0.5, 0.0, -1.0, -2.0, -6.0 }},
        {{ -4.0, -2.0,  3.0, 1.0, -1.0, 0.5, -0.2, -1.0, -5.0 }},
        {{ -1.0,  0.5, -3.0, 0.0,  2.5, 2.0,  0.5, -1.0, -6.0 }},
        {{  1.0,  2.0,  1.5, 1.0,  0.0, -0.5, -1.8, -3.0, -8.0 }},
        {{ -3.0, -1.0, -2.0, 1.0,  3.0, 3.0,  2.0,  1.0, -3.0 }},
        {{ -6.0, -3.0, -4.0, 0.0,  1.0, 0.0, -3.5, -7.0, -12.0 }}
    }};

    for (int t = 0; t < getNumToneTargets(); ++t)
    {
        const auto& target = getToneTarget (t);
        for (const auto& shape : inputs)
        {
            const auto input = makeInput (shape, 0.85);
            const auto result = computeCorrection (input, target, sampleRate);

            check (result.confidence >= 0.0 && result.confidence <= 1.0,
                   "correction confidence is in 0..1");
            check (std::isfinite (result.maxAbsGainDb), "max gain is finite");
            check (result.maxAbsGainDb <= kGlobalMaxGainDb + 1.0e-9,
                   "fitted gains respect the global cap");

            for (const double gain : result.gainsDb)
                check (std::isfinite (gain) && std::abs (gain) <= kGlobalMaxGainDb + 1.0e-9,
                       "each fitted gain is finite and capped");

            for (const double level : result.predictedDb)
                check (std::isfinite (level), "predicted levels are finite");

            metaleq::EqSettings eq {};
            for (int band = 0; band < metaleq::kNumBands; ++band)
            {
                auto& b = eq.bands[static_cast<size_t> (band)];
                b.freq = kCorrectionFrequencies[static_cast<size_t> (band)];
                b.gainDb = result.gainsDb[static_cast<size_t> (band)];
                b.q = kCorrectionQ;
                b.on = true;
            }
            check (metaleq::params::isValidEqSettings (eq),
                   "the fitted five-band correction is a valid EQ setting");
        }
    }
}

void testAlreadySuitableInput()
{
    using namespace metaleq::tone;

    for (int t = 0; t < getNumToneTargets(); ++t)
    {
        const auto& target = getToneTarget (t);
        const auto input = makeInput (targetShape (target), 1.0);
        const auto result = computeCorrection (input, target, sampleRate);

        check (result.alreadyClose, "an already-matching input is recognised");
        check (result.maxAbsGainDb < 0.25, "an already-matching input needs no meaningful correction");

        // A modest, real mismatch must produce a larger correction than the
        // already-matching case.
        auto mismatched = targetShape (target);
        mismatched[2] += 3.0;
        mismatched[7] -= 2.0;
        const auto mismatchResult = computeCorrection (makeInput (mismatched, 0.95), target, sampleRate);
        check (mismatchResult.maxAbsGainDb > result.maxAbsGainDb + 0.2,
               "a real mismatch produces a larger correction than a match");
    }
}

void testDeadbandAndConfidenceTuning()
{
    using namespace metaleq::tone;

    // The key differentiators now use a 0.5 dB deadband. A 0.75 dB mismatch
    // that used to be ignored must produce a small but real correction.
    {
        const auto& target = getToneTarget (0);
        auto shape = targetShape (target);
        shape[2] += 0.75; // 350 Hz
        const auto result = computeCorrection (makeInput (shape, 1.0), target, sampleRate);
        check (result.maxAbsGainDb > 0.05,
               "a 0.75 dB mismatch on a key band is no longer swallowed by the deadband");

        // A 0.25 dB mismatch remains inside the deadband and should be
        // essentially ignored.
        auto tiny = targetShape (target);
        tiny[2] += 0.25;
        const auto tinyResult = computeCorrection (makeInput (tiny, 1.0), target, sampleRate);
        check (tinyResult.maxAbsGainDb < 0.10,
               "a 0.25 dB mismatch remains inside the deadband");
    }

    // High-confidence targets should use more of the available correction
    // than low-confidence targets for the same mismatch.
    {
        ToneTarget high = getToneTarget (0);
        ToneTarget low = high;
        high.confidence = 0.90;
        low.confidence = 0.40;

        auto shape = targetShape (high);
        shape[2] += 2.0;
        const auto highResult = computeCorrection (makeInput (shape, 1.0), high, sampleRate);
        const auto lowResult = computeCorrection (makeInput (shape, 1.0), low, sampleRate);
        check (highResult.maxAbsGainDb > lowResult.maxAbsGainDb + 0.05,
               "higher-confidence targets receive a larger (but still limited) correction");
    }

    // The global cap is now 12 dB. An extreme synthetic destination must be
    // allowed past the old 6 dB ceiling while remaining finite and capped.
    {
        ToneTarget extreme = getToneTarget (0);
        extreme.confidence = 1.0;
        for (auto& feature : extreme.features)
        {
            feature.relativeDb = 12.0;
            feature.toleranceDb = 0.0;
            feature.weight = 1.0;
            feature.maxCorrectionDb = 12.0;
        }

        const auto result = computeCorrection (makeInput ({ 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1.0),
                                               extreme, sampleRate);
        check (result.maxAbsGainDb > 6.0,
               "the raised 12 dB ceiling is reachable for an extreme mismatch");
        check (result.maxAbsGainDb <= kGlobalMaxGainDb + 1.0e-9,
               "even an extreme mismatch stops at the 12 dB safety cap");
        for (const double gain : result.gainsDb)
            check (std::isfinite (gain) && std::abs (gain) <= kGlobalMaxGainDb + 1.0e-9,
                   "the 12 dB cap applies per band");
    }
}

void testInputAdaptation()
{
    using namespace metaleq::tone;

    const auto& target = getToneTarget (0); // 2000s metalcore

    auto muddy = makeInput (targetShape (target), 0.95);
    muddy.relativeDb[2] += 4.0;  // low-mid build-up
    muddy.relativeDb[1] += 2.0;
    muddy.relativeDb[4] -= 1.5;

    auto thin = makeInput (targetShape (target), 0.95);
    thin.relativeDb[2] -= 3.0;
    thin.relativeDb[1] -= 2.0;
    thin.relativeDb[4] += 2.0;

    auto bright = makeInput (targetShape (target), 0.95);
    bright.relativeDb[7] += 3.0;
    bright.relativeDb[8] += 3.0;

    const auto muddyResult = computeCorrection (muddy, target, sampleRate);
    const auto thinResult = computeCorrection (thin, target, sampleRate);
    const auto brightResult = computeCorrection (bright, target, sampleRate);

    check (muddyResult.gainsDb != thinResult.gainsDb,
           "muddy and thin inputs do not receive the same correction");

    // The muddy input has too much low-mid energy, so its 350 Hz gain should be
    // more negative than the thin input's.
    const int lowMidBand = 1; // 350 Hz correction band
    check (muddyResult.gainsDb[static_cast<size_t> (lowMidBand)]
               < thinResult.gainsDb[static_cast<size_t> (lowMidBand)] - 0.5,
           "the muddy input is cut more at 350 Hz than the thin input");

    // A bright/fizzy input must be pulled down at the 7 kHz correction band.
    check (brightResult.gainsDb[4] < -0.5,
           "the bright input receives a high-frequency cut");
}

void testTargetDistinctness()
{
    using namespace metaleq::tone;

    const auto input = makeInput ({ -2.5, -1.0, -0.5, 0.8, 1.0, 0.2, -1.2, -2.5, -6.5 }, 0.9);

    std::vector<std::array<double, kNumCorrectionBands>> corrections;
    for (int t = 0; t < getNumToneTargets(); ++t)
        corrections.push_back (computeCorrection (input, getToneTarget (t), sampleRate).gainsDb);

    int distinct = 0;
    for (size_t i = 0; i < corrections.size(); ++i)
    {
        bool unique = true;
        for (size_t j = 0; j < i; ++j)
        {
            double distance = 0.0;
            for (int b = 0; b < kNumCorrectionBands; ++b)
                distance = std::max (distance,
                                     std::abs (corrections[i][static_cast<size_t> (b)]
                                               - corrections[j][static_cast<size_t> (b)]));
            if (distance < 0.25)
            {
                unique = false;
                break;
            }
        }
        if (unique)
            ++distinct;
    }

    check (distinct >= 12, "at least twelve targets produce a distinct recommendation for one input");

    auto distanceBetween = [] (int a, int b)
    {
        const auto left = computeCorrection (
            makeInput ({ -2.5, -1.0, -0.5, 0.8, 1.0, 0.2, -1.2, -2.5, -6.5 }, 0.9),
            getToneTarget (a), sampleRate).gainsDb;
        const auto right = computeCorrection (
            makeInput ({ -2.5, -1.0, -0.5, 0.8, 1.0, 0.2, -1.2, -2.5, -6.5 }, 0.9),
            getToneTarget (b), sampleRate).gainsDb;
        double distance = 0.0;
        for (int band = 0; band < kNumCorrectionBands; ++band)
            distance = std::max (distance,
                                 std::abs (left[static_cast<size_t> (band)]
                                           - right[static_cast<size_t> (band)]));
        return distance;
    };

    const int ajfa = findToneTargetById ("genre-thrash-ajfa");
    const int blackAlbum = findToneTargetById ("genre-thrash-black-album");
    const int lead = findToneTargetById ("genre-metalcore-lead");
    const int rhythm = findToneTargetById ("genre-metalcore-2000s");
    const int modern = findToneTargetById ("genre-metalcore-modern");
    const int bobRock = findToneTargetById ("producer-bob-rock");

    check (ajfa >= 0 && blackAlbum >= 0 && lead >= 0 && rhythm >= 0 && modern >= 0 && bobRock >= 0,
           "the key comparison targets exist");

    check (distanceBetween (ajfa, blackAlbum) >= 0.5,
           "AJFA and the Black Album recommend different corrections on the same input");
    check (distanceBetween (lead, rhythm) >= 0.5,
           "lead and rhythm metalcore recommend different corrections on the same input");
    check (distanceBetween (modern, rhythm) >= 0.3,
           "modern and 2000s metalcore recommend different corrections on the same input");
    check (distanceBetween (blackAlbum, bobRock) >= 0.3,
           "the Black Album and Bob Rock profiles are distinguishable on the same input");

    // Target identity must not collapse to the same destination either.
    std::set<std::string> shapes;
    for (int t = 0; t < getNumToneTargets(); ++t)
    {
        const auto shape = targetShape (getToneTarget (t));
        std::string key;
        for (const double v : shape)
            key += std::to_string (static_cast<int> (std::lround (v * 2.0))) + ",";
        shapes.insert (key);
    }
    check (shapes.size() == 16, "all sixteen target shapes are distinct");
}

/** Cross-language fixture: the offline SpectrumProfiler and the native
    analyser must agree on the same deterministic buffer. The signal is
    defined identically in Tools/SpectrumProfiler/profile_stems.py
    (synthetic_signal) and generated here with the same operations.
*/
void testAnalyzerProfilerEquivalence()
{
    using namespace metaleq::tone;
    namespace fixture = metaleq::test::fixture;

    const int numSamples = static_cast<int> (fixture::kSampleRate * fixture::kSeconds);
    std::vector<float> samples (static_cast<size_t> (numSamples));

    constexpr double twoPi = 6.283185307179586476925286766559;
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double> (i) / fixture::kSampleRate;
        const auto noise = ((1103515245u * static_cast<std::uint32_t> (i) + 12345u) >> 16) & 0x7fffu;
        const double value = 0.25 * std::sin (twoPi * 1000.0 * t)
                           + 0.10 * std::sin (twoPi * 250.0 * t)
                           + 0.06 * std::sin (twoPi * 4000.0 * t)
                           + 0.02 * std::sin (twoPi * 9500.0 * t)
                           + 0.01 * (static_cast<double> (noise) / 32768.0 - 1.0);
        samples[static_cast<size_t> (i)] = static_cast<float> (value);
    }

    const auto profile = analyseInput (samples.data(), numSamples, fixture::kSampleRate);
    check (profile.valid, "profiler fixture: analysis is valid");
    check (profile.frames == 92, "profiler fixture: expected 92 non-silent frames");
    check (fixture::kExpectedMeanRelDb.size() == static_cast<size_t> (kNumFeatures),
           "profiler fixture: expected values match the current feature count");
    check (fixture::kLabels.size() == static_cast<size_t> (kNumFeatures),
           "profiler fixture: labels match the current feature count");

    for (int i = 0; i < kNumFeatures; ++i)
    {
        check (std::string (fixture::kLabels[static_cast<size_t> (i)])
                   == kFeatures[static_cast<size_t> (i)].shortLabel,
               "profiler fixture: band order matches kFeatures");
        checkNear (profile.relativeDb[static_cast<size_t> (i)],
                   fixture::kExpectedMeanRelDb[static_cast<size_t> (i)],
                   fixture::kToleranceDb,
                   "profiler and native analyser agree on every feature band");
    }
}

void testEvidenceTraceability()
{
    using namespace metaleq::tone;

    for (int t = 0; t < getNumToneTargets(); ++t)
    {
        const auto& target = getToneTarget (t);
        check (target.confidence > 0.0, "target has a positive confidence");
        check (std::string (target.limitations).size() > 40,
               "target states a substantive limitation");

        bool hasDocumented = false;
        bool hasInterpretation = false;
        for (int s = 0; s < target.numSources; ++s)
        {
            const auto& ref = target.sources[static_cast<size_t> (s)];
            if (ref.evidence == Evidence::documented)
                hasDocumented = true;
            if (ref.evidence == Evidence::interpretation)
                hasInterpretation = true;
        }
        check (hasDocumented || target.category == Category::genre,
               "each non-genre target cites at least one documented source");

        if (target.confidence < 0.5)
            check (hasInterpretation || target.numSources >= 3,
                   "low-confidence targets declare interpretation or enough sources");
    }

    // The model must not pretend that a secondary production analysis is a
    // documented quote from the artist.
    const auto& ajfa = getToneTarget (findToneTargetById ("genre-thrash-ajfa"));
    bool foundThompson = false;
    for (int s = 0; s < ajfa.numSources; ++s)
        if (std::string (getSource (ajfa.sources[static_cast<size_t> (s)].sourceIndex).title)
                .find ("Steve Thompson") != std::string::npos)
            foundThompson = true;
    check (foundThompson, "the AJFA research record includes the mixer's account");
}

} // namespace

int main()
{
    testTargetTable();
    testInputAnalysis();
    testCaptureDiagnostics();
    testCorrectionSafety();
    testAlreadySuitableInput();
    testDeadbandAndConfidenceTuning();
    testInputAdaptation();
    testTargetDistinctness();
    testEvidenceTraceability();
    testAnalyzerProfilerEquivalence();

    if (failures != 0)
    {
        std::cerr << failures << " analyzer model test(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "MetalEQ analyzer target-model, input-analysis and correction tests passed.\n";
    return EXIT_SUCCESS;
}

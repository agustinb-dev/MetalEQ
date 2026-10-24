/*
    MetalEQ - evidence-traced tonal targets and adaptive correction.

    This header is the heart of the V3.2 analyzer overhaul. It separates three
    concepts that the old analyzer conflated:

      1. INPUT ANALYSIS
         The captured guitar is reduced to a small, stable feature vector: the
         energy in eight broad log-spaced bands, expressed in dB relative to
         the 500-2000 Hz region of the same capture. Broad bands are used on
         purpose: distorted guitars produce a moving harmonic comb, and a
         narrow-band comparison would mistake note choice for tone.

      2. TARGET MODEL
         Every profile is a structured set of tonal objectives, not an EQ
         preset. Each feature carries an estimated target level, a tolerance
         (the band within which no correction is justified), an importance
         weight, a per-feature correction limit and an evidence tag. A target
         also carries its reference recordings/period, a plain-language
         objective, its limitations, a confidence value and source records.

      3. CORRECTION
         The correction is the difference between the selected target and the
         input actually analysed, converted to the five broad musical bands
         that MetalEQ can apply. A deadband prevents unnecessary processing,
         low-confidence features are weighted down, and gains are limited.

    IMPORTANT HONESTY RULE
    ---------------------
    The numeric values are engineering estimates translated from documented
    production descriptions using the rubric in Docs/AnalyzerResearch.md. They
    are NOT measured album spectra and must never be presented as the exact EQ
    used on a record. No reference audio was analysed for this build: the
    research record states that limitation explicitly.

    Numbers are deliberately rounded to 0.5 dB. The target models are
    reviewable data, not opaque DSP constants: see MetalEqToneTargets.cpp and
    the research document for the evidence behind every profile.
*/

#pragma once

#include <array>
#include <cstddef>

#include "MetalEqDsp.h"

namespace metaleq::tone
{

//==============================================================================
inline constexpr int kNumFeatures = 8;
inline constexpr int kNumCorrectionBands = kNumBands;

/** The five broad musical bands the analyzer may drive. These match
    MetalEQAudioProcessor::applyAnalyzerSuggestion().
*/
inline constexpr std::array<double, kNumCorrectionBands> kCorrectionFrequencies
    { 120.0, 350.0, 1000.0, 2800.0, 7000.0 };
inline constexpr double kCorrectionQ = 0.8;

/** Absolute correction limits for the fitted five-band EQ. The musical band
    parameters accept +/-12 dB, so the analyzer may now use up to that safety
    ceiling. Per-feature caps in the target models remain tighter.
*/
inline constexpr double kGlobalMaxGainDb = 12.0;
/** Broad-band gain caps used by applyAnalyzerSuggestion's validation. */
inline constexpr double kMinBandGainDb = -12.0;
inline constexpr double kMaxBandGainDb = 12.0;

//==============================================================================
enum class Category : int
{
    genre = 0,
    artist = 1,
    producer = 2
};

const char* categoryLabel (Category category) noexcept;

/** How a claim is supported. Kept coarse on purpose: the model data never
    pretends that an engineering interpretation is a documented fact.
*/
enum class Evidence : int
{
    documented = 0,   // a direct interview, credit list or documented practice
    secondary = 1,    // a trade/education analysis or review, not a primary source
    interpretation = 2, // our translation from evidence to a numeric objective
    unknown = 3       // explicitly not established
};

const char* evidenceLabel (Evidence evidence) noexcept;

//==============================================================================
struct FeatureSpec
{
    double centreHz = 1000.0;
    double lowHz    = 900.0;
    double highHz   = 1100.0;
    const char* label = "";
    const char* shortLabel = "";
    const char* description = "";
};

/** Eight broad, log-spaced analysis bands. The edges are wider than typical
    harmonic spacing so a played note cannot masquerade as a tonal problem.
*/
inline constexpr std::array<FeatureSpec, kNumFeatures> kFeatures {{
    {   90.0,    70.0,   115.0, "Sub weight",     "90",   "Lowest guitar body and rumble below the palm-mute thump" },
    {  175.0,   140.0,   220.0, "Bass body",      "175",  "Palm-mute weight and low-string body" },
    {  350.0,   280.0,   440.0, "Low-mid body",   "350",  "Boxiness, mud and low-mid build-up" },
    {  700.0,   560.0,   880.0, "Midrange",       "700",  "Fundamental midrange weight and body" },
    { 1400.0,  1120.0,  1760.0, "Upper-mid bite", "1.4k", "Aggression, articulation and upper-mid cut" },
    { 2800.0,  2240.0,  3520.0, "Presence",       "2.8k", "Pick attack and presence" },
    { 5600.0,  4480.0,  7040.0, "Brightness",     "5.6k", "Harshness and upper brightness" },
    { 9500.0,  7600.0, 12000.0, "Fizz / air",     "9.5k", "Fizz, sizzle and top-end air" }
}};

//==============================================================================
struct Source
{
    const char* title = "";
    const char* url   = "";
    const char* date  = "";
};

struct SourceRef
{
    int sourceIndex = -1;
    const char* claim = "";
    Evidence evidence = Evidence::interpretation;
};

struct FeatureTarget
{
    /** Estimated target level in dB relative to the 500-2000 Hz region of the
        same capture. Positive means "more of this range than the mid
        reference"; negative means "less".
    */
    double relativeDb = 0.0;
    double toleranceDb = 1.0;
    double weight = 1.0;
    double maxCorrectionDb = 3.0;
    Evidence evidence = Evidence::interpretation;
    const char* rationale = "";
};

struct ToneTarget
{
    /** Stable id. Never reuse or rename an existing id. */
    const char* id = "";
    const char* label = "";
    /** Compact label for the target selector. */
    const char* shortLabel = "";
    Category category = Category::genre;
    /** Selected album / period. */
    const char* albumPeriod = "";
    /** What the target is trying to achieve, in plain language. */
    const char* objective = "";
    /** What the evidence cannot support, stated honestly. */
    const char* limitations = "";
    /** Overall confidence in the destination, 0..1. */
    double confidence = 0.5;
    std::array<FeatureTarget, kNumFeatures> features {};
    std::array<SourceRef, 5> sources {};
    int numSources = 0;
};

int getNumToneTargets() noexcept;
const ToneTarget& getToneTarget (int index) noexcept;
int findToneTargetById (const char* id) noexcept;

int getNumSources() noexcept;
const Source& getSource (int index) noexcept;

const char* evidenceTag (Evidence evidence) noexcept;

//==============================================================================
/** Result of measuring a captured guitar tone. */
struct InputProfile
{
    std::array<double, kNumFeatures> relativeDb {};
    /** 0..1 measurement confidence (capture length and level). */
    double confidence = 0.0;
    /** Un-normalised level of the 500-2000 Hz reference region, dB. */
    double referenceDb = -120.0;
    int frames = 0;
    bool valid = false;
};

/** Result of comparing an input profile with a target model. */
struct CorrectionResult
{
    std::array<double, kNumCorrectionBands> gainsDb {};
    /** Target minus input, after the deadband (dB). */
    std::array<double, kNumFeatures> desiredDb {};
    /** Input plus the fitted EQ, evaluated in the feature bands (dB). */
    std::array<double, kNumFeatures> predictedDb {};
    std::array<double, kNumFeatures> targetDb {};
    double confidence = 0.0;
    double maxAbsGainDb = 0.0;
    double residualDb = 0.0;
    bool alreadyClose = false;
};

/** Analyse captured mono audio and return the eight-band input profile.
    Safe to call on the analyzer worker thread; allocates only there.
*/
InputProfile analyseInput (const float* samples, int numSamples, double sampleRate);

/** The target's eight feature levels. */
std::array<double, kNumFeatures> targetShape (const ToneTarget& target) noexcept;

/** Compare the measured input with the target and fit the five broad bands.
    Pure function: no allocation, no IO, safe for tests.
*/
CorrectionResult computeCorrection (const InputProfile& input,
                                    const ToneTarget& target,
                                    double sampleRate);

/** Evaluate the eight feature bands after applying `gainsDb` at the five
    correction frequencies.
*/
std::array<double, kNumFeatures> predictShape (
    const std::array<double, kNumFeatures>& inputRelativeDb,
    const std::array<double, kNumCorrectionBands>& gainsDb,
    double sampleRate);

/** Magnitude response in dB of one RBJ peaking section at `freq`, using a
    48 kHz reference rate. Mainly a test/sanity helper; the solver uses the
    rate-aware overload below.
*/
double peakingResponseDb (double freq, double centreHz, double q, double gainDb) noexcept;

/** Magnitude response in dB of one RBJ peaking section at the actual rate. */
double peakingResponseAtRate (double freq, double centreHz, double q,
                              double gainDb, double sampleRate) noexcept;

/** Clamp the fitted gains and report whether the correction is meaningful. */
bool correctionIsMeaningful (const std::array<double, kNumCorrectionBands>& gainsDb) noexcept;

} // namespace metaleq::tone

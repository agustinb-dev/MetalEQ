#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    using namespace metaleq;

    bool isSupportedUserPresetSchema (const juce::var& schema)
    {
        const auto text = schema.toString();
        return text == "1" || text == "2" || text == "3";
    }

    bool parsePresetNumber (const juce::String& text, double& result)
    {
        const auto* cursor = text.toRawUTF8();
        const auto* start = cursor;
        if (*cursor == '+' || *cursor == '-')
            ++cursor;

        bool hasDigits = false;
        while (*cursor >= '0' && *cursor <= '9')
        {
            hasDigits = true;
            ++cursor;
        }
        if (*cursor == '.')
        {
            ++cursor;
            while (*cursor >= '0' && *cursor <= '9')
            {
                hasDigits = true;
                ++cursor;
            }
        }
        if (! hasDigits)
            return false;

        if (*cursor == 'e' || *cursor == 'E')
        {
            ++cursor;
            if (*cursor == '+' || *cursor == '-')
                ++cursor;
            const auto* exponentStart = cursor;
            while (*cursor >= '0' && *cursor <= '9')
                ++cursor;
            if (cursor == exponentStart)
                return false;
        }
        if (*cursor != '\0')
            return false;

        char* end = nullptr;
        result = std::strtod (start, &end);
        return end == cursor && std::isfinite (result);
    }

    //==========================================================================
    // Parameter identities. Never change these strings: they are the plugin's
    // automation map and part of every saved session.
    //==========================================================================
    constexpr const char* kBypassId = "bypass";

}

//==============================================================================
const char* MetalEQAudioProcessor::ParameterIDs::hpFreq   = "hpFreq";
const char* MetalEQAudioProcessor::ParameterIDs::hpSlope  = "hpSlope";
const char* MetalEQAudioProcessor::ParameterIDs::lpFreq   = "lpFreq";
const char* MetalEQAudioProcessor::ParameterIDs::lpSlope  = "lpSlope";
const char* MetalEQAudioProcessor::ParameterIDs::lpOn     = "lpOn";
const char* MetalEQAudioProcessor::ParameterIDs::lsFreq   = "lsFreq";
const char* MetalEQAudioProcessor::ParameterIDs::lsGain   = "lsGain";
const char* MetalEQAudioProcessor::ParameterIDs::hsFreq   = "hsFreq";
const char* MetalEQAudioProcessor::ParameterIDs::hsGain   = "hsGain";
const char* MetalEQAudioProcessor::ParameterIDs::bandFreq[metaleq::kNumBands] =
    { "band1Freq", "band2Freq", "band3Freq", "band4Freq", "band5Freq" };
const char* MetalEQAudioProcessor::ParameterIDs::bandGain[metaleq::kNumBands] =
    { "band1Gain", "band2Gain", "band3Gain", "band4Gain", "band5Gain" };
const char* MetalEQAudioProcessor::ParameterIDs::bandQ[metaleq::kNumBands] =
    { "band1Q", "band2Q", "band3Q", "band4Q", "band5Q" };
const char* MetalEQAudioProcessor::ParameterIDs::bandOn[metaleq::kNumBands] =
    { "band1On", "band2On", "band3On", "band4On", "band5On" };
const char* MetalEQAudioProcessor::ParameterIDs::resFreq[metaleq::kNumSurgicalBands] =
    { "res1Freq", "res2Freq", "res3Freq", "res4Freq", "res5Freq", "res6Freq" };
const char* MetalEQAudioProcessor::ParameterIDs::resGain[metaleq::kNumSurgicalBands] =
    { "res1Gain", "res2Gain", "res3Gain", "res4Gain", "res5Gain", "res6Gain" };
const char* MetalEQAudioProcessor::ParameterIDs::resQ[metaleq::kNumSurgicalBands] =
    { "res1Q", "res2Q", "res3Q", "res4Q", "res5Q", "res6Q" };
const char* MetalEQAudioProcessor::ParameterIDs::resOn[metaleq::kNumSurgicalBands] =
    { "res1On", "res2On", "res3On", "res4On", "res5On", "res6On" };
const char* MetalEQAudioProcessor::ParameterIDs::dynFreq[metaleq::kNumDynamicBands] =
    { "dyn1Freq", "dyn2Freq", "dyn3Freq", "dyn4Freq" };
const char* MetalEQAudioProcessor::ParameterIDs::dynQ[metaleq::kNumDynamicBands] =
    { "dyn1Q", "dyn2Q", "dyn3Q", "dyn4Q" };
const char* MetalEQAudioProcessor::ParameterIDs::dynThreshold[metaleq::kNumDynamicBands] =
    { "dyn1Threshold", "dyn2Threshold", "dyn3Threshold", "dyn4Threshold" };
const char* MetalEQAudioProcessor::ParameterIDs::dynRatio[metaleq::kNumDynamicBands] =
    { "dyn1Ratio", "dyn2Ratio", "dyn3Ratio", "dyn4Ratio" };
const char* MetalEQAudioProcessor::ParameterIDs::dynAttack[metaleq::kNumDynamicBands] =
    { "dyn1Attack", "dyn2Attack", "dyn3Attack", "dyn4Attack" };
const char* MetalEQAudioProcessor::ParameterIDs::dynRelease[metaleq::kNumDynamicBands] =
    { "dyn1Release", "dyn2Release", "dyn3Release", "dyn4Release" };
const char* MetalEQAudioProcessor::ParameterIDs::dynRange[metaleq::kNumDynamicBands] =
    { "dyn1Range", "dyn2Range", "dyn3Range", "dyn4Range" };
const char* MetalEQAudioProcessor::ParameterIDs::dynOn[metaleq::kNumDynamicBands] =
    { "dyn1On", "dyn2On", "dyn3On", "dyn4On" };
const char* MetalEQAudioProcessor::ParameterIDs::shapeOn[6] =
    { "shapeTightenOn", "shapeScoopOn", "shapeMidPunchOn", "shapePickAttackOn", "shapeFizzTamerOn", "shapeBrightenOn" };
const char* MetalEQAudioProcessor::ParameterIDs::trim     = "trim";
const char* MetalEQAudioProcessor::ParameterIDs::tight    = "tight";
const char* MetalEQAudioProcessor::ParameterIDs::body     = "body";
const char* MetalEQAudioProcessor::ParameterIDs::scoop    = "scoop";
const char* MetalEQAudioProcessor::ParameterIDs::bite     = "bite";
const char* MetalEQAudioProcessor::ParameterIDs::fizzCut  = "fizzCut";
const char* MetalEQAudioProcessor::ParameterIDs::preset   = "preset";
const char* MetalEQAudioProcessor::ParameterIDs::abCompare = "abCompare";
const char* MetalEQAudioProcessor::ParameterIDs::bypass   = kBypassId;
const char* MetalEQAudioProcessor::ParameterIDs::resAudition = "resAudition";

juce::StringArray MetalEQAudioProcessor::ParameterIDs::allEqParameterIds()
{
    juce::StringArray ids { hpFreq, hpSlope, lpFreq, lpSlope, lpOn, lsFreq, lsGain, hsFreq, hsGain };

    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        ids.add (bandFreq[i]);
        ids.add (bandGain[i]);
        ids.add (bandQ[i]);
        ids.add (bandOn[i]);
    }

    for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
    {
        ids.add (resFreq[i]);
        ids.add (resGain[i]);
        ids.add (resQ[i]);
        ids.add (resOn[i]);
    }

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
    {
        ids.add (dynFreq[i]);
        ids.add (dynQ[i]);
        ids.add (dynThreshold[i]);
        ids.add (dynRatio[i]);
        ids.add (dynAttack[i]);
        ids.add (dynRelease[i]);
        ids.add (dynRange[i]);
        ids.add (dynOn[i]);
    }

    ids.add (trim);
    ids.add (tight);
    ids.add (body);
    ids.add (scoop);
    ids.add (bite);
    ids.add (fizzCut);
    for (const auto* id : shapeOn)
        ids.add (id);

    return ids;
}

//==============================================================================
MetalEQAudioProcessor::MetalEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    apvts = std::make_unique<juce::AudioProcessorValueTreeState> (*this, nullptr, "PARAMS", createParameterLayout());
    apvts->addParameterListener (ParameterIDs::preset, this);
    apvts->addParameterListener (ParameterIDs::abCompare, this);
    apvts->addParameterListener (ParameterIDs::lpOn, this);

    rememberCurrentAsLoadedPreset ("Custom", false);
    captureActiveAbSlot();
    abSlots[1] = abSlots[0];

    analyzerCapture.reset (new float[static_cast<size_t> (maxCaptureSamples)]);
    for (auto& gain : analyzerSuggestion)
        gain.store (0.0f, std::memory_order_relaxed);

    for (auto& value : resonanceFreq)         value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceQ)            value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceProminence)   value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceResidual)     value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceConfidence)   value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceCut)          value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicEligible)  value.store (false, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicQ)         value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicThreshold) value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicRatio)     value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicAttack)    value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicRelease)   value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicMaxCut)    value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : resonanceDynamicExcess)    value.store (0.0f, std::memory_order_relaxed);

    for (auto& level : peakLevels)
        level.store (0.0f, std::memory_order_relaxed);
    for (auto& level : inputPeakLevels)
        level.store (0.0f, std::memory_order_relaxed);
    for (auto& reduction : dynamicReductionDb)
        reduction.store (0.0f, std::memory_order_relaxed);

    engine.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    auditionEngine.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    dynamicEngine.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    auditionDynamicEngine.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    analyzerWorker = std::thread ([this] { analyzerWorkerLoop(); });
}

MetalEQAudioProcessor::~MetalEQAudioProcessor()
{
    cancelPendingUpdate();

    if (apvts != nullptr)
    {
        apvts->removeParameterListener (ParameterIDs::preset, this);
        apvts->removeParameterListener (ParameterIDs::abCompare, this);
        apvts->removeParameterListener (ParameterIDs::lpOn, this);
    }

    stopAnalyzerWorker.store (true, std::memory_order_release);
    if (analyzerWorker.joinable())
        analyzerWorker.join();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MetalEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    namespace p = metaleq::params;

    //--- crossovers ----------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::hpFreq, "High-Pass Freq",
        juce::NormalisableRange<float> ((float) p::hpFreqMin, (float) p::hpFreqMax, 0.01f, 0.35f),
        (float) p::hpFreqDefault));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ParameterIDs::hpSlope, "High-Pass Slope",
        juce::StringArray { "Off", "12 dB/oct", "24 dB/oct", "36 dB/oct" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::lpFreq, "Low-Pass Freq",
        juce::NormalisableRange<float> ((float) p::lpFreqMin, (float) p::lpFreqMax, 0.01f, 0.35f),
        (float) p::lpFreqDefault));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ParameterIDs::lpSlope, "Low-Pass Slope",
        juce::StringArray { "Off", "12 dB/oct", "24 dB/oct", "36 dB/oct" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::lpOn, "Low-Pass Enabled", false));

    //--- shelves -------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::lsFreq, "Low Shelf Freq",
        juce::NormalisableRange<float> ((float) p::lsFreqMin, (float) p::lsFreqMax, 0.01f, 0.4f),
        (float) p::lsFreqDefault));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::lsGain, "Low Shelf Gain",
        juce::NormalisableRange<float> ((float) p::shelfGainMin, (float) p::shelfGainMax, 0.01f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::hsFreq, "High Shelf Freq",
        juce::NormalisableRange<float> ((float) p::hsFreqMin, (float) p::hsFreqMax, 0.01f, 0.4f),
        (float) p::hsFreqDefault));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::hsGain, "High Shelf Gain",
        juce::NormalisableRange<float> ((float) p::shelfGainMin, (float) p::shelfGainMax, 0.01f),
        0.0f));

    //--- five parametric bands ----------------------------------------------
    const double defaultFreqs[metaleq::kNumBands] = { 120.0, 400.0, 1200.0, 3500.0, 8000.0 };

    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::bandFreq[i], "Band " + juce::String (i + 1) + " Freq",
            juce::NormalisableRange<float> ((float) p::bandFreqMin, (float) p::bandFreqMax, 0.01f, 0.4f),
            (float) defaultFreqs[i]));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::bandGain[i], "Band " + juce::String (i + 1) + " Gain",
            juce::NormalisableRange<float> ((float) p::bandGainMin, (float) p::bandGainMax, 0.01f),
            0.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::bandQ[i], "Band " + juce::String (i + 1) + " Q",
            juce::NormalisableRange<float> ((float) p::bandQMin, (float) p::bandQMax, 0.01f),
            1.0f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            ParameterIDs::bandOn[i], "Band " + juce::String (i + 1) + " On", true));
    }

    //--- V3 surgical resonance bands ----------------------------------------
    const double defaultResFreqs[metaleq::kNumSurgicalBands] =
        { 250.0, 620.0, 1200.0, 2400.0, 4000.0, 6200.0 };

    for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::resFreq[i], "Resonance " + juce::String (i + 1) + " Freq",
            juce::NormalisableRange<float> ((float) p::surgicalFreqMin, (float) p::surgicalFreqMax, 0.01f, 0.4f),
            (float) defaultResFreqs[i]));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::resGain[i], "Resonance " + juce::String (i + 1) + " Gain",
            juce::NormalisableRange<float> ((float) p::surgicalGainMin, (float) p::surgicalGainMax, 0.01f),
            0.0f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::resQ[i], "Resonance " + juce::String (i + 1) + " Q",
            juce::NormalisableRange<float> ((float) p::surgicalQMin, (float) p::surgicalQMax, 0.01f),
            7.0f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            ParameterIDs::resOn[i], "Resonance " + juce::String (i + 1) + " On", false));
    }

    //--- V3.1 dynamic resonance bands ---------------------------------------
    const auto dynamicDefaults = metaleq::defaultDynamicBands();

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
    {
        const auto n = juce::String (i + 1);
        const auto& defaults = dynamicDefaults[static_cast<size_t> (i)];

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynFreq[i], "Dynamic " + n + " Freq",
            juce::NormalisableRange<float> ((float) p::dynamicFreqMin, (float) p::dynamicFreqMax, 0.01f, 0.4f),
            (float) defaults.freq));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynQ[i], "Dynamic " + n + " Q",
            juce::NormalisableRange<float> ((float) p::dynamicQMin, (float) p::dynamicQMax, 0.01f),
            (float) defaults.q));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynThreshold[i], "Dynamic " + n + " Threshold",
            juce::NormalisableRange<float> ((float) p::dynamicThresholdMin, (float) p::dynamicThresholdMax, 0.1f),
            (float) defaults.thresholdDb));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynRatio[i], "Dynamic " + n + " Ratio",
            juce::NormalisableRange<float> ((float) p::dynamicRatioMin, (float) p::dynamicRatioMax, 0.1f),
            (float) defaults.ratio));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynAttack[i], "Dynamic " + n + " Attack",
            juce::NormalisableRange<float> ((float) p::dynamicAttackMin, (float) p::dynamicAttackMax, 0.1f, 0.6f),
            (float) defaults.attackMs));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynRelease[i], "Dynamic " + n + " Release",
            juce::NormalisableRange<float> ((float) p::dynamicReleaseMin, (float) p::dynamicReleaseMax, 1.0f, 0.5f),
            (float) defaults.releaseMs));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ParameterIDs::dynRange[i], "Dynamic " + n + " Max Cut",
            juce::NormalisableRange<float> ((float) p::dynamicCutMin, (float) p::dynamicCutMax, 0.1f),
            (float) defaults.maxCutDb));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            ParameterIDs::dynOn[i], "Dynamic " + n + " On", defaults.on));
    }

    //--- output trim ---------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParameterIDs::trim, "Output Trim",
        juce::NormalisableRange<float> ((float) p::trimMin, (float) p::trimMax, 0.01f),
        0.0f));

    //--- macros --------------------------------------------------------------
    struct MacroSpec { const char* id; const char* name; float defaultValue; };
    const MacroSpec macros[] =
    {
        { ParameterIDs::tight,   "TIGHT",    0.0f },
        { ParameterIDs::body,    "BODY",     0.5f },
        { ParameterIDs::scoop,   "SCOOP",    0.0f },
        { ParameterIDs::bite,    "BITE",     0.0f },
        { ParameterIDs::fizzCut, "FIZZ CUT", 0.0f }
    };

    for (const auto& m : macros)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            m.id, m.name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.005f), m.defaultValue));

    for (int i = 0; i < metaleq::getNumShapes(); ++i)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            ParameterIDs::shapeOn[i], metaleq::getShapeDefinition (i).name, false));

    //--- preset selector -----------------------------------------------------
    {
        // Canonical mapping: index 0 is Custom, factory preset i is index i+1.
        juce::StringArray presetNames { "Custom" };

        for (int i = 0; i < metaleq::getNumPresets(); ++i)
            presetNames.add (metaleq::getPreset (i).name);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            ParameterIDs::preset, "Preset", presetNames, 0));
    }

    //--- A/B compare ---------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::abCompare, "A/B Compare", false));

    //--- bypass --------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::bypass, "Bypass", false));

    //--- resonance delta monitor (not saved with the tone) ------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::resAudition,
                                                           "Resonance Delta Monitor", false));

    return layout;
}

//==============================================================================
void MetalEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    analyzerSampleRate.store (static_cast<int> (sampleRate > 0.0 ? sampleRate : 48000.0), std::memory_order_relaxed);
    analyzerCaptureLimit.store (juce::jmin (maxCaptureSamples,
        static_cast<int> ((sampleRate > 0.0 ? sampleRate : 48000.0) * analyzerDurationSeconds)),
        std::memory_order_relaxed);

    // Snap straight to the current settings: there is no need to glide from a
    // default state at the start of a playback run.
    metaleq::ChainSpec chain;
    metaleq::buildChain (chain, readEqSettings(), readMacroSettings(), sampleRate);
    engine.setChain (chain, 1);
    cachedEq = readEqSettings();
    cachedMacros = readMacroSettings();
    cachedSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    chainCacheValid = true;

    dynamicEngine.prepare (cachedSampleRate);
    dynamicEngine.setSettings (cachedEq.dynamic, 1);

    // The delta monitor runs a second, resonance-only path. Prepare it with
    // the same sample rate and give both scratch buffers enough room for the
    // host's block size; no allocation happens in processBlock().
    auditionEngine.prepare (sampleRate > 0.0 ? sampleRate : 48000.0);
    metaleq::ChainSpec auditionChain;
    metaleq::buildSurgicalChain (auditionChain, cachedEq, cachedSampleRate);
    auditionEngine.setChain (auditionChain, 1);
    auditionDynamicEngine.prepare (cachedSampleRate);
    auditionDynamicEngine.setSettings (cachedEq.dynamic, 1);
    cachedAuditionEq = cachedEq;
    cachedAuditionSampleRate = cachedSampleRate;
    auditionChainCacheValid = true;
    wasAuditioning = false;

    const int scratchSamples = juce::jmax (1024, samplesPerBlock);
    auditionDryBuffer.setSize (metaleq::kMaxChannels, scratchSamples, false, true, false);
    auditionWetBuffer.setSize (metaleq::kMaxChannels, scratchSamples, false, true, false);

    for (auto& level : peakLevels)
        level.store (0.0f, std::memory_order_relaxed);
    for (auto& level : inputPeakLevels)
        level.store (0.0f, std::memory_order_relaxed);
    for (auto& reduction : dynamicReductionDb)
        reduction.store (0.0f, std::memory_order_relaxed);
}

void MetalEQAudioProcessor::releaseResources()
{
    engine.reset();
    auditionEngine.reset();
    dynamicEngine.reset();
    auditionDynamicEngine.reset();
    chainCacheValid = false;
    auditionChainCacheValid = false;
    wasAuditioning = false;

    for (auto& reduction : dynamicReductionDb)
        reduction.store (0.0f, std::memory_order_relaxed);
}

void MetalEQAudioProcessor::reset()
{
    engine.reset();
    auditionEngine.reset();
    dynamicEngine.reset();
    auditionDynamicEngine.reset();
    chainCacheValid = false;
    auditionChainCacheValid = false;
    wasAuditioning = false;

    for (auto& reduction : dynamicReductionDb)
        reduction.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
bool MetalEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainOut == layouts.getMainInputChannelSet();
}

void MetalEQAudioProcessor::captureAnalyzerInput (const juce::AudioBuffer<float>& buffer) noexcept
{
    // Snapshot before claiming the writer slot. A callback delayed across a
    // cancel/restart must not be allowed to join the new capture.
    const auto callbackGeneration = analyzerCaptureGeneration.load (std::memory_order_acquire);
    const auto callbackState = analyzerCaptureState.load (std::memory_order_acquire);
    if (callbackState != AnalyzerCaptureState::requested && callbackState != AnalyzerCaptureState::recording)
        return;

    bool expectedWriter = false;
    if (! analyzerCaptureWriterActive.compare_exchange_strong (expectedWriter, true,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return;

    struct WriterRelease
    {
        std::atomic<bool>& active;
        ~WriterRelease() { active.store (false, std::memory_order_release); }
    } releaseWriter { analyzerCaptureWriterActive };

    if (callbackGeneration != analyzerCaptureGeneration.load (std::memory_order_acquire))
        return;

    if (analyzerCaptureCancelled.load (std::memory_order_acquire))
    {
        auto state = analyzerCaptureState.load (std::memory_order_acquire);
        while (state == AnalyzerCaptureState::arming || state == AnalyzerCaptureState::requested
               || state == AnalyzerCaptureState::recording || state == AnalyzerCaptureState::cancelPending)
        {
            if (analyzerCaptureState.compare_exchange_weak (state, AnalyzerCaptureState::idle,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                break;
        }
        return;
    }

    const int channels = juce::jmin (buffer.getNumChannels(), metaleq::kMaxChannels);
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    auto state = analyzerCaptureState.load (std::memory_order_acquire);
    if (state == AnalyzerCaptureState::requested)
    {
        auto expected = AnalyzerCaptureState::requested;
        if (analyzerCaptureState.compare_exchange_strong (expected, AnalyzerCaptureState::recording,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            analyzerSampleCount.store (0, std::memory_order_relaxed);
            const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
            analyzerSampleRate.store (static_cast<int> (rate + 0.5), std::memory_order_relaxed);
            analyzerCaptureLimit.store (juce::jmin (maxCaptureSamples,
                static_cast<int> (rate * analyzerDurationSeconds)), std::memory_order_relaxed);
            state = AnalyzerCaptureState::recording;
        }
        else
        {
            state = expected;
        }
    }

    if (analyzerCaptureCancelled.load (std::memory_order_acquire))
    {
        auto expected = state;
        if (expected == AnalyzerCaptureState::arming || expected == AnalyzerCaptureState::requested
            || expected == AnalyzerCaptureState::recording || expected == AnalyzerCaptureState::cancelPending)
            analyzerCaptureState.compare_exchange_strong (expected, AnalyzerCaptureState::idle,
                std::memory_order_acq_rel, std::memory_order_acquire);
        return;
    }

    if (state == AnalyzerCaptureState::cancelPending)
    {
        analyzerCaptureState.store (AnalyzerCaptureState::idle, std::memory_order_release);
        return;
    }

    if (state != AnalyzerCaptureState::recording)
        return;

    int captured = analyzerSampleCount.load (std::memory_order_relaxed);
    const int limit = analyzerCaptureLimit.load (std::memory_order_relaxed);
    const auto* const* input = buffer.getArrayOfReadPointers();
    for (int i = 0; i < samples && captured < limit; ++i)
    {
        if (analyzerCaptureCancelled.load (std::memory_order_acquire))
        {
            analyzerCaptureState.store (AnalyzerCaptureState::idle, std::memory_order_release);
            return;
        }
        const float mono = channels > 1 ? 0.5f * (input[0][i] + input[1][i]) : input[0][i];
        analyzerCapture[static_cast<size_t> (captured)] = mono;
        ++captured;
    }

    analyzerSampleCount.store (captured, std::memory_order_release);
    state = analyzerCaptureState.load (std::memory_order_acquire);
    if (analyzerCaptureCancelled.load (std::memory_order_acquire) || state == AnalyzerCaptureState::cancelPending)
    {
        analyzerCaptureState.store (AnalyzerCaptureState::idle, std::memory_order_release);
        return;
    }

    if (captured >= limit)
    {
        auto expected = AnalyzerCaptureState::recording;
        if (! analyzerCaptureState.compare_exchange_strong (expected, AnalyzerCaptureState::ready,
                std::memory_order_release, std::memory_order_relaxed)
            && expected == AnalyzerCaptureState::cancelPending)
            analyzerCaptureState.store (AnalyzerCaptureState::idle, std::memory_order_release);
    }
}

void MetalEQAudioProcessor::serviceDeferredAnalyzerStart() noexcept
{
    if (! analyzerDeferredStartRequested.load (std::memory_order_acquire)
        || analyzerCaptureWriterActive.load (std::memory_order_acquire))
        return;

    // Carry a cancellation epoch across the handoff. Stop can invalidate this
    // service attempt even after the pending flag is claimed and before the
    // new capture reaches its published state.
    const auto cancelGeneration = analyzerCancelGeneration.load (std::memory_order_acquire);
    if (! analyzerDeferredStartRequested.load (std::memory_order_acquire))
        return;

    const auto state = analyzerCaptureState.load (std::memory_order_acquire);
    if (state != AnalyzerCaptureState::cancelPending && state != AnalyzerCaptureState::idle
        && state != AnalyzerCaptureState::complete && state != AnalyzerCaptureState::failed)
        return;

    bool pending = true;
    if (analyzerDeferredStartRequested.compare_exchange_strong (pending, false,
            std::memory_order_acq_rel, std::memory_order_acquire))
        startAnalyzerCaptureWithGeneration (
            analyzerDeferredStartProfile.load (std::memory_order_relaxed),
            analyzerDeferredStartMode.load (std::memory_order_relaxed),
            cancelGeneration);
}

//==============================================================================
void MetalEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // No denormals, no exceptions, no allocation: everything below is arithmetic
    // on preallocated memory.
    juce::ScopedNoDenormals noDenormals;

    midiMessages.clear();

    const auto numChannels = juce::jmin (buffer.getNumChannels(), metaleq::kMaxChannels);
    const auto numSamples  = buffer.getNumSamples();

    // Anything the host left in channels we do not process must be silenced.
    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // Capture before EQ. The same fixed-buffer capture also runs in the host's
    // bypass callback.
    captureAnalyzerInput (buffer);
    serviceDeferredAnalyzerStart();

    if (meteringEnabled.load (std::memory_order_relaxed))
        for (int ch = 0; ch < numChannels; ++ch)
            inputPeakLevels[static_cast<size_t> (ch)].store (
                buffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);

    const bool bypassed = apvts->getRawParameterValue (ParameterIDs::bypass)->load() >= 0.5f;

    if (bypassed)
    {
        // Keep the GUI meter meaningful while bypassed.
        if (meteringEnabled.load (std::memory_order_relaxed))
            for (int ch = 0; ch < numChannels; ++ch)
                peakLevels[static_cast<size_t> (ch)].store (
                    buffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);

        for (auto& reduction : dynamicReductionDb)
            reduction.store (0.0f, std::memory_order_relaxed);

        // Drop the dynamic filters and force a clean rebuild on un-bypass so a
        // stale detector state cannot apply an old cut to the first block.
        dynamicEngine.reset();
        auditionDynamicEngine.reset();
        chainCacheValid = false;
        auditionChainCacheValid = false;

        wasAuditioning = false;
        return;
    }

    //--- rebuild the chain only when the sound actually changed ---------------
    auto eq      = readEqSettings();
    auto macros  = readMacroSettings();
    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

    if (! chainCacheValid
        || eq != cachedEq
        || macros != cachedMacros
        || sampleRate != cachedSampleRate)
    {
        metaleq::ChainSpec chain;
        metaleq::buildChain (chain, eq, macros, sampleRate);

        // Two milliseconds of coefficient glide makes every change - including
        // a whole preset recall or host automation jump - free of clicks.
        const int rampLength = juce::jmax (2, juce::jmin (numSamples,
            static_cast<int> (sampleRate * 0.002)));

        engine.setChain (chain, rampLength);
        dynamicEngine.setSettings (eq.dynamic, rampLength);
        cachedEq = eq;
        cachedMacros = macros;
        cachedSampleRate = sampleRate;
        chainCacheValid = true;
    }

    //--- optional delta monitor ---------------------------------------------
    // The monitor plays the post-static signal minus the same signal after the
    // static surgical and dynamic resonance stages. That is exactly what the
    // resonance section removes, and it includes both cut types. It is a
    // monitoring aid, never saved with the tone.
    const bool hasSurgicalCut = metaleq::hasActiveSurgicalCuts (eq);
    const bool hasDynamicCut  = metaleq::hasActiveDynamicCuts (eq);

    const bool auditionRequested =
        apvts->getRawParameterValue (ParameterIDs::resAudition)->load() >= 0.5f;
    const bool audition = auditionRequested
        && (hasSurgicalCut || hasDynamicCut)
        && numSamples <= auditionDryBuffer.getNumSamples()
        && numSamples <= auditionWetBuffer.getNumSamples()
        && numChannels <= auditionDryBuffer.getNumChannels();

    if (audition && ! wasAuditioning)
    {
        auditionEngine.reset();
        auditionDynamicEngine.reset();
        auditionChainCacheValid = false;
    }

    //--- run the static EQ ---------------------------------------------------
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // The delta reference is the fully static-EQ'd signal, captured before the
    // dynamic stage so the monitor compares like with like.
    if (audition)
        for (int ch = 0; ch < numChannels; ++ch)
            auditionDryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    //--- zero-latency dynamic resonance section ------------------------------
    dynamicEngine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
        dynamicReductionDb[static_cast<size_t> (i)].store (
            static_cast<float> (dynamicEngine.getGainReductionDb (i)),
            std::memory_order_relaxed);

    if (audition)
    {
        const int rampLength = juce::jmax (2, juce::jmin (numSamples,
            static_cast<int> (sampleRate * 0.002)));

        if (! auditionChainCacheValid
            || eq.surgical != cachedAuditionEq.surgical
            || eq.dynamic != cachedAuditionEq.dynamic
            || sampleRate != cachedAuditionSampleRate)
        {
            metaleq::ChainSpec auditionChain;
            metaleq::buildSurgicalChain (auditionChain, eq, sampleRate);
            auditionEngine.setChain (auditionChain, rampLength);
            auditionDynamicEngine.setSettings (eq.dynamic, rampLength);
            cachedAuditionEq = eq;
            cachedAuditionSampleRate = sampleRate;
            auditionChainCacheValid = true;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            auditionWetBuffer.copyFrom (ch, 0, auditionDryBuffer, ch, 0, numSamples);

        auditionEngine.process (auditionWetBuffer.getArrayOfWritePointers(),
                                numChannels, numSamples);
        auditionDynamicEngine.process (auditionWetBuffer.getArrayOfWritePointers(),
                                       numChannels, numSamples);

        // 6 dB of monitor gain: the difference signal is naturally quiet.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* output = buffer.getWritePointer (ch);
            const auto* dry = auditionDryBuffer.getReadPointer (ch);
            const auto* wet = auditionWetBuffer.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                output[i] = 2.0f * (dry[i] - wet[i]);
        }
    }

    wasAuditioning = audition;

    //--- metering (audio thread: one pass, relaxed atomics only) -------------
    if (meteringEnabled.load (std::memory_order_relaxed))
        for (int ch = 0; ch < numChannels; ++ch)
            peakLevels[static_cast<size_t> (ch)].store (
                buffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);
}

void MetalEQAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    // Keep audio untouched while the analyzer capture and deferred-start state
    // machine progress exactly as they do in the normal callback.
    captureAnalyzerInput (buffer);
    serviceDeferredAnalyzerStart();
    if (meteringEnabled.load (std::memory_order_relaxed))
        for (int ch = 0; ch < juce::jmin (buffer.getNumChannels(), metaleq::kMaxChannels); ++ch)
        {
            const float peak = buffer.getMagnitude (ch, 0, buffer.getNumSamples());
            inputPeakLevels[static_cast<size_t> (ch)].store (peak, std::memory_order_relaxed);
            peakLevels[static_cast<size_t> (ch)].store (peak, std::memory_order_relaxed);
        }

    dynamicEngine.reset();
    auditionDynamicEngine.reset();
    chainCacheValid = false;
    auditionChainCacheValid = false;
    wasAuditioning = false;

    for (auto& reduction : dynamicReductionDb)
        reduction.store (0.0f, std::memory_order_relaxed);

    juce::ignoreUnused (midiMessages);
}

//==============================================================================
void MetalEQAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Programmatic recalls write parameters while this flag is set; they must
    // not be mistaken for host automation.
    if (applyingPreset.load (std::memory_order_acquire))
        return;

    bool needService = false;

    if (parameterID == ParameterIDs::preset)
    {
        pendingPresetIndex.store (juce::roundToInt (newValue), std::memory_order_release);
        needService = true;
    }
    else if (parameterID == ParameterIDs::abCompare)
    {
        pendingAbSlot.store (newValue >= 0.5f ? 1 : 0, std::memory_order_release);
        needService = true;
    }
    else if (parameterID == ParameterIDs::lpOn && newValue >= 0.5f)
    {
        pendingLowPassFix.store (true, std::memory_order_release);
        needService = true;
    }

    if (! needService)
        return;

    // AsyncUpdater coalesces rapid automation into one message-thread pass.
    // In headless tests there is no message queue, so service immediately.
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        triggerAsyncUpdate();
    else
        servicePendingAutomation();
}

void MetalEQAudioProcessor::handleAsyncUpdate()
{
    servicePendingAutomation();
}

void MetalEQAudioProcessor::serviceLowPassFix()
{
    if (! pendingLowPassFix.exchange (false, std::memory_order_acq_rel))
        return;

    if (apvts->getRawParameterValue (ParameterIDs::lpOn)->load() < 0.5f)
        return;

    if (apvts->getRawParameterValue (ParameterIDs::lpSlope)->load() >= 0.5f)
        return;

    applyingPreset.store (true, std::memory_order_relaxed);
    setChoiceParameter (ParameterIDs::lpSlope, 2);
    applyingPreset.store (false, std::memory_order_release);
}

void MetalEQAudioProcessor::servicePendingAutomation()
{
    serviceLowPassFix();

    const int preset = pendingPresetIndex.exchange (-1, std::memory_order_acq_rel);
    if (preset >= 0 && preset != getSelectedPresetIndex())
        applyPreset (preset);

    const int slot = pendingAbSlot.exchange (-1, std::memory_order_acq_rel);
    if (slot >= 0 && slot != activeAbSlot)
        switchAbSlotInternal (slot, false);
}

//==============================================================================
// Real A/B slots
//==============================================================================
void MetalEQAudioProcessor::captureActiveAbSlot()
{
    auto& slot = abSlots[static_cast<size_t> (activeAbSlot)];
    slot.params = getSoundParameterState (false);
    slot.loadedPresetState = loadedPresetState.createCopy();
    slot.presetName = lastPresetName;
    slot.presetId = lastPresetId;
    slot.userPreset = loadedPresetIsUser;
    slot.valid = true;
}

bool MetalEQAudioProcessor::loadAbSlot (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= numAbSlots)
        return false;

    auto& slot = abSlots[static_cast<size_t> (slotIndex)];
    if (! slot.valid)
        return false;

    juce::String error;
    if (! applySoundParameterState (slot.params, error, false))
        return false;

    lastPresetName = slot.presetName.isNotEmpty() ? slot.presetName : juce::String ("Custom");
    lastPresetId = slot.presetId;
    loadedPresetIsUser = slot.userPreset;
    loadedPresetState = slot.loadedPresetState.createCopy();
    syncPresetChoiceToLoadedIdentity();
    return true;
}

void MetalEQAudioProcessor::switchAbSlotInternal (int slot, bool writeParameter)
{
    if (slot < 0 || slot >= numAbSlots || slot == activeAbSlot)
        return;

    captureActiveAbSlot();

    if (! abSlots[static_cast<size_t> (slot)].valid)
        return;

    const int previous = activeAbSlot;
    activeAbSlot = slot;

    if (! loadAbSlot (slot))
    {
        activeAbSlot = previous;
        return;
    }

    if (writeParameter)
    {
        applyingPreset.store (true, std::memory_order_relaxed);
        setFloatParameter (ParameterIDs::abCompare, slot == 1 ? 1.0 : 0.0);
        applyingPreset.store (false, std::memory_order_release);
    }
}

void MetalEQAudioProcessor::setAbSlot (int slot)
{
    if (slot < 0 || slot >= numAbSlots || slot == activeAbSlot)
        return;

    switchAbSlotInternal (slot, true);
}

bool MetalEQAudioProcessor::copyActiveAbToOther()
{
    captureActiveAbSlot();
    const int other = (activeAbSlot + 1) % numAbSlots;
    abSlots[static_cast<size_t> (other)] = abSlots[static_cast<size_t> (activeAbSlot)];
    return abSlots[static_cast<size_t> (other)].valid;
}

bool MetalEQAudioProcessor::swapAbSlots()
{
    captureActiveAbSlot();

    if (! abSlots[0].valid || ! abSlots[1].valid)
        return false;

    std::swap (abSlots[0], abSlots[1]);
    return loadAbSlot (activeAbSlot);
}

//==============================================================================
juce::AudioProcessorEditor* MetalEQAudioProcessor::createEditor()
{
    return new MetalEQAudioProcessorEditor (*this);
}

const juce::String MetalEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

//==============================================================================
void MetalEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Make sure the inactive A/B slot reflects the latest edits before saving.
    captureActiveAbSlot();

    juce::ValueTree state ("MetalEQState");

    state.setProperty ("version", 4, nullptr);
    state.setProperty ("lastPresetId", lastPresetId, nullptr);
    state.setProperty ("lastPresetName", lastPresetName, nullptr);
    state.setProperty ("lastPresetIsUser", loadedPresetIsUser, nullptr);
    state.setProperty ("activeAbSlot", activeAbSlot, nullptr);

    auto parameters = getSoundParameterState (true);
    const char* uiParameterIds[] = { ParameterIDs::preset, ParameterIDs::abCompare, ParameterIDs::bypass };
    for (const auto* id : uiParameterIds)
    {
        if (const auto* parameter = apvts->getParameter (id))
        {
            juce::ValueTree node ("PARAM");
            node.setProperty ("id", id, nullptr);
            node.setProperty ("value", juce::String (
                static_cast<double> (parameter->convertFrom0to1 (parameter->getValue())), 9), nullptr);
            parameters.appendChild (node, nullptr);
        }
    }
    state.appendChild (parameters, nullptr);

    juce::ValueTree baseline ("BASELINE");
    if (loadedPresetState.isValid())
        baseline.appendChild (loadedPresetState.createCopy(), nullptr);
    state.appendChild (baseline, nullptr);

    juce::ValueTree ab ("AB");
    ab.setProperty ("active", activeAbSlot, nullptr);
    for (int i = 0; i < numAbSlots; ++i)
    {
        const auto& snapshot = abSlots[static_cast<size_t> (i)];
        juce::ValueTree slot ("SLOT");
        slot.setProperty ("index", i, nullptr);
        slot.setProperty ("presetName", snapshot.presetName, nullptr);
        slot.setProperty ("presetId", snapshot.presetId, nullptr);
        slot.setProperty ("presetIsUser", snapshot.userPreset, nullptr);

        if (snapshot.params.isValid())
            slot.appendChild (snapshot.params.createCopy(), nullptr);
        if (snapshot.loadedPresetState.isValid())
            slot.appendChild (snapshot.loadedPresetState.createCopy(), nullptr);

        ab.appendChild (slot, nullptr);
    }
    state.appendChild (ab, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void MetalEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto tree = juce::ValueTree::readFromData (data, sizeInBytes);

    if (! tree.isValid() || ! tree.hasType ("MetalEQState"))
        return;

    cancelPendingUpdate();
    pendingPresetIndex.store (-1, std::memory_order_release);
    pendingAbSlot.store (-1, std::memory_order_release);
    pendingLowPassFix.store (false, std::memory_order_release);

    const int version = static_cast<int> (tree.getProperty ("version", 1));

    // Our own writes must not be mistaken for user edits.
    applyingPreset.store (true, std::memory_order_relaxed);

    const auto child = tree.getChildWithName ("PARAMS");
    if (child.isValid())
        apvts->replaceState (child.createCopy());

    // V3 added the surgical bands. An older session has no values for them:
    // start them flat and off rather than inheriting whatever was in APVTS.
    if (version < 3)
    {
        static constexpr double defaultResFreqs[metaleq::kNumSurgicalBands] =
            { 250.0, 620.0, 1200.0, 2400.0, 4000.0, 6200.0 };
        for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
        {
            setFloatParameter (ParameterIDs::resFreq[i], defaultResFreqs[i]);
            setFloatParameter (ParameterIDs::resGain[i], 0.0);
            setFloatParameter (ParameterIDs::resQ[i], 7.0);
            setFloatParameter (ParameterIDs::resOn[i], 0.0);
        }
        setFloatParameter (ParameterIDs::resAudition, 0.0);
    }

    // V3.1 added the four dynamic bands. Old sessions must start with them
    // disabled, not with whatever APVTS happened to contain.
    if (version < 4)
    {
        const auto defaults = metaleq::defaultDynamicBands();
        for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
        {
            const auto& band = defaults[static_cast<size_t> (i)];
            setFloatParameter (ParameterIDs::dynFreq[i], band.freq);
            setFloatParameter (ParameterIDs::dynQ[i], band.q);
            setFloatParameter (ParameterIDs::dynThreshold[i], band.thresholdDb);
            setFloatParameter (ParameterIDs::dynRatio[i], band.ratio);
            setFloatParameter (ParameterIDs::dynAttack[i], band.attackMs);
            setFloatParameter (ParameterIDs::dynRelease[i], band.releaseMs);
            setFloatParameter (ParameterIDs::dynRange[i], band.maxCutDb);
            setFloatParameter (ParameterIDs::dynOn[i], band.on ? 1.0 : 0.0);
        }
    }

    lastPresetName = tree.getProperty ("lastPresetName").toString().isNotEmpty()
                       ? tree.getProperty ("lastPresetName").toString()
                       : juce::String ("Custom");
    lastPresetId = tree.getProperty ("lastPresetId").toString();
    loadedPresetIsUser = static_cast<bool> (tree.getProperty ("lastPresetIsUser", false));

    if (lastPresetId.isEmpty() && ! loadedPresetIsUser)
    {
        const int factory = metaleq::findPresetByName (lastPresetName.toRawUTF8());
        if (factory >= 0)
            lastPresetId = metaleq::getPreset (factory).id;
    }

    // Low-pass migration: old builds defaulted lpOn to true while the slope
    // was Off. Both versions produced no filter; preserve that sound by
    // clearing the contradictory enable flag.
    if (apvts->getRawParameterValue (ParameterIDs::lpOn)->load() >= 0.5f
        && apvts->getRawParameterValue (ParameterIDs::lpSlope)->load() < 0.5f)
        setFloatParameter (ParameterIDs::lpOn, 0.0);

    loadedPresetState = juce::ValueTree();
    const auto baseline = tree.getChildWithName ("BASELINE");
    if (baseline.isValid() && baseline.getChildWithName ("PARAMS").isValid())
        loadedPresetState = migrateSoundParameterState (baseline.getChildWithName ("PARAMS"));

    for (auto& slot : abSlots)
        slot = AbSlotSnapshot {};

    const auto ab = tree.getChildWithName ("AB");
    if (version >= 2 && ab.isValid())
    {
        activeAbSlot = juce::jlimit (0, numAbSlots - 1,
            static_cast<int> (ab.getProperty ("active", 0)));

        for (int i = 0; i < ab.getNumChildren(); ++i)
        {
            const auto slotTree = ab.getChild (i);
            if (! slotTree.hasType ("SLOT"))
                continue;
            const int index = juce::jlimit (0, numAbSlots - 1,
                static_cast<int> (slotTree.getProperty ("index", i)));
            auto& slot = abSlots[static_cast<size_t> (index)];
            slot.presetName = slotTree.getProperty ("presetName").toString();
            slot.presetId = slotTree.getProperty ("presetId").toString();
            slot.userPreset = static_cast<bool> (slotTree.getProperty ("presetIsUser", false));

            if (slotTree.getChildWithName ("PARAMS").isValid())
                slot.params = migrateSoundParameterState (slotTree.getChildWithName ("PARAMS"), false);
            if (slotTree.getChildWithName ("BASELINE").isValid())
                slot.loadedPresetState = migrateSoundParameterState (slotTree.getChildWithName ("BASELINE"));
            slot.valid = slot.params.isValid();
        }
    }

    if (! loadedPresetState.isValid())
        loadedPresetState = getSoundParameterState (true);

    if (version >= 2 && ab.isValid())
    {
        // The current APVTS parameters are authoritative for the active slot.
        // Restore its metadata/baseline from the saved slot, then refresh the
        // parameter snapshot from the values the host just restored.
        auto& active = abSlots[static_cast<size_t> (activeAbSlot)];
        if (active.presetName.isEmpty())
            active.presetName = lastPresetName;
        if (active.presetId.isEmpty())
            active.presetId = lastPresetId;
        active.userPreset = loadedPresetIsUser;
        if (! active.loadedPresetState.isValid())
            active.loadedPresetState = loadedPresetState.createCopy();
        active.valid = true;
        captureActiveAbSlot();
        loadedPresetState = active.loadedPresetState.createCopy();

        for (int i = 0; i < numAbSlots; ++i)
            if (! abSlots[static_cast<size_t> (i)].valid)
                abSlots[static_cast<size_t> (i)] = abSlots[static_cast<size_t> (activeAbSlot)];
    }
    else
    {
        activeAbSlot = 0;
        captureActiveAbSlot();
        abSlots[1] = abSlots[0];
    }

    syncPresetChoiceToLoadedIdentity();

    applyingPreset.store (false, std::memory_order_release);

    // The audio thread rebuilds its own engine chain from APVTS atomics on the
    // next block. State restore never writes the engine concurrently.
}

//==============================================================================
// Settings
//==============================================================================
metaleq::EqSettings MetalEQAudioProcessor::readEqSettings() const
{
    namespace p = metaleq::params;

    auto load = [this] (const char* id)
    {
        return apvts->getRawParameterValue (id)->load();
    };

    metaleq::EqSettings eq;

    eq.hpFreq   = load (ParameterIDs::hpFreq);
    eq.lpFreq   = load (ParameterIDs::lpFreq);
    eq.hpSlope  = juce::jlimit (0, 3, (int) load (ParameterIDs::hpSlope));
    eq.lpSlope  = juce::jlimit (0, 3, (int) load (ParameterIDs::lpSlope));
    eq.lpEnabled = load (ParameterIDs::lpOn) >= 0.5f;
    eq.lsFreq   = load (ParameterIDs::lsFreq);
    eq.lsGainDb = load (ParameterIDs::lsGain);
    eq.hsFreq   = load (ParameterIDs::hsFreq);
    eq.hsGainDb = load (ParameterIDs::hsGain);
    eq.trimDb   = load (ParameterIDs::trim);

    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        auto& b = eq.bands[static_cast<size_t> (i)];

        b.freq   = metaleq::clampValue (static_cast<double> (load (ParameterIDs::bandFreq[i])), p::bandFreqMin, p::bandFreqMax);
        b.gainDb = metaleq::clampValue (static_cast<double> (load (ParameterIDs::bandGain[i])), p::bandGainMin, p::bandGainMax);
        b.q      = metaleq::clampValue (static_cast<double> (load (ParameterIDs::bandQ[i])), p::bandQMin, p::bandQMax);
        b.on     = load (ParameterIDs::bandOn[i]) >= 0.5f;
    }

    for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
    {
        auto& b = eq.surgical[static_cast<size_t> (i)];

        b.freq   = metaleq::clampValue (static_cast<double> (load (ParameterIDs::resFreq[i])), p::surgicalFreqMin, p::surgicalFreqMax);
        b.gainDb = metaleq::clampValue (static_cast<double> (load (ParameterIDs::resGain[i])), p::surgicalGainMin, p::surgicalGainMax);
        b.q      = metaleq::clampValue (static_cast<double> (load (ParameterIDs::resQ[i])), p::surgicalQMin, p::surgicalQMax);
        b.on     = load (ParameterIDs::resOn[i]) >= 0.5f;
    }

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
    {
        auto& b = eq.dynamic[static_cast<size_t> (i)];

        b.freq        = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynFreq[i])), p::dynamicFreqMin, p::dynamicFreqMax);
        b.q           = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynQ[i])), p::dynamicQMin, p::dynamicQMax);
        b.thresholdDb = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynThreshold[i])), p::dynamicThresholdMin, p::dynamicThresholdMax);
        b.ratio       = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynRatio[i])), p::dynamicRatioMin, p::dynamicRatioMax);
        b.attackMs    = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynAttack[i])), p::dynamicAttackMin, p::dynamicAttackMax);
        b.releaseMs   = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynRelease[i])), p::dynamicReleaseMin, p::dynamicReleaseMax);
        b.maxCutDb    = metaleq::clampValue (static_cast<double> (load (ParameterIDs::dynRange[i])), p::dynamicCutMin, p::dynamicCutMax);
        b.on          = load (ParameterIDs::dynOn[i]) >= 0.5f;
    }

    for (int i = 0; i < static_cast<int> (eq.shapes.size()); ++i)
        eq.shapes[static_cast<size_t> (i)] = load (ParameterIDs::shapeOn[i]) >= 0.5f;

    return eq;
}

metaleq::MacroSettings MetalEQAudioProcessor::readMacroSettings() const
{
    auto load = [this] (const char* id)
    {
        return apvts->getRawParameterValue (id)->load();
    };

    metaleq::MacroSettings m;

    m.tight   = metaleq::clampValue ((double) load (ParameterIDs::tight),   0.0, 1.0);
    m.body    = metaleq::clampValue ((double) load (ParameterIDs::body),    0.0, 1.0);
    m.scoop   = metaleq::clampValue ((double) load (ParameterIDs::scoop),   0.0, 1.0);
    m.bite    = metaleq::clampValue ((double) load (ParameterIDs::bite),    0.0, 1.0);
    m.fizzCut = metaleq::clampValue ((double) load (ParameterIDs::fizzCut), 0.0, 1.0);

    return m;
}

metaleq::EqSettings MetalEQAudioProcessor::neutralEqSettings()
{
    metaleq::EqSettings eq;
    eq.hpFreq = metaleq::params::hpFreqDefault;
    eq.lpFreq = metaleq::params::lpFreqDefault;
    eq.hpSlope = 0;
    eq.lpSlope = 0;
    eq.lpEnabled = false;
    eq.lsFreq = metaleq::params::lsFreqDefault;
    eq.lsGainDb = 0.0;
    eq.hsFreq = metaleq::params::hsFreqDefault;
    eq.hsGainDb = 0.0;
    eq.trimDb = 0.0;

    static constexpr double defaultFreqs[metaleq::kNumBands] = { 120.0, 400.0, 1200.0, 3500.0, 8000.0 };
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        auto& b = eq.bands[static_cast<size_t> (i)];
        b.freq   = defaultFreqs[i];
        b.gainDb = 0.0;
        b.q      = 1.0;
        b.on     = true;
    }

    static constexpr double defaultResFreqs[metaleq::kNumSurgicalBands] =
        { 250.0, 620.0, 1200.0, 2400.0, 4000.0, 6200.0 };
    for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
    {
        auto& b = eq.surgical[static_cast<size_t> (i)];
        b.freq   = defaultResFreqs[i];
        b.gainDb = 0.0;
        b.q      = 7.0;
        b.on     = false;
    }

    return eq;
}

metaleq::MacroSettings MetalEQAudioProcessor::neutralMacroSettings()
{
    return { 0.0, 0.5, 0.0, 0.0, 0.0 };
}

//==============================================================================
// Parameter writes (message thread only)
//==============================================================================
void MetalEQAudioProcessor::setFloatParameter (const char* id, double plainValue)
{
    if (auto* parameter = apvts->getParameter (id))
    {
        const auto& range = parameter->getNormalisableRange();

        if (! metaleq::isFiniteValue (plainValue))
            return;

        parameter->setValueNotifyingHost (range.convertTo0to1 ((float) plainValue));
    }
}

void MetalEQAudioProcessor::setChoiceParameter (const char* id, int index)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts->getParameter (id)))
    {
        const int numChoices = choice->choices.size();

        if (numChoices <= 1)
            return;

        choice->setValueNotifyingHost ((float) juce::jlimit (0, index, numChoices - 1) / (float) (numChoices - 1));
    }
}

void MetalEQAudioProcessor::applyEqSettings (const metaleq::EqSettings& eq, bool includeCrossovers)
{
    setFloatParameter (ParameterIDs::hpFreq, eq.hpFreq);
    setFloatParameter (ParameterIDs::lpFreq, eq.lpFreq);
    setFloatParameter (ParameterIDs::lpOn, eq.lpEnabled ? 1.0 : 0.0);
    setFloatParameter (ParameterIDs::lsFreq, eq.lsFreq);
    setFloatParameter (ParameterIDs::lsGain, eq.lsGainDb);
    setFloatParameter (ParameterIDs::hsFreq, eq.hsFreq);
    setFloatParameter (ParameterIDs::hsGain, eq.hsGainDb);
    setFloatParameter (ParameterIDs::trim, eq.trimDb);

    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        const auto& b = eq.bands[static_cast<size_t> (i)];

        setFloatParameter (ParameterIDs::bandFreq[i], b.freq);
        setFloatParameter (ParameterIDs::bandGain[i], b.gainDb);
        setFloatParameter (ParameterIDs::bandQ[i], b.q);
        setFloatParameter (ParameterIDs::bandOn[i], b.on ? 1.0 : 0.0);
    }

    for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
    {
        const auto& b = eq.surgical[static_cast<size_t> (i)];

        setFloatParameter (ParameterIDs::resFreq[i], b.freq);
        setFloatParameter (ParameterIDs::resGain[i], b.gainDb);
        setFloatParameter (ParameterIDs::resQ[i], b.q);
        setFloatParameter (ParameterIDs::resOn[i], b.on ? 1.0 : 0.0);
    }

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
    {
        const auto& b = eq.dynamic[static_cast<size_t> (i)];

        setFloatParameter (ParameterIDs::dynFreq[i], b.freq);
        setFloatParameter (ParameterIDs::dynQ[i], b.q);
        setFloatParameter (ParameterIDs::dynThreshold[i], b.thresholdDb);
        setFloatParameter (ParameterIDs::dynRatio[i], b.ratio);
        setFloatParameter (ParameterIDs::dynAttack[i], b.attackMs);
        setFloatParameter (ParameterIDs::dynRelease[i], b.releaseMs);
        setFloatParameter (ParameterIDs::dynRange[i], b.maxCutDb);
        setFloatParameter (ParameterIDs::dynOn[i], b.on ? 1.0 : 0.0);
    }

    if (includeCrossovers)
    {
        setChoiceParameter (ParameterIDs::hpSlope, eq.hpSlope);
        setChoiceParameter (ParameterIDs::lpSlope, eq.lpSlope);
    }
}

void MetalEQAudioProcessor::applyMacroSettings (const metaleq::MacroSettings& macros)
{
    setFloatParameter (ParameterIDs::tight,   macros.tight);
    setFloatParameter (ParameterIDs::body,    macros.body);
    setFloatParameter (ParameterIDs::scoop,   macros.scoop);
    setFloatParameter (ParameterIDs::bite,    macros.bite);
    setFloatParameter (ParameterIDs::fizzCut, macros.fizzCut);
}

//==============================================================================
void MetalEQAudioProcessor::applyPreset (int index)
{
    // Canonical mapping: 0 = Custom, 1..N = factory preset 0..N-1.
    const int safeIndex = juce::jlimit (0, metaleq::getNumPresets(), index);

    if (safeIndex == 0)
    {
        resetToNeutral();
        return;
    }

    applyingPreset.store (true, std::memory_order_relaxed);

    const auto& preset = metaleq::getPreset (safeIndex - 1);

    applyEqSettings (preset.eq, true);

    for (const auto* id : ParameterIDs::shapeOn)
        setFloatParameter (id, 0.0);

    if (preset.setsMacros)
        applyMacroSettings (preset.macros);

    setChoiceParameter (ParameterIDs::preset, safeIndex);

    lastPresetName = preset.name;
    lastPresetId = preset.id;
    loadedPresetIsUser = false;
    rememberCurrentAsLoadedPreset (lastPresetName, false);

    applyingPreset.store (false, std::memory_order_release);
}

bool MetalEQAudioProcessor::applyPresetById (const juce::String& presetId)
{
    const int factoryIndex = metaleq::findPresetById (presetId.toRawUTF8());
    if (factoryIndex < 0)
        return false;

    applyPreset (factoryIndex + 1);
    return true;
}

void MetalEQAudioProcessor::applyNeutralParameters()
{
    applyEqSettings (neutralEqSettings(), true);
    applyMacroSettings (neutralMacroSettings());
    for (const auto* id : ParameterIDs::shapeOn)
        setFloatParameter (id, 0.0);
}

void MetalEQAudioProcessor::setLowPassEnabled (bool enabled)
{
    setFloatParameter (ParameterIDs::lpOn, enabled ? 1.0 : 0.0);
    if (enabled && apvts->getRawParameterValue (ParameterIDs::lpSlope)->load() < 0.5f)
        setChoiceParameter (ParameterIDs::lpSlope, 2);
}

void MetalEQAudioProcessor::resetToNeutral()
{
    applyingPreset.store (true, std::memory_order_relaxed);

    applyNeutralParameters();
    setChoiceParameter (ParameterIDs::preset, 0);
    lastPresetName = "Custom";
    lastPresetId.clear();
    loadedPresetIsUser = false;
    rememberCurrentAsLoadedPreset (lastPresetName, false);

    applyingPreset.store (false, std::memory_order_release);
}

void MetalEQAudioProcessor::syncPresetChoiceToLoadedIdentity()
{
    int choice = 0;

    if (! loadedPresetIsUser)
    {
        int factoryIndex = metaleq::findPresetById (lastPresetId.toRawUTF8());
        if (factoryIndex < 0)
            factoryIndex = metaleq::findPresetByName (lastPresetName.toRawUTF8());

        if (factoryIndex >= 0)
            choice = factoryIndex + 1;
    }

    applyingPreset.store (true, std::memory_order_relaxed);
    setChoiceParameter (ParameterIDs::preset, choice);
    applyingPreset.store (false, std::memory_order_release);
}

//==============================================================================
int MetalEQAudioProcessor::getSelectedPresetIndex() const
{
    if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (
            apvts->getParameter (ParameterIDs::preset)))
        return juce::jlimit (0, metaleq::getNumPresets(), choice->getIndex());
    return 0;
}

bool MetalEQAudioProcessor::isPresetModified() const
{
    if (! loadedPresetState.isValid())
        return true;

    // Output trim is a global, level-matching control for A/B listening, not
    // part of the recalled tone. Moving it must not mark either slot dirty.
    const auto current = getSoundParameterState();
    int compared = 0;
    for (int i = 0; i < current.getNumChildren(); ++i)
    {
        const auto child = current.getChild (i);
        const auto id = child.getProperty ("id").toString();
        if (id == ParameterIDs::trim)
            continue;

        ++compared;
        bool found = false;
        for (int j = 0; j < loadedPresetState.getNumChildren(); ++j)
        {
            const auto baseline = loadedPresetState.getChild (j);
            if (baseline.getProperty ("id").toString() == id)
            {
                found = true;
                const double a = child.getProperty ("value").toString().getDoubleValue();
                const double b = baseline.getProperty ("value").toString().getDoubleValue();
                if (std::abs (a - b) > 1.0e-5)
                    return true;
                break;
            }
        }
        if (! found)
            return true;
    }

    int baselineCompared = 0;
    for (int j = 0; j < loadedPresetState.getNumChildren(); ++j)
        if (loadedPresetState.getChild (j).getProperty ("id").toString() != ParameterIDs::trim)
            ++baselineCompared;

    return compared != baselineCompared;
}

MetalEQAudioProcessor::DisplayState MetalEQAudioProcessor::getDisplayState() const
{
    DisplayState state;

    state.eq      = readEqSettings();
    state.macros  = readMacroSettings();
    state.presetIndex = getSelectedPresetIndex();
    state.presetName = lastPresetName;
    state.presetId = lastPresetId;
    state.presetDirty = isPresetModified();
    state.userPreset = loadedPresetIsUser;
    state.abSlot     = activeAbSlot;
    state.abCompare  = activeAbSlot == 1;
    state.bypassed   = apvts->getRawParameterValue (ParameterIDs::bypass)->load() >= 0.5f;

    return state;
}

double MetalEQAudioProcessor::getDisplaySampleRate() const noexcept
{
    const double rate = getSampleRate();
    return rate > 0.0 ? rate : 48000.0;
}

float MetalEQAudioProcessor::getPeakLevel (int channel) const noexcept
{
    if (channel < 0 || channel >= metaleq::kMaxChannels)
        return 0.0f;

    return peakLevels[static_cast<size_t> (channel)].load (std::memory_order_relaxed);
}

float MetalEQAudioProcessor::getInputPeakLevel (int channel) const noexcept
{
    if (channel < 0 || channel >= metaleq::kMaxChannels)
        return 0.0f;
    return inputPeakLevels[static_cast<size_t> (channel)].load (std::memory_order_relaxed);
}

float MetalEQAudioProcessor::getDynamicGainReductionDb (int band) const noexcept
{
    if (band < 0 || band >= metaleq::kNumDynamicBands)
        return 0.0f;

    return dynamicReductionDb[static_cast<size_t> (band)].load (std::memory_order_relaxed);
}

double MetalEQAudioProcessor::getResponseDb (double freq) const
{
    return metaleq::responseDb (readEqSettings(), readMacroSettings(), getDisplaySampleRate(), freq);
}

//==============================================================================
// User preset file operations run on the message thread. Project state remains
// entirely inside APVTS and never depends on these files.
juce::ValueTree MetalEQAudioProcessor::getSoundParameterState (bool includeTrim) const
{
    const auto ids = ParameterIDs::allEqParameterIds();
    juce::ValueTree state ("PARAMS");
    for (const auto& id : ids)
    {
        if (! includeTrim && id == ParameterIDs::trim)
            continue;

        if (const auto* parameter = apvts->getParameter (id))
        {
            juce::ValueTree node ("PARAM");
            node.setProperty ("id", id, nullptr);
            const auto plainValue = parameter->convertFrom0to1 (parameter->getValue());
            node.setProperty ("value", juce::String (static_cast<double> (plainValue), 9), nullptr);
            state.appendChild (node, nullptr);
        }
    }
    return state;
}

void MetalEQAudioProcessor::rememberCurrentAsLoadedPreset (const juce::String& name, bool isUser)
{
    loadedPresetState = getSoundParameterState();
    lastPresetName = name;
    loadedPresetIsUser = isUser;
}

juce::File MetalEQAudioProcessor::getUserPresetDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("MetalEQ").getChildFile ("Presets");
}

bool MetalEQAudioProcessor::isSafePresetName (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty() || name.length() > 48 || name == "." || name == "..")
        return false;
    for (auto c : name)
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
            || c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    return true;
}

juce::File MetalEQAudioProcessor::getUserPresetFile (const juce::String& name) const
{
    const auto legalName = juce::File::createLegalFileName (name.trim()).substring (0, 72);
    return getUserPresetDirectory().getChildFile (legalName + ".metaleq");
}

juce::StringArray MetalEQAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    const auto directory = getUserPresetDirectory();
    if (! directory.isDirectory())
        return names;

    juce::Array<juce::File> files;
    directory.findChildFiles (files, juce::File::findFiles, false, "*.metaleq");
    for (const auto& file : files)
    {
        if (file.getSize() <= 0 || file.getSize() > 256 * 1024)
            continue;
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            continue;
        const auto tree = juce::ValueTree::fromXml (*xml);
        const auto name = tree.getProperty ("name").toString();
        if (tree.hasType ("MetalEQUserPreset") && isSupportedUserPresetSchema (tree.getProperty ("schemaVersion"))
            && isSafePresetName (name) && ! names.contains (name, true))
            names.add (name);
    }
    names.sort (true);
    return names;
}

bool MetalEQAudioProcessor::saveUserPreset (const juce::String& rawName, bool overwrite, juce::String& error)
{
    const auto name = rawName.trim();
    error.clear();
    if (! isSafePresetName (name))
    {
        error = "Use a preset name from 1 to 48 characters. File path characters are not allowed.";
        return false;
    }
    if (metaleq::findPresetByName (name.toRawUTF8()) >= 0)
    {
        error = "Factory presets are read-only. Choose a different name.";
        return false;
    }

    auto file = getUserPresetFile (name);
    for (const auto& existing : getUserPresetNames())
        if (existing.equalsIgnoreCase (name))
        {
            file = getUserPresetFile (existing);
            if (! overwrite)
            {
                error = "A user preset with that name already exists.";
                return false;
            }
            break;
        }

    if (! getUserPresetDirectory().createDirectory())
    {
        error = "Could not create the user preset folder.";
        return false;
    }
    const bool nameAlreadyExists = getUserPresetNames().contains (name, true);
    if (file.existsAsFile() && ! nameAlreadyExists)
    {
        error = "A file with the requested preset filename already exists.";
        return false;
    }
    juce::ValueTree root ("MetalEQUserPreset");
    root.setProperty ("schemaVersion", 3, nullptr);
    root.setProperty ("name", name, nullptr);
    root.appendChild (getSoundParameterState(), nullptr);
    const auto xml = root.createXml();
    if (xml == nullptr || ! file.replaceWithText (xml->toString()))
    {
        error = "The preset could not be written. Check that the user preset folder is writable.";
        return false;
    }

    applyingPreset.store (true, std::memory_order_relaxed);
    setChoiceParameter (ParameterIDs::preset, 0);
    lastPresetId.clear();
    rememberCurrentAsLoadedPreset (name, true);
    applyingPreset.store (false, std::memory_order_release);
    return true;
}

bool MetalEQAudioProcessor::isOptionalResonanceParameterId (const juce::String& id)
{
    for (int i = 0; i < metaleq::kNumSurgicalBands; ++i)
        if (id == ParameterIDs::resFreq[i] || id == ParameterIDs::resGain[i]
            || id == ParameterIDs::resQ[i] || id == ParameterIDs::resOn[i])
            return true;

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
        if (id == ParameterIDs::dynFreq[i] || id == ParameterIDs::dynQ[i]
            || id == ParameterIDs::dynThreshold[i] || id == ParameterIDs::dynRatio[i]
            || id == ParameterIDs::dynAttack[i] || id == ParameterIDs::dynRelease[i]
            || id == ParameterIDs::dynRange[i] || id == ParameterIDs::dynOn[i])
            return true;

    return false;
}

juce::ValueTree MetalEQAudioProcessor::migrateSoundParameterState (const juce::ValueTree& state,
                                                                   bool includeTrim) const
{
    if (! state.isValid() || ! state.hasType ("PARAMS"))
        return state;

    auto expectedIds = ParameterIDs::allEqParameterIds();
    if (! includeTrim)
        expectedIds.removeString (ParameterIDs::trim);

    auto migrated = state.createCopy();

    for (const auto& id : expectedIds)
    {
        bool found = false;
        for (int i = 0; i < migrated.getNumChildren(); ++i)
            if (migrated.getChild (i).getProperty ("id").toString() == id)
            {
                found = true;
                break;
            }

        if (found)
            continue;

        // Never invent values for the original schema. Only the V3 surgical and
        // V3.1 dynamic parameters may be missing from a state written by an
        // older build; anything else is damaged or unsupported and must fail
        // validation.
        if (! isOptionalResonanceParameterId (id))
            return state;

        const auto* parameter = apvts->getParameter (id);
        if (parameter == nullptr)
            return state;

        const auto defaultValue = parameter->convertFrom0to1 (parameter->getDefaultValue());
        juce::ValueTree node ("PARAM");
        node.setProperty ("id", id, nullptr);
        node.setProperty ("value", juce::String (static_cast<double> (defaultValue), 9), nullptr);
        migrated.appendChild (node, nullptr);
    }

    return migrated;
}

void MetalEQAudioProcessor::markCurrentToneAsCustom()
{
    applyingPreset.store (true, std::memory_order_relaxed);
    setChoiceParameter (ParameterIDs::preset, 0);
    lastPresetName = "Custom";
    lastPresetId.clear();
    loadedPresetIsUser = false;
    rememberCurrentAsLoadedPreset (lastPresetName, false);
    applyingPreset.store (false, std::memory_order_release);
}

bool MetalEQAudioProcessor::applySoundParameterState (const juce::ValueTree& state, juce::String& error,
                                                       bool includeTrim)
{
    if (! state.isValid() || ! state.hasType ("PARAMS"))
    {
        error = "Preset has no valid parameter state.";
        return false;
    }

    auto expectedIds = ParameterIDs::allEqParameterIds();
    if (! includeTrim)
        expectedIds.removeString (ParameterIDs::trim);

    auto migratedState = migrateSoundParameterState (state, includeTrim);

    juce::StringArray seenIds;
    if (migratedState.getNumChildren() != expectedIds.size())
    {
        error = "Preset is incomplete or uses an unsupported parameter schema.";
        return false;
    }
    int lowPassSlope = -1;
    int lowPassEnabledChild = -1;
    for (int i = 0; i < migratedState.getNumChildren(); ++i)
    {
        const auto child = migratedState.getChild (i);
        const auto id = child.getProperty ("id").toString();
        const auto rawValue = child.getProperty ("value").toString();
        if (id == ParameterIDs::lpSlope)
            lowPassSlope = i;
        if (id == ParameterIDs::lpOn)
            lowPassEnabledChild = i;
    }

    // Version-1 user presets written by an earlier build could contain a
    // blank lpOn value if APVTS had not materialized that default node yet.
    // Derive only this known legacy field from the stored slope; all other
    // missing or malformed values remain errors.
    auto validatedState = migratedState.createCopy();
    if (lowPassSlope >= 0 && lowPassEnabledChild >= 0
        && validatedState.getChild (lowPassEnabledChild).getProperty ("value").toString().trim().isEmpty())
    {
        const auto slopeText = validatedState.getChild (lowPassSlope).getProperty ("value").toString().trim();
        double slopeValue = 0.0;
        const auto* slopeParameter = apvts->getParameter (ParameterIDs::lpSlope);
        if (slopeText.isEmpty() || ! parsePresetNumber (slopeText, slopeValue) || slopeParameter == nullptr
            || slopeValue < slopeParameter->getNormalisableRange().start
            || slopeValue > slopeParameter->getNormalisableRange().end)
        {
            error = "Preset contains an unknown or invalid parameter: lpSlope=" + slopeText;
            return false;
        }
        validatedState.getChild (lowPassEnabledChild).setProperty (
            "value", slopeValue > 0.0 ? "1" : "0", nullptr);
    }

    for (int i = 0; i < validatedState.getNumChildren(); ++i)
    {
        const auto child = validatedState.getChild (i);
        const auto id = child.getProperty ("id").toString();
        const auto rawValue = child.getProperty ("value").toString().trim();
        double value = 0.0;
        const bool parsed = parsePresetNumber (rawValue, value);
        const auto* parameter = apvts->getParameter (id);
        const auto range = parameter != nullptr ? parameter->getNormalisableRange()
                                                : juce::NormalisableRange<float> { 0.0f, 1.0f };
        if (! expectedIds.contains (id) || seenIds.contains (id) || rawValue.isEmpty()
            || ! parsed
            || value < range.start || value > range.end
            || parameter == nullptr)
        {
            error = "Preset contains an unknown or invalid parameter: " + id + "=" + rawValue;
            return false;
        }
        seenIds.add (id);
    }

    // A later build changed low-pass semantics: the filter is audible only
    // when enabled AND slope > 0. Migrate the contradictory legacy combination
    // by disabling it, which preserves the sound old sessions and presets had.
    for (int i = 0; i < validatedState.getNumChildren(); ++i)
    {
        auto child = validatedState.getChild (i);
        if (child.getProperty ("id").toString() != ParameterIDs::lpOn)
            continue;
        if (child.getProperty ("value").toString().getDoubleValue() < 0.5)
            break;
        for (int j = 0; j < validatedState.getNumChildren(); ++j)
        {
            const auto slopeChild = validatedState.getChild (j);
            if (slopeChild.getProperty ("id").toString() == ParameterIDs::lpSlope
                && slopeChild.getProperty ("value").toString().getDoubleValue() <= 0.0)
            {
                child.setProperty ("value", "0", nullptr);
                break;
            }
        }
        break;
    }

    applyingPreset.store (true, std::memory_order_relaxed);
    for (int i = 0; i < validatedState.getNumChildren(); ++i)
    {
        const auto child = validatedState.getChild (i);
        const auto id = child.getProperty ("id").toString();
        if (! includeTrim && id == ParameterIDs::trim)
            continue;

        if (auto* parameter = apvts->getParameter (id))
        {
            const auto plainValue = static_cast<float> (child.getProperty ("value").toString().getDoubleValue());
            parameter->setValueNotifyingHost (parameter->getNormalisableRange().convertTo0to1 (plainValue));
        }
    }
    applyingPreset.store (false, std::memory_order_release);
    return true;
}

bool MetalEQAudioProcessor::loadUserPreset (const juce::String& name, juce::String& error)
{
    error.clear();
    if (! isSafePresetName (name) || metaleq::findPresetByName (name.toRawUTF8()) >= 0)
    {
        error = "That is not a valid user preset name.";
        return false;
    }
    const auto file = getUserPresetFile (name);
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 256 * 1024)
    {
        error = "The user preset file is missing or too large.";
        return false;
    }
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        error = "The user preset file is damaged or is not valid XML.";
        return false;
    }
    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("MetalEQUserPreset") || ! isSupportedUserPresetSchema (root.getProperty ("schemaVersion"))
        || ! root.getProperty ("name").toString().equalsIgnoreCase (name))
    {
        error = "This preset was created by an incompatible version or has invalid metadata.";
        return false;
    }
    if (! applySoundParameterState (root.getChildWithName ("PARAMS"), error))
        return false;

    applyingPreset.store (true, std::memory_order_relaxed);
    setChoiceParameter (ParameterIDs::preset, 0);
    lastPresetId.clear();
    rememberCurrentAsLoadedPreset (root.getProperty ("name").toString(), true);
    applyingPreset.store (false, std::memory_order_release);
    return true;
}

bool MetalEQAudioProcessor::renameUserPreset (const juce::String& oldRaw, const juce::String& newRaw, juce::String& error)
{
    error.clear();
    const auto oldName = oldRaw.trim();
    const auto newName = newRaw.trim();
    if (! isSafePresetName (oldName) || ! isSafePresetName (newName)
        || metaleq::findPresetByName (oldName.toRawUTF8()) >= 0
        || metaleq::findPresetByName (newName.toRawUTF8()) >= 0)
    {
        error = "Factory presets are read-only. Enter a valid user-preset name.";
        return false;
    }
    for (const auto& existing : getUserPresetNames())
        if (existing.equalsIgnoreCase (newName) && ! existing.equalsIgnoreCase (oldName))
        {
            error = "A preset with that name already exists.";
            return false;
        }
    const auto oldFile = getUserPresetFile (oldName);
    if (! oldFile.existsAsFile())
    {
        error = "The selected user preset no longer exists.";
        return false;
    }
    const auto xml = juce::XmlDocument::parse (oldFile);
    if (xml == nullptr)
    {
        error = "The selected preset file is damaged.";
        return false;
    }
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType ("MetalEQUserPreset") || ! isSupportedUserPresetSchema (tree.getProperty ("schemaVersion")))
    {
        error = "The selected file is not a compatible user preset.";
        return false;
    }
    tree.setProperty ("name", newName, nullptr);
    const auto newFile = getUserPresetFile (newName);
    const auto newXml = tree.createXml();
    if (newFile.existsAsFile() || newXml == nullptr || ! newFile.replaceWithText (newXml->toString()))
    {
        error = "The renamed preset could not be written.";
        return false;
    }
    if (! oldFile.deleteFile())
    {
        newFile.deleteFile();
        error = "The old preset file could not be removed; rename was rolled back.";
        return false;
    }
    if (loadedPresetIsUser && lastPresetName.equalsIgnoreCase (oldName))
        lastPresetName = newName;
    return true;
}

bool MetalEQAudioProcessor::deleteUserPreset (const juce::String& name, juce::String& error)
{
    error.clear();
    if (! isSafePresetName (name) || metaleq::findPresetByName (name.toRawUTF8()) >= 0)
    {
        error = "Factory presets cannot be deleted.";
        return false;
    }
    const auto file = getUserPresetFile (name);
    if (! file.existsAsFile() || ! file.deleteFile())
    {
        error = "The user preset could not be deleted.";
        return false;
    }
    if (loadedPresetIsUser && lastPresetName.equalsIgnoreCase (name))
    {
        applyingPreset.store (true, std::memory_order_relaxed);
        loadedPresetIsUser = false;
        lastPresetName = "Custom";
        lastPresetId.clear();
        setChoiceParameter (ParameterIDs::preset, 0);
        rememberCurrentAsLoadedPreset (lastPresetName, false);
        applyingPreset.store (false, std::memory_order_release);
    }
    return true;
}

void MetalEQAudioProcessor::revertToLoadedPreset()
{
    juce::String error;
    if (applySoundParameterState (loadedPresetState, error))
        syncPresetChoiceToLoadedIdentity();
}

void MetalEQAudioProcessor::startAnalyzerCapture (int targetProfile)
{
    startAnalyzerCaptureWithGeneration (
        targetProfile, 0, analyzerCancelGeneration.load (std::memory_order_acquire));
}

void MetalEQAudioProcessor::startAnalyzerCaptureWithGeneration (
    int targetProfile, int mode, unsigned long long cancelGeneration)
{
    bool expectedStart = false;
    if (! analyzerCaptureStartInProgress.compare_exchange_strong (expectedStart, true,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return;

    if (cancelGeneration != analyzerCancelGeneration.load (std::memory_order_acquire))
    {
        analyzerCaptureStartInProgress.store (false, std::memory_order_release);
        return;
    }

    auto state = analyzerCaptureState.load (std::memory_order_acquire);
    if ((state == AnalyzerCaptureState::cancelPending
            || (state == AnalyzerCaptureState::idle
                && analyzerCaptureCancelled.load (std::memory_order_acquire)))
        && analyzerCaptureWriterActive.load (std::memory_order_acquire))
    {
        analyzerDeferredStartProfile.store (juce::jlimit (0, metaleq::getNumAnalyzerTargets() - 1, targetProfile), std::memory_order_relaxed);
        analyzerDeferredStartMode.store (mode, std::memory_order_relaxed);
        analyzerDeferredStartRequested.store (true, std::memory_order_release);
        if (analyzerCaptureWriterActive.load (std::memory_order_acquire))
        {
            analyzerCaptureStartInProgress.store (false, std::memory_order_release);
            return;
        }

        // The writer may have released its guard just before the request was
        // published. Continue the normal start here instead of waiting for a
        // later callback to notice it.
        analyzerDeferredStartRequested.store (false, std::memory_order_release);
        state = analyzerCaptureState.load (std::memory_order_acquire);
    }

    if (state == AnalyzerCaptureState::cancelPending
        && ! analyzerCaptureWriterActive.load (std::memory_order_acquire))
    {
        analyzerCaptureState.compare_exchange_strong (state, AnalyzerCaptureState::idle,
            std::memory_order_acq_rel, std::memory_order_acquire);
        state = analyzerCaptureState.load (std::memory_order_acquire);
    }

    if ((state != AnalyzerCaptureState::idle && state != AnalyzerCaptureState::complete
         && state != AnalyzerCaptureState::failed)
        || analyzerCaptureWriterActive.load (std::memory_order_acquire))
    {
        analyzerCaptureStartInProgress.store (false, std::memory_order_release);
        return;
    }

    // Only a serialized start may clear this before publishing arming, so a
    // cancellation of the new capture cannot be erased by another start.
    analyzerCaptureCancelled.store (false, std::memory_order_release);

    if (cancelGeneration != analyzerCancelGeneration.load (std::memory_order_acquire))
    {
        analyzerCaptureCancelled.store (true, std::memory_order_release);
        analyzerCaptureStartInProgress.store (false, std::memory_order_release);
        return;
    }

    if (! analyzerCaptureState.compare_exchange_strong (state, AnalyzerCaptureState::arming,
            std::memory_order_acq_rel, std::memory_order_acquire))
    {
        analyzerCaptureStartInProgress.store (false, std::memory_order_release);
        return;
    }

    // If Stop raced while the old state was idle, it could not transition that
    // state. The generation check closes that gap after arming is published.
    if (cancelGeneration != analyzerCancelGeneration.load (std::memory_order_acquire))
    {
        analyzerCaptureCancelled.store (true, std::memory_order_release);
        auto armingState = AnalyzerCaptureState::arming;
        analyzerCaptureState.compare_exchange_strong (armingState, AnalyzerCaptureState::idle,
            std::memory_order_acq_rel, std::memory_order_acquire);
        analyzerCaptureStartInProgress.store (false, std::memory_order_release);
        return;
    }

    analyzerDeferredStartRequested.store (false, std::memory_order_release);
    analyzerCaptureGeneration.fetch_add (1, std::memory_order_acq_rel);
    analyzerTargetProfile.store (juce::jlimit (0, metaleq::getNumAnalyzerTargets() - 1, targetProfile), std::memory_order_relaxed);
    analyzerMode.store (mode == 1 ? 1 : 0, std::memory_order_release);
    auto arming = AnalyzerCaptureState::arming;
    analyzerCaptureState.compare_exchange_strong (arming, AnalyzerCaptureState::requested,
        std::memory_order_release, std::memory_order_acquire);
    analyzerCaptureStartInProgress.store (false, std::memory_order_release);
}

void MetalEQAudioProcessor::cancelAnalyzerCapture() noexcept
{
    if (analyzerDeferredStartRequested.exchange (false, std::memory_order_acq_rel))
    {
        analyzerCancelGeneration.fetch_add (1, std::memory_order_acq_rel);
        analyzerCaptureCancelled.store (true, std::memory_order_release);
        return;
    }

    analyzerCancelGeneration.fetch_add (1, std::memory_order_acq_rel);

    auto state = analyzerCaptureState.load (std::memory_order_acquire);
    if (state != AnalyzerCaptureState::arming && state != AnalyzerCaptureState::requested
        && state != AnalyzerCaptureState::recording && state != AnalyzerCaptureState::cancelPending)
        return;

    analyzerCaptureCancelled.store (true, std::memory_order_release);
    while (state == AnalyzerCaptureState::arming || state == AnalyzerCaptureState::requested
           || state == AnalyzerCaptureState::recording)
    {
        if (analyzerCaptureState.compare_exchange_weak (state, AnalyzerCaptureState::cancelPending,
                std::memory_order_acq_rel, std::memory_order_acquire))
            break;
    }
}

MetalEQAudioProcessor::AnalyzerStatus MetalEQAudioProcessor::captureStatus() const noexcept
{
    const auto state = analyzerCaptureState.load (std::memory_order_acquire);
    if (analyzerDeferredStartRequested.load (std::memory_order_acquire))
        return AnalyzerStatus::capturing;
    if (analyzerCaptureCancelled.load (std::memory_order_acquire)
        && (state == AnalyzerCaptureState::arming || state == AnalyzerCaptureState::requested
            || state == AnalyzerCaptureState::recording || state == AnalyzerCaptureState::cancelPending))
        return AnalyzerStatus::idle;

    switch (state)
    {
    case AnalyzerCaptureState::arming:
    case AnalyzerCaptureState::requested:
    case AnalyzerCaptureState::recording:
    case AnalyzerCaptureState::cancelPending:
        return AnalyzerStatus::capturing;
    case AnalyzerCaptureState::ready:
    case AnalyzerCaptureState::analyzing:
        return AnalyzerStatus::analyzing;
    case AnalyzerCaptureState::complete:
        return AnalyzerStatus::ready;
    case AnalyzerCaptureState::failed:
        return AnalyzerStatus::failed;
    case AnalyzerCaptureState::idle:
    default:
        return AnalyzerStatus::idle;
    }
}

MetalEQAudioProcessor::AnalyzerStatus MetalEQAudioProcessor::getAnalyzerStatus() const noexcept
{
    return analyzerMode.load (std::memory_order_acquire) == 0
        ? captureStatus() : AnalyzerStatus::idle;
}

float MetalEQAudioProcessor::getAnalyzerProgress() const noexcept
{
    if (analyzerMode.load (std::memory_order_acquire) != 0)
        return 0.0f;

    if (analyzerDeferredStartRequested.load (std::memory_order_acquire))
        return 0.0f;
    const int limit = analyzerCaptureLimit.load (std::memory_order_relaxed);
    return limit > 0 ? juce::jlimit (0.0f, 1.0f,
        static_cast<float> (analyzerSampleCount.load (std::memory_order_relaxed)) / limit) : 0.0f;
}

MetalEQAudioProcessor::AnalyzerStatus MetalEQAudioProcessor::getResonanceStatus() const noexcept
{
    return analyzerMode.load (std::memory_order_acquire) == 1
        ? captureStatus() : AnalyzerStatus::idle;
}

float MetalEQAudioProcessor::getResonanceProgress() const noexcept
{
    if (analyzerMode.load (std::memory_order_acquire) != 1)
        return 0.0f;

    if (analyzerDeferredStartRequested.load (std::memory_order_acquire))
        return 0.0f;
    const int limit = analyzerCaptureLimit.load (std::memory_order_relaxed);
    return limit > 0 ? juce::jlimit (0.0f, 1.0f,
        static_cast<float> (analyzerSampleCount.load (std::memory_order_relaxed)) / limit) : 0.0f;
}

void MetalEQAudioProcessor::getAnalyzerSuggestion (
    std::array<float, metaleq::kNumBands>& gains) const noexcept
{
    for (int i = 0; i < metaleq::kNumBands; ++i)
        gains[static_cast<size_t> (i)] = analyzerSuggestion[static_cast<size_t> (i)].load (std::memory_order_acquire);
}

double MetalEQAudioProcessor::getAnalyzerResponseDb (double freq, float amount) const
{
    static constexpr double frequencies[metaleq::kNumBands] = { 120.0, 350.0, 1000.0, 2800.0, 7000.0 };
    auto eq = readEqSettings();
    amount = juce::jlimit (0.0f, 1.0f, amount);
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        auto& band = eq.bands[static_cast<size_t> (i)];
        band.freq = frequencies[i];
        band.gainDb = analyzerSuggestion[static_cast<size_t> (i)].load (std::memory_order_acquire) * amount;
        band.q = 0.8;
        band.on = true;
    }
    return metaleq::responseDb (eq, readMacroSettings(), getDisplaySampleRate(), freq);
}

void MetalEQAudioProcessor::applyAnalyzerSuggestion (float amount)
{
    if (getAnalyzerStatus() != AnalyzerStatus::ready)
        return;

    static constexpr double frequencies[metaleq::kNumBands] = { 120.0, 350.0, 1000.0, 2800.0, 7000.0 };
    amount = juce::jlimit (0.0f, 1.0f, amount);
    applyingPreset.store (true, std::memory_order_relaxed);
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        setFloatParameter (ParameterIDs::bandFreq[i], frequencies[i]);
        setFloatParameter (ParameterIDs::bandGain[i],
            analyzerSuggestion[static_cast<size_t> (i)].load (std::memory_order_acquire) * amount);
        setFloatParameter (ParameterIDs::bandQ[i], 0.8);
        setFloatParameter (ParameterIDs::bandOn[i], 1.0);
    }
    setChoiceParameter (ParameterIDs::preset, 0);
    lastPresetName = "Custom";
    lastPresetId.clear();
    loadedPresetIsUser = false;
    rememberCurrentAsLoadedPreset (lastPresetName, false);
    applyingPreset.store (false, std::memory_order_release);
}

void MetalEQAudioProcessor::startResonanceScan()
{
    const auto state = analyzerCaptureState.load (std::memory_order_acquire);
    if (state != AnalyzerCaptureState::idle && state != AnalyzerCaptureState::complete
        && state != AnalyzerCaptureState::failed)
        return;
    if (analyzerDeferredStartRequested.load (std::memory_order_acquire)
        || analyzerCaptureWriterActive.load (std::memory_order_acquire))
        return;

    clearResonanceCandidates();
    startAnalyzerCaptureWithGeneration (
        0, 1, analyzerCancelGeneration.load (std::memory_order_acquire));
}

void MetalEQAudioProcessor::cancelResonanceScan() noexcept
{
    cancelAnalyzerCapture();
}

bool MetalEQAudioProcessor::getResonanceEnoughVariation() const noexcept
{
    return resonanceEnoughVariation.load (std::memory_order_acquire);
}

int MetalEQAudioProcessor::getResonanceCandidateCount() const noexcept
{
    return juce::jlimit (0, metaleq::kMaxResonanceCandidates,
        resonanceCandidateCount.load (std::memory_order_acquire));
}

bool MetalEQAudioProcessor::getResonanceCandidate (
    int index, metaleq::ResonanceCandidate& candidate) const noexcept
{
    if (index < 0 || index >= getResonanceCandidateCount())
        return false;

    const auto slot = static_cast<size_t> (index);
    candidate.freq         = resonanceFreq[slot].load (std::memory_order_acquire);
    candidate.q            = resonanceQ[slot].load (std::memory_order_acquire);
    candidate.prominenceDb = resonanceProminence[slot].load (std::memory_order_acquire);
    candidate.residualDb   = resonanceResidual[slot].load (std::memory_order_acquire);
    candidate.confidence   = resonanceConfidence[slot].load (std::memory_order_acquire);
    candidate.suggestedCutDb = resonanceCut[slot].load (std::memory_order_acquire);
    return true;
}

bool MetalEQAudioProcessor::getResonanceDynamicSuggestion (
    int candidateIndex,
    metaleq::DynamicBandSettings& band,
    double& confidence,
    double& measuredExcessDb) const noexcept
{
    if (candidateIndex < 0 || candidateIndex >= getResonanceCandidateCount())
        return false;

    const auto slot = static_cast<size_t> (candidateIndex);
    if (! resonanceDynamicEligible[slot].load (std::memory_order_acquire))
        return false;

    band.freq        = resonanceFreq[slot].load (std::memory_order_relaxed);
    band.q           = resonanceDynamicQ[slot].load (std::memory_order_relaxed);
    band.thresholdDb = resonanceDynamicThreshold[slot].load (std::memory_order_relaxed);
    band.ratio       = resonanceDynamicRatio[slot].load (std::memory_order_relaxed);
    band.attackMs    = resonanceDynamicAttack[slot].load (std::memory_order_relaxed);
    band.releaseMs   = resonanceDynamicRelease[slot].load (std::memory_order_relaxed);
    band.maxCutDb    = resonanceDynamicMaxCut[slot].load (std::memory_order_relaxed);
    band.on          = true;

    confidence = resonanceConfidence[slot].load (std::memory_order_relaxed);
    measuredExcessDb = resonanceDynamicExcess[slot].load (std::memory_order_relaxed);
    return true;
}

unsigned long long MetalEQAudioProcessor::getResonanceRevision() const noexcept
{
    return resonanceRevision.load (std::memory_order_acquire);
}

void MetalEQAudioProcessor::clearResonanceCandidates() noexcept
{
    for (auto& eligible : resonanceDynamicEligible)
        eligible.store (false, std::memory_order_relaxed);

    resonanceCandidateCount.store (0, std::memory_order_release);
    resonanceEnoughVariation.store (false, std::memory_order_relaxed);
    resonanceRevision.fetch_add (1, std::memory_order_acq_rel);
}

void MetalEQAudioProcessor::applyResonanceSelection (const std::vector<int>& candidateIndices)
{
    if (getResonanceStatus() != AnalyzerStatus::ready)
        return;

    const int candidateCount = getResonanceCandidateCount();
    std::vector<int> selected;
    selected.reserve (metaleq::kNumSurgicalBands);

    for (const int index : candidateIndices)
    {
        if (index < 0 || index >= candidateCount)
            continue;
        if (std::find (selected.begin(), selected.end(), index) != selected.end())
            continue;
        selected.push_back (index);
        if (static_cast<int> (selected.size()) >= metaleq::kNumSurgicalBands)
            break;
    }

    applyingPreset.store (true, std::memory_order_relaxed);

    for (int band = 0; band < metaleq::kNumSurgicalBands; ++band)
    {
        const bool hasCandidate = band < static_cast<int> (selected.size());

        if (hasCandidate)
        {
            metaleq::ResonanceCandidate candidate;
            getResonanceCandidate (selected[static_cast<size_t> (band)], candidate);

            setFloatParameter (ParameterIDs::resFreq[band], candidate.freq);
            setFloatParameter (ParameterIDs::resGain[band], candidate.suggestedCutDb);
            setFloatParameter (ParameterIDs::resQ[band], candidate.q);
            setFloatParameter (ParameterIDs::resOn[band], 1.0);
        }
        else
        {
            setFloatParameter (ParameterIDs::resGain[band], 0.0);
            setFloatParameter (ParameterIDs::resOn[band], 0.0);
        }
    }

    applyingPreset.store (false, std::memory_order_release);
    markCurrentToneAsCustom();
}

void MetalEQAudioProcessor::clearSurgicalBands()
{
    bool changed = false;
    for (int band = 0; band < metaleq::kNumSurgicalBands; ++band)
        if (apvts->getRawParameterValue (ParameterIDs::resOn[band])->load() >= 0.5f
            || std::abs (apvts->getRawParameterValue (ParameterIDs::resGain[band])->load()) > 1.0e-3f)
            changed = true;

    if (! changed)
        return;

    applyingPreset.store (true, std::memory_order_relaxed);
    for (int band = 0; band < metaleq::kNumSurgicalBands; ++band)
    {
        setFloatParameter (ParameterIDs::resGain[band], 0.0);
        setFloatParameter (ParameterIDs::resOn[band], 0.0);
    }
    applyingPreset.store (false, std::memory_order_release);
    markCurrentToneAsCustom();
}

void MetalEQAudioProcessor::applyResonanceToDynamic (const std::vector<int>& candidateIndices)
{
    if (getResonanceStatus() != AnalyzerStatus::ready)
        return;

    std::array<metaleq::DynamicBandSettings, metaleq::kNumDynamicBands> selected {};
    std::vector<int> seen;
    seen.reserve (metaleq::kNumDynamicBands);
    int numSelected = 0;

    for (const int index : candidateIndices)
    {
        if (numSelected >= metaleq::kNumDynamicBands)
            break;
        if (index < 0 || index >= getResonanceCandidateCount())
            continue;
        if (std::find (seen.begin(), seen.end(), index) != seen.end())
            continue;

        metaleq::DynamicBandSettings band;
        double confidence = 0.0;
        double excessDb = 0.0;
        if (! getResonanceDynamicSuggestion (index, band, confidence, excessDb))
            continue;

        seen.push_back (index);
        selected[static_cast<size_t> (numSelected++)] = band;
    }

    if (numSelected == 0)
        return;

    applyingPreset.store (true, std::memory_order_relaxed);

    for (int bandIndex = 0; bandIndex < metaleq::kNumDynamicBands; ++bandIndex)
    {
        if (bandIndex < numSelected)
        {
            const auto& band = selected[static_cast<size_t> (bandIndex)];
            setFloatParameter (ParameterIDs::dynFreq[bandIndex], band.freq);
            setFloatParameter (ParameterIDs::dynQ[bandIndex], band.q);
            setFloatParameter (ParameterIDs::dynThreshold[bandIndex], band.thresholdDb);
            setFloatParameter (ParameterIDs::dynRatio[bandIndex], band.ratio);
            setFloatParameter (ParameterIDs::dynAttack[bandIndex], band.attackMs);
            setFloatParameter (ParameterIDs::dynRelease[bandIndex], band.releaseMs);
            setFloatParameter (ParameterIDs::dynRange[bandIndex], band.maxCutDb);
            setFloatParameter (ParameterIDs::dynOn[bandIndex], 1.0);
        }
        else
        {
            setFloatParameter (ParameterIDs::dynRange[bandIndex], 0.0);
            setFloatParameter (ParameterIDs::dynOn[bandIndex], 0.0);
        }
    }

    applyingPreset.store (false, std::memory_order_release);
    markCurrentToneAsCustom();
}

void MetalEQAudioProcessor::clearDynamicBands()
{
    bool changed = false;
    for (int band = 0; band < metaleq::kNumDynamicBands; ++band)
        if (apvts->getRawParameterValue (ParameterIDs::dynOn[band])->load() >= 0.5f
            || std::abs (apvts->getRawParameterValue (ParameterIDs::dynRange[band])->load()) > 1.0e-3f)
            changed = true;

    if (! changed)
        return;

    applyingPreset.store (true, std::memory_order_relaxed);
    for (int band = 0; band < metaleq::kNumDynamicBands; ++band)
    {
        setFloatParameter (ParameterIDs::dynRange[band], 0.0);
        setFloatParameter (ParameterIDs::dynOn[band], 0.0);
    }
    applyingPreset.store (false, std::memory_order_release);
    markCurrentToneAsCustom();
}

void MetalEQAudioProcessor::analyzerWorkerLoop()
{
    while (! stopAnalyzerWorker.load (std::memory_order_acquire))
    {
        auto expected = AnalyzerCaptureState::ready;
        if (analyzerCaptureState.compare_exchange_strong (expected, AnalyzerCaptureState::analyzing,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            const int samples = analyzerSampleCount.load (std::memory_order_acquire);
            const double sampleRate = static_cast<double> (analyzerSampleRate.load (std::memory_order_relaxed));

            if (analyzerMode.load (std::memory_order_acquire) == 1)
                analyzeResonanceCapture (samples, sampleRate);
            else
                analyzeCapture (samples, sampleRate);
        }
        else
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    }
}

void MetalEQAudioProcessor::analyzeCapture (int sampleCount, double sampleRate)
{
    constexpr int fftOrder = 11;
    constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::array<float, fftSize * 2> frame {};
    std::array<double, fftSize / 2 + 1> spectrum {};
    int frames = 0;

    for (int offset = 0; offset + fftSize <= sampleCount; offset += fftSize / 2)
    {
        double energy = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const float sample = analyzerCapture[static_cast<size_t> (offset + i)];
            energy += static_cast<double> (sample) * sample;
            const float window = 0.5f - 0.5f * std::cos (
                2.0f * static_cast<float> (metaleq::kPi) * i / (fftSize - 1));
            frame[static_cast<size_t> (i)] = sample * window;
        }
        if (energy / fftSize > 1.0e-10)
        {
            fft.performFrequencyOnlyForwardTransform (frame.data(), true);
            for (int bin = 0; bin <= fftSize / 2; ++bin)
            {
                const double magnitude = frame[static_cast<size_t> (bin)];
                spectrum[static_cast<size_t> (bin)] += magnitude * magnitude;
            }
            ++frames;
        }
    }

    if (frames == 0)
    {
        analyzerCaptureState.store (AnalyzerCaptureState::failed, std::memory_order_release);
        return;
    }

    static constexpr double frequencies[metaleq::kNumBands] = { 120.0, 350.0, 1000.0, 2800.0, 7000.0 };
    std::array<double, metaleq::kNumBands> measured {};
    for (int band = 0; band < metaleq::kNumBands; ++band)
    {
        const int centre = juce::jlimit (1, fftSize / 2,
            static_cast<int> (frequencies[band] * fftSize / sampleRate));
        double sum = 0.0;
        int count = 0;
        for (int bin = juce::jmax (1, centre - 3); bin <= juce::jmin (fftSize / 2, centre + 3); ++bin)
        {
            sum += spectrum[static_cast<size_t> (bin)];
            ++count;
        }
        measured[static_cast<size_t> (band)] = 10.0 * std::log10 (sum / juce::jmax (1, count) + 1.0e-20);
    }

    const auto& target = metaleq::getAnalyzerTarget (analyzerTargetProfile.load (std::memory_order_relaxed));
    const int targetPreset = metaleq::findPresetById (target.presetId);
    const auto& profile = targetPreset >= 0 ? metaleq::getPreset (targetPreset) : metaleq::getPreset (0);
    const auto macros = profile.setsMacros ? profile.macros : neutralMacroSettings();
    const double measuredReference = measured[2];
    const double targetReference = metaleq::responseDb (profile.eq, macros, sampleRate, frequencies[2]);
    for (int band = 0; band < metaleq::kNumBands; ++band)
    {
        const double target = metaleq::responseDb (profile.eq, macros, sampleRate, frequencies[band]);
        const float correction = static_cast<float> (juce::jlimit (-6.0, 6.0,
            (target - targetReference) - (measured[static_cast<size_t> (band)] - measuredReference)));
        analyzerSuggestion[static_cast<size_t> (band)].store (correction, std::memory_order_relaxed);
    }
    analyzerCaptureState.store (AnalyzerCaptureState::complete, std::memory_order_release);
}

void MetalEQAudioProcessor::analyzeResonanceCapture (int sampleCount, double sampleRate)
{
    metaleq::ResonanceAnalysisResult result;
    metaleq::analyseResonances (analyzerCapture.get(), sampleCount, sampleRate, result);

    if (result.framesAnalysed <= 0)
    {
        clearResonanceCandidates();
        analyzerCaptureState.store (AnalyzerCaptureState::failed, std::memory_order_release);
        return;
    }

    const int count = juce::jlimit (0, metaleq::kMaxResonanceCandidates, result.numCandidates);

    for (int i = 0; i < count; ++i)
    {
        const auto& candidate = result.candidates[static_cast<size_t> (i)];
        const auto slot = static_cast<size_t> (i);
        resonanceFreq[slot].store (static_cast<float> (candidate.freq), std::memory_order_relaxed);
        resonanceQ[slot].store (static_cast<float> (candidate.q), std::memory_order_relaxed);
        resonanceProminence[slot].store (static_cast<float> (candidate.prominenceDb), std::memory_order_relaxed);
        resonanceResidual[slot].store (static_cast<float> (candidate.residualDb), std::memory_order_relaxed);
        resonanceConfidence[slot].store (static_cast<float> (candidate.confidence), std::memory_order_relaxed);
        resonanceCut[slot].store (static_cast<float> (candidate.suggestedCutDb), std::memory_order_relaxed);
    }

    for (auto& eligible : resonanceDynamicEligible)
        eligible.store (false, std::memory_order_relaxed);

    for (int i = 0; i < result.numDynamicSuggestions; ++i)
    {
        const auto& suggestion = result.dynamicSuggestions[static_cast<size_t> (i)];
        const int candidateIndex = suggestion.candidateIndex;
        if (candidateIndex < 0 || candidateIndex >= count)
            continue;

        const auto slot = static_cast<size_t> (candidateIndex);
        const auto& band = suggestion.band;
        resonanceDynamicQ[slot].store (static_cast<float> (band.q), std::memory_order_relaxed);
        resonanceDynamicThreshold[slot].store (static_cast<float> (band.thresholdDb),
                                                std::memory_order_relaxed);
        resonanceDynamicRatio[slot].store (static_cast<float> (band.ratio), std::memory_order_relaxed);
        resonanceDynamicAttack[slot].store (static_cast<float> (band.attackMs),
                                            std::memory_order_relaxed);
        resonanceDynamicRelease[slot].store (static_cast<float> (band.releaseMs),
                                             std::memory_order_relaxed);
        resonanceDynamicMaxCut[slot].store (static_cast<float> (band.maxCutDb),
                                            std::memory_order_relaxed);
        resonanceDynamicExcess[slot].store (static_cast<float> (suggestion.measuredExcessDb),
                                            std::memory_order_relaxed);
        resonanceDynamicEligible[slot].store (true, std::memory_order_release);
    }

    resonanceEnoughVariation.store (result.enoughVariation, std::memory_order_relaxed);
    resonanceCandidateCount.store (count, std::memory_order_release);
    resonanceRevision.fetch_add (1, std::memory_order_acq_rel);

    analyzerCaptureState.store (AnalyzerCaptureState::complete, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MetalEQAudioProcessor();
}

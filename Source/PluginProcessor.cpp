#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_dsp/juce_dsp.h>
#include <chrono>

namespace
{
    using namespace metaleq;

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
const char* MetalEQAudioProcessor::ParameterIDs::trim     = "trim";
const char* MetalEQAudioProcessor::ParameterIDs::tight    = "tight";
const char* MetalEQAudioProcessor::ParameterIDs::body     = "body";
const char* MetalEQAudioProcessor::ParameterIDs::scoop    = "scoop";
const char* MetalEQAudioProcessor::ParameterIDs::bite     = "bite";
const char* MetalEQAudioProcessor::ParameterIDs::fizzCut  = "fizzCut";
const char* MetalEQAudioProcessor::ParameterIDs::preset   = "preset";
const char* MetalEQAudioProcessor::ParameterIDs::abCompare = "abCompare";
const char* MetalEQAudioProcessor::ParameterIDs::bypass   = kBypassId;

juce::StringArray MetalEQAudioProcessor::ParameterIDs::allEqParameterIds()
{
    juce::StringArray ids { hpFreq, hpSlope, lpFreq, lpSlope, lsFreq, lsGain, hsFreq, hsGain };

    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        ids.add (bandFreq[i]);
        ids.add (bandGain[i]);
        ids.add (bandQ[i]);
        ids.add (bandOn[i]);
    }

    ids.add (trim);
    ids.add (tight);
    ids.add (body);
    ids.add (scoop);
    ids.add (bite);
    ids.add (fizzCut);

    return ids;
}

//==============================================================================
MetalEQAudioProcessor::MetalEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    apvts = std::make_unique<juce::AudioProcessorValueTreeState> (*this, nullptr, "PARAMS", createParameterLayout());

    analyzerCapture.reset (new float[static_cast<size_t> (maxCaptureSamples)]);
    for (auto& gain : analyzerSuggestion)
        gain.store (0.0f, std::memory_order_relaxed);

    for (auto& level : peakLevels)
        level.store (0.0f, std::memory_order_relaxed);

    engine.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    analyzerWorker = std::thread ([this] { analyzerWorkerLoop(); });
}

MetalEQAudioProcessor::~MetalEQAudioProcessor()
{
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

    //--- preset selector -----------------------------------------------------
    {
        juce::StringArray presetNames { "Custom (Modified)" };

        for (int i = 0; i < metaleq::getNumPresets(); ++i)
            presetNames.add (metaleq::getPreset (i).name);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            ParameterIDs::preset, "Preset", presetNames, 0));
    }

    //--- A/B compare ---------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::abCompare, "A/B Compare", false));

    //--- bypass --------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::bypass, "Bypass", false));

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

    for (auto& level : peakLevels)
        level.store (0.0f, std::memory_order_relaxed);

    juce::ignoreUnused (samplesPerBlock);
}

void MetalEQAudioProcessor::releaseResources()
{
    engine.reset();
}

void MetalEQAudioProcessor::reset()
{
    engine.reset();
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
            analyzerDeferredStartProfile.load (std::memory_order_relaxed), cancelGeneration);
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

    const bool bypassed = apvts->getRawParameterValue (ParameterIDs::bypass)->load() >= 0.5f;
    const bool abCompare = apvts->getRawParameterValue (ParameterIDs::abCompare)->load() >= 0.5f;

    //--- rebuild the chain from the current parameter values -----------------
    auto eq      = readEqSettings();
    auto macros  = readMacroSettings();

    if (abCompare)
    {
        // Level-conscious comparison: identical output trim, no EQ at all, so
        // the only difference the guitarist hears is the EQ itself.
        eq      = neutralEqSettings();
        macros  = neutralMacroSettings();
        eq.trimDb = apvts->getRawParameterValue (ParameterIDs::trim)->load();
    }

    metaleq::ChainSpec chain;
    metaleq::buildChain (chain, eq, macros, getSampleRate());

    // Two milliseconds of coefficient glide makes every change - including a
    // whole preset recall or host automation jump - free of clicks.
    const int rampLength = juce::jmax (2, juce::jmin (numSamples, (int) (getSampleRate() * 0.002)));

    engine.setChain (chain, rampLength);

    if (bypassed)
    {
        // Keep the GUI meter meaningful while bypassed.
        if (meteringEnabled.load (std::memory_order_relaxed))
            for (int ch = 0; ch < numChannels; ++ch)
                peakLevels[static_cast<size_t> (ch)].store (
                    buffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);

        return;
    }

    //--- run the EQ ----------------------------------------------------------
    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples);

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
    juce::ignoreUnused (midiMessages);
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
    juce::ValueTree state ("MetalEQState");

    state.setProperty ("version", 1, nullptr);
    state.setProperty ("lastPresetName", lastPresetName, nullptr);
    state.appendChild (apvts->state, nullptr);

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

    // Our own writes must not be mistaken for user edits.
    applyingPreset.store (true, std::memory_order_relaxed);

    const auto child = tree.getChildWithName ("PARAMS");

    if (child.isValid())
        apvts->state = child;

    lastPresetName = tree.getProperty ("lastPresetName").toString().isNotEmpty()
                       ? tree.getProperty ("lastPresetName").toString()
                       : juce::String ("Custom");

    applyingPreset.store (false, std::memory_order_relaxed);

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
    eq.hpSlope = 0;
    eq.lpSlope = 0;

    for (auto& b : eq.bands)
    {
        b.gainDb = 0.0;
        b.on     = true;
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
    const int safeIndex = juce::jlimit (0, metaleq::getNumPresets(), index);

    applyingPreset.store (true, std::memory_order_relaxed);

    if (safeIndex == 0)
    {
        resetToNeutral();
        setChoiceParameter (ParameterIDs::preset, 0);
        lastPresetName = "Custom";
        applyingPreset.store (false, std::memory_order_relaxed);
        return;
    }

    const auto& preset = metaleq::getPreset (safeIndex);

    applyEqSettings (preset.eq, true);

    if (preset.setsMacros)
        applyMacroSettings (preset.macros);

    setChoiceParameter (ParameterIDs::preset, safeIndex);

    lastPresetName = preset.name;

    applyingPreset.store (false, std::memory_order_relaxed);
}

void MetalEQAudioProcessor::applyShape (int index)
{
    if (index < 0 || index >= metaleq::getNumShapes())
        return;

    const auto& shape = metaleq::getShape (index);

    applyingPreset.store (true, std::memory_order_relaxed);

    applyEqSettings (shape.eq, shape.setsCrossovers);

    if (shape.setsMacros)
        applyMacroSettings (shape.macros);

    // A shape is a curve, not a factory preset: the preset display follows the
    // normal "user edited it" path once the guard is released.
    applyingPreset.store (false, std::memory_order_relaxed);

    // Selecting "Custom" is the honest description of "the EQ now differs from
    // the recalled preset", so do it explicitly.
    if (getSelectedPresetIndex() > 0)
        setChoiceParameter (ParameterIDs::preset, 0);

    lastPresetName = "Custom";
}

void MetalEQAudioProcessor::resetToNeutral()
{
    applyEqSettings (neutralEqSettings(), true);
    applyMacroSettings (neutralMacroSettings());
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
    return getSelectedPresetIndex() == 0;
}

MetalEQAudioProcessor::DisplayState MetalEQAudioProcessor::getDisplayState() const
{
    DisplayState state;

    state.eq      = readEqSettings();
    state.macros  = readMacroSettings();
    state.presetIndex = getSelectedPresetIndex();
    state.presetName = (state.presetIndex > 0) ? juce::String (metaleq::getPreset (state.presetIndex).name)
                                               : juce::String ("Custom");
    state.abCompare  = apvts->getRawParameterValue (ParameterIDs::abCompare)->load() >= 0.5f;
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

double MetalEQAudioProcessor::getResponseDb (double freq) const
{
    return metaleq::responseDb (readEqSettings(), readMacroSettings(), getDisplaySampleRate(), freq);
}

void MetalEQAudioProcessor::startAnalyzerCapture (int targetProfile)
{
    startAnalyzerCaptureWithGeneration (
        targetProfile, analyzerCancelGeneration.load (std::memory_order_acquire));
}

void MetalEQAudioProcessor::startAnalyzerCaptureWithGeneration (
    int targetProfile, unsigned long long cancelGeneration)
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
        analyzerDeferredStartProfile.store (juce::jlimit (0, 3, targetProfile), std::memory_order_relaxed);
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
    analyzerTargetProfile.store (juce::jlimit (0, 3, targetProfile), std::memory_order_relaxed);
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

MetalEQAudioProcessor::AnalyzerStatus MetalEQAudioProcessor::getAnalyzerStatus() const noexcept
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

float MetalEQAudioProcessor::getAnalyzerProgress() const noexcept
{
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
    applyingPreset.store (false, std::memory_order_relaxed);
    setChoiceParameter (ParameterIDs::preset, 0);
    lastPresetName = "Custom";
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
            analyzeCapture (samples, static_cast<double> (analyzerSampleRate.load (std::memory_order_relaxed)));
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

    const int presetIndices[4] = { 0, 6, 7, 13 };
    const auto& profile = metaleq::getPreset (presetIndices[analyzerTargetProfile.load (std::memory_order_relaxed)]);
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

//==============================================================================
//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MetalEQAudioProcessor();
}

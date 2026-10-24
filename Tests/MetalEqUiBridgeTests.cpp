/*
    Tests for the C++ <-> JavaScript UI bridge (metaleq::ui::UiBridge).

    The bridge is the only path through which the React front-end can change
    plugin state, so these tests pin down the guarantees the audio thread and
    the host rely on:

      1. validateParameterWrite() never lets an out-of-range or non-finite
         value through - JavaScript is never authoritative.
      2. handleSetParameter() keeps the legacy low-pass enable mirror in sync
         with the slope selector, which is now the filter's bypass.
      3. applyPreset() uses the canonical v2 mapping: 0 = Custom and
         selector 1..N = factory preset 0..N-1.
      4. The v2 snapshot exposes stable preset ids, taxonomy and analyzer
         targets, and the A/B native functions control the real A/B slots.

    Like MetalEqStateTests, these exercise the real processor and run headless
    (no audio device, no WebView, no message loop) under CTest.
*/

#include <cmath>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include "../Source/PluginProcessor.h"
#include "../Source/dsp/MetalEqGraphAxis.h"
#include "../Source/ui/MetalEqUiBridge.h"

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

void setPlain (MetalEQAudioProcessor& processor, const char* id, float value)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->getNormalisableRange().convertTo0to1 (value));
}

float readPlain (MetalEQAudioProcessor& processor, const char* id)
{
    const auto* parameter = processor.getValueTreeState().getParameter (id);
    return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : -999.0f;
}

/** Builds a juce::Array argument list for a native function call. */
template <typename... Items>
juce::Array<juce::var> makeArgs (Items&&... items)
{
    juce::Array<juce::var> args;
    (args.add (juce::var (std::forward<Items> (items))), ...);
    return args;
}

/** Looks up a native function registered by UiBridge::addOptions and invokes
    it synchronously, capturing its result. Returns false when the function is
    not registered under that name.
*/
bool callNative (metaleq::ui::UiBridge& bridge,
                 const char* name,
                 const juce::Array<juce::var>& args,
                 juce::var& result)
{
    // addOptions() returns by value, so the Options must outlive the lookup:
    // getNativeFunctions() hands back a reference into it.
    const auto options = bridge.addOptions ({});
    const auto& functions = options.getNativeFunctions();

    if (const auto it = functions.find (juce::Identifier { name }); it != functions.end())
    {
        bool completed = false;
        it->second (args, [&] (juce::var v)
        {
            result = std::move (v);
            completed = true;
        });
        return completed;
    }

    return false;
}

bool resultOk (const juce::var& result)
{
    if (auto* obj = result.getDynamicObject())
        return obj->getProperty ("ok").equals (true);
    return false;
}

juce::var getProperty (const juce::var& object, const char* name)
{
    if (auto* obj = object.getDynamicObject())
        return obj->getProperty (name);
    return {};
}
} // namespace

//==============================================================================
static void testValidateParameterWrite (MetalEQAudioProcessor& processor)
{
    auto& apvts = processor.getValueTreeState();
    juce::String error;

    // Unknown ids are rejected outright.
    check (! metaleq::ui::validateParameterWrite (apvts, "notAParameter", 1.0, error).has_value(),
           "validateParameterWrite rejects an unknown parameter id");
    check (error.contains ("notAParameter"), "the rejection message names the bad id");

    // Non-finite numbers are rejected - a NaN must never reach the DSP.
    check (! metaleq::ui::validateParameterWrite (apvts, "lsGain",
                std::numeric_limits<double>::quiet_NaN(), error).has_value(),
           "validateParameterWrite rejects NaN");
    check (! metaleq::ui::validateParameterWrite (apvts, "lsGain",
                std::numeric_limits<double>::infinity(), error).has_value(),
           "validateParameterWrite rejects infinity");

    // Out-of-range values are clamped into the parameter's range.
    const auto& lsGainRange = apvts.getParameter ("lsGain")->getNormalisableRange();
    const auto high = metaleq::ui::validateParameterWrite (apvts, "lsGain", 999.0, error);
    check (high.has_value(), "an over-range gain still validates");
    if (high.has_value())
        checkNear (*high, lsGainRange.end, 1.0e-6, "an over-range gain clamps to the range maximum");

    const auto low = metaleq::ui::validateParameterWrite (apvts, "lsGain", -999.0, error);
    check (low.has_value(), "an under-range gain still validates");
    if (low.has_value())
        checkNear (*low, lsGainRange.start, 1.0e-6, "an under-range gain clamps to the range minimum");

    // Booleans snap to 0 or 1.
    const auto boolHigh = metaleq::ui::validateParameterWrite (apvts, "lpOn", 0.75, error);
    check (boolHigh.has_value() && *boolHigh == 1.0, "a boolean above 0.5 normalises to 1");
    const auto boolLow = metaleq::ui::validateParameterWrite (apvts, "lpOn", 0.25, error);
    check (boolLow.has_value() && *boolLow == 0.0, "a boolean below 0.5 normalises to 0");

    // Choice parameters round to the nearest index and clamp to the list.
    const auto choice = metaleq::ui::validateParameterWrite (apvts, "lpSlope", 99.0, error);
    check (choice.has_value(), "an over-range slope index still validates");
    if (choice.has_value())
        checkNear (*choice, 3.0, 1.0e-6, "an over-range slope index clamps to the last choice");
}

//==============================================================================
static void testSetParameterLpSlopeSyncsEnable (MetalEQAudioProcessor& processor)
{
    metaleq::ui::UiBridge bridge (processor);

    // The low-pass slope selector is its bypass. Selecting a slope must clear
    // or set the legacy lpOn mirror so saved state stays coherent, while the
    // filter itself follows the slope.
    setPlain (processor, "lpSlope", 0.0f);
    setPlain (processor, "lpOn", 0.0f);
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 0,
           "the low-pass slope starts at Off for the slope test");
    check (! processor.getDisplayState().eq.lpEnabled,
           "Off slope keeps the low-pass bypassed");

    juce::var result;
    const bool completed = callNative (bridge, "setParameter", makeArgs ("lpSlope", 2.0), result);
    check (completed, "setParameter is registered with the bridge");
    check (resultOk (result), "selecting a low-pass slope succeeds");
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 2,
           "the selected low-pass slope is stored");
    check (readPlain (processor, "lpOn") > 0.5f,
           "selecting a low-pass slope enables the legacy mirror");
    check (processor.getDisplayState().eq.lpEnabled,
           "the low-pass is audible from the slope alone");

    check (callNative (bridge, "setParameter", makeArgs ("lpSlope", 0.0), result),
           "setParameter can bypass the low-pass");
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 0,
           "Off is stored as the low-pass slope");
    check (readPlain (processor, "lpOn") < 0.5f,
           "Off clears the legacy low-pass enable mirror");
    check (! processor.getDisplayState().eq.lpEnabled,
           "Off slope bypasses the low-pass again");

    // The legacy parameter is still automatable but no longer gates the filter
    // or forces a slope; the visible selector is the single source of truth.
    check (callNative (bridge, "setParameter", makeArgs ("lpOn", 1.0), result),
           "the legacy low-pass enable parameter still accepts writes");
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 0,
           "writing the legacy enable does not change the slope");
    check (! processor.getDisplayState().eq.lpEnabled,
           "writing the legacy enable does not make the low-pass audible");

    // A non-finite write is rejected and leaves the parameter untouched.
    setPlain (processor, "lsGain", 3.0f);
    check (callNative (bridge, "setParameter",
                       makeArgs ("lsGain", std::numeric_limits<double>::quiet_NaN()), result),
           "setParameter handles a NaN value");
    check (! resultOk (result), "a NaN parameter write is rejected");
    checkNear (readPlain (processor, "lsGain"), 3.0, 1.0e-4,
               "a rejected NaN write leaves the gain unchanged");

    // An out-of-range write is clamped, not rejected.
    check (callNative (bridge, "setParameter", makeArgs ("lsGain", 999.0), result),
           "setParameter handles an over-range value");
    check (resultOk (result), "an over-range write is accepted after clamping");
    const auto& lsGainRange = processor.getValueTreeState().getParameter ("lsGain")->getNormalisableRange();
    checkNear (readPlain (processor, "lsGain"), lsGainRange.end, 1.0e-3,
               "an over-range write lands on the clamped value");
}

//==============================================================================
static void testSectionMonitorExclusivity (MetalEQAudioProcessor& processor)
{
    metaleq::ui::UiBridge bridge (processor);
    setPlain (processor, "resAudition", 0.0f);
    setPlain (processor, "dynAudition", 0.0f);

    juce::var result;
    check (callNative (bridge, "setParameter", makeArgs ("resAudition", 1.0), result),
           "setParameter can enable the surgical delta monitor");
    check (resultOk (result), "enabling the surgical delta monitor succeeds");
    check (readPlain (processor, "resAudition") > 0.5f,
           "the surgical delta monitor is on");
    check (readPlain (processor, "dynAudition") < 0.5f,
           "enabling the surgical monitor clears the dynamic monitor");

    check (callNative (bridge, "setParameter", makeArgs ("dynAudition", 1.0), result),
           "setParameter can enable the dynamic delta monitor");
    check (resultOk (result), "enabling the dynamic delta monitor succeeds");
    check (readPlain (processor, "dynAudition") > 0.5f,
           "the dynamic delta monitor is on");
    check (readPlain (processor, "resAudition") < 0.5f,
           "enabling the dynamic monitor clears the surgical monitor");

    check (callNative (bridge, "setParameter", makeArgs ("dynAudition", 0.0), result),
           "setParameter can disable the dynamic delta monitor");
    check (resultOk (result), "disabling the dynamic delta monitor succeeds");
    check (readPlain (processor, "dynAudition") < 0.5f,
           "the dynamic delta monitor is off");
    check (readPlain (processor, "resAudition") < 0.5f,
           "disabling the dynamic monitor leaves the surgical monitor off");
}

//==============================================================================
static void testApplyPresetIndexMapping (MetalEQAudioProcessor& processor)
{
    metaleq::ui::UiBridge bridge (processor);

    // Canonical v2 mapping: index 0 is Custom, factory preset i is index i+1.
    for (int reported = 1; reported <= metaleq::getNumPresets(); ++reported)
    {
        processor.applyPreset (0);
        juce::var result;
        check (callNative (bridge, "applyPreset", makeArgs (static_cast<double> (reported)), result),
               "applyPreset is registered with the bridge");
        check (resultOk (result), "applyPreset reports success");

        const auto& expected = metaleq::getPreset (reported - 1);
        check (processor.getLastPresetName() == expected.name,
               "applyPreset loads the factory preset at the canonical index");
        check (processor.getLastPresetId() == expected.id,
               "applyPreset reports the stable factory id");
        checkNear (readPlain (processor, "lpFreq"), expected.eq.lpFreq, 1.0e-3,
                   "applyPreset loads the same low-pass cutoff as the indexed factory preset");
    }

    // Index 0 is "Custom": it resets to neutral rather than loading a factory
    // preset, and must not throw the bridge out of sync.
    setPlain (processor, "lsGain", 6.0f);
    juce::var result;
    check (callNative (bridge, "applyPreset", makeArgs (0.0), result), "applyPreset handles index 0");
    check (resultOk (result), "applying the Custom preset succeeds");
    checkNear (readPlain (processor, "lsGain"), 0.0, 1.0e-4,
               "applying the Custom preset resets the gain to neutral");

    // Stable-id recall is the project-safe path.
    processor.applyPreset (0);
    check (callNative (bridge, "applyPresetById", makeArgs (juce::String (metaleq::getPreset (7).id)), result),
           "applyPresetById is registered with the bridge");
    check (resultOk (result), "applyPresetById succeeds");
    check (processor.getLastPresetId() == metaleq::getPreset (7).id,
           "applyPresetById recalls the matching preset");
    check (callNative (bridge, "applyPresetById", makeArgs (juce::String ("missing")), result),
           "applyPresetById handles an unknown id");
    check (! resultOk (result), "applyPresetById rejects an unknown id");

    // A missing or non-numeric index is rejected, not silently defaulted.
    check (callNative (bridge, "applyPreset", juce::Array<juce::var> {}, result), "applyPreset handles no argument");
    check (! resultOk (result), "applyPreset rejects a missing index");
    check (callNative (bridge, "applyPreset", makeArgs (juce::String { "x" }), result),
           "applyPreset handles a non-numeric index");
    check (! resultOk (result), "applyPreset rejects a non-numeric index");
}

//==============================================================================
static void testCurveRangeIsFixed (MetalEQAudioProcessor& processor)
{
    // Create a response that reaches well outside both graph edges: a steep
    // high-pass drops the low end far below -15 dB, while stacked boosts push
    // the midrange above +15 dB. The bridge must still report a fixed window.
    setPlain (processor, "hpSlope", 3.0f);
    setPlain (processor, "hpFreq", 180.0f);
    setPlain (processor, "lsGain", 12.0f);
    setPlain (processor, "hsGain", 12.0f);

    const char* const gainIds[] = { "band1Gain", "band2Gain", "band3Gain", "band4Gain", "band5Gain" };
    const char* const freqIds[] = { "band1Freq", "band2Freq", "band3Freq", "band4Freq", "band5Freq" };
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        setPlain (processor, freqIds[i], 1000.0f);
        setPlain (processor, gainIds[i], 12.0f);
    }

    check (processor.getResponseDb (20.0) < metaleq::graph::kGraphMinDb,
           "the test response extends below the graph floor");
    check (processor.getResponseDb (1000.0) > metaleq::graph::kGraphMaxDb,
           "the test response extends above the graph ceiling");

    metaleq::ui::UiBridge bridge (processor);
    juce::var snapshot;
    check (callNative (bridge, "uiReady", juce::Array<juce::var> {}, snapshot),
           "uiReady is registered for the fixed curve-range test");

    const auto curve = getProperty (snapshot, "curve");
    check (curve.isObject(), "snapshot contains the response curve object");
    checkNear (static_cast<double> (getProperty (curve, "minDb")),
               metaleq::graph::kGraphMinDb, 1.0e-12,
               "curve metadata keeps the fixed -15 dB graph floor");
    checkNear (static_cast<double> (getProperty (curve, "maxDb")),
               metaleq::graph::kGraphMaxDb, 1.0e-12,
               "curve metadata keeps the fixed +15 dB graph ceiling");

    const auto points = getProperty (curve, "points");
    check (points.isArray() && points.size() == metaleq::ui::UiBridge::numCurvePoints,
           "curve contains the expected number of response points");
}

//==============================================================================
static void testSnapshotV7 (MetalEQAudioProcessor& processor)
{
    metaleq::ui::UiBridge bridge (processor);
    juce::var snapshot;
    check (callNative (bridge, "uiReady", juce::Array<juce::var> {}, snapshot),
           "uiReady is registered with the bridge");

    check (static_cast<int> (getProperty (snapshot, "protocolVersion")) == 7,
           "snapshot advertises protocol version 7");

    const auto resonance = getProperty (snapshot, "resonance");
    check (resonance.isObject(), "snapshot contains the V3 resonance state");

    const auto surgical = getProperty (resonance, "surgical");
    const auto dynamic = getProperty (resonance, "dynamic");
    check (surgical.isObject(), "resonance state contains the surgical scan");
    check (dynamic.isObject(), "resonance state contains the dynamic scan");
    check (getProperty (surgical, "status").isInt(), "surgical scan carries a status");
    check (getProperty (dynamic, "status").isInt(), "dynamic scan carries a status");
    check (getProperty (surgical, "candidates").isArray(),
           "surgical scan carries a candidate list");
    check (getProperty (dynamic, "candidates").isArray(),
           "dynamic scan carries a dynamic-suggestion list");
    check (getProperty (surgical, "enoughVariation").isBool(),
           "surgical scan reports whether the capture varied enough");
    check (getProperty (dynamic, "enoughVariation").isBool(),
           "dynamic scan reports whether the capture varied enough");

    const auto params = getProperty (snapshot, "params");
    check (params.isObject(), "snapshot contains parameter values");
    check (getProperty (params, "res1Freq").isDouble(), "snapshot exposes surgical frequency parameters");
    check (getProperty (params, "res6On").isDouble(), "snapshot exposes surgical enable parameters");
    check (getProperty (params, "dyn1Freq").isDouble(), "snapshot exposes dynamic frequency parameters");
    check (getProperty (params, "dyn4Range").isDouble(), "snapshot exposes dynamic maximum-cut parameters");
    check (getProperty (params, "dyn4On").isDouble(), "snapshot exposes dynamic enable parameters");
    check (getProperty (params, "resAudition").isDouble(),
           "snapshot exposes the surgical delta monitor");
    check (getProperty (params, "dynAudition").isDouble(),
           "snapshot exposes the dynamic delta monitor");

    const auto dynamicReduction = getProperty (snapshot, "dynamicReduction");
    check (dynamicReduction.isArray() && dynamicReduction.size() == metaleq::kNumDynamicBands,
           "snapshot exposes one dynamic gain-reduction value per band");

    juce::var result;
    check (callNative (bridge, "startResonanceScan", makeArgs (false), result),
           "startResonanceScan is registered for the surgical scan");
    check (resultOk (result), "startResonanceScan starts the surgical scan");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "the surgical scan reports capturing");

    check (callNative (bridge, "cancelResonanceScan", makeArgs (false), result),
           "cancelResonanceScan is registered for the surgical scan");
    check (resultOk (result), "cancelResonanceScan returns to idle");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::idle,
           "cancelling the scan clears its status");

    // Regression: cancelling used to leave the capture in cancelPending, so a
    // second start was silently rejected.
    check (callNative (bridge, "startResonanceScan", makeArgs (false), result),
           "startResonanceScan can restart after a cancelled capture");
    check (resultOk (result), "restarting the surgical scan succeeds");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "the restarted surgical scan reports capturing");

    check (callNative (bridge, "startResonanceScan", makeArgs (true), result),
           "the dynamic scan can start while the surgical capture is pending");
    check (resultOk (result), "starting the dynamic scan succeeds");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::idle,
           "starting the dynamic scan replaces the unfinished surgical capture");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::dynamic)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "the dynamic scan reports capturing independently");

    check (callNative (bridge, "cancelResonanceScan", makeArgs (false), result),
           "stopping the idle surgical scan is accepted");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::dynamic)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "stopping one section does not cancel the other section's capture");

    check (callNative (bridge, "cancelResonanceScan", makeArgs (true), result),
           "cancelResonanceScan stops the dynamic scan");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::dynamic)
               == MetalEQAudioProcessor::AnalyzerStatus::idle,
           "cancelling the dynamic scan leaves it idle");

    check (callNative (bridge, "startResonanceScan", makeArgs (true), result),
           "the dynamic scan can restart after its own cancelled capture");
    check (resultOk (result), "restarting the dynamic scan succeeds");
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::dynamic)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "the restarted dynamic scan reports capturing");

    check (callNative (bridge, "cancelResonanceScan", makeArgs (true), result),
           "the restarted dynamic scan can be stopped again");

    check (callNative (bridge, "applyResonanceSelection", makeArgs (juce::Array<juce::var> {}), result),
           "applyResonanceSelection is registered");
    check (resultOk (result), "applying an empty selection is a no-op success");
    check (callNative (bridge, "clearSurgicalBands", juce::Array<juce::var> {}, result),
           "clearSurgicalBands is registered");
    check (resultOk (result), "clearing flat surgical bands succeeds");
    check (callNative (bridge, "applyResonanceToDynamic", makeArgs (juce::Array<juce::var> {}), result),
           "applyResonanceToDynamic is registered");
    check (resultOk (result), "applying an empty dynamic selection is a no-op success");
    check (callNative (bridge, "clearDynamicBands", juce::Array<juce::var> {}, result),
           "clearDynamicBands is registered");
    check (resultOk (result), "clearing flat dynamic bands succeeds");

    setPlain (processor, "dyn1Range", 5.0f);
    setPlain (processor, "dyn1On", 1.0f);
    check (readPlain (processor, "dyn1On") > 0.5f, "a dynamic test band is enabled before clearing");
    check (callNative (bridge, "clearDynamicBands", juce::Array<juce::var> {}, result),
           "clearDynamicBands remains registered after a band is active");
    check (resultOk (result), "clearing active dynamic bands succeeds");
    check (readPlain (processor, "dyn1On") < 0.5f, "clearDynamicBands disables every dynamic band");
    check (readPlain (processor, "dyn1Range") < 1.0e-3f, "clearDynamicBands zeroes every maximum cut");

    const auto presets = getProperty (snapshot, "factoryPresets");
    check (presets.isArray() && presets.size() == metaleq::getNumPresets(),
           "snapshot contains every factory preset");
    if (presets.isArray() && presets.size() > 0)
    {
        const auto first = presets[0];
        check (getProperty (first, "index").equals (1), "factory snapshot indices start at 1");
        check (getProperty (first, "id").toString().isNotEmpty(), "factory snapshot carries stable id");
        check (getProperty (first, "genre").toString().isNotEmpty(), "factory snapshot carries genre");
        check (getProperty (first, "subgenre").toString().isNotEmpty(), "factory snapshot carries subgenre");
    }

    const auto targets = getProperty (snapshot, "analyzerTargets");
    check (targets.isArray() && targets.size() == metaleq::tone::getNumToneTargets(),
           "snapshot contains analyzer targets");
    if (targets.isArray() && targets.size() > 0)
    {
        check (getProperty (targets[0], "id").toString() == metaleq::tone::getToneTarget (0).id,
               "analyzer target carries its stable id");
        check (getProperty (targets[0], "categoryLabel").toString().isNotEmpty(),
               "analyzer target carries its category");
        check (getProperty (targets[0], "objective").toString().isNotEmpty(),
               "analyzer target carries a tonal objective");
        check (getProperty (targets[0], "confidence").isDouble(),
               "analyzer target carries a confidence");
        const auto sources = getProperty (targets[0], "sources");
        check (sources.isArray() && sources.size() > 0,
               "analyzer target carries its research sources");
        if (sources.isArray() && sources.size() > 0)
            check (getProperty (sources[0], "url").toString().startsWith ("http"),
                   "target source carries a URL");
    }

    check (callNative (bridge, "setAnalyzerTarget", makeArgs (2.0), result),
           "setAnalyzerTarget is registered");
    check (resultOk (result), "setAnalyzerTarget succeeds");
    check (processor.getAnalyzerTargetIndex() == 2,
           "setAnalyzerTarget updates the selected destination");

    check (callNative (bridge, "openExternalUrl", makeArgs ("file:///etc/passwd"), result),
           "openExternalUrl is registered");
    check (! resultOk (result), "openExternalUrl refuses non-http(s) URLs");

    const auto preset = getProperty (snapshot, "preset");
    check (getProperty (preset, "abSlot").isInt(), "preset snapshot carries the active A/B slot");
    check (getProperty (preset, "presetId").isString(), "preset snapshot carries the stable id field");

    check (callNative (bridge, "setAbSlot", makeArgs (1.0), result), "setAbSlot is registered");
    check (resultOk (result), "setAbSlot succeeds");
    check (processor.getActiveAbSlot() == 1, "setAbSlot switches to B");

    check (callNative (bridge, "copyAbToOther", juce::Array<juce::var> {}, result),
           "copyAbToOther is registered");
    check (resultOk (result), "copyAbToOther succeeds");
    check (callNative (bridge, "swapAbSlots", juce::Array<juce::var> {}, result),
           "swapAbSlots is registered");
    check (resultOk (result), "swapAbSlots succeeds");
    check (callNative (bridge, "setAbSlot", makeArgs (0.0), result), "setAbSlot handles A");
    check (resultOk (result), "setAbSlot returns to A");
    check (processor.getActiveAbSlot() == 0, "setAbSlot switches back to A");
}

//==============================================================================
static void testResonanceRestartDuringLiveCapture (MetalEQAudioProcessor& processor)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor.prepareToPlay (sampleRate, blockSize);

    metaleq::ui::UiBridge bridge (processor);
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            buffer.setSample (channel, sample,
                0.1f * std::sin (juce::MathConstants<float>::twoPi
                                 * 440.0f * static_cast<float> (sample) / static_cast<float> (sampleRate)));
    juce::MidiBuffer midi;

    juce::var result;
    check (callNative (bridge, "startResonanceScan", makeArgs (false), result),
           "the live-capture test starts a surgical scan");
    processor.processBlock (buffer, midi);
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "an audio callback moves the surgical scan into its capture");
    check (processor.getResonanceProgress (MetalEQAudioProcessor::ResonanceScanTarget::surgical) > 0.0f,
           "the live surgical capture reports progress");

    check (callNative (bridge, "cancelResonanceScan", makeArgs (false), result),
           "the live surgical scan can be stopped mid-capture");
    processor.processBlock (buffer, midi);
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::idle,
           "the cancelled live capture returns to idle on the next callback");

    check (callNative (bridge, "startResonanceScan", makeArgs (false), result),
           "the surgical scan restarts after a cancelled live capture");
    check (resultOk (result), "the live restart succeeds");
    processor.processBlock (buffer, midi);
    check (processor.getResonanceStatus (MetalEQAudioProcessor::ResonanceScanTarget::surgical)
               == MetalEQAudioProcessor::AnalyzerStatus::capturing,
           "the restarted live scan reports capturing");
    check (processor.getResonanceProgress (MetalEQAudioProcessor::ResonanceScanTarget::surgical) > 0.0f,
           "the restarted live scan reports new progress");

    check (callNative (bridge, "cancelResonanceScan", makeArgs (false), result),
           "the restarted live scan can be stopped");
    processor.processBlock (buffer, midi);
    processor.releaseResources();
}

//==============================================================================
static void testAnalyzerModelIntegration (MetalEQAudioProcessor& processor)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const double t = static_cast<double> (sample) / sampleRate;
            double value = 0.0;
            for (const auto freq : { 90.0, 175.0, 350.0, 700.0, 1400.0, 2800.0, 5600.0, 9500.0 })
                value += 0.012 * std::sin (2.0 * metaleq::kPi * freq * t);
            buffer.setSample (channel, sample, static_cast<float> (value));
        }
    }
    juce::MidiBuffer midi;

    processor.startAnalyzerCapture (0);

    for (int block = 0; block < 900; ++block)
    {
        processor.processBlock (buffer, midi);
        const auto status = processor.getAnalyzerStatus();
        if (status == MetalEQAudioProcessor::AnalyzerStatus::ready
            || status == MetalEQAudioProcessor::AnalyzerStatus::failed)
            break;
    }

    for (int attempt = 0; attempt < 200; ++attempt)
    {
        const auto status = processor.getAnalyzerStatus();
        if (status == MetalEQAudioProcessor::AnalyzerStatus::ready
            || status == MetalEQAudioProcessor::AnalyzerStatus::failed)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    check (processor.getAnalyzerStatus() == MetalEQAudioProcessor::AnalyzerStatus::ready,
           "a synthetic capture produces a ready analyzer report");
    check (processor.getAnalyzerResultTarget() == 0,
           "the report records the target it was computed for");

    MetalEQAudioProcessor::AnalyzerReport first;
    check (processor.getAnalyzerReport (first), "the analyzer report is available");
    check (first.valid && first.inputConfidence > 0.0f,
           "the report carries a measured input profile");

    double maxGain = 0.0;
    for (const auto gain : first.gains)
    {
        maxGain = std::max (maxGain, std::abs (static_cast<double> (gain)));
        check (std::isfinite (gain) && std::abs (gain) <= 6.0f,
               "the integrated suggestion stays inside the safety cap");
    }
    check (maxGain <= 6.0, "the integrated analyzer respects the global gain cap");
    check (! first.alreadyClose || maxGain < 0.25,
           "an already-close report is flagged rather than over-corrected");

    const auto revisionBefore = processor.getAnalyzerRevision();
    processor.setAnalyzerTarget (5); // ...And Justice for All
    check (processor.getAnalyzerTargetIndex() == 5,
           "the target switch updates the selected destination");
    check (processor.getAnalyzerResultTarget() == 5,
           "the recommendation is recalculated for the new destination");
    check (processor.getAnalyzerRevision() > revisionBefore,
           "the target switch publishes a new analyzer revision");

    MetalEQAudioProcessor::AnalyzerReport second;
    check (processor.getAnalyzerReport (second), "the recalculated report is available");
    check (second.target != first.target,
           "the target model changed with the destination");

    processor.applyAnalyzerSuggestion (1.0f);
    checkNear (readPlain (processor, "band1Gain"), second.gains[0], 0.05,
               "Apply correction writes the first band gain");
    checkNear (readPlain (processor, "band2Gain"), second.gains[1], 0.05,
               "Apply correction writes the second band gain");
    checkNear (static_cast<double> (readPlain (processor, "band1Freq")), 120.0, 0.01,
               "Apply correction uses the analyzer's 120 Hz band");

    processor.releaseResources();
}

//==============================================================================
int main()
{
    {
        MetalEQAudioProcessor processor;
        testValidateParameterWrite (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testSetParameterLpSlopeSyncsEnable (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testSectionMonitorExclusivity (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testApplyPresetIndexMapping (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testCurveRangeIsFixed (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testSnapshotV7 (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testResonanceRestartDuringLiveCapture (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testAnalyzerModelIntegration (processor);
    }

    if (failures == 0)
        std::cout << "All MetalEqUiBridge tests passed.\n";
    else
        std::cerr << failures << " MetalEqUiBridge check(s) failed.\n";

    return failures == 0 ? 0 : 1;
}

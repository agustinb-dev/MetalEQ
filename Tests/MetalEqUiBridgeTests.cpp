/*
    Tests for the C++ <-> JavaScript UI bridge (metaleq::ui::UiBridge).

    The bridge is the only path through which the React front-end can change
    plugin state, so these tests pin down the guarantees the audio thread and
    the host rely on:

      1. validateParameterWrite() never lets an out-of-range or non-finite
         value through - JavaScript is never authoritative.
      2. handleSetParameter() reproduces the native low-pass enable side
         effect (enabling while the slope is Off jumps it to 24 dB/oct).
      3. applyPreset() uses the canonical v2 mapping: 0 = Custom and
         selector 1..N = factory preset 0..N-1.
      4. The v2 snapshot exposes stable preset ids, taxonomy and analyzer
         targets, and the A/B native functions control the real A/B slots.

    Like MetalEqStateTests, these exercise the real processor and run headless
    (no audio device, no WebView, no message loop) under CTest.
*/

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
static void testSetParameterLpSideEffect (MetalEQAudioProcessor& processor)
{
    metaleq::ui::UiBridge bridge (processor);

    // Match the native toggle: enabling the low-pass while the slope is Off
    // must jump the slope to 24 dB/oct (choice index 2 of 0..3) so the filter
    // is audible immediately.
    setPlain (processor, "lpSlope", 0.0f);
    setPlain (processor, "lpOn", 0.0f);
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 0,
           "the low-pass slope starts at Off for the side-effect test");

    juce::var result;
    const bool completed = callNative (bridge, "setParameter", makeArgs ("lpOn", 1.0), result);
    check (completed, "setParameter is registered with the bridge");
    check (resultOk (result), "enabling the low-pass succeeds");
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 2,
           "enabling the low-pass while the slope is Off jumps it to 24 dB/oct");

    // Turning it back off must leave the slope alone.
    check (callNative (bridge, "setParameter", makeArgs ("lpOn", 0.0), result), "setParameter runs again");
    check (static_cast<int> (readPlain (processor, "lpSlope")) == 2,
           "disabling the low-pass does not reset the slope");

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
static void testSnapshotV2 (MetalEQAudioProcessor& processor)
{
    metaleq::ui::UiBridge bridge (processor);
    juce::var snapshot;
    check (callNative (bridge, "uiReady", juce::Array<juce::var> {}, snapshot),
           "uiReady is registered with the bridge");

    check (static_cast<int> (getProperty (snapshot, "protocolVersion")) == 2,
           "snapshot advertises protocol version 2");

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
    check (targets.isArray() && targets.size() == metaleq::getNumAnalyzerTargets(),
           "snapshot contains analyzer targets");
    if (targets.isArray() && targets.size() > 0)
        check (getProperty (targets[0], "presetId").toString() == metaleq::getAnalyzerTarget (0).presetId,
               "analyzer target carries its stable preset id");

    const auto preset = getProperty (snapshot, "preset");
    check (getProperty (preset, "abSlot").isInt(), "preset snapshot carries the active A/B slot");
    check (getProperty (preset, "presetId").isString(), "preset snapshot carries the stable id field");

    juce::var result;
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
int main()
{
    {
        MetalEQAudioProcessor processor;
        testValidateParameterWrite (processor);
    }

    {
        MetalEQAudioProcessor processor;
        testSetParameterLpSideEffect (processor);
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
        testSnapshotV2 (processor);
    }

    if (failures == 0)
        std::cout << "All MetalEqUiBridge tests passed.\n";
    else
        std::cerr << failures << " MetalEqUiBridge check(s) failed.\n";

    return failures == 0 ? 0 : 1;
}

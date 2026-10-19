/*
    Tests for the C++ <-> JavaScript UI bridge (metaleq::ui::UiBridge).

    The bridge is the only path through which the React front-end can change
    plugin state, so these tests pin down the guarantees the audio thread and
    the host rely on:

      1. validateParameterWrite() never lets an out-of-range or non-finite
         value through - JavaScript is never authoritative.
      2. handleSetParameter() reproduces the native low-pass enable side
         effect (enabling while the slope is Off jumps it to 24 dB/oct), so
         the web UI and the preserved native UI behave identically.
      3. applyPreset() mirrors the native combo's index mapping exactly,
         including its shared off-by-one quirk, so a preset chosen in the web
         UI loads the same settings as the same choice in the native UI.

    Like MetalEqStateTests, these exercise the real processor and run headless
    (no audio device, no WebView, no message loop) under CTest.
*/

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "../Source/PluginProcessor.h"
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
}

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

    // The web UI reports factory preset i (0-based) as index i+1 because index
    // 0 is reserved for "Custom". handleApplyPreset must pass that number
    // straight through to the processor, which reads getPreset(safeIndex) - the
    // same +1 shift the native combo box applies. This is a shared quirk: the
    // name shown for a factory slot is off by one from the values it loads,
    // identically in both UIs. These checks pin that mapping down so the web
    // UI can never drift from the native one.
    for (int reported = 1; reported < metaleq::getNumPresets() + 2; ++reported)
    {
        processor.applyPreset (0);
        juce::var result;
        check (callNative (bridge, "applyPreset", makeArgs (static_cast<double> (reported)), result),
               "applyPreset is registered with the bridge");
        check (resultOk (result), "applyPreset reports success");

        // Reproduce what the processor does with the same index and compare a
        // value the preset actually changes.
        const int expectedIndex = juce::jlimit (0, metaleq::getNumPresets(), reported);
        const auto& expected = metaleq::getPreset (juce::jlimit (0, metaleq::getNumPresets() - 1, expectedIndex));
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

    // A missing or non-numeric index is rejected, not silently defaulted.
    check (callNative (bridge, "applyPreset", juce::Array<juce::var> {}, result), "applyPreset handles no argument");
    check (! resultOk (result), "applyPreset rejects a missing index");
    check (callNative (bridge, "applyPreset", makeArgs (juce::String { "x" }), result),
           "applyPreset handles a non-numeric index");
    check (! resultOk (result), "applyPreset rejects a non-numeric index");
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

    if (failures == 0)
        std::cout << "All MetalEqUiBridge tests passed.\n";
    else
        std::cerr << failures << " MetalEqUiBridge check(s) failed.\n";

    return failures == 0 ? 0 : 1;
}

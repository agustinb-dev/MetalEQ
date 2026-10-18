#include <cmath>
#include <cstdlib>
#include <iostream>
#include <chrono>

#include "../Source/PluginProcessor.h"

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
}

int main()
{
    MetalEQAudioProcessor processor;
    const auto token = juce::String (static_cast<juce::int64> (
        std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const auto presetA = "EQ Roundtrip " + token;
    const auto presetB = "EQ Renamed " + token;
    const auto defaultPreset = "EQ Default LP " + token;
    juce::String error;

    // Save at untouched defaults first: APVTS may have a PARAMS child whose
    // value property has not yet been materialized for a default parameter.
    check (processor.saveUserPreset (defaultPreset, false, error), "untouched defaults save as a user preset");
    const auto presetDirectory = processor.getUserPresetDirectory();
    const auto defaultFile = presetDirectory.getChildFile (
        juce::File::createLegalFileName (defaultPreset) + ".metaleq");
    const auto defaultXml = juce::XmlDocument::parse (defaultFile);
    bool defaultLowPassWasSerialized = false;
    if (defaultXml != nullptr)
    {
        const auto defaultTree = juce::ValueTree::fromXml (*defaultXml).getChildWithName ("PARAMS");
        for (int i = 0; i < defaultTree.getNumChildren(); ++i)
        {
            const auto child = defaultTree.getChild (i);
            if (child.getProperty ("id").toString() == MetalEQAudioProcessor::ParameterIDs::lpOn)
                defaultLowPassWasSerialized = child.hasProperty ("value")
                    && child.getProperty ("value").toString() == "1.000000000";
        }
    }
    check (defaultLowPassWasSerialized, "untouched default lpOn is serialized with an explicit value");
    check (processor.deleteUserPreset (defaultPreset, error), "default serialization fixture is removed");

    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandFreq[2], 2137.0f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandGain[2], -3.25f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpOn, 1.0f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::shapeOn[3], 1.0f);
    check (processor.saveUserPreset (presetA, false, error), "user preset saves");
    check (error.isEmpty(), "successful save has no error");

    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandFreq[2], 500.0f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandGain[2], 2.0f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpOn, 0.0f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::shapeOn[3], 0.0f);
    const bool loadedPreset = processor.loadUserPreset (presetA, error);
    if (! loadedPreset)
        std::cerr << "User preset load error: " << error << '\n';
    check (loadedPreset, "user preset loads");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandFreq[2]) - 2137.0f) < 1.0f,
           "preset round trip restores frequency");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandGain[2]) + 3.25f) < 0.02f,
           "preset round trip restores gain");
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpOn) > 0.5f,
           "preset round trip restores low-pass enable");
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::shapeOn[3]) > 0.5f,
           "preset round trip restores independent shape state");

    check (! processor.saveUserPreset (presetA, false, error), "duplicate save is rejected without overwrite");
    check (processor.saveUserPreset (presetA, true, error), "explicit overwrite succeeds");
    check (processor.renameUserPreset (presetA, presetB, error), "user preset rename succeeds");
    check (processor.getUserPresetNames().contains (presetB), "renamed preset is listed");

    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::shapeOn[3], 1.0f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::shapeOn[3], 0.0f);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::shapeOn[3]) > 0.5f,
           "DAW state round trip restores shape toggle");

    check (! processor.saveUserPreset (metaleq::getPreset (0).name, true, error),
           "factory presets cannot be overwritten by user preset operations");
    check (! processor.deleteUserPreset (metaleq::getPreset (0).name, error),
           "factory presets cannot be deleted");
    check (processor.deleteUserPreset (presetB, error), "user preset deletion succeeds");

    const auto invalidName = "EQ Invalid " + token;
    presetDirectory.createDirectory();
    const auto invalidFile = presetDirectory.getChildFile (
        juce::File::createLegalFileName (invalidName) + ".metaleq");
    check (invalidFile.replaceWithText ("<NotAMetalEQPreset />"), "invalid-preset fixture is written");
    check (! processor.loadUserPreset (invalidName, error), "malformed user preset is rejected");
    invalidFile.deleteFile();

    const auto legacyName = "EQ Legacy LP " + token;
    const auto legacyFile = presetDirectory.getChildFile (
        juce::File::createLegalFileName (legacyName) + ".metaleq");
    const auto makeLegacyFixture = [&] (const juce::String& slope)
    {
        check (processor.saveUserPreset (legacyName, true, error), "legacy fixture base preset saves");
        const auto xml = juce::XmlDocument::parse (legacyFile);
        if (xml == nullptr)
            return false;
        auto root = juce::ValueTree::fromXml (*xml);
        auto params = root.getChildWithName ("PARAMS");
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            auto child = params.getChild (i);
            const auto id = child.getProperty ("id").toString();
            if (id == MetalEQAudioProcessor::ParameterIDs::lpOn)
                child.setProperty ("value", "", nullptr);
            else if (id == MetalEQAudioProcessor::ParameterIDs::lpSlope)
                child.setProperty ("value", slope, nullptr);
        }
        const auto updatedXml = root.createXml();
        return updatedXml != nullptr && legacyFile.replaceWithText (updatedXml->toString());
    };
    check (makeLegacyFixture ("0.0"), "legacy Off-slope fixture is written");
    check (processor.loadUserPreset (legacyName, error), "legacy blank lpOn with Off slope loads");
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpOn) < 0.5f,
           "legacy blank lpOn migrates to disabled when slope is Off");
    check (makeLegacyFixture ("2.0"), "legacy active-slope fixture is written");
    check (processor.loadUserPreset (legacyName, error), "legacy blank lpOn with active slope loads");
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpOn) > 0.5f,
           "legacy blank lpOn migrates to enabled when slope is active");
    legacyFile.deleteFile();
    check (! processor.loadUserPreset ("../outside", error), "path traversal preset name is rejected");
    check (! processor.saveUserPreset ("bad/name", false, error), "unsafe preset name is rejected");

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "MetalEQ state and user-preset tests passed.\n";
    return EXIT_SUCCESS;
}

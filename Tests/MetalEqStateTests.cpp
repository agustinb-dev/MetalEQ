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

void testFactoryRecall (MetalEQAudioProcessor& processor)
{
    check (processor.applyPresetById (metaleq::getPreset (5).id), "factory preset recalls by stable id");
    check (processor.getLastPresetId() == metaleq::getPreset (5).id, "recalled stable id is reported");
    check (processor.getLastPresetName() == metaleq::getPreset (5).name, "recalled name is reported");
    check (processor.getSelectedPresetIndex() == 6, "canonical factory selector index is factory index + 1");
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::preset) > 5.5f,
           "preset parameter reports canonical index");
    check (! processor.applyPresetById ("not-a-real-preset"), "unknown stable id recall fails");

    // Direct canonical recall, including the off-by-one boundary that used to
    // be wrong in the UI (selector 1 is factory[0]).
    processor.applyPreset (1);
    check (processor.getLastPresetName() == metaleq::getPreset (0).name,
           "selector 1 recalls factory preset 0");
    check (processor.getSelectedPresetIndex() == 1, "selector 1 is reported canonically");

    processor.applyPreset (0);
    check (processor.getLastPresetName() == "Custom", "selector 0 resets to Custom");
    check (processor.getLastPresetId().isEmpty(), "Custom has no factory stable id");
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpOn) < 0.5f,
           "Custom is transparent at the top end");

    // The neutral reference preset must reset macros as its description
    // promises, not merely flatten the five bands.
    const int neutralIndex = metaleq::findPresetById ("utility-neutral-flat");
    check (neutralIndex >= 0, "neutral reference preset resolves by stable id");
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::tight, 0.9f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::body, 0.1f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bite, 0.8f);
    processor.applyPreset (neutralIndex + 1);
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::tight)) < 0.01f,
           "neutral preset resets Tight");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::body) - 0.5f) < 0.01f,
           "neutral preset centres Body");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::bite)) < 0.01f,
           "neutral preset resets Bite");

    // Dirty tracking and revert.
    processor.applyPreset (2);
    check (! processor.getDisplayState().presetDirty, "fresh factory recall is clean");
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandGain[2], -4.0f);
    check (processor.getDisplayState().presetDirty, "user edit marks the preset dirty");
    processor.revertToLoadedPreset();
    check (! processor.getDisplayState().presetDirty, "revert clears dirty state");
    check (processor.getLastPresetName() == metaleq::getPreset (1).name,
           "revert keeps the loaded preset identity");
}

void testAbSlots (MetalEQAudioProcessor& processor)
{
    processor.applyPreset (1);
    const auto factoryA = processor.getLastPresetName();
    const auto factoryAId = processor.getLastPresetId();

    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::trim, 3.0f);

    processor.setAbSlot (1);
    check (processor.getActiveAbSlot() == 1, "B slot becomes active");
    check (processor.getLastPresetName() == "Custom", "B starts as the neutral snapshot");
    check (processor.getLastPresetId().isEmpty(), "neutral B has no factory id");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::trim) - 3.0f) < 0.01f,
           "output trim stays global across A/B");

    processor.applyPreset (2);
    const auto factoryB = processor.getLastPresetName();
    const auto factoryBId = processor.getLastPresetId();
    check (factoryB != factoryA, "A and B can hold different presets");
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::trim, 3.0f);

    processor.setAbSlot (0);
    check (processor.getLastPresetName() == factoryA, "A retains its own preset");
    check (processor.getLastPresetId() == factoryAId, "A retains its own stable id");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::trim) - 3.0f) < 0.01f,
           "trim remains global when switching back to A");

    processor.setAbSlot (1);
    check (processor.getLastPresetName() == factoryB, "B retains its own preset");
    check (processor.getLastPresetId() == factoryBId, "B retains its own stable id");

    // Copy active B into A.
    check (processor.copyActiveAbToOther(), "copy active slot to the other slot");
    processor.setAbSlot (0);
    check (processor.getLastPresetName() == factoryB, "copied slot contains the active sound");

    // Give A a different sound, then swap. The active slot index does not
    // change, but its contents become the other slot's contents.
    processor.applyPreset (3);
    const auto beforeSwap = processor.getLastPresetName();
    const auto other = factoryB;
    check (processor.swapAbSlots(), "swap both A/B slots");
    check (processor.getLastPresetName() == other, "swap loads the other slot's sound");
    check (processor.getLastPresetName() != beforeSwap, "swap actually changes the active sound");
    processor.setAbSlot (1);
    check (processor.getLastPresetName() == beforeSwap, "swap exchanged the A and B contents");
}

void testStateRoundTrip (MetalEQAudioProcessor& processor)
{
    processor.setAbSlot (0);
    processor.applyPreset (1);
    const auto aName = processor.getLastPresetName();
    const auto aId = processor.getLastPresetId();
    processor.setAbSlot (1);
    processor.applyPreset (2);
    const auto bName = processor.getLastPresetName();
    const auto bId = processor.getLastPresetId();
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::trim, -2.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    const auto tree = juce::ValueTree::readFromData (state.getData(), static_cast<size_t> (state.getSize()));
    check (tree.isValid() && tree.hasType ("MetalEQState"), "state has the expected root type");
    check (static_cast<int> (tree.getProperty ("version")) == 2, "state uses schema version 2");
    check (tree.getChildWithName ("AB").isValid(), "state stores A/B slots");
    check (tree.getChildWithName ("BASELINE").isValid(), "state stores a dirty baseline");
    check (tree.getChildWithName ("AB").getNumChildren() == 2, "state stores both A/B slots");

    processor.resetToNeutral();
    processor.setAbSlot (0);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    check (processor.getActiveAbSlot() == 1, "state restores the active A/B slot");
    check (processor.getLastPresetName() == bName, "state restores the active slot preset name");
    check (processor.getLastPresetId() == bId, "state restores the active stable id");
    check (! processor.getDisplayState().presetDirty, "restored slot is clean");
    check (std::abs (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::trim) + 2.0f) < 0.01f,
           "state restores the global output trim");

    processor.setAbSlot (0);
    check (processor.getLastPresetName() == aName, "state restores the inactive A slot sound");
    check (processor.getLastPresetId() == aId, "state restores the inactive A slot id");

    // Changing a parameter marks the slot dirty; revert restores its baseline.
    processor.setAbSlot (1);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandGain[1], 5.0f);
    check (processor.getDisplayState().presetDirty, "editing a restored slot marks it dirty");
    processor.revertToLoadedPreset();
    check (! processor.getDisplayState().presetDirty, "reverting a restored slot clears dirty");
}
} // namespace

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
                    && child.getProperty ("value").toString() == "0.000000000";
        }
    }
    check (defaultLowPassWasSerialized, "untouched default lpOn is serialized as disabled");
    check (processor.deleteUserPreset (defaultPreset, error), "default serialization fixture is removed");

    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandFreq[2], 2137.0f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::bandGain[2], -3.25f);
    setPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpSlope, 2.0f);
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
    check (readPlain (processor, MetalEQAudioProcessor::ParameterIDs::lpSlope) > 1.5f,
           "preset round trip restores low-pass slope");

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

    testFactoryRecall (processor);
    testAbSlots (processor);
    testStateRoundTrip (processor);

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "MetalEQ state and user-preset tests passed.\n";
    return EXIT_SUCCESS;
}

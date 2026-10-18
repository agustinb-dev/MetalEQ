#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class MetalEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit MetalEQAudioProcessorEditor (MetalEQAudioProcessor&);
    ~MetalEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class EQGraph final : public juce::Component
    {
    public:
        explicit EQGraph (MetalEQAudioProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
        void setSuggestion (float amount, bool visible);
    private:
        MetalEQAudioProcessor& processor;
        float suggestionAmount = 0.0f;
        bool showSuggestion = false;
    };

    void timerCallback() override;
    void updateAnalyzerUi();
    void applyShape (int shapeIndex);
    void updateAdvancedVisibility();
    void refreshPresetList();
    void saveAsPreset();
    void savePresetNamed (const juce::String& name, bool overwrite);
    void renameSelectedPreset();
    void deleteSelectedPreset();
    void showPresetManagementMenu();
    void showPresetError (const juce::String& message);
    void setParameterPlainValue (const char* id, float value);

    MetalEQAudioProcessor& audioProcessor;
    EQGraph eqGraph;

    juce::ComboBox presetCombo, targetCombo, lpSlopeCombo;
    juce::ComboBox hpSlopeCombo;
    juce::Label presetLabel, targetLabel, lpLabel, hpLabel, presetStateLabel;
    juce::ToggleButton bypassButton { "Bypass" }, abButton { "A/B" }, lpToggle { "Enabled" };
    juce::ToggleButton tightenButton { "TIGHTEN" }, scoopButton { "SCOOP" },
        midPunchButton { "MID PUNCH" }, pickAttackButton { "PICK ATTACK" },
        fizzButton { "FIZZ TAMER" }, brightenButton { "BRIGHTEN" };
    juce::TextButton resetButton { "RESET EQ" }, advancedButton { "FINE EQ +" },
        saveButton { "Save" }, managePresetsButton { "Manage" };

    juce::Slider tightSlider, bodySlider, scoopSlider, biteSlider, fizzSlider, trimSlider,
        hpFreqSlider, lpFreqSlider;
    juce::Label tightLabel, bodyLabel, scoopMacroLabel, biteLabel, fizzLabel, trimLabel, lpFreqLabel, hpFreqLabel;
    std::array<juce::Slider, metaleq::kNumBands> bandFreqSliders, bandGainSliders, bandQSliders;
    std::array<juce::ToggleButton, metaleq::kNumBands> bandEnabledButtons;
    std::array<juce::Label, metaleq::kNumBands> bandLabels;
    std::array<juce::Label, metaleq::kNumBands> bandFreqLabels, bandGainLabels, bandQLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, metaleq::kNumBands> bandFreqAttachments,
        bandGainAttachments, bandQAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, metaleq::kNumBands> bandEnabledAttachments;
    bool advancedVisible = false;
    juce::TextButton captureButton { "Capture 8 seconds" }, applyButton { "Apply suggestion" };
    juce::Slider amountSlider;
    juce::Label amountLabel, analyzerStatusLabel, analyzerDisclaimer;
    juce::StringArray userPresetNames;
    int currentUserPresetItem = 0;
    float lastInputPeak = 0.0f, lastOutputPeak = 0.0f;
    juce::Rectangle<int> inputMeterBounds, outputMeterBounds;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tightAttachment,
        bodyAttachment, scoopAttachment, biteAttachment, fizzAttachment, trimAttachment, hpFreqAttachment, lpFreqAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment,
        abAttachment, lpAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> hpSlopeAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 6> shapeAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lpSlopeAttachment;
    int collapsedHeight = 900;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEQAudioProcessorEditor)
};

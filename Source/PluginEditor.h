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
    void setParameterPlainValue (const char* id, float value);

    MetalEQAudioProcessor& audioProcessor;
    EQGraph eqGraph;

    juce::ComboBox presetCombo, targetCombo;
    juce::Label presetLabel, targetLabel;
    juce::ToggleButton bypassButton { "Bypass" }, abButton { "A/B" };
    juce::TextButton tightenButton { "TIGHTEN" }, scoopButton { "SCOOP" },
        midPunchButton { "MID PUNCH" }, pickAttackButton { "PICK ATTACK" },
        fizzButton { "FIZZ TAMER" }, brightenButton { "BRIGHTEN" }, resetButton { "RESET" };

    juce::Slider tightSlider, bodySlider, scoopSlider, biteSlider, fizzSlider, trimSlider;
    juce::Label tightLabel, bodyLabel, scoopMacroLabel, biteLabel, fizzLabel, trimLabel;
    juce::TextButton captureButton { "Capture 8 seconds" }, applyButton { "Apply suggestion" };
    juce::Slider amountSlider;
    juce::Label amountLabel, analyzerStatusLabel, analyzerDisclaimer;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tightAttachment,
        bodyAttachment, scoopAttachment, biteAttachment, fizzAttachment, trimAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, abAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEQAudioProcessorEditor)
};

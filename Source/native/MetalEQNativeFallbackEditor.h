/*
    Minimal native compatibility editor.

    The product UI is the React front-end. If the host cannot create a working
    WebView (missing runtime, locked-down Linux webkit, etc.) this panel keeps
    the plugin usable: preset recall, bypass, reset and the real A/B slots.
    Every parameter remains automatable and saved by the host; advanced editing
    is intentionally left to the host's generic parameter UI.
*/

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"

class MetalEQNativeFallbackEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit MetalEQNativeFallbackEditor (MetalEQAudioProcessor&);
    ~MetalEQNativeFallbackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetList();
    void refreshFromProcessor();
    void presetSelected();
    void saveAsPreset();

    /** Cheap directory snapshot (file names, sizes and mtimes). Used to detect
        preset changes without parsing every .metaleq file on each timer tick.
    */
    juce::StringArray scanUserPresetFiles() const;

    // The timer runs at 8 Hz; scanning the directory about once per second is
    // enough to pick up presets saved or deleted elsewhere.
    static constexpr int presetScanIntervalTicks = 8;

    MetalEQAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label presetLabel;
    juce::ComboBox presetCombo;
    juce::TextButton saveButton { "Save as..." };
    juce::ToggleButton bypassButton { "Bypass" };
    juce::TextButton resetButton { "Reset to neutral" };

    juce::Label abLabel;
    juce::TextButton aButton { "A" };
    juce::TextButton bButton { "B" };
    juce::TextButton copyAbButton { "Copy active -> other" };
    juce::TextButton swapAbButton { "Swap A/B" };

    juce::Label diagnosticsLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::StringArray userPresetNames;
    juce::StringArray lastPresetDirectoryScan;
    int presetScanTicks = 0;
    bool updatingPresetCombo = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEQNativeFallbackEditor)
};

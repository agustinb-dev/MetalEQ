#include "PluginEditor.h"
#include "dsp/MetalEqGraphAxis.h"

namespace
{
    const juce::Colour background { 0xff111419 };
    const juce::Colour panel { 0xff1a1e24 };
    const juce::Colour accent { 0xffff765e };
    const juce::Colour green { 0xff69d6a6 };

    juce::String formatFrequency (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + " kHz"
                            : juce::String (static_cast<int> (std::round (hz))) + " Hz";
    }
}

MetalEQAudioProcessorEditor::MetalEQAudioProcessorEditor (MetalEQAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor), eqGraph (processor)
{
    setSize (960, 900);
    setResizable (true, true);
    setResizeLimits (780, 790, 1440, 1280);

    auto& state = audioProcessor.getValueTreeState();
    addAndMakeVisible (eqGraph);

    const auto setLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setColour (juce::Label::textColourId, juce::Colour (0xffc3c8d0));
        addAndMakeVisible (label);
    };
    const auto addKnob = [this, &setLabel] (juce::Slider& slider, juce::Label& label,
                                             const juce::String& title, const juce::String& hint)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 20);
        slider.setTooltip (hint);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, panel);
        setLabel (label, title);
        addAndMakeVisible (slider);
    };

    addKnob (tightSlider, tightLabel, "TIGHT", "Controls low-end looseness and low-mid buildup.");
    addKnob (bodySlider, bodyLabel, "BODY", "Adds or reduces low-mid fullness. Centre is neutral.");
    addKnob (scoopSlider, scoopMacroLabel, "SCOOP", "Broad midrange reduction. Heavy scooping can reduce mix clarity.");
    addKnob (biteSlider, biteLabel, "BITE", "Controls upper-mid pick attack and aggression.");
    addKnob (fizzSlider, fizzLabel, "FIZZ CUT", "Tames upper-frequency harshness while preserving useful attack.");
    addKnob (trimSlider, trimLabel, "OUTPUT", "Output level after EQ; useful for level-matched A/B.");
    trimSlider.setTextValueSuffix (" dB");
    tightSlider.setDoubleClickReturnValue (true, 0.0);
    bodySlider.setDoubleClickReturnValue (true, 0.5);
    scoopSlider.setDoubleClickReturnValue (true, 0.0);
    biteSlider.setDoubleClickReturnValue (true, 0.0);
    fizzSlider.setDoubleClickReturnValue (true, 0.0);
    trimSlider.setDoubleClickReturnValue (true, 0.0);

    tightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::tight, tightSlider);
    bodyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::body, bodySlider);
    scoopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::scoop, scoopSlider);
    biteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::bite, biteSlider);
    fizzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::fizzCut, fizzSlider);
    trimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::trim, trimSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, MetalEQAudioProcessor::ParameterIDs::bypass, bypassButton);
    abAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, MetalEQAudioProcessor::ParameterIDs::abCompare, abButton);

    juce::ToggleButton* shapeButtons[] = { &tightenButton, &scoopButton, &midPunchButton,
        &pickAttackButton, &fizzButton, &brightenButton };
    const juce::String shapeTips[] = {
        "Tightens excess lows and clears low-mid looseness.",
        "Applies a broad midrange scoop; use gently in dense mixes.",
        "Adds forward midrange body and authority.",
        "Adds pick definition around the attack region.",
        "Reduces fizz and harshness while retaining pick attack.",
        "Adds controlled presence to a dark guitar tone."
    };
    for (int i = 0; i < 6; ++i)
    {
        auto* button = shapeButtons[i];
        button->setClickingTogglesState (true);
        button->setTooltip (shapeTips[i]);
        button->setColour (juce::ToggleButton::tickColourId, accent);
        button->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (*button);
        shapeAttachments[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            state, MetalEQAudioProcessor::ParameterIDs::shapeOn[i], *button);
    }

    // Fine controls are hidden by default so the main live-use view stays
    // compact. Every band remains available without opening another window.
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        auto& f = bandFreqSliders[static_cast<size_t> (i)];
        auto& gain = bandGainSliders[static_cast<size_t> (i)];
        auto& q = bandQSliders[static_cast<size_t> (i)];
        auto& on = bandEnabledButtons[static_cast<size_t> (i)];
        auto& title = bandLabels[static_cast<size_t> (i)];
        title.setText ("BAND " + juce::String (i + 1), juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);
        on.setButtonText ("ON");
        on.setTooltip ("Enable or bypass this parametric EQ band.");
        addAndMakeVisible (on);
        auto configureDetailSlider = [this] (juce::Slider& slider, juce::Label& label,
                                             const juce::String& name, const juce::String& tip,
                                             const juce::String& suffix, int decimals)
        {
            label.setText (name, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centredLeft);
            label.setFont (juce::Font (juce::FontOptions (10.0f)));
            label.setColour (juce::Label::textColourId, juce::Colour (0xffaeb5be));
            addAndMakeVisible (label);
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 20);
            slider.setTextValueSuffix (suffix);
            slider.setNumDecimalPlacesToDisplay (decimals);
            slider.setTooltip (tip);
            addAndMakeVisible (slider);
        };
        configureDetailSlider (f, bandFreqLabels[static_cast<size_t> (i)], "FREQ",
            "Band centre frequency. Logarithmic range from 60 Hz to 16 kHz.", " Hz", 0);
        configureDetailSlider (gain, bandGainLabels[static_cast<size_t> (i)], "GAIN",
            "Boost or cut this band in decibels.", " dB", 1);
        configureDetailSlider (q, bandQLabels[static_cast<size_t> (i)], "Q",
            "Bandwidth: lower Q is broad, higher Q is narrow.", "", 2);
        bandFreqAttachments[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, MetalEQAudioProcessor::ParameterIDs::bandFreq[i], f);
        bandGainAttachments[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, MetalEQAudioProcessor::ParameterIDs::bandGain[i], gain);
        bandQAttachments[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, MetalEQAudioProcessor::ParameterIDs::bandQ[i], q);
        bandEnabledAttachments[static_cast<size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            state, MetalEQAudioProcessor::ParameterIDs::bandOn[i], on);
        f.setVisible (false);
        gain.setVisible (false);
        q.setVisible (false);
        on.setVisible (false);
        title.setVisible (false);
        bandFreqLabels[static_cast<size_t> (i)].setVisible (false);
        bandGainLabels[static_cast<size_t> (i)].setVisible (false);
        bandQLabels[static_cast<size_t> (i)].setVisible (false);
    }

    bypassButton.setTooltip ("Bypasses the EQ while leaving the audio untouched.");
    abButton.setTooltip ("Compare against flat EQ at the same output trim.");
    juce::Component* headerButtons[] = { &bypassButton, &abButton, &resetButton, &saveButton,
        &managePresetsButton, &captureButton, &applyButton };
    for (auto* button : headerButtons)
        addAndMakeVisible (*button);
    resetButton.setTooltip ("Reset all EQ and macros to neutral. Shape toggles are switched off.");
    addAndMakeVisible (presetCombo);
    addAndMakeVisible (presetLabel);
    addAndMakeVisible (presetStateLabel);
    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetStateLabel.setJustificationType (juce::Justification::centred);

    presetCombo.setTooltip ("Factory presets are read-only. User presets are stored in your Windows app-data folder.");
    presetCombo.addItem ("Custom / Neutral", 1);
    for (int i = 0; i < metaleq::getNumPresets(); ++i)
        presetCombo.addItem (metaleq::getPreset (i).name, i + 2);
    presetCombo.addSeparator();
    refreshPresetList();
    presetCombo.onChange = [this]
    {
        const int item = presetCombo.getSelectedId();
        if (item == 1)
            audioProcessor.applyPreset (0);
        else if (item >= 2 && item <= metaleq::getNumPresets() + 1)
            audioProcessor.applyPreset (item - 1);
        else if (item >= 1000)
        {
            const int index = item - 1000;
            if (juce::isPositiveAndBelow (index, userPresetNames.size()))
            {
                juce::String error;
                if (! audioProcessor.loadUserPreset (userPresetNames[index], error))
                    showPresetError (error);
            }
        }
        eqGraph.repaint();
    };

    saveButton.setButtonText ("Save");
    saveButton.setTooltip ("Overwrite the selected user preset after confirmation, or open Save As for a factory or custom sound.");
    managePresetsButton.setTooltip ("Manage user presets: Save As creates a preset and asks before overwriting; Rename and Delete apply to the selected user preset; Revert restores the loaded preset.");
    managePresetsButton.setName ("Manage user presets");
    managePresetsButton.setAccessible (true);
    saveButton.onClick = [this]
    {
        if (! audioProcessor.isLoadedPresetUser())
        {
            saveAsPreset();
            return;
        }
        const auto name = audioProcessor.getLastPresetName();
        juce::Component::SafePointer<MetalEQAudioProcessorEditor> safeThis (this);
        juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon,
            "Overwrite user preset?", "Replace ‘" + name + "’ with the current settings?",
            "Overwrite", "Cancel", this,
            juce::ModalCallbackFunction::create ([safeThis, name] (int result)
            {
                if (result == 1 && safeThis != nullptr)
                    safeThis->savePresetNamed (name, true);
            }));
    };
    managePresetsButton.onClick = [this] { showPresetManagementMenu(); };
    resetButton.onClick = [this] { audioProcessor.resetToNeutral(); eqGraph.repaint(); };

    bypassButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    abButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white);

    setLabel (targetLabel, "TARGET");
    targetCombo.addItem ("Metalcore · Tight Rhythm", 1);
    targetCombo.addItem ("Thrash · Technical Clarity", 2);
    targetCombo.addItem ("Classic Thrash · Midrange Punch", 3);
    targetCombo.addItem ("Post-Amp · Mix Cleanup", 4);
    targetCombo.setSelectedId (1, juce::dontSendNotification);
    targetCombo.setTooltip ("The analyzer compares the captured spectrum with this target; it does not detect the source genre.");
    addAndMakeVisible (targetCombo);

    setLabel (hpLabel, "HIGH-PASS");
    hpLabel.setTooltip ("Removes low-frequency rumble; use a lower cutoff on drop tunings.");
    hpFreqSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    hpFreqSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 22);
    hpFreqSlider.setNumDecimalPlacesToDisplay (0);
    hpFreqSlider.setTextValueSuffix (" Hz");
    hpFreqSlider.setTooltip ("High-pass cutoff. Tightens low end; avoid cutting the fundamental of low-tuned notes.");
    addAndMakeVisible (hpFreqSlider);
    hpFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::hpFreq, hpFreqSlider);
    hpSlopeCombo.addItemList ({ "Off", "12 dB/oct", "24 dB/oct", "36 dB/oct" }, 1);
    hpSlopeCombo.setTooltip ("Steeper slopes remove more low-frequency energy below cutoff.");
    addAndMakeVisible (hpSlopeCombo);
    hpSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, MetalEQAudioProcessor::ParameterIDs::hpSlope, hpSlopeCombo);

    setLabel (lpLabel, "LOW-PASS / HIGH CUT");
    setLabel (lpFreqLabel, "CUTOFF");
    lpToggle.setTooltip ("Enable or bypass the high-frequency low-pass filter. This reduces top-end content; it is not a noise gate.");
    addAndMakeVisible (lpToggle);
    lpToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    lpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, MetalEQAudioProcessor::ParameterIDs::lpOn, lpToggle);
    lpToggle.onClick = [this] { audioProcessor.setLowPassEnabled (lpToggle.getToggleState()); };
    lpFreqSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lpFreqSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    lpFreqSlider.setNumDecimalPlacesToDisplay (0);
    lpFreqSlider.setTextValueSuffix (" Hz");
    lpFreqSlider.setTooltip ("Cutoff frequency. For post-amp guitars, audition around 8–12 kHz when the top end is fizzy.");
    addAndMakeVisible (lpFreqSlider);
    lpFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::lpFreq, lpFreqSlider);
    lpSlopeCombo.addItemList ({ "Off", "12 dB/oct", "24 dB/oct", "36 dB/oct" }, 1);
    lpSlopeCombo.setTooltip ("Steeper slopes remove more high frequencies above the cutoff.");
    addAndMakeVisible (lpSlopeCombo);
    lpSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, MetalEQAudioProcessor::ParameterIDs::lpSlope, lpSlopeCombo);
    addAndMakeVisible (advancedButton);
    advancedButton.setTooltip ("Open five precise parametric bands with frequency, gain, Q and bypass controls.");
    advancedButton.onClick = [this] { updateAdvancedVisibility(); };

    setLabel (amountLabel, "AMOUNT");
    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setRange (0.0, 1.0, 0.01);
    amountSlider.setValue (0.75, juce::dontSendNotification);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    addAndMakeVisible (amountSlider);
    analyzerStatusLabel.setJustificationType (juce::Justification::centredLeft);
    analyzerDisclaimer.setText ("Average-spectrum suggestion only. Capture is temporary and never saved in presets.", juce::dontSendNotification);
    analyzerDisclaimer.setJustificationType (juce::Justification::centredLeft);
    analyzerDisclaimer.setFont (juce::Font (juce::FontOptions (12.0f)));
    analyzerDisclaimer.setColour (juce::Label::textColourId, juce::Colour (0xff9aa1aa));
    addAndMakeVisible (analyzerStatusLabel);
    addAndMakeVisible (analyzerDisclaimer);

    captureButton.onClick = [this]
    {
        if (audioProcessor.getAnalyzerStatus() == MetalEQAudioProcessor::AnalyzerStatus::capturing)
            audioProcessor.cancelAnalyzerCapture();
        else
            audioProcessor.startAnalyzerCapture (targetCombo.getSelectedId() - 1);
        updateAnalyzerUi();
    };
    applyButton.onClick = [this]
    {
        audioProcessor.applyAnalyzerSuggestion (static_cast<float> (amountSlider.getValue()));
        eqGraph.repaint();
    };
    amountSlider.onValueChange = [this] { updateAnalyzerUi(); };

    audioProcessor.setMeteringEnabled (true);
    startTimerHz (15);
    updateAnalyzerUi();
}

MetalEQAudioProcessorEditor::~MetalEQAudioProcessorEditor()
{
    audioProcessor.setMeteringEnabled (false);
}

void MetalEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (24.0f).withStyle ("Bold")));
    g.drawText ("MetalEQ", 20, 7, 170, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff9da4ae));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const int titleWidth = juce::jmax (140, juce::jmin (390, getWidth() - 450));
    g.drawText ("Zero-latency guitar EQ · before or after an amp", 190, 12, titleWidth, 20, juce::Justification::centredLeft);

    const auto drawMeter = [&g] (juce::Rectangle<int> r, const juce::String& title, float peak)
    {
        if (r.isEmpty()) return;
        g.setColour (juce::Colour (0xff333840));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        auto fill = r.toFloat().reduced (1.0f);
        fill.setWidth (fill.getWidth() * juce::jlimit (0.0f, 1.0f, peak));
        g.setColour (peak > 0.95f ? juce::Colour (0xffef5b5b) : green);
        g.fillRoundedRectangle (fill, 2.0f);
        g.setColour (juce::Colour (0xffaeb4bd));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (title, r.getX(), r.getY() - 15, r.getWidth(), 14, juce::Justification::centredLeft);
    };
    drawMeter (inputMeterBounds, "INPUT PEAK", lastInputPeak);
    drawMeter (outputMeterBounds, "OUTPUT PEAK", lastOutputPeak);
}

void MetalEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);
    area.removeFromTop (34);
    auto presetRow = area.removeFromTop (36);
    presetLabel.setBounds (presetRow.removeFromLeft (64));
    constexpr int stateWidth = 74, saveWidth = 58, manageWidth = 78,
                  bypassWidth = 72, abWidth = 52;
    constexpr int fixedPresetControls = stateWidth + saveWidth + manageWidth + bypassWidth + abWidth;
    const int presetComboWidth = juce::jmax (180, presetRow.getWidth() - fixedPresetControls);
    presetCombo.setBounds (presetRow.removeFromLeft (presetComboWidth).reduced (2, 2));
    presetStateLabel.setBounds (presetRow.removeFromLeft (stateWidth).reduced (2, 0));
    saveButton.setBounds (presetRow.removeFromLeft (saveWidth).reduced (2, 2));
    managePresetsButton.setBounds (presetRow.removeFromLeft (manageWidth).reduced (2, 2));
    bypassButton.setBounds (presetRow.removeFromLeft (bypassWidth).reduced (2, 2));
    abButton.setBounds (presetRow.removeFromLeft (abWidth).reduced (2, 2));

    eqGraph.setBounds (area.removeFromTop (juce::jlimit (210, 292, getHeight() / 3)).reduced (0, 7));

    auto knobs = area.removeFromTop (140);
    juce::Slider* sliders[] = { &tightSlider, &bodySlider, &scoopSlider, &biteSlider, &fizzSlider, &trimSlider };
    juce::Label* labels[] = { &tightLabel, &bodyLabel, &scoopMacroLabel, &biteLabel, &fizzLabel, &trimLabel };
    for (int i = 0; i < 6; ++i)
    {
        auto cell = knobs.removeFromLeft (i == 5 ? knobs.getWidth() : knobs.getWidth() / (6 - i));
        labels[i]->setBounds (cell.removeFromTop (20));
        sliders[i]->setBounds (cell.reduced (5, 0));
    }

    auto shapes = area.removeFromTop (42);
    juce::ToggleButton* buttons[] = { &tightenButton, &scoopButton, &midPunchButton,
        &pickAttackButton, &fizzButton, &brightenButton };
    for (int i = 0; i < 6; ++i)
        buttons[i]->setBounds (shapes.removeFromLeft (shapes.getWidth() / (7 - i)).reduced (2, 2));
    shapes.removeFromLeft (4);
    resetButton.setBounds (shapes.removeFromLeft (juce::jmin (100, shapes.getWidth())).reduced (2, 2));

    auto highPass = area.removeFromTop (46).reduced (4, 2);
    hpLabel.setBounds (highPass.removeFromLeft (104));
    hpFreqSlider.setBounds (highPass.removeFromLeft (juce::jmax (180, highPass.getWidth() - 126)));
    hpSlopeCombo.setBounds (highPass.removeFromRight (116).reduced (2, 3));

    auto lowPass = area.removeFromTop (50).reduced (4, 2);
    lpLabel.setBounds (lowPass.removeFromLeft (144));
    lpToggle.setBounds (lowPass.removeFromLeft (78));
    lpFreqLabel.setBounds (lowPass.removeFromLeft (55));
    lpFreqSlider.setBounds (lowPass.removeFromLeft (juce::jmax (150, lowPass.getWidth() - 110)));
    lpSlopeCombo.setBounds (lowPass.removeFromRight (108).reduced (2, 3));

    advancedButton.setBounds (area.removeFromTop (30).removeFromLeft (150).reduced (2, 2));
    if (advancedVisible)
    {
        auto fine = area.removeFromTop (220).reduced (2, 2);
        for (int i = 0; i < metaleq::kNumBands; ++i)
        {
            auto card = fine.removeFromLeft (fine.getWidth() / (metaleq::kNumBands - i)).reduced (4, 2);
            juce::ToggleButton& enabled = bandEnabledButtons[static_cast<size_t> (i)];
            juce::Label& title = bandLabels[static_cast<size_t> (i)];
            juce::Slider& frequency = bandFreqSliders[static_cast<size_t> (i)];
            juce::Slider& gain = bandGainSliders[static_cast<size_t> (i)];
            juce::Slider& q = bandQSliders[static_cast<size_t> (i)];
            enabled.setBounds (card.removeFromTop (25));
            title.setBounds (card.removeFromTop (18));
            auto freqRow = card.removeFromTop (56);
            bandFreqLabels[static_cast<size_t> (i)].setBounds (freqRow.removeFromTop (14));
            frequency.setBounds (freqRow);
            auto gainRow = card.removeFromTop (56);
            bandGainLabels[static_cast<size_t> (i)].setBounds (gainRow.removeFromTop (14));
            gain.setBounds (gainRow);
            auto qRow = card.removeFromTop (56);
            bandQLabels[static_cast<size_t> (i)].setBounds (qRow.removeFromTop (14));
            q.setBounds (qRow);
        }
    }

    area.removeFromTop (5);
    auto targetRow = area.removeFromTop (34);
    targetLabel.setBounds (targetRow.removeFromLeft (68));
    const int targetComboWidth = juce::jlimit (150, 320,
        targetRow.getWidth() - (68 + 148 + 64 + 120 + 100));
    targetCombo.setBounds (targetRow.removeFromLeft (targetComboWidth).reduced (2, 2));
    captureButton.setBounds (targetRow.removeFromLeft (148).reduced (2, 2));
    amountLabel.setBounds (targetRow.removeFromLeft (64));
    amountSlider.setBounds (targetRow.removeFromLeft (juce::jmax (120, targetRow.getWidth() - 104)).reduced (2, 2));
    applyButton.setBounds (targetRow.removeFromRight (100).reduced (2, 2));
    analyzerStatusLabel.setBounds (area.removeFromTop (22));
    analyzerDisclaimer.setBounds (area.removeFromTop (20));

    inputMeterBounds = { getWidth() - 250, 15, 92, 8 };
    outputMeterBounds = { getWidth() - 125, 15, 92, 8 };
}

void MetalEQAudioProcessorEditor::timerCallback()
{
    lastInputPeak = juce::jmax (audioProcessor.getInputPeakLevel (0), audioProcessor.getInputPeakLevel (1));
    lastOutputPeak = juce::jmax (audioProcessor.getPeakLevel (0), audioProcessor.getPeakLevel (1));

    const auto display = audioProcessor.getDisplayState();
    const auto presetName = display.presetName;
    int selectedId = display.presetIndex == 0 ? 1 : display.presetIndex + 1;
    if (display.userPreset)
    {
        const int userIndex = userPresetNames.indexOf (presetName);
        if (userIndex >= 0)
            selectedId = 1000 + userIndex;
    }
    if (presetCombo.getSelectedId() != selectedId)
        presetCombo.setSelectedId (selectedId, juce::dontSendNotification);
    const auto kind = display.userPreset ? juce::String ("USER")
        : (display.presetIndex > 0 ? juce::String ("FACTORY") : juce::String ("CUSTOM"));
    presetStateLabel.setText (kind + (display.presetDirty ? " *" : ""), juce::dontSendNotification);
    presetStateLabel.setColour (juce::Label::textColourId, display.presetDirty ? accent : green);
    saveButton.setEnabled (true);
    updateAnalyzerUi();
    repaint();
    eqGraph.repaint();
}

void MetalEQAudioProcessorEditor::updateAnalyzerUi()
{
    using Status = MetalEQAudioProcessor::AnalyzerStatus;
    const auto status = audioProcessor.getAnalyzerStatus();
    const bool ready = status == Status::ready;
    captureButton.setButtonText (status == Status::capturing ? "Stop capture" : "Capture 8 seconds");
    applyButton.setEnabled (ready);
    std::array<float, metaleq::kNumBands> suggestion {};
    audioProcessor.getAnalyzerSuggestion (suggestion);
    eqGraph.setSuggestion (static_cast<float> (amountSlider.getValue()), ready);

    if (status == Status::capturing)
        analyzerStatusLabel.setText ("Capturing input: " + juce::String (audioProcessor.getAnalyzerProgress() * 100.0f, 0)
            + "% · play through the host.", juce::dontSendNotification);
    else if (status == Status::analyzing)
        analyzerStatusLabel.setText ("Analyzing captured average spectrum…", juce::dontSendNotification);
    else if (ready)
        analyzerStatusLabel.setText ("Suggested gains: " + juce::String (suggestion[0], 1) + " / "
            + juce::String (suggestion[1], 1) + " / " + juce::String (suggestion[2], 1) + " / "
            + juce::String (suggestion[3], 1) + " / " + juce::String (suggestion[4], 1) + " dB",
            juce::dontSendNotification);
    else if (status == Status::failed)
        analyzerStatusLabel.setText ("No usable audio was captured. Play audio and try again.", juce::dontSendNotification);
    else
        analyzerStatusLabel.setText ("Ready · analyzer listens only when you start capture.", juce::dontSendNotification);
}

void MetalEQAudioProcessorEditor::updateAdvancedVisibility()
{
    advancedVisible = ! advancedVisible;
    if (advancedVisible)
        collapsedHeight = getHeight();
    advancedButton.setButtonText (advancedVisible ? "FINE EQ −" : "FINE EQ +");
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        bandFreqSliders[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandGainSliders[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandQSliders[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandEnabledButtons[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandLabels[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandFreqLabels[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandGainLabels[static_cast<size_t> (i)].setVisible (advancedVisible);
        bandQLabels[static_cast<size_t> (i)].setVisible (advancedVisible);
    }
    setSize (getWidth(), advancedVisible
        ? juce::jlimit (1010, 1280, collapsedHeight + 220)
        : collapsedHeight);
    resized();
}

void MetalEQAudioProcessorEditor::applyShape (int shapeIndex)
{
    audioProcessor.applyShape (shapeIndex);
    eqGraph.repaint();
}

void MetalEQAudioProcessorEditor::refreshPresetList()
{
    const auto previous = audioProcessor.getLastPresetName();
    const auto previousUser = audioProcessor.isLoadedPresetUser();
    userPresetNames = audioProcessor.getUserPresetNames();
    presetCombo.clear (juce::dontSendNotification);
    presetCombo.addItem ("Custom / Neutral", 1);
    for (int i = 0; i < metaleq::getNumPresets(); ++i)
        presetCombo.addItem (metaleq::getPreset (i).name, i + 2);
    presetCombo.addSeparator();
    for (int i = 0; i < userPresetNames.size(); ++i)
        presetCombo.addItem ("User · " + userPresetNames[i], 1000 + i);
    if (previousUser)
    {
        const int index = userPresetNames.indexOf (previous);
        if (index >= 0)
            presetCombo.setSelectedId (1000 + index, juce::dontSendNotification);
    }
    else
    {
        const int index = audioProcessor.getSelectedPresetIndex();
        presetCombo.setSelectedId (index == 0 ? 1 : index + 1, juce::dontSendNotification);
    }
}

void MetalEQAudioProcessorEditor::showPresetManagementMenu()
{
    const bool isUserPreset = audioProcessor.isLoadedPresetUser();
    const bool isDirty = audioProcessor.getDisplayState().presetDirty;
    juce::Component::SafePointer<MetalEQAudioProcessorEditor> safeThis (this);
    juce::PopupMenu menu;
    menu.addItem ("Save As…", [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->saveAsPreset();
    });
    menu.addItem ("Rename…", isUserPreset, false, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->renameSelectedPreset();
    });
    menu.addItem ("Delete…", isUserPreset, false, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->deleteSelectedPreset();
    });
    menu.addSeparator();
    menu.addItem ("Revert to loaded preset", isDirty, false, [safeThis]
    {
        if (safeThis != nullptr)
        {
            safeThis->audioProcessor.revertToLoadedPreset();
            safeThis->eqGraph.repaint();
        }
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&managePresetsButton));
}

void MetalEQAudioProcessorEditor::showPresetError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "MetalEQ preset", message);
}

void MetalEQAudioProcessorEditor::saveAsPreset()
{
    auto* prompt = new juce::AlertWindow ("Save user preset", "Choose a name for this EQ sound.", juce::AlertWindow::NoIcon, this);
    prompt->addTextEditor ("presetName", "My Guitar Tone");
    prompt->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    prompt->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safePrompt (prompt);
    juce::Component::SafePointer<MetalEQAudioProcessorEditor> safeThis (this);
    prompt->enterModalState (true, juce::ModalCallbackFunction::create ([safePrompt, safeThis] (int result)
    {
        if (result == 1 && safePrompt != nullptr && safeThis != nullptr)
        {
            const auto name = safePrompt->getTextEditorContents ("presetName").trim();
            const bool exists = safeThis->audioProcessor.getUserPresetNames().contains (name, true);
            if (! exists)
                safeThis->savePresetNamed (name, false);
            else
            {
                juce::Component::SafePointer<MetalEQAudioProcessorEditor> editor (safeThis);
                juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon,
                    "Overwrite user preset?", "Replace ‘" + name + "’ with the current settings?",
                    "Overwrite", "Cancel", safeThis,
                    juce::ModalCallbackFunction::create ([editor, name] (int confirmed)
                    {
                        if (confirmed == 1 && editor != nullptr)
                            editor->savePresetNamed (name, true);
                    }));
            }
        }
    }), true);
}

void MetalEQAudioProcessorEditor::savePresetNamed (const juce::String& name, bool overwrite)
{
    juce::String error;
    if (! audioProcessor.saveUserPreset (name, overwrite, error))
        showPresetError (error);
    else
        refreshPresetList();
}

void MetalEQAudioProcessorEditor::renameSelectedPreset()
{
    if (! audioProcessor.isLoadedPresetUser())
    {
        showPresetError ("Select or load a user preset first. Factory presets are read-only.");
        return;
    }
    const auto oldName = audioProcessor.getLastPresetName();
    auto* prompt = new juce::AlertWindow ("Rename user preset", "Enter a new preset name.", juce::AlertWindow::NoIcon, this);
    prompt->addTextEditor ("presetName", oldName);
    prompt->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    prompt->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safePrompt (prompt);
    juce::Component::SafePointer<MetalEQAudioProcessorEditor> safeThis (this);
    prompt->enterModalState (true, juce::ModalCallbackFunction::create ([safePrompt, safeThis, oldName] (int result)
    {
        if (result == 1 && safePrompt != nullptr && safeThis != nullptr)
        {
            juce::String error;
            if (! safeThis->audioProcessor.renameUserPreset (oldName, safePrompt->getTextEditorContents ("presetName"), error))
                safeThis->showPresetError (error);
            else
                safeThis->refreshPresetList();
        }
    }), true);
}

void MetalEQAudioProcessorEditor::deleteSelectedPreset()
{
    if (! audioProcessor.isLoadedPresetUser())
    {
        showPresetError ("Select or load a user preset first. Factory presets are read-only.");
        return;
    }
    const auto name = audioProcessor.getLastPresetName();
    juce::Component::SafePointer<MetalEQAudioProcessorEditor> safeThis (this);
    juce::AlertWindow::showOkCancelBox (juce::AlertWindow::WarningIcon,
        "Delete user preset?", "Permanently delete ‘" + name + "’?",
        "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([safeThis, name] (int result)
        {
            if (result == 1 && safeThis != nullptr)
            {
                juce::String error;
                if (! safeThis->audioProcessor.deleteUserPreset (name, error))
                    safeThis->showPresetError (error);
                else
                    safeThis->refreshPresetList();
            }
        }));
}

void MetalEQAudioProcessorEditor::setParameterPlainValue (const char* id, float value)
{
    if (auto* parameter = audioProcessor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->getNormalisableRange().convertTo0to1 (value));
}

void MetalEQAudioProcessorEditor::EQGraph::setSuggestion (float amount, bool visible)
{
    suggestionAmount = amount;
    showSuggestion = visible;
}

void MetalEQAudioProcessorEditor::EQGraph::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto plot = getLocalBounds().reduced (42, 20).toFloat();
    plot.removeFromBottom (23.0f);
    plot.removeFromTop (6.0f);
    if (plot.getWidth() < 80.0f || plot.getHeight() < 60.0f)
        return;

    const auto freqToX = [plot] (double hz)
    {
        return plot.getX() + plot.getWidth() * static_cast<float> (metaleq::graph::frequencyToUnit (hz));
    };
    const auto dbToY = [plot] (double db)
    {
        return plot.getBottom() - plot.getHeight() * static_cast<float> (metaleq::graph::dbToUnit (db));
    };

    // Guitar-EQ regions are subtle backdrop cues, not fixed tonal rules.
    struct Region { double from, to; const char* label; };
    static constexpr Region regions[] = {
        { 20, 80, "RUMBLE" }, { 80, 200, "LOW END" }, { 200, 400, "BODY / MUD" },
        { 400, 1500, "CHARACTER" }, { 1500, 4000, "ATTACK / BITE" },
        { 4000, 7000, "PRESENCE" }, { 7000, 20000, "FIZZ / TOP" }
    };
    const juce::Colour regionColours[] = { juce::Colour (0xff23272d), juce::Colour (0xff20262b),
        juce::Colour (0xff24252b), juce::Colour (0xff20252b), juce::Colour (0xff24262b),
        juce::Colour (0xff20252b), juce::Colour (0xff242329) };
    for (size_t i = 0; i < std::size (regions); ++i)
    {
        juce::Rectangle<float> zone (freqToX (regions[i].from), plot.getY(),
            freqToX (regions[i].to) - freqToX (regions[i].from), plot.getHeight());
        g.setColour (regionColours[i]);
        g.fillRect (zone);
        if (zone.getWidth() > 55.0f)
        {
            g.setColour (juce::Colour (0xff59616b));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText (regions[i].label, zone.toNearestInt().withHeight (13), juce::Justification::centredTop, false);
        }
    }

    for (int db = -18; db <= 12; db += 6)
    {
        const float y = dbToY (db);
        g.setColour (db == 0 ? juce::Colour (0xff78818b) : juce::Colour (0xff363c44));
        g.drawHorizontalLine (static_cast<int> (std::round (y)), plot.getX(), plot.getRight());
        g.setColour (db == 0 ? juce::Colour (0xffd1d5dc) : juce::Colour (0xff9098a2));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (juce::String (db) + " dB", 2, static_cast<int> (y - 7.0f), 38, 14, juce::Justification::centredRight);
    }

    for (size_t i = 0; i < metaleq::graph::guitarLandmarks.size(); ++i)
    {
        const double hz = metaleq::graph::guitarLandmarks[i];
        const bool major = hz == 20.0 || hz == 100.0 || hz == 500.0 || hz == 1000.0
                        || hz == 3000.0 || hz == 8000.0 || hz == 20000.0;
        const float x = freqToX (hz);
        g.setColour (major ? juce::Colour (0xff535a63) : juce::Colour (0xff333940));
        g.drawVerticalLine (static_cast<int> (std::round (x)), plot.getY(), plot.getBottom());
    }

    const auto labels = metaleq::graph::visibleLabelIndices (plot.getWidth(), 45.0);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (const auto index : labels)
    {
        const double hz = metaleq::graph::guitarLandmarks[index];
        const float x = freqToX (hz);
        auto label = juce::Rectangle<int> (static_cast<int> (x - 22.0f), getHeight() - 18, 44, 14);
        if (hz == 20.0) label.setX (40);
        if (hz == 20000.0) label.setX (getWidth() - 84);
        g.setColour (juce::Colour (0xffc0c6ce));
        g.drawText (metaleq::graph::frequencyLabel (hz), label, juce::Justification::centred, false);
    }

    juce::Path response;
    constexpr int points = 400;
    for (int i = 0; i < points; ++i)
    {
        const double hz = metaleq::graph::unitToFrequency (i / static_cast<double> (points - 1));
        const float x = freqToX (hz);
        const float y = dbToY (processor.getResponseDb (hz));
        if (i == 0) response.startNewSubPath (x, y); else response.lineTo (x, y);
    }
    g.setColour (accent);
    g.strokePath (response, juce::PathStrokeType (2.2f));

    const auto state = processor.getDisplayState();
    if (state.eq.hpSlope > 0)
    {
        const float x = freqToX (state.eq.hpFreq);
        g.setColour (juce::Colour (0xff9bb4d0).withAlpha (0.7f));
        g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());
        g.fillEllipse (x - 3.5f, dbToY (-3.0) - 3.5f, 7.0f, 7.0f);
    }
    if (state.eq.lpSlope > 0)
    {
        const float x = freqToX (state.eq.lpFreq);
        g.setColour (state.eq.lpEnabled ? green.withAlpha (0.85f) : juce::Colour (0xff737b85));
        g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());
        g.fillEllipse (x - 4.0f, dbToY (-3.0) - 4.0f, 8.0f, 8.0f);
    }

    if (showSuggestion)
    {
        juce::Path proposed;
        for (int i = 0; i < points; ++i)
        {
            const double hz = metaleq::graph::unitToFrequency (i / static_cast<double> (points - 1));
            const float x = freqToX (hz);
            const float y = dbToY (processor.getAnalyzerResponseDb (hz, suggestionAmount));
            if (i == 0) proposed.startNewSubPath (x, y); else proposed.lineTo (x, y);
        }
        g.setColour (green);
        g.strokePath (proposed, juce::PathStrokeType (1.8f));
    }

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText ("CURRENT EQ", 48, 3, 94, 15, juce::Justification::centredLeft);
    if (showSuggestion)
    {
        g.setColour (green);
        g.drawText ("SUGGESTED", 146, 3, 90, 15, juce::Justification::centredLeft);
    }
}

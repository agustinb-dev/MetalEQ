#include "PluginEditor.h"

namespace
{
    constexpr float pi = 3.14159265358979323846f;
}

MetalEQAudioProcessorEditor::MetalEQAudioProcessorEditor (MetalEQAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor), eqGraph (processor)
{
    setSize (920, 760);
    setResizable (true, true);
    setResizeLimits (760, 650, 1300, 1000);

    addAndMakeVisible (eqGraph);

    auto& state = audioProcessor.getValueTreeState();
    const auto addKnob = [this] (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 18);
        slider.setTooltip (text);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    };

    addKnob (tightSlider, tightLabel, "TIGHT");
    addKnob (bodySlider, bodyLabel, "BODY");
    addKnob (scoopSlider, scoopMacroLabel, "SCOOP");
    addKnob (biteSlider, biteLabel, "BITE");
    addKnob (fizzSlider, fizzLabel, "FIZZ CUT");
    addKnob (trimSlider, trimLabel, "OUTPUT TRIM");
    trimSlider.setTextValueSuffix (" dB");

    tightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::tight, tightSlider);
    bodyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::body, bodySlider);
    scoopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::scoop, scoopSlider);
    biteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::bite, biteSlider);
    fizzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::fizzCut, fizzSlider);
    trimAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, MetalEQAudioProcessor::ParameterIDs::trim, trimSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, MetalEQAudioProcessor::ParameterIDs::bypass, bypassButton);
    abAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, MetalEQAudioProcessor::ParameterIDs::abCompare, abButton);

    for (auto* button : { &tightenButton, &scoopButton, &midPunchButton, &pickAttackButton,
                          &fizzButton, &brightenButton, &resetButton, &captureButton, &applyButton })
        addAndMakeVisible (*button);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (abButton);

    presetLabel.setText ("Factory preset", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);
    addAndMakeVisible (presetCombo);
    presetCombo.addItem ("Custom", 1);
    for (int i = 0; i < metaleq::getNumPresets(); ++i)
        presetCombo.addItem (metaleq::getPreset (i).name, i + 2);
    presetCombo.onChange = [this]
    {
        if (presetCombo.getSelectedId() > 0)
            audioProcessor.applyPreset (presetCombo.getSelectedId() - 1);
    };

    targetLabel.setText ("Target profile", juce::dontSendNotification);
    targetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (targetLabel);
    addAndMakeVisible (targetCombo);
    targetCombo.addItem ("Metalcore · Tight Rhythm", 1);
    targetCombo.addItem ("Thrash · Technical Clarity", 2);
    targetCombo.addItem ("Classic Thrash · Midrange Punch", 3);
    targetCombo.addItem ("Post-Amp · Mix Cleanup", 4);
    targetCombo.setSelectedId (1, juce::dontSendNotification);

    amountLabel.setText ("Amount", juce::dontSendNotification);
    amountLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (amountLabel);
    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setRange (0.0, 1.0, 0.01);
    amountSlider.setValue (0.75, juce::dontSendNotification);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
    addAndMakeVisible (amountSlider);

    analyzerStatusLabel.setJustificationType (juce::Justification::centredLeft);
    analyzerDisclaimer.setText (
        "Average-spectrum starting suggestion compared with the selected target. This does not recognize the source genre.",
        juce::dontSendNotification);
    analyzerDisclaimer.setJustificationType (juce::Justification::centredLeft);
    analyzerDisclaimer.setFont (juce::Font (juce::FontOptions (12.0f)));
    analyzerDisclaimer.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (analyzerStatusLabel);
    addAndMakeVisible (analyzerDisclaimer);

    tightenButton.onClick = [this] { applyShape (0); };
    scoopButton.onClick = [this] { applyShape (1); };
    midPunchButton.onClick = [this] { applyShape (2); };
    pickAttackButton.onClick = [this] { applyShape (3); };
    fizzButton.onClick = [this] { applyShape (4); };
    brightenButton.onClick = [this] { applyShape (5); };
    resetButton.onClick = [this] { applyShape (6); };
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

    presetCombo.setSelectedId (audioProcessor.getSelectedPresetIndex() == 0
        ? 1 : audioProcessor.getSelectedPresetIndex() + 1, juce::dontSendNotification);
    startTimerHz (15);
    updateAnalyzerUi();
}

MetalEQAudioProcessorEditor::~MetalEQAudioProcessorEditor() = default;

void MetalEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171b));
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (24.0f).withStyle ("Bold")));
    g.drawText ("MetalEQ", 20, 12, 240, 34, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (juce::Colours::lightgrey);
    g.drawText ("Zero-latency metal guitar EQ", 150, 20, 260, 22, juce::Justification::centredLeft);
}

void MetalEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (34);
    auto presetRow = area.removeFromTop (34);
    presetLabel.setBounds (presetRow.removeFromLeft (110));
    presetCombo.setBounds (presetRow.removeFromLeft (430).reduced (4, 2));
    bypassButton.setBounds (presetRow.removeFromLeft (100).reduced (4, 2));
    abButton.setBounds (presetRow.removeFromLeft (90).reduced (4, 2));

    eqGraph.setBounds (area.removeFromTop (255).reduced (0, 8));

    auto knobs = area.removeFromTop (132);
    const int knobWidth = knobs.getWidth() / 6;
    juce::Slider* sliders[] = { &tightSlider, &bodySlider, &scoopSlider, &biteSlider, &fizzSlider, &trimSlider };
    juce::Label* labels[] = { &tightLabel, &bodyLabel, &scoopMacroLabel, &biteLabel, &fizzLabel, &trimLabel };
    for (int i = 0; i < 6; ++i)
    {
        auto cell = knobs.removeFromLeft (i == 5 ? knobs.getWidth() : knobWidth);
        labels[i]->setBounds (cell.removeFromTop (22));
        sliders[i]->setBounds (cell.reduced (8, 0));
    }

    auto shapes = area.removeFromTop (38);
    juce::TextButton* buttons[] = { &tightenButton, &scoopButton, &midPunchButton, &pickAttackButton,
                                    &fizzButton, &brightenButton, &resetButton };
    for (int i = 0; i < 7; ++i)
        buttons[i]->setBounds (shapes.removeFromLeft (shapes.getWidth() / (7 - i)).reduced (3, 2));

    auto analyzer = area.reduced (0, 8);
    auto targetRow = analyzer.removeFromTop (32);
    targetLabel.setBounds (targetRow.removeFromLeft (110));
    targetCombo.setBounds (targetRow.removeFromLeft (310).reduced (4, 2));
    captureButton.setBounds (targetRow.removeFromLeft (180).reduced (4, 2));
    amountLabel.setBounds (targetRow.removeFromLeft (68));
    amountSlider.setBounds (targetRow.reduced (4, 2));
    analyzerStatusLabel.setBounds (analyzer.removeFromTop (24));
    analyzerDisclaimer.setBounds (analyzer.removeFromTop (24));
    applyButton.setBounds (analyzer.removeFromTop (34).removeFromLeft (180).reduced (3, 2));
}

void MetalEQAudioProcessorEditor::timerCallback()
{
    const int selected = audioProcessor.getSelectedPresetIndex();
    const int comboId = selected == 0 ? 1 : selected + 1;
    if (presetCombo.getSelectedId() != comboId)
        presetCombo.setSelectedId (comboId, juce::dontSendNotification);
    updateAnalyzerUi();
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
            + "%  ·  play audio in the host; capture is capped at 8 seconds.", juce::dontSendNotification);
    else if (status == Status::analyzing)
        analyzerStatusLabel.setText ("Analyzing captured average spectrum…", juce::dontSendNotification);
    else if (ready)
        analyzerStatusLabel.setText ("Suggested band gains: "
            + juce::String (suggestion[0], 1) + " / " + juce::String (suggestion[1], 1) + " / "
            + juce::String (suggestion[2], 1) + " / " + juce::String (suggestion[3], 1) + " / "
            + juce::String (suggestion[4], 1) + " dB", juce::dontSendNotification);
    else if (status == Status::failed)
        analyzerStatusLabel.setText ("No usable audio was captured. Play audio and try again.", juce::dontSendNotification);
    else
        analyzerStatusLabel.setText ("Ready. Capture runs only while the host sends audio.", juce::dontSendNotification);
}

void MetalEQAudioProcessorEditor::applyShape (int shapeIndex)
{
    audioProcessor.applyShape (shapeIndex);
    presetCombo.setSelectedId (1, juce::dontSendNotification);
}

void MetalEQAudioProcessorEditor::setParameterPlainValue (const char* id, float value)
{
    if (auto* parameter = audioProcessor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->getNormalisableRange().convertTo0to1 (value));
}

void MetalEQAudioProcessorEditor::EQGraph::setSuggestion (
    float amount, bool visible)
{
    suggestionAmount = amount;
    showSuggestion = visible;
}

void MetalEQAudioProcessorEditor::EQGraph::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff090a0d));
    g.setColour (juce::Colour (0xff30343a));
    for (int i = 0; i <= 4; ++i)
    {
        const float y = bounds.getY() + bounds.getHeight() * i / 4.0f;
        g.drawHorizontalLine (static_cast<int> (y), bounds.getX(), bounds.getRight());
    }
    for (int i = 0; i <= 6; ++i)
    {
        const float x = bounds.getX() + bounds.getWidth() * i / 6.0f;
        g.drawVerticalLine (static_cast<int> (x), bounds.getY(), bounds.getBottom());
    }

    const auto freqToX = [bounds] (double freq)
    {
        return bounds.getX() + bounds.getWidth() * static_cast<float> (
            std::log (freq / 20.0) / std::log (20000.0 / 20.0));
    };
    const auto dbToY = [bounds] (double db)
    {
        return bounds.getY() + bounds.getHeight() * (1.0f - static_cast<float> ((db + 18.0) / 30.0));
    };

    juce::Path response;
    constexpr int points = 300;
    for (int i = 0; i < points; ++i)
    {
        const double freq = 20.0 * std::pow (1000.0, i / static_cast<double> (points - 1));
        const float x = freqToX (freq);
        const float y = dbToY (processor.getResponseDb (freq));
        if (i == 0) response.startNewSubPath (x, y); else response.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xffff765e));
    g.strokePath (response, juce::PathStrokeType (2.2f));

    if (showSuggestion)
    {
        juce::Path proposed;
        for (int i = 0; i < points; ++i)
        {
            const double freq = 20.0 * std::pow (1000.0, i / static_cast<double> (points - 1));
            const float x = freqToX (freq);
            const float y = dbToY (processor.getAnalyzerResponseDb (freq, suggestionAmount));
            if (i == 0) proposed.startNewSubPath (x, y); else proposed.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xff69d6a6));
        g.strokePath (proposed, juce::PathStrokeType (1.8f));
    }

    g.setColour (juce::Colours::lightgrey);
    g.setFont (11.0f);
    g.drawText ("20 Hz", 4, getHeight() - 18, 50, 14, juce::Justification::left);
    g.drawText ("20 kHz", getWidth() - 55, getHeight() - 18, 50, 14, juce::Justification::right);
    g.setColour (juce::Colour (0xffff765e));
    g.drawText ("Current EQ", 12, 8, 90, 16, juce::Justification::left);
    if (showSuggestion)
    {
        g.setColour (juce::Colour (0xff69d6a6));
        g.drawText ("Suggested", 106, 8, 90, 16, juce::Justification::left);
    }
}

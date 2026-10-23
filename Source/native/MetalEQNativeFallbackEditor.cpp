#include "MetalEQNativeFallbackEditor.h"

namespace
{
constexpr juce::uint32 background = 0xff111419;
constexpr juce::uint32 panel      = 0xff1a1e24;
constexpr juce::uint32 accent     = 0xff2dd4bf;
constexpr juce::uint32 text       = 0xffe7eaee;
constexpr juce::uint32 textDim    = 0xff9aa1aa;

constexpr int customItemId = 1;
constexpr int firstFactoryItemId = 2;
constexpr int firstUserItemId = 1000;
} // namespace

//==============================================================================
MetalEQNativeFallbackEditor::MetalEQNativeFallbackEditor (MetalEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (720, 380);

    titleLabel.setText ("MetalEQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (text));
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("Native compatibility mode - the web UI could not start.",
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
    addAndMakeVisible (statusLabel);

    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
    addAndMakeVisible (presetLabel);

    presetCombo.setTooltip ("Factory presets are grouped by genre in the full UI. "
                            "User presets are stored in your app-data folder.");
    presetCombo.onChange = [this] { presetSelected(); };
    addAndMakeVisible (presetCombo);

    saveButton.onClick = [this] { saveAsPreset(); };
    addAndMakeVisible (saveButton);

    bypassButton.setTooltip ("Bypass the EQ without changing any settings.");
    addAndMakeVisible (bypassButton);

    resetButton.setTooltip ("Reset the active A/B slot to a neutral EQ.");
    resetButton.onClick = [this] { processor.resetToNeutral(); refreshFromProcessor(); };
    addAndMakeVisible (resetButton);

    abLabel.setText ("REAL A/B", juce::dontSendNotification);
    abLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
    addAndMakeVisible (abLabel);

    aButton.setTooltip ("Listen to slot A. Both slots keep their own EQ, macros and shapes.");
    bButton.setTooltip ("Listen to slot B. Both slots keep their own EQ, macros and shapes.");
    aButton.onClick = [this] { processor.setAbSlot (0); refreshFromProcessor(); };
    bButton.onClick = [this] { processor.setAbSlot (1); refreshFromProcessor(); };
    addAndMakeVisible (aButton);
    addAndMakeVisible (bButton);

    copyAbButton.setTooltip ("Copy the active slot's sound to the other slot.");
    copyAbButton.onClick = [this]
    {
        processor.copyActiveAbToOther();
        refreshFromProcessor();
    };
    addAndMakeVisible (copyAbButton);

    swapAbButton.setTooltip ("Exchange the complete A and B sounds.");
    swapAbButton.onClick = [this]
    {
        processor.swapAbSlots();
        refreshFromProcessor();
    };
    addAndMakeVisible (swapAbButton);

    diagnosticsLabel.setColour (juce::Label::textColourId, juce::Colour (textDim));
    diagnosticsLabel.setJustificationType (juce::Justification::topLeft);
    diagnosticsLabel.setText ("All parameters remain automatable and are saved by the host. "
                              "Use the host's generic parameter panel for detailed editing.",
                              juce::dontSendNotification);
    addAndMakeVisible (diagnosticsLabel);

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getValueTreeState(), MetalEQAudioProcessor::ParameterIDs::bypass, bypassButton);

    refreshPresetList();
    refreshFromProcessor();
    startTimerHz (8);
}

MetalEQNativeFallbackEditor::~MetalEQNativeFallbackEditor() = default;

//==============================================================================
void MetalEQNativeFallbackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (background));

    auto bounds = getLocalBounds().reduced (20).toFloat();
    g.setColour (juce::Colour (panel));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (juce::Colour (accent));
    g.fillRoundedRectangle (bounds.getX() + 18.0f, bounds.getY() + 22.0f, 4.0f, 30.0f, 2.0f);
}

void MetalEQNativeFallbackEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (16);
    area.reduce (18, 0);

    titleLabel.setBounds (area.removeFromTop (32));
    statusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (14);

    auto presetRow = area.removeFromTop (30);
    presetLabel.setBounds (presetRow.removeFromLeft (70));
    saveButton.setBounds (presetRow.removeFromRight (100));
    presetRow.removeFromRight (8);
    presetCombo.setBounds (presetRow);

    area.removeFromTop (14);

    auto utilityRow = area.removeFromTop (30);
    bypassButton.setBounds (utilityRow.removeFromLeft (100));
    utilityRow.removeFromLeft (8);
    resetButton.setBounds (utilityRow.removeFromLeft (160));

    area.removeFromTop (18);

    abLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (4);

    auto abRow = area.removeFromTop (32);
    aButton.setBounds (abRow.removeFromLeft (52));
    abRow.removeFromLeft (6);
    bButton.setBounds (abRow.removeFromLeft (52));
    abRow.removeFromLeft (14);
    copyAbButton.setBounds (abRow.removeFromLeft (170));
    abRow.removeFromLeft (6);
    swapAbButton.setBounds (abRow.removeFromLeft (110));

    area.removeFromTop (18);
    diagnosticsLabel.setBounds (area);
}

//==============================================================================
void MetalEQNativeFallbackEditor::timerCallback()
{
    auto names = processor.getUserPresetNames();
    if (names != userPresetNames)
        refreshPresetList();

    refreshFromProcessor();
}

void MetalEQNativeFallbackEditor::refreshPresetList()
{
    userPresetNames = processor.getUserPresetNames();

    updatingPresetCombo = true;
    presetCombo.clear (juce::dontSendNotification);
    presetCombo.addItem ("Custom / Neutral", customItemId);

    for (int i = 0; i < metaleq::getNumPresets(); ++i)
        presetCombo.addItem (metaleq::getPreset (i).name, firstFactoryItemId + i);

    if (! userPresetNames.isEmpty())
    {
        presetCombo.addSeparator();
        for (int i = 0; i < userPresetNames.size(); ++i)
            presetCombo.addItem ("User: " + userPresetNames[i], firstUserItemId + i);
    }

    updatingPresetCombo = false;
    refreshFromProcessor();
}

void MetalEQNativeFallbackEditor::refreshFromProcessor()
{
    const auto state = processor.getDisplayState();

    if (! updatingPresetCombo && ! presetCombo.isPopupActive())
    {
        int itemId = customItemId;

        if (state.userPreset)
        {
            const int userIndex = userPresetNames.indexOf (state.presetName, true);
            if (userIndex >= 0)
                itemId = firstUserItemId + userIndex;
        }
        else if (! state.presetId.isEmpty())
        {
            const int factory = metaleq::findPresetById (state.presetId.toRawUTF8());
            if (factory >= 0)
                itemId = firstFactoryItemId + factory;
        }

        if (presetCombo.getSelectedId() != itemId)
            presetCombo.setSelectedId (itemId, juce::dontSendNotification);
    }

    const auto active = processor.getActiveAbSlot();
    aButton.setColour (juce::TextButton::buttonColourId,
                       active == 0 ? juce::Colour (accent) : juce::Colour (panel));
    bButton.setColour (juce::TextButton::buttonColourId,
                       active == 1 ? juce::Colour (accent) : juce::Colour (panel));

    const auto kind = state.userPreset ? "USER" : (state.presetIndex > 0 ? "FACTORY" : "CUSTOM");
    const auto dirty = state.presetDirty ? " *" : "";
    diagnosticsLabel.setText (
        juce::String ("Active slot: ") + (active == 0 ? "A" : "B")
        + "   |   " + kind + dirty
        + "   |   " + state.presetName
        + "\nAll parameters remain automatable and are saved by the host.",
        juce::dontSendNotification);
}

void MetalEQNativeFallbackEditor::presetSelected()
{
    if (updatingPresetCombo)
        return;

    const int itemId = presetCombo.getSelectedId();
    if (itemId == customItemId)
    {
        processor.applyPreset (0);
    }
    else if (itemId >= firstFactoryItemId && itemId < firstFactoryItemId + metaleq::getNumPresets())
    {
        // Item 2 is factory[0], which is canonical preset index 1.
        processor.applyPreset (itemId - firstFactoryItemId + 1);
    }
    else if (itemId >= firstUserItemId)
    {
        const int index = itemId - firstUserItemId;
        if (juce::isPositiveAndBelow (index, userPresetNames.size()))
        {
            juce::String error;
            if (! processor.loadUserPreset (userPresetNames[index], error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "MetalEQ user preset", error);
        }
    }

    refreshFromProcessor();
}

void MetalEQNativeFallbackEditor::saveAsPreset()
{
    auto* window = new juce::AlertWindow ("Save user preset",
                                          "Choose a name for the active sound.",
                                          juce::MessageBoxIconType::NoIcon,
                                          this);
    window->addTextEditor ("name", "My Guitar Tone", "Preset name:");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window] (int result)
        {
            if (result != 1)
                return;

            const auto name = window->getTextEditorContents ("name").trim();
            if (name.isEmpty())
                return;

            juce::String error;
            if (! processor.saveUserPreset (name, false, error))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "MetalEQ user preset", error);
                return;
            }

            refreshPresetList();
        }), true);
}

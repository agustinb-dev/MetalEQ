/*
    MetalEQ - WebView-based plugin editor.

    The editor hosts the React front-end (Gui/, packaged into the binary via
    CMake + juce_add_binary_data) in a juce::WebBrowserComponent and talks to
    it through metaleq::ui::UiBridge (protocol documented in MetalEqUiBridge.h).

    Responsibilities kept on the native side:
    * the audio-thread-free tick timer (UiBridge::tick at 20 Hz),
    * loading the page from the embedded archive and refusing navigation
      anywhere else,
    * a watchdog: if the page never calls uiReady() (broken WebView2 runtime,
      missing assets) the editor swaps in the preserved native UI
      (MetalEQNativeFallbackEditor) so the plugin always remains usable,
    * resizing the host window when the front-end opens the Advanced view.

    The native fallback runs as a child component; its setSize() calls are
    propagated to this top-level editor via a ComponentListener so window
    resize semantics match the original editor exactly.
*/

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "native/MetalEQNativeFallbackEditor.h"
#include "ui/MetalEqUiBridge.h"

class MetalEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer,
                                          private juce::ComponentListener
{
public:
    explicit MetalEQAudioProcessorEditor (MetalEQAudioProcessor&);
    ~MetalEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Host-drag support: JUCE hosts call this while a resize is in flight. */
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

private:
    //==========================================================================
    /** WebBrowserComponent restricted to our own pages (see pageAboutToLoad). */
    class UiBrowser final : public juce::WebBrowserComponent
    {
    public:
        using WebBrowserComponent::WebBrowserComponent;

        bool pageAboutToLoad (const juce::String& newURL) override;
    };

    void timerCallback() override;

    // ComponentListener for the nested native fallback editor: forwards its
    // resize requests to this top-level editor.
    void componentMovedOrResized (juce::Component& component, bool, bool) override;
    void componentParentHierarchyChanged (juce::Component& component) override;

    void buildWebview();
    void attachNativeFallback();
    void detachNativeFallback();
    void updateCollapsedHeight (bool expanded);

    MetalEQAudioProcessor& processorRef;

    metaleq::ui::UiBridge bridge { processorRef };

    UiBrowser webComponent;
    std::unique_ptr<MetalEQNativeFallbackEditor> fallbackEditor;

    // Advanced-view expansion: the WebView asks for extra height for the
    // numeric controls, shelves and analyzer.
    int collapsedHeight = 900;
    bool advancedExpanded = false;
    bool fallbackActive = false;

    // uiReady watchdog: if the page has not checked in after a few seconds
    // we assume it cannot run and switch to the native fallback.
    static constexpr int watchdogTicksLimit = 100; // 5 s at 20 Hz
    int watchdogTicks = 0;

    bool wasVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEQAudioProcessorEditor)
};

#include "PluginEditor.h"
#include "ui/MetalEqWebUiResources.h"

namespace
{
    // Kept in sync with the front-end's ?dev server (Gui/vite.config.ts).
    // Only reachable when METALEQ_UI_DEV_SERVER is defined (development
    // builds); production builds never allow http(s) navigation.
   #if METALEQ_UI_DEV_SERVER
    const juce::String localDevServerAddress { "http://localhost:5173/" };
   #endif

    const juce::Colour background { 0xff111419 };
}

// The web UI runs on WebView2 on Windows and on the system WebKitGTK engine
// everywhere else. The two backends expose different option structs, so the
// platform-specific parts are assembled by a helper that returns a
// fully-built Options object.
static juce::WebBrowserComponent::Options makeWebUiOptions()
{
   #if defined (_WIN32) || defined (_WIN64)
    return juce::WebBrowserComponent::Options {}
                .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                .withWinWebView2Options (
                    juce::WebBrowserComponent::Options::WinWebView2 {}
                        .withUserDataFolder (juce::File::getSpecialLocation (
                            juce::File::SpecialLocationType::tempDirectory)
                                                .getChildFile ("MetalEQ-WebView2")));
   #else
    // There is no explicit "webkit" backend: defaultBackend resolves to
    // gtk-webkit2 on Linux and WKWebView on macOS, which is what we want
    // everywhere that is not Windows.
    return juce::WebBrowserComponent::Options {}
                .withBackend (juce::WebBrowserComponent::Options::Backend::defaultBackend);
   #endif
}

//==============================================================================
bool MetalEQAudioProcessorEditor::UiBrowser::pageAboutToLoad (const juce::String& newURL)
{
    // Single-page app: allow our own resource root (and, in development
    // builds, the Vite dev server) and refuse everything else, so a stray
    // link can never navigate the plugin's browser to the internet.
    if (newURL.startsWith (getResourceProviderRoot()))
        return true;

   #if METALEQ_UI_DEV_SERVER
    if (newURL.startsWith (localDevServerAddress))
        return true;
   #endif

    return newURL == "about:blank";
}

//==============================================================================
MetalEQAudioProcessorEditor::MetalEQAudioProcessorEditor (MetalEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p),
      webComponent (makeWebUiOptions()
                        .withNativeIntegrationEnabled()
                        .withOptionsFrom (bridge)
                        .withResourceProvider (
                            [] (const juce::String& url) { return metaleq::ui::findWebUiResource (url); },
                            juce::WebBrowserComponent::getResourceProviderRoot()))
{
    setOpaque (true);

    bridge.setWebComponent (&webComponent);
    bridge.setExpansionHandler ([this] (bool expanded)
    {
        updateCollapsedHeight (expanded);
    });

    addAndMakeVisible (webComponent);
    webComponent.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setSize (960, 900);
    setResizable (true, true);
    setResizeLimits (780, 790, 1440, 1280);

    startTimerHz (metaleq::ui::UiBridge::tickHz);
}

MetalEQAudioProcessorEditor::~MetalEQAudioProcessorEditor()
{
    stopTimer();
    detachNativeFallback();
    bridge.setWebComponent (nullptr);
    bridge.setExpansionHandler (nullptr);
}

//==============================================================================
void MetalEQAudioProcessorEditor::timerCallback()
{
    if (fallbackActive)
        return;

    // Watchdog: the page is expected to call uiReady() shortly after load.
    // If it never does (WebView2 runtime missing, assets broken) hand the
    // user the native UI instead of an unresponsive window.
    if (! bridge.isReady())
    {
        if (++watchdogTicks >= watchdogTicksLimit)
        {
            juce::Logger::writeToLog ("MetalEQ: WebView UI did not initialise; using the native editor.");
            attachNativeFallback();
        }
        return;
    }

    bridge.tick();
}

void MetalEQAudioProcessorEditor::visibilityChanged()
{
    // Browsers pause timers while hidden; when the editor becomes visible
    // again the page may have missed ticks (or been recreated by the host),
    // so force a full resync either way.
    if (isVisible() && ! wasVisible && bridge.isReady())
        bridge.forceFullSync();

    wasVisible = isVisible();
}

void MetalEQAudioProcessorEditor::parentHierarchyChanged()
{
    visibilityChanged();
}

//==============================================================================
void MetalEQAudioProcessorEditor::updateCollapsedHeight (bool expanded)
{
    if (fineExpanded == expanded)
        return;

    fineExpanded = expanded;

    if (expanded)
        collapsedHeight = juce::jmin (getHeight(), 1280);

    // Same rule as the native FINE EQ button: +220 px, clamped to the
    // resize limits, so the host window grows exactly like before.
    const auto targetHeight = expanded ? juce::jlimit (1010, 1280, collapsedHeight + 220)
                                       : collapsedHeight;
    setSize (getWidth(), targetHeight);
}

void MetalEQAudioProcessorEditor::attachNativeFallback()
{
    if (fallbackActive)
        return;

    fallbackActive = true;
    stopTimer();

    // The native editor is embedded as a plain child; its own resize calls
    // (FINE EQ expand) are forwarded to this editor by the ComponentListener.
    fallbackEditor = std::make_unique<MetalEQNativeFallbackEditor> (processorRef);
    fallbackEditor->addComponentListener (this);
    addAndMakeVisible (*fallbackEditor);
    fallbackEditor->setBounds (getLocalBounds());

    webComponent.setVisible (false);
    resized();
}

void MetalEQAudioProcessorEditor::detachNativeFallback()
{
    if (fallbackEditor != nullptr)
    {
        fallbackEditor->removeComponentListener (this);
        fallbackEditor.reset();
    }
    fallbackActive = false;
}

void MetalEQAudioProcessorEditor::componentMovedOrResized (juce::Component& component, bool /*wasMoved*/, bool /*wasResized*/)
{
    // The nested fallback editor asked for a new size (its FINE EQ button):
    // grow/shrink this top-level editor to match, like the original window.
    if (&component == fallbackEditor.get())
    {
        const auto w = component.getWidth();
        const auto h = component.getHeight();
        if (w > 0 && h > 0 && (w != getWidth() || h != getHeight()))
            setSize (w, h);
    }
}

void MetalEQAudioProcessorEditor::componentParentHierarchyChanged (juce::Component& component)
{
    if (&component == fallbackEditor.get() && fallbackEditor != nullptr && fallbackEditor->getParentComponent() == this)
        fallbackEditor->setBounds (getLocalBounds());
}

//==============================================================================
void MetalEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void MetalEQAudioProcessorEditor::resized()
{
    if (fallbackEditor != nullptr)
    {
        fallbackEditor->setBounds (getLocalBounds());
        return;
    }

    webComponent.setBounds (getLocalBounds());
}

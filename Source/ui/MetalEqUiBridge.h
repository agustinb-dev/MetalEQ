/*
    MetalEQ - C++ <-> JavaScript communication bridge.

    The bridge owns the parameter synchronisation protocol between the
    AudioProcessor (authoritative) and the React front-end hosted in a
    juce::WebBrowserComponent.

    Threading contract
    -----------------
    * Every member of this class runs on the message thread only: native
      functions are invoked on the message thread by JUCE, tick() is called
      from the editor's timer, and all processor access goes through the
      message-thread API of MetalEQAudioProcessor.
    * The audio thread never touches the bridge, the WebView, or any state
      owned here. Parameters travel exclusively through APVTS atomics.

    Protocol (version 2)
    --------------------
    JS -> C++ via registered native functions (promise based):
        uiReady()                              -> full initial snapshot
        setParameter (id, plainValue)          -> { ok, value, error? }
        applyPreset (canonicalIndex)           // 0 Custom, 1..N factory
        applyPresetById (stableId)
        loadUserPreset (name)                  -> { ok, error? }
        saveUserPreset (name, overwrite)       -> { ok, error? }
        renameUserPreset (old, new)            -> { ok, error? }
        deleteUserPreset (name)                -> { ok, error? }
        revertToPreset()
        resetToNeutral()
        setAbSlot (slot)                       // 0 = A, 1 = B
        copyAbToOther()
        swapAbSlots()
        startAnalyzerCapture (targetProfile)
        cancelAnalyzerCapture()
        applyAnalyzerSuggestion (amount)
        getSuggestionCurve (amount)            -> { ok, curve, error? }
        setEditorExpanded (expanded)           -> { ok } (Advanced view window grow)

    C++ -> JS via one event ("metaleq.tick") carrying a JSON object:
        {
          seq,            // monotonic tick counter
          params,         // { id: plainValue } - full map on the first tick
                          // and after forceFullSync(), diffs otherwise
          preset,         // { index, presetId, name, kind, dirty, abSlot } when changed
          userPresets,    // [ names ] when changed
          factoryPresets, // metadata on the uiReady snapshot only
          analyzerTargets,// stable target ids/labels on the uiReady snapshot only
          meters,         // { in, out } every tick (cheap atomics)
          analyzer,       // { status, progress, suggestion[5] } when changed
          curve           // { points: [dB x 240], minDb: -15, maxDb: 15 }
                          // when EQ-relevant params changed
        }

    Design rules
    ------------
    * JavaScript is never authoritative: it sends requests, the native side
      validates (known id, finite number, clamped to the parameter's
      NormalisableRange, snapped by the parameter itself) and the next tick
      carries back the authoritative value.
    * Only the operations listed above exist. Unknown ids and malformed
      values are rejected; nothing executes arbitrary JavaScript.
    * The response curve is computed natively with the exact DSP chain
      (metaleq::buildChain + chainResponseDb) that the audio thread uses,
      so the displayed curve cannot drift from the sound.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>
#include <vector>

#include "../PluginProcessor.h"

namespace metaleq::ui
{

//==============================================================================
/** Validates and normalises one JS -> C++ parameter write.
    Returns the clamped plain value, or std::nullopt when the id is unknown
    or the value is not a finite number.
*/
std::optional<double> validateParameterWrite (juce::AudioProcessorValueTreeState& apvts,
                                              const juce::String& id,
                                              const juce::var& value,
                                              juce::String& error);

//==============================================================================
/** Owns the JS <-> native protocol for one editor instance.

    Derives from juce::WebBrowserComponent::OptionsBuilder so the editor can
    hand it to Options::withOptionsFrom(), which routes every native-function
    call into addOptions() below.
*/
class UiBridge : public juce::OptionsBuilder<juce::WebBrowserComponent::Options>
{
public:
    static constexpr int protocolVersion = 2;
    static constexpr int numCurvePoints  = 240;
    static constexpr int tickHz          = 20;

    explicit UiBridge (MetalEQAudioProcessor& processor);
    ~UiBridge();

    /** Adds the bridge's native functions to a WebBrowserComponent options set. */
    juce::WebBrowserComponent::Options addOptions (juce::WebBrowserComponent::Options options);

    /** OptionsBuilder hook: lets the editor pass this bridge to
        WebBrowserComponent::Options::withOptionsFrom().
    */
    juce::WebBrowserComponent::Options buildOptions (const juce::WebBrowserComponent::Options& initialOptions) override
    {
        return addOptions (initialOptions);
    }

    /** The WebView that events are emitted to. Set by the editor before load. */
    void setWebComponent (juce::WebBrowserComponent* web) noexcept { webComponent = web; }

    /** Called by the front-end when the Advanced view opens/closes so the
        editor can resize the host window. Set by the editor; runs on the
        message thread.
    */
    void setExpansionHandler (std::function<void (bool)> handler) noexcept
    {
        expansionHandler = std::move (handler);
    }

    /** True once the front-end called uiReady(): ticks are suppressed until
        then so no event is ever emitted before its listener exists.
    */
    bool isReady() const noexcept { return uiReady; }

    /** Resets diff tracking so the next tick re-sends everything. Call after
        the WebView was hidden/shown or reloaded.
    */
    void forceFullSync() noexcept { fullSyncRequired = true; }

    /** Clears the ready flag (page (re)loaded: JS state is gone). */
    void onPageReload() noexcept { uiReady = false; }

    /** Emits one state tick. Called from the editor timer at tickHz. */
    void tick();

private:
    //==========================================================================
    // Native function handlers. All return a JSON-serialisable juce::var.
    juce::var handleUiReady();
    juce::var handleSetParameter (const juce::Array<juce::var>& args);
    juce::var handleApplyPreset (const juce::Array<juce::var>& args);
    juce::var handleApplyPresetById (const juce::Array<juce::var>& args);
    juce::var handleLoadUserPreset (const juce::Array<juce::var>& args);
    juce::var handleSaveUserPreset (const juce::Array<juce::var>& args);
    juce::var handleRenameUserPreset (const juce::Array<juce::var>& args);
    juce::var handleDeleteUserPreset (const juce::Array<juce::var>& args);
    juce::var handleRevertToPreset();
    juce::var handleResetToNeutral();
    juce::var handleSetAbSlot (const juce::Array<juce::var>& args);
    juce::var handleCopyAbToOther();
    juce::var handleSwapAbSlots();
    juce::var handleStartAnalyzerCapture (const juce::Array<juce::var>& args);
    juce::var handleCancelAnalyzerCapture();
    juce::var handleApplyAnalyzerSuggestion (const juce::Array<juce::var>& args);
    juce::var handleGetSuggestionCurve (const juce::Array<juce::var>& args);
    juce::var handleSetEditorExpanded (const juce::Array<juce::var>& args);

    //==========================================================================
    juce::var buildFullSnapshot();
    juce::var buildParamsObject (bool includeAll);
    juce::var buildPresetObject();
    juce::var buildAnalyzerObject();
    juce::var buildCurveArray();
    juce::var buildFactoryPresetList();
    juce::var buildAnalyzerTargetList();

    double readPlainValue (const char* paramId) const;
    void markUserPresetsChanged() noexcept { userPresetsDirty = true; }
    void markCurveDirty() noexcept { curveDirty = true; }

    static juce::var okResult();
    static juce::var errorResult (const juce::String& message);

    //==========================================================================
    MetalEQAudioProcessor& processor;
    juce::WebBrowserComponent* webComponent = nullptr;
    std::function<void (bool)> expansionHandler;

    bool uiReady = false;
    bool fullSyncRequired = true;
    bool curveDirty = true;
    bool userPresetsDirty = true;

    juce::int64 seq = 0;

    // Diff tracking: the authoritative plain value last delivered to JS.
    std::vector<std::pair<juce::String, double>> lastSentValues;
    std::array<float, metaleq::kNumBands> lastSuggestion {};
    int lastAnalyzerStatus = -1;
    float lastAnalyzerProgress = -1.0f;
    juce::StringArray lastUserPresets;
    double lastSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UiBridge)
};

} // namespace metaleq::ui

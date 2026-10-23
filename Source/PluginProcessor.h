/*
    MetalEQ - audio processor.

    Threading contract
    -----------------
    * The audio thread only ever *reads* atomic parameter values, rebuilds a
      fixed-size filter chain description and calls EqEngine::process(). It never
      allocates, locks, logs, touches the filesystem or calls into the GUI.
    * The message thread owns all parameter *writes* (host automation, GUI,
      preset recall) and the editor.
*/

#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "dsp/MetalEqDsp.h"
#include "dsp/MetalEqParams.h"
#include "dsp/MetalEqPresets.h"

//==============================================================================
class MetalEQAudioProcessor;

/** Message-thread listener that notices user edits so the preset display can
    switch to "Custom".
*/
class MetalEQAudioProcessor  : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    //==============================================================================
    MetalEQAudioProcessor();
    ~MetalEQAudioProcessor() override;

    //==============================================================================
    static constexpr const char* getPluginName()       { return "MetalEQ"; }
    static constexpr const char* getPluginManufacturer() { return "MetalEQ Audio"; }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override  { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    /** Zero lookahead, zero added latency: this is a linear-time IIR EQ. */
    bool supportsDoublePrecisionProcessing() const { return false; }

    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return apvts != nullptr ? apvts->getParameter (ParameterIDs::bypass) : nullptr;
    }

    //==============================================================================
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** Stable parameter identifiers. These strings are part of the saved state
        and of every host's automation map, so they must never change.
    */
    struct ParameterIDs
    {
        static const char* hpFreq;
        static const char* hpSlope;
        static const char* lpFreq;
        static const char* lpSlope;
        static const char* lpOn;
        static const char* lsFreq;
        static const char* lsGain;
        static const char* hsFreq;
        static const char* hsGain;
        static const char* bandFreq[metaleq::kNumBands];
        static const char* bandGain[metaleq::kNumBands];
        static const char* bandQ[metaleq::kNumBands];
        static const char* bandOn[metaleq::kNumBands];
        static const char* shapeOn[6];
        static const char* trim;
        static const char* tight;
        static const char* body;
        static const char* scoop;
        static const char* bite;
        static const char* fizzCut;
        static const char* preset;
        static const char* abCompare;
        static const char* bypass;

        static juce::StringArray allEqParameterIds();
    };

    //==============================================================================
    // Editor / automation support (all message-thread only)
    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return *apvts; }

    /** Recalls a factory preset by canonical index (0 = Custom, 1..N = factory). */
    void applyPreset (int index);

    /** Recalls a factory preset by its stable id. Returns false if unknown. */
    bool applyPresetById (const juce::String& presetId);

    void setLowPassEnabled (bool enabled);

    /** Real A/B comparison. Both slots store the complete EQ, macros and shape
        state; output trim deliberately stays global for level-matched listening.
    */
    static constexpr int numAbSlots = 2;
    int getActiveAbSlot() const noexcept { return activeAbSlot; }
    void setAbSlot (int slot);
    bool copyActiveAbToOther();
    bool swapAbSlots();

    /** Back to the fully neutral state, macros included. */
    void resetToNeutral();

    /** Name of the factory preset that was last recalled (or "Custom"). */
    juce::String getLastPresetName() const { return lastPresetName; }

    /** Stable id of the loaded factory preset, empty for Custom/user presets. */
    juce::String getLastPresetId() const { return lastPresetId; }

    /** Canonical selector index (0 = custom, 1..N = factory). */
    int getSelectedPresetIndex() const;

    struct DisplayState
    {
        metaleq::EqSettings     eq {};
        metaleq::MacroSettings macros {};
        juce::String            presetName;
        juce::String            presetId;
        int                     presetIndex = 0;
        int                     abSlot = 0;
        bool                    abCompare  = false;
        bool                    bypassed   = false;
        bool                    presetDirty = false;
        bool                    userPreset = false;
    };

    /** Snapshot of everything the GUI needs. Reads atomics only, so it is safe
        to call from the message thread while audio is running.
    */
    DisplayState getDisplayState() const;

    /** Sample rate to use for drawing: the live one, or a sane default before
        the host has called prepareToPlay().
    */
    double getDisplaySampleRate() const noexcept;

    /** Peak level of the last processed block, for the GUI meter. */
    float getPeakLevel (int channel) const noexcept;
    float getInputPeakLevel (int channel) const noexcept;

    void setMeteringEnabled (bool shouldBeEnabled) noexcept
        {
            meteringEnabled.store (shouldBeEnabled, std::memory_order_relaxed);
        }

    /** True when the last recalled preset has been modified by the user. */
    bool isPresetModified() const;

    // User-preset file operations are message-thread-only. They intentionally
    // store EQ parameters only; analyzer recordings and UI/runtime data never
    // enter the preset file.
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset (const juce::String& name, bool overwrite, juce::String& error);
    bool loadUserPreset (const juce::String& name, juce::String& error);
    bool renameUserPreset (const juce::String& oldName, const juce::String& newName, juce::String& error);
    bool deleteUserPreset (const juce::String& name, juce::String& error);
    void revertToLoadedPreset();
    bool isLoadedPresetUser() const noexcept { return loadedPresetIsUser; }
    juce::File getUserPresetDirectory() const;

    /** Frequency response in dB at `freq` for the current settings. */
    double getResponseDb (double freq) const;

    enum class AnalyzerStatus : int { idle = 0, capturing = 1, captureComplete = 2, analyzing = 3, ready = 4, failed = 5 };
    static constexpr int analyzerDurationSeconds = 8;
    void startAnalyzerCapture (int targetProfile);
    void cancelAnalyzerCapture() noexcept;
    AnalyzerStatus getAnalyzerStatus() const noexcept;
    float getAnalyzerProgress() const noexcept;
    void getAnalyzerSuggestion (std::array<float, metaleq::kNumBands>& gains) const noexcept;
    double getAnalyzerResponseDb (double freq, float amount) const;
    void applyAnalyzerSuggestion (float amount);

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void analyzerWorkerLoop();
    void analyzeCapture (int sampleCount, double sampleRate);
    void captureAnalyzerInput (const juce::AudioBuffer<float>& buffer) noexcept;
    void serviceDeferredAnalyzerStart() noexcept;
    void startAnalyzerCaptureWithGeneration (int targetProfile, unsigned long long cancelGeneration);

    metaleq::EqSettings     readEqSettings() const;
    metaleq::MacroSettings  readMacroSettings() const;
    static metaleq::EqSettings    neutralEqSettings();
    static metaleq::MacroSettings neutralMacroSettings();

    void applyEqSettings (const metaleq::EqSettings& eq, bool includeCrossovers);
    void applyMacroSettings (const metaleq::MacroSettings& macros);
    void applyNeutralParameters();
    void setFloatParameter (const char* id, double plainValue);
    void setChoiceParameter (const char* id, int index);
    juce::ValueTree getSoundParameterState (bool includeTrim = true) const;
    bool applySoundParameterState (const juce::ValueTree& state, juce::String& error,
                                   bool includeTrim = true);
    void rememberCurrentAsLoadedPreset (const juce::String& name, bool isUser);
    juce::File getUserPresetFile (const juce::String& name) const;
    static bool isSafePresetName (const juce::String& name);

    /** One half of the real A/B comparison. The active slot lives in APVTS;
        the other slot is a complete stored snapshot. */
    struct AbSlotSnapshot
    {
        juce::ValueTree params;
        juce::ValueTree loadedPresetState;
        juce::String presetName { "Custom" };
        juce::String presetId;
        bool userPreset = false;
        bool valid = false;
    };

    void captureActiveAbSlot();
    bool loadAbSlot (int slot);
    void switchAbSlotInternal (int slot, bool writeParameter);
    void syncPresetChoiceToLoadedIdentity();
    void serviceLowPassFix();
    void servicePendingAutomation();

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout;

    std::unique_ptr<juce::AudioProcessorValueTreeState> apvts;
    juce::String lastPresetName { "Custom" };
    juce::String lastPresetId;
    juce::ValueTree loadedPresetState;
    bool loadedPresetIsUser = false;

    std::array<AbSlotSnapshot, numAbSlots> abSlots;
    int activeAbSlot = 0;

    /** Guards against reacting to the parameter writes we make ourselves. */
    std::atomic<bool> applyingPreset { false };
    std::atomic<bool> meteringEnabled { false };

    // Host automation may arrive on any thread. The listener only stores an
    // intent; the actual parameter writes happen asynchronously on the message
    // thread through AsyncUpdater.
    std::atomic<int>  pendingPresetIndex { -1 };
    std::atomic<int>  pendingAbSlot { -1 };
    std::atomic<bool> pendingLowPassFix { false };

    // Audio-thread-only chain cache. The chain is rebuilt only when a relevant
    // parameter or the sample rate actually changes.
    metaleq::EqSettings    cachedEq {};
    metaleq::MacroSettings cachedMacros {};
    double                 cachedSampleRate = 0.0;
    bool                   chainCacheValid = false;

    static constexpr int maxCaptureRate = 192000;
    static constexpr int maxCaptureSamples = maxCaptureRate * analyzerDurationSeconds;
    enum class AnalyzerCaptureState : int { idle, arming, requested, recording, cancelPending, ready, analyzing, complete, failed };
    std::unique_ptr<float[]> analyzerCapture;
    std::atomic<AnalyzerCaptureState> analyzerCaptureState { AnalyzerCaptureState::idle };
    std::atomic<unsigned long long> analyzerCaptureGeneration { 0 };
    std::atomic<bool> analyzerCaptureWriterActive { false };
    std::atomic<bool> analyzerCaptureCancelled { false };
    std::atomic<bool> analyzerCaptureStartInProgress { false };
    std::atomic<bool> analyzerDeferredStartRequested { false };
    std::atomic<int> analyzerDeferredStartProfile { 0 };
    std::atomic<unsigned long long> analyzerCancelGeneration { 0 };
    std::atomic<int> analyzerSampleCount { 0 };
    std::atomic<int> analyzerCaptureLimit { maxCaptureSamples };
    std::atomic<int> analyzerTargetProfile { 0 };
    std::atomic<int> analyzerSampleRate { 48000 };
    std::array<std::atomic<float>, metaleq::kNumBands> analyzerSuggestion {};
    std::atomic<bool> stopAnalyzerWorker { false };
    std::thread analyzerWorker;

    metaleq::EqEngine engine;

    std::array<std::atomic<float>, metaleq::kMaxChannels> peakLevels {};
    std::array<std::atomic<float>, metaleq::kMaxChannels> inputPeakLevels {};

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEQAudioProcessor)
};

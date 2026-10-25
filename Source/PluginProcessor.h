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
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "dsp/MetalEqAnalysis.h"
#include "dsp/MetalEqDsp.h"
#include "dsp/MetalEqParams.h"
#include "dsp/MetalEqPresets.h"
#include "dsp/MetalEqToneTargets.h"

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
    bool supportsDoublePrecisionProcessing() const override { return false; }

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
        static const char* resFreq[metaleq::kNumSurgicalBands];
        static const char* resGain[metaleq::kNumSurgicalBands];
        static const char* resQ[metaleq::kNumSurgicalBands];
        static const char* resOn[metaleq::kNumSurgicalBands];
        static const char* dynFreq[metaleq::kNumDynamicBands];
        static const char* dynQ[metaleq::kNumDynamicBands];
        static const char* dynThreshold[metaleq::kNumDynamicBands];
        static const char* dynRatio[metaleq::kNumDynamicBands];
        static const char* dynAttack[metaleq::kNumDynamicBands];
        static const char* dynRelease[metaleq::kNumDynamicBands];
        static const char* dynRange[metaleq::kNumDynamicBands];
        static const char* dynOn[metaleq::kNumDynamicBands];
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
        static const char* resAudition;
        static const char* dynAudition;

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

    /** Current dynamic-band gain reduction in positive dB, for the UI meter. */
    float getDynamicGainReductionDb (int band) const noexcept;

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

    /** captureComplete is the short window between the audio thread finishing
        the recording and the worker thread picking the take up for analysis.
        It is emitted so the UI can show "analysing" rather than "idle" and
        disable a second start. */
    enum class AnalyzerStatus : int { idle = 0, capturing = 1, captureComplete = 2, analyzing = 3, ready = 4, failed = 5 };
    static constexpr int analyzerDurationSeconds = 8;
    /** Starts an 8-second capture. Returns false when a previous take is still
        being analysed, or when the shared capture buffer is otherwise busy. */
    bool startAnalyzerCapture (int targetProfile);
    void cancelAnalyzerCapture() noexcept;
    AnalyzerStatus getAnalyzerStatus() const noexcept;
    float getAnalyzerProgress() const noexcept;
    void getAnalyzerSuggestion (std::array<float, metaleq::kNumBands>& gains) const noexcept;
    double getAnalyzerResponseDb (double freq, float amount) const;
    /** Copies the current recommendation into the five musical bands.
        Returns false when no recommendation matches the selected target. */
    bool applyAnalyzerSuggestion (float amount);

    //--- V3.2 evidence-based analyzer ---------------------------------------
    /** Chooses a target without starting a capture. When a previous capture is
        still available, the correction is recalculated immediately, so the
        player can compare destinations on the same input.
    */
    void setAnalyzerTarget (int targetIndex);

    int getAnalyzerTargetIndex() const noexcept;
    /** Index of the target the current recommendation was computed for, or -1. */
    int getAnalyzerResultTarget() const noexcept;
    /** Monotonic revision, bumped whenever the recommendation changes. */
    unsigned long long getAnalyzerRevision() const noexcept;

    /** Separated input/target/correction data for the UI. */
    struct AnalyzerReport
    {
        std::array<float, metaleq::tone::kNumFeatures> input {};
        std::array<float, metaleq::tone::kNumFeatures> target {};
        std::array<float, metaleq::tone::kNumFeatures> predicted {};
        std::array<float, metaleq::kNumBands> gains {};
        float inputConfidence = 0.0f;
        float correctionConfidence = 0.0f;
        bool alreadyClose = false;
        bool valid = false;
    };
    bool getAnalyzerReport (AnalyzerReport& report) const noexcept;

    //--- V3 resonance scan ---------------------------------------------------
    /** The two resonance sections capture independently. Each target owns its
        own status, progress and result set, so a scan in one panel never
        replaces the suggestions shown by the other.
    */
    enum class ResonanceScanTarget : int { surgical = 0, dynamic = 1 };

    /** Starts an independent 8-second capture for one resonance section.
        Returns false when a previous scan is still being analysed.
    */
    bool startResonanceScan (ResonanceScanTarget target);
    void cancelResonanceScan (ResonanceScanTarget target) noexcept;
    AnalyzerStatus getResonanceStatus (ResonanceScanTarget target) const noexcept;
    float getResonanceProgress (ResonanceScanTarget target) const noexcept;
    bool getResonanceEnoughVariation (ResonanceScanTarget target) const noexcept;
    int getResonanceCandidateCount() const noexcept;
    bool getResonanceCandidate (int index, metaleq::ResonanceCandidate& candidate) const noexcept;
    /** Number of dynamic-band suggestions produced by the dynamic scan. */
    int getResonanceDynamicSuggestionCount() const noexcept;
    unsigned long long getResonanceRevision() const noexcept;
    /** Copies the selected surgical candidates into the six surgical bands,
        strongest selection first. Bands not covered by the selection are
        switched off.
    */
    void applyResonanceSelection (const std::vector<int>& candidateIndices);
    void clearSurgicalBands();
    /** Full dynamic-band suggestion for one index in the dynamic scan result. */
    bool getResonanceDynamicSuggestion (int index,
                                        metaleq::DynamicBandSettings& band,
                                        double& confidence,
                                        double& measuredExcessDb) const noexcept;
    /** Copies the selected dynamic suggestions into the four dynamic bands in
        selection order. Bands not covered by the selection are switched off.
    */
    void applyResonanceToDynamic (const std::vector<int>& candidateIndices);
    void clearDynamicBands();

    //==============================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void analyzerWorkerLoop();
    void analyzeCapture (int sampleCount, double sampleRate);
    void analyzeResonanceCapture (int sampleCount, double sampleRate, int target);
    void storeAnalyzerResult (const metaleq::tone::InputProfile& input,
                              const metaleq::tone::CorrectionResult& correction,
                              int targetIndex) noexcept;
    void clearAnalyzerResult() noexcept;
    bool readAnalyzerInput (metaleq::tone::InputProfile& input) const noexcept;
    void captureAnalyzerInput (const juce::AudioBuffer<float>& buffer) noexcept;
    void serviceDeferredAnalyzerStart() noexcept;
    bool startAnalyzerCaptureWithGeneration (int targetProfile, int mode, unsigned long long cancelGeneration);
    AnalyzerStatus captureStatus() const noexcept;
    static int resonanceTargetIndex (ResonanceScanTarget target) noexcept;
    static int resonanceAnalyzerMode (ResonanceScanTarget target) noexcept;
    static int resonanceTargetForAnalyzerMode (int mode) noexcept;
    void clearResonanceResults (ResonanceScanTarget target) noexcept;
    void publishResonanceResults (ResonanceScanTarget target,
                                  const metaleq::ResonanceAnalysisResult& result) noexcept;
    void setResonanceStatus (ResonanceScanTarget target, AnalyzerStatus status) noexcept;

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
    /** Appends defaults for V3 surgical and V3.1 dynamic parameters that did
        not exist when an older user preset or project state was written.
        Missing legacy fields are still rejected.
    */
    juce::ValueTree migrateSoundParameterState (const juce::ValueTree& state,
                                                bool includeTrim = true) const;
    static bool isOptionalResonanceParameterId (const juce::String& id);
    void markCurrentToneAsCustom();
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
    std::atomic<int> analyzerResultTarget { -1 };
    std::atomic<unsigned long long> analyzerRevision { 0 };
    std::atomic<bool> analyzerReportValid { false };
    std::atomic<int> analyzerSampleRate { 48000 };

    /** Guards the feature vectors. Written by the analyzer worker, read by the
        message thread. Never touched by the audio thread.
    */
    mutable juce::SpinLock analyzerReportLock;
    std::array<float, metaleq::tone::kNumFeatures> analyzerInputShape {};
    std::array<float, metaleq::tone::kNumFeatures> analyzerTargetShape {};
    std::array<float, metaleq::tone::kNumFeatures> analyzerPredictedShape {};
    float analyzerInputConfidence = 0.0f;
    float analyzerCorrectionConfidence = 0.0f;
    bool analyzerAlreadyClose = false;
    /** 0 = target-profile analyzer, 1 = surgical scan, 2 = dynamic scan. */
    std::atomic<int> analyzerMode { 0 };
    std::atomic<int> analyzerDeferredStartMode { 0 };
    std::array<std::atomic<float>, metaleq::kNumBands> analyzerSuggestion {};
    std::atomic<bool> stopAnalyzerWorker { false };
    std::thread analyzerWorker;

    //--- V3 resonance scan results ------------------------------------------
    // Surgical candidates and dynamic suggestions are captured separately.
    std::array<std::atomic<int>, 2> resonanceStatus {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> resonanceFreq {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> resonanceQ {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> resonanceProminence {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> resonanceResidual {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> resonanceConfidence {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> resonanceCut {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceFreq {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceQ {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceThreshold {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceRatio {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceAttack {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceRelease {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceMaxCut {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceConfidence {};
    std::array<std::atomic<float>, metaleq::kMaxResonanceCandidates> dynamicResonanceExcess {};
    std::atomic<int> resonanceCandidateCount { 0 };
    std::atomic<bool> resonanceEnoughVariation { false };
    std::atomic<int> dynamicResonanceCount { 0 };
    std::atomic<bool> dynamicResonanceEnoughVariation { false };
    std::atomic<unsigned long long> resonanceRevision { 0 };

    metaleq::EqEngine engine;

    // V3.1 zero-latency dynamic resonance section, applied after the static
    // chain. The audition engine is a second instance so the dynamic delta
    // monitor can run without sharing detector state with the main path.
    metaleq::DynamicResonanceEngine dynamicEngine;
    metaleq::DynamicResonanceEngine auditionDynamicEngine;

    // Section-specific delta monitors ("hear what this resonance section
    // removes"). The surgical monitor runs the inverse surgical chain to
    // recover the pre-surgical signal; the dynamic monitor runs a second
    // dynamic engine. Both engines and all scratch buffers are prepared on
    // the message thread; the audio thread only copies and multiplies in
    // preallocated memory. The two monitors are independent.
    metaleq::EqEngine auditionEngine;
    metaleq::EqSettings cachedAuditionEq {};
    double cachedAuditionSampleRate = 0.0;
    bool auditionChainCacheValid = false;
    bool wasAuditionSurgical = false;
    bool wasAuditionDynamic = false;
    juce::AudioBuffer<float> auditionDryBuffer;
    juce::AudioBuffer<float> auditionWetBuffer;

    std::array<std::atomic<float>, metaleq::kNumDynamicBands> dynamicReductionDb {};
    std::array<std::atomic<float>, metaleq::kMaxChannels> peakLevels {};
    std::array<std::atomic<float>, metaleq::kMaxChannels> inputPeakLevels {};

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEQAudioProcessor)
};

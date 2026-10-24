#include "MetalEqUiBridge.h"

#include "../dsp/MetalEqGraphAxis.h"

#include <cmath>

namespace metaleq::ui
{

/** Builds a JUCE string from UTF-8 source data. The target prose contains
    typographic characters (em dashes, ellipses, accented names), so a plain
    String (const char*) ASCII constructor would assert and mangle it.
*/
static juce::String utf8 (const char* text)
{
    return text != nullptr ? juce::String::fromUTF8 (text) : juce::String();
}


namespace
{
    bool isChoiceParameter (const juce::AudioProcessorParameter* p)
    {
        return dynamic_cast<const juce::AudioParameterChoice*> (p) != nullptr;
    }

    bool isBoolParameter (const juce::AudioProcessorParameter* p)
    {
        return dynamic_cast<const juce::AudioParameterBool*> (p) != nullptr;
    }

    juce::var numberOrNull (const juce::Array<juce::var>& args, size_t index)
    {
        if (index >= args.size())
            return {};
        const auto& v = args[index];
        if (v.isInt() || v.isInt64() || v.isDouble())
            return v;
        return {};
    }

    bool boolFlag (const juce::Array<juce::var>& args, size_t index)
    {
        if (index >= args.size())
            return false;
        const auto& v = args[index];
        if (v.isBool())
            return static_cast<bool> (v);
        if (v.isInt() || v.isInt64() || v.isDouble())
            return static_cast<double> (v) != 0.0;
        return false;
    }

    bool nearlyEqual (double a, double b) noexcept
    {
        const double scale = juce::jmax (1.0, std::abs (b));
        return std::abs (a - b) <= 1.0e-6 * scale;
    }

    bool readCandidateIndices (const juce::Array<juce::var>& args,
                               const juce::String& functionName,
                               std::vector<int>& indices,
                               juce::String& error)
    {
        if (args.isEmpty())
            return true;

        if (! args[0].isArray())
        {
            error = functionName + " expects an array of candidate indices.";
            return false;
        }

        const auto* array = args[0].getArray();
        if (array == nullptr)
        {
            error = functionName + " expects an array of candidate indices.";
            return false;
        }

        for (const auto& value : *array)
        {
            if (! (value.isInt() || value.isInt64() || value.isDouble()))
            {
                error = "Candidate indices must be integers.";
                return false;
            }

            const double plain = static_cast<double> (value);
            if (! metaleq::isFiniteValue (plain) || plain < 0.0)
            {
                error = "Candidate indices must be non-negative integers.";
                return false;
            }

            indices.push_back (static_cast<int> (plain));
        }

        return true;
    }
}

//==============================================================================
std::optional<double> validateParameterWrite (juce::AudioProcessorValueTreeState& apvts,
                                              const juce::String& id,
                                              const juce::var& value,
                                              juce::String& error)
{
    auto* parameter = apvts.getParameter (id);

    if (parameter == nullptr)
    {
        error = "Unknown parameter id: " + id;
        return std::nullopt;
    }

    if (! (value.isInt() || value.isInt64() || value.isDouble()))
    {
        error = "Value for " + id + " must be a finite number.";
        return std::nullopt;
    }

    const double plain = static_cast<double> (value);

    if (! metaleq::isFiniteValue (plain))
    {
        error = "Value for " + id + " must be a finite number.";
        return std::nullopt;
    }

    if (isBoolParameter (parameter))
        return plain >= 0.5 ? 1.0 : 0.0;

    if (isChoiceParameter (parameter))
    {
        const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (parameter);
        const auto last = static_cast<double> (choice->choices.size() - 1);
        return juce::jlimit (0.0, last, std::round (plain));
    }

    const auto& range = parameter->getNormalisableRange();
    return juce::jlimit (static_cast<double> (range.start), static_cast<double> (range.end), plain);
}

//==============================================================================
UiBridge::UiBridge (MetalEQAudioProcessor& p)
    : processor (p)
{
    for (const auto& id : MetalEQAudioProcessor::ParameterIDs::allEqParameterIds())
        lastSentValues.emplace_back (id, std::numeric_limits<double>::quiet_NaN());

    // bypass and abCompare are part of the same diff stream.
    lastSentValues.emplace_back (MetalEQAudioProcessor::ParameterIDs::bypass,
                                 std::numeric_limits<double>::quiet_NaN());
    lastSentValues.emplace_back (MetalEQAudioProcessor::ParameterIDs::abCompare,
                                 std::numeric_limits<double>::quiet_NaN());
    lastSentValues.emplace_back (MetalEQAudioProcessor::ParameterIDs::resAudition,
                                 std::numeric_limits<double>::quiet_NaN());
    lastSentValues.emplace_back (MetalEQAudioProcessor::ParameterIDs::dynAudition,
                                 std::numeric_limits<double>::quiet_NaN());

    for (auto& g : lastSuggestion)
        g = 0.0f;
}

UiBridge::~UiBridge() = default;

juce::var UiBridge::okResult()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", true);
    return juce::var (obj);
}

juce::var UiBridge::errorResult (const juce::String& message)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", false);
    obj->setProperty ("error", message);
    return juce::var (obj);
}

juce::WebBrowserComponent::Options UiBridge::addOptions (juce::WebBrowserComponent::Options options)
{
    auto* self = this;

    return options
        .withNativeFunction ("uiReady",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleUiReady());
            })
        .withNativeFunction ("setParameter",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleSetParameter (args));
            })
        .withNativeFunction ("applyPreset",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleApplyPreset (args));
            })
        .withNativeFunction ("applyPresetById",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleApplyPresetById (args));
            })
        .withNativeFunction ("loadUserPreset",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleLoadUserPreset (args));
            })
        .withNativeFunction ("saveUserPreset",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleSaveUserPreset (args));
            })
        .withNativeFunction ("renameUserPreset",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleRenameUserPreset (args));
            })
        .withNativeFunction ("deleteUserPreset",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleDeleteUserPreset (args));
            })
        .withNativeFunction ("revertToPreset",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleRevertToPreset());
            })
        .withNativeFunction ("resetToNeutral",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleResetToNeutral());
            })
        .withNativeFunction ("setAbSlot",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleSetAbSlot (args));
            })
        .withNativeFunction ("copyAbToOther",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleCopyAbToOther());
            })
        .withNativeFunction ("swapAbSlots",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleSwapAbSlots());
            })
        .withNativeFunction ("setAnalyzerTarget",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleSetAnalyzerTarget (args));
            })
        .withNativeFunction ("startAnalyzerCapture",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleStartAnalyzerCapture (args));
            })
        .withNativeFunction ("cancelAnalyzerCapture",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleCancelAnalyzerCapture());
            })
        .withNativeFunction ("applyAnalyzerSuggestion",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleApplyAnalyzerSuggestion (args));
            })
        .withNativeFunction ("getSuggestionCurve",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleGetSuggestionCurve (args));
            })
        .withNativeFunction ("startResonanceScan",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleStartResonanceScan (args));
            })
        .withNativeFunction ("cancelResonanceScan",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleCancelResonanceScan (args));
            })
        .withNativeFunction ("applyResonanceSelection",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleApplyResonanceSelection (args));
            })
        .withNativeFunction ("clearSurgicalBands",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleClearSurgicalBands());
            })
        .withNativeFunction ("applyResonanceToDynamic",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleApplyResonanceToDynamic (args));
            })
        .withNativeFunction ("clearDynamicBands",
            [self] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleClearDynamicBands());
            })
        .withNativeFunction ("openExternalUrl",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleOpenExternalUrl (args));
            })
        .withNativeFunction ("setEditorExpanded",
            [self] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (self->handleSetEditorExpanded (args));
            });
}

//==============================================================================
double UiBridge::readPlainValue (const char* paramId) const
{
    if (const auto* parameter = processor.getValueTreeState().getParameter (paramId))
        return static_cast<double> (parameter->convertFrom0to1 (parameter->getValue()));
    return 0.0;
}

juce::var UiBridge::handleUiReady()
{
    uiReady = true;
    fullSyncRequired = true;
    return buildFullSnapshot();
}

juce::var UiBridge::handleSetParameter (const juce::Array<juce::var>& args)
{
    if (args.size() != 2 || ! args[0].isString())
        return errorResult ("setParameter expects (id, value).");

    const auto id = args[0].toString();
    juce::String error;
    const auto validated = validateParameterWrite (processor.getValueTreeState(), id, numberOrNull (args, 1), error);

    if (! validated.has_value())
        return errorResult (error);

    auto& apvts = processor.getValueTreeState();

    if (auto* parameter = apvts.getParameter (id))
    {
        if (isChoiceParameter (parameter))
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
            {
                const auto index = static_cast<int> (*validated);
                choice->setValueNotifyingHost (
                    static_cast<float> (index) / static_cast<float> (juce::jmax (1, choice->choices.size() - 1)));
            }
        }
        else
        {
            const auto& range = parameter->getNormalisableRange();
            parameter->setValueNotifyingHost (range.convertTo0to1 (static_cast<float> (*validated)));
        }
    }

    // The two section monitors are mutually exclusive: enabling one clears the
    // other, so the player always hears exactly one removed signal.
    if (*validated >= 0.5)
    {
        const char* otherId = nullptr;
        if (id == MetalEQAudioProcessor::ParameterIDs::resAudition)
            otherId = MetalEQAudioProcessor::ParameterIDs::dynAudition;
        else if (id == MetalEQAudioProcessor::ParameterIDs::dynAudition)
            otherId = MetalEQAudioProcessor::ParameterIDs::resAudition;

        if (otherId != nullptr)
        {
            if (auto* other = apvts.getParameter (otherId))
                other->setValueNotifyingHost (other->getNormalisableRange().convertTo0to1 (0.0f));
        }
    }

    // The section monitors do not change the EQ response, so they do not
    // invalidate the graph curve.
    if (id != MetalEQAudioProcessor::ParameterIDs::resAudition
        && id != MetalEQAudioProcessor::ParameterIDs::dynAudition)
        markCurveDirty();

    // The low-pass slope is its bypass: Off clears the legacy lpOn mirror so
    // saved state and host automation remain coherent even though the web UI
    // no longer exposes a separate enable toggle.
    if (id == MetalEQAudioProcessor::ParameterIDs::lpSlope)
    {
        if (auto* onParameter = apvts.getParameter (MetalEQAudioProcessor::ParameterIDs::lpOn))
            onParameter->setValueNotifyingHost (*validated > 0.5 ? 1.0f : 0.0f);
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", true);
    obj->setProperty ("value", readPlainValue (id.toRawUTF8()));
    return juce::var (obj);
}

juce::var UiBridge::handleApplyPreset (const juce::Array<juce::var>& args)
{
    const auto indexVar = numberOrNull (args, 0);
    if (indexVar.isVoid())
        return errorResult ("applyPreset expects a preset index.");

    processor.applyPreset (static_cast<int> (static_cast<double> (indexVar)));
    markCurveDirty();
    markUserPresetsChanged();
    return okResult();
}

juce::var UiBridge::handleApplyPresetById (const juce::Array<juce::var>& args)
{
    if (args.size() < 1 || ! args[0].isString())
        return errorResult ("applyPresetById expects a stable preset id.");

    if (! processor.applyPresetById (args[0].toString()))
        return errorResult ("Unknown factory preset id.");

    markCurveDirty();
    markUserPresetsChanged();
    return okResult();
}

juce::var UiBridge::handleLoadUserPreset (const juce::Array<juce::var>& args)
{
    if (args.size() < 1 || ! args[0].isString())
        return errorResult ("loadUserPreset expects a preset name.");

    juce::String error;
    if (! processor.loadUserPreset (args[0].toString(), error))
        return errorResult (error);

    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleSaveUserPreset (const juce::Array<juce::var>& args)
{
    if (args.size() < 1 || ! args[0].isString())
        return errorResult ("saveUserPreset expects (name, overwrite).");

    const bool overwrite = args.size() > 1 && static_cast<bool> (static_cast<double> (args[1]));
    juce::String error;

    if (! processor.saveUserPreset (args[0].toString(), overwrite, error))
        return errorResult (error);

    markUserPresetsChanged();
    return okResult();
}

juce::var UiBridge::handleRenameUserPreset (const juce::Array<juce::var>& args)
{
    if (args.size() < 2 || ! args[0].isString() || ! args[1].isString())
        return errorResult ("renameUserPreset expects (oldName, newName).");

    juce::String error;
    if (! processor.renameUserPreset (args[0].toString(), args[1].toString(), error))
        return errorResult (error);

    markUserPresetsChanged();
    return okResult();
}

juce::var UiBridge::handleDeleteUserPreset (const juce::Array<juce::var>& args)
{
    if (args.size() < 1 || ! args[0].isString())
        return errorResult ("deleteUserPreset expects a preset name.");

    juce::String error;
    if (! processor.deleteUserPreset (args[0].toString(), error))
        return errorResult (error);

    markUserPresetsChanged();
    return okResult();
}

juce::var UiBridge::handleRevertToPreset()
{
    processor.revertToLoadedPreset();
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleResetToNeutral()
{
    processor.resetToNeutral();
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleSetAbSlot (const juce::Array<juce::var>& args)
{
    const auto slot = numberOrNull (args, 0);
    if (slot.isVoid())
        return errorResult ("setAbSlot expects 0 (A) or 1 (B).");

    processor.setAbSlot (static_cast<int> (static_cast<double> (slot)));
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleCopyAbToOther()
{
    if (! processor.copyActiveAbToOther())
        return errorResult ("The other A/B slot could not be copied.");

    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleSwapAbSlots()
{
    if (! processor.swapAbSlots())
        return errorResult ("The A/B slots could not be swapped.");

    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleSetAnalyzerTarget (const juce::Array<juce::var>& args)
{
    const auto target = numberOrNull (args, 0);
    if (target.isVoid())
        return errorResult ("setAnalyzerTarget expects a target index.");

    processor.setAnalyzerTarget (juce::jlimit (0, metaleq::tone::getNumToneTargets() - 1,
        static_cast<int> (static_cast<double> (target))));
    return okResult();
}

juce::var UiBridge::handleStartAnalyzerCapture (const juce::Array<juce::var>& args)
{
    const auto profile = numberOrNull (args, 0);
    if (profile.isVoid())
        return errorResult ("startAnalyzerCapture expects a target profile index.");

    processor.startAnalyzerCapture (juce::jlimit (0, metaleq::tone::getNumToneTargets() - 1,
        static_cast<int> (static_cast<double> (profile))));
    return okResult();
}

juce::var UiBridge::handleCancelAnalyzerCapture()
{
    processor.cancelAnalyzerCapture();
    return okResult();
}

juce::var UiBridge::handleApplyAnalyzerSuggestion (const juce::Array<juce::var>& args)
{
    const auto amount = numberOrNull (args, 0);
    if (amount.isVoid())
        return errorResult ("applyAnalyzerSuggestion expects an amount in 0..1.");

    processor.applyAnalyzerSuggestion (static_cast<float> (juce::jlimit (0.0, 1.0, static_cast<double> (amount))));
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleGetSuggestionCurve (const juce::Array<juce::var>& args)
{
    const auto amount = numberOrNull (args, 0);
    if (amount.isVoid())
        return errorResult ("getSuggestionCurve expects an amount in 0..1.");

    const auto clamped = static_cast<float> (juce::jlimit (0.0, 1.0, static_cast<double> (amount)));
    juce::Array<juce::var> curve;

    for (int i = 0; i < numCurvePoints; ++i)
    {
        const double unit = static_cast<double> (i) / (numCurvePoints - 1);
        const double freq = metaleq::kGraphMinFreq
                          * std::pow (metaleq::kGraphMaxFreq / metaleq::kGraphMinFreq, unit);
        curve.add (processor.getAnalyzerResponseDb (freq, clamped));
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", true);
    obj->setProperty ("curve", curve);
    return juce::var (obj);
}

juce::var UiBridge::handleStartResonanceScan (const juce::Array<juce::var>& args)
{
    const bool dynamic = boolFlag (args, 0);
    const auto target = dynamic ? MetalEQAudioProcessor::ResonanceScanTarget::dynamic
                                : MetalEQAudioProcessor::ResonanceScanTarget::surgical;

    if (! processor.startResonanceScan (target))
        return errorResult ("A previous scan is still being analysed. Try again in a moment.");

    return okResult();
}

juce::var UiBridge::handleCancelResonanceScan (const juce::Array<juce::var>& args)
{
    const bool dynamic = boolFlag (args, 0);
    processor.cancelResonanceScan (dynamic ? MetalEQAudioProcessor::ResonanceScanTarget::dynamic
                                           : MetalEQAudioProcessor::ResonanceScanTarget::surgical);
    return okResult();
}

juce::var UiBridge::handleApplyResonanceSelection (const juce::Array<juce::var>& args)
{
    std::vector<int> indices;
    juce::String error;

    if (! readCandidateIndices (args, "applyResonanceSelection", indices, error))
        return errorResult (error);

    processor.applyResonanceSelection (indices);
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleClearSurgicalBands()
{
    processor.clearSurgicalBands();
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleApplyResonanceToDynamic (const juce::Array<juce::var>& args)
{
    std::vector<int> indices;
    juce::String error;

    if (! readCandidateIndices (args, "applyResonanceToDynamic", indices, error))
        return errorResult (error);

    processor.applyResonanceToDynamic (indices);
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleClearDynamicBands()
{
    processor.clearDynamicBands();
    markCurveDirty();
    return okResult();
}

juce::var UiBridge::handleOpenExternalUrl (const juce::Array<juce::var>& args)
{
    if (args.isEmpty() || ! args[0].isString())
        return errorResult ("openExternalUrl expects a URL string.");

    const auto url = args[0].toString().trim();
    if (! url.startsWithIgnoreCase ("https://") && ! url.startsWithIgnoreCase ("http://"))
        return errorResult ("Only http and https links can be opened.");

    if (! juce::URL (url).launchInDefaultBrowser())
        return errorResult ("The link could not be opened.");

    return okResult();
}

juce::var UiBridge::handleSetEditorExpanded (const juce::Array<juce::var>& args)
{
    const auto expanded = numberOrNull (args, 0);
    if (expanded.isVoid())
        return errorResult ("setEditorExpanded expects a boolean.");

    if (expansionHandler != nullptr)
        expansionHandler (static_cast<double> (expanded) >= 0.5);

    return okResult();
}

//==============================================================================
juce::var UiBridge::buildParamsObject (bool includeAll)
{
    auto* obj = new juce::DynamicObject();

    for (auto& entry : lastSentValues)
    {
        const double plain = readPlainValue (entry.first.toRawUTF8());

        if (includeAll || ! nearlyEqual (plain, entry.second))
        {
            obj->setProperty (entry.first, plain);
            entry.second = plain;
        }
    }

    return juce::var (obj);
}

juce::var UiBridge::buildPresetObject()
{
    const auto state = processor.getDisplayState();

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("index", state.presetIndex);
    obj->setProperty ("presetId", state.presetId);
    obj->setProperty ("name", state.presetName);
    obj->setProperty ("kind", state.userPreset ? "user"
                        : (state.presetIndex > 0 ? "factory" : "custom"));
    obj->setProperty ("dirty", state.presetDirty);
    obj->setProperty ("abSlot", state.abSlot);
    return juce::var (obj);
}

juce::var UiBridge::buildAnalyzerObject()
{
    using Status = MetalEQAudioProcessor::AnalyzerStatus;
    const auto status = processor.getAnalyzerStatus();
    const auto progress = processor.getAnalyzerProgress();
    const int targetIndex = processor.getAnalyzerTargetIndex();
    const int resultTarget = processor.getAnalyzerResultTarget();
    const auto revision = processor.getAnalyzerRevision();

    const bool changed = revision != lastAnalyzerRevision
                      || targetIndex != lastAnalyzerTarget
                      || resultTarget != lastAnalyzerResultTarget
                      || static_cast<int> (status) != lastAnalyzerStatus
                      || ! nearlyEqual (progress, lastAnalyzerProgress);

    lastAnalyzerRevision = revision;
    lastAnalyzerTarget = targetIndex;
    lastAnalyzerResultTarget = resultTarget;
    lastAnalyzerStatus = static_cast<int> (status);
    lastAnalyzerProgress = progress;

    if (! changed && ! fullSyncRequired)
        return {};

    const auto& target = metaleq::tone::getToneTarget (targetIndex);
    MetalEQAudioProcessor::AnalyzerReport report;
    const bool hasReport = processor.getAnalyzerReport (report);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("status", static_cast<int> (status));
    obj->setProperty ("progress", progress);
    obj->setProperty ("targetIndex", targetIndex);
    obj->setProperty ("targetId", utf8 (target.id));
    obj->setProperty ("targetLabel", utf8 (target.label));
    obj->setProperty ("resultTarget", resultTarget);
    obj->setProperty ("hasReport", hasReport);

    juce::Array<juce::var> gains;
    for (int i = 0; i < metaleq::kNumBands; ++i)
    {
        lastSuggestion[static_cast<size_t> (i)] = hasReport
            ? report.gains[static_cast<size_t> (i)] : 0.0f;
        gains.add (lastSuggestion[static_cast<size_t> (i)]);
    }
    obj->setProperty ("suggestion", gains);

    if (hasReport)
    {
        auto addFeatureArray = [obj] (const char* name,
                                      const std::array<float, metaleq::tone::kNumFeatures>& values)
        {
            juce::Array<juce::var> array;
            for (const auto value : values)
                array.add (value);
            obj->setProperty (name, array);
        };

        addFeatureArray ("input", report.input);
        addFeatureArray ("target", report.target);
        addFeatureArray ("predicted", report.predicted);

        juce::Array<juce::var> featureFreqs;
        for (const auto& feature : metaleq::tone::kFeatures)
            featureFreqs.add (feature.centreHz);
        obj->setProperty ("featureFreqs", featureFreqs);

        juce::Array<juce::var> correctionFreqs;
        for (const auto freq : metaleq::tone::kCorrectionFrequencies)
            correctionFreqs.add (freq);
        obj->setProperty ("correctionFreqs", correctionFreqs);

        obj->setProperty ("inputConfidence", report.inputConfidence);
        obj->setProperty ("correctionConfidence", report.correctionConfidence);
        obj->setProperty ("alreadyClose", report.alreadyClose);
    }

    obj->setProperty ("changed", true);
    return juce::var (obj);
}

juce::var UiBridge::buildResonanceObject()
{
    const auto surgicalStatus = processor.getResonanceStatus (
        MetalEQAudioProcessor::ResonanceScanTarget::surgical);
    const auto dynamicStatus = processor.getResonanceStatus (
        MetalEQAudioProcessor::ResonanceScanTarget::dynamic);
    const auto surgicalProgress = processor.getResonanceProgress (
        MetalEQAudioProcessor::ResonanceScanTarget::surgical);
    const auto dynamicProgress = processor.getResonanceProgress (
        MetalEQAudioProcessor::ResonanceScanTarget::dynamic);
    const auto revision = processor.getResonanceRevision();

    const bool changed = static_cast<int> (surgicalStatus) != lastResonanceStatus[0]
                      || static_cast<int> (dynamicStatus) != lastResonanceStatus[1]
                      || ! nearlyEqual (surgicalProgress, lastResonanceProgress[0])
                      || ! nearlyEqual (dynamicProgress, lastResonanceProgress[1])
                      || revision != lastResonanceRevision;

    lastResonanceStatus[0] = static_cast<int> (surgicalStatus);
    lastResonanceStatus[1] = static_cast<int> (dynamicStatus);
    lastResonanceProgress[0] = surgicalProgress;
    lastResonanceProgress[1] = dynamicProgress;
    lastResonanceRevision = revision;

    if (! changed && ! fullSyncRequired)
        return {};

    auto* surgical = new juce::DynamicObject();
    surgical->setProperty ("status", static_cast<int> (surgicalStatus));
    surgical->setProperty ("progress", surgicalProgress);
    surgical->setProperty ("enoughVariation", processor.getResonanceEnoughVariation (
        MetalEQAudioProcessor::ResonanceScanTarget::surgical));

    juce::Array<juce::var> candidates;
    const int count = processor.getResonanceCandidateCount();

    for (int i = 0; i < count; ++i)
    {
        metaleq::ResonanceCandidate candidate;
        if (! processor.getResonanceCandidate (i, candidate))
            continue;

        auto* item = new juce::DynamicObject();
        item->setProperty ("freq", candidate.freq);
        item->setProperty ("q", candidate.q);
        item->setProperty ("prominence", candidate.prominenceDb);
        item->setProperty ("residual", candidate.residualDb);
        item->setProperty ("confidence", candidate.confidence);
        item->setProperty ("cutDb", candidate.suggestedCutDb);
        candidates.add (juce::var (item));
    }

    surgical->setProperty ("candidates", candidates);

    auto* dynamic = new juce::DynamicObject();
    dynamic->setProperty ("status", static_cast<int> (dynamicStatus));
    dynamic->setProperty ("progress", dynamicProgress);
    dynamic->setProperty ("enoughVariation", processor.getResonanceEnoughVariation (
        MetalEQAudioProcessor::ResonanceScanTarget::dynamic));

    juce::Array<juce::var> dynamicCandidates;
    const int dynamicCount = processor.getResonanceDynamicSuggestionCount();

    for (int i = 0; i < dynamicCount; ++i)
    {
        metaleq::DynamicBandSettings band;
        double confidence = 0.0;
        double excessDb = 0.0;

        if (! processor.getResonanceDynamicSuggestion (i, band, confidence, excessDb))
            continue;

        auto* dynamicItem = new juce::DynamicObject();
        dynamicItem->setProperty ("candidateIndex", i);
        dynamicItem->setProperty ("freq", band.freq);
        dynamicItem->setProperty ("q", band.q);
        dynamicItem->setProperty ("threshold", band.thresholdDb);
        dynamicItem->setProperty ("ratio", band.ratio);
        dynamicItem->setProperty ("attack", band.attackMs);
        dynamicItem->setProperty ("release", band.releaseMs);
        dynamicItem->setProperty ("maxCut", band.maxCutDb);
        dynamicItem->setProperty ("confidence", confidence);
        dynamicItem->setProperty ("excessDb", excessDb);
        dynamicCandidates.add (juce::var (dynamicItem));
    }

    dynamic->setProperty ("candidates", dynamicCandidates);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("surgical", juce::var (surgical));
    obj->setProperty ("dynamic", juce::var (dynamic));

    return juce::var (obj);
}

juce::var UiBridge::buildDynamicReductionArray()
{
    juce::Array<juce::var> reductions;

    for (int i = 0; i < metaleq::kNumDynamicBands; ++i)
        reductions.add (processor.getDynamicGainReductionDb (i));

    return juce::var (reductions);
}

juce::var UiBridge::buildCurveArray()
{
    const auto state = processor.getDisplayState();
    const double sampleRate = processor.getDisplaySampleRate();

    metaleq::ChainSpec chain;
    metaleq::buildChain (chain, state.eq, state.macros, sampleRate);

    juce::Array<juce::var> curve;

    for (int i = 0; i < numCurvePoints; ++i)
    {
        const double unit = static_cast<double> (i) / (numCurvePoints - 1);
        const double freq = metaleq::kGraphMinFreq
                          * std::pow (metaleq::kGraphMaxFreq / metaleq::kGraphMinFreq, unit);
        curve.add (metaleq::chainResponseDb (chain, sampleRate, freq));
    }

    auto* obj = new juce::DynamicObject();

    // The graph always uses a fixed -15..+15 dB window. Steep pass-band
    // rolloff and stacked boosts are clipped at the edges instead of scaling
    // the useful EQ range down to a few pixels.
    obj->setProperty ("points", curve);
    obj->setProperty ("minDb", metaleq::graph::kGraphMinDb);
    obj->setProperty ("maxDb", metaleq::graph::kGraphMaxDb);
    obj->setProperty ("sampleRate", sampleRate);
    return juce::var (obj);
}

juce::var UiBridge::buildFactoryPresetList()
{
    juce::Array<juce::var> presets;

    for (int i = 0; i < metaleq::getNumPresets(); ++i)
    {
        const auto& preset = metaleq::getPreset (i);
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("index", i + 1); // 0 is reserved for Custom
        obj->setProperty ("id", preset.id);
        obj->setProperty ("name", preset.name);
        obj->setProperty ("genre", preset.genre);
        obj->setProperty ("subgenre", preset.subgenre);
        obj->setProperty ("placement", metaleq::placementToString (preset.placement));
        obj->setProperty ("description", preset.description);
        presets.add (juce::var (obj));
    }

    return juce::var (presets);
}

juce::var UiBridge::buildAnalyzerTargetList()
{
    juce::Array<juce::var> targets;

    for (int i = 0; i < metaleq::tone::getNumToneTargets(); ++i)
    {
        const auto& target = metaleq::tone::getToneTarget (i);
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("index", i);
        obj->setProperty ("id", utf8 (target.id));
        obj->setProperty ("label", utf8 (target.label));
        obj->setProperty ("shortLabel", utf8 (target.shortLabel));
        obj->setProperty ("category", static_cast<int> (target.category));
        obj->setProperty ("categoryLabel", utf8 (metaleq::tone::categoryLabel (target.category)));
        obj->setProperty ("albumPeriod", utf8 (target.albumPeriod));
        obj->setProperty ("objective", utf8 (target.objective));
        obj->setProperty ("limitations", utf8 (target.limitations));
        obj->setProperty ("confidence", target.confidence);

        juce::Array<juce::var> sources;
        for (int s = 0; s < target.numSources; ++s)
        {
            const auto& ref = target.sources[static_cast<size_t> (s)];
            if (ref.sourceIndex < 0 || ref.sourceIndex >= metaleq::tone::getNumSources())
                continue;

            const auto& source = metaleq::tone::getSource (ref.sourceIndex);
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("title", utf8 (source.title));
            entry->setProperty ("url", utf8 (source.url));
            entry->setProperty ("date", utf8 (source.date));
            entry->setProperty ("claim", utf8 (ref.claim));
            entry->setProperty ("evidence", static_cast<int> (ref.evidence));
            entry->setProperty ("evidenceLabel", utf8 (metaleq::tone::evidenceLabel (ref.evidence)));
            sources.add (juce::var (entry));
        }
        obj->setProperty ("sources", sources);

        targets.add (juce::var (obj));
    }

    return juce::var (targets);
}

juce::var UiBridge::buildFullSnapshot()
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty ("protocolVersion", protocolVersion);
    obj->setProperty ("params", buildParamsObject (true));
    obj->setProperty ("preset", buildPresetObject());
    obj->setProperty ("factoryPresets", buildFactoryPresetList());
    obj->setProperty ("analyzerTargets", buildAnalyzerTargetList());

    lastUserPresets = processor.getUserPresetNames();
    juce::Array<juce::var> userPresets;
    for (const auto& name : lastUserPresets)
        userPresets.add (name);
    obj->setProperty ("userPresets", userPresets);

    if (const auto analyzer = buildAnalyzerObject())
        obj->setProperty ("analyzer", analyzer);
    else
        obj->setProperty ("analyzer", buildAnalyzerObject());

    if (const auto resonance = buildResonanceObject())
        obj->setProperty ("resonance", resonance);
    else
        obj->setProperty ("resonance", buildResonanceObject());

    if (const auto curve = buildCurveArray())
        obj->setProperty ("curve", curve);

    obj->setProperty ("dynamicReduction", buildDynamicReductionArray());

    userPresetsDirty = false;
    curveDirty = false;
    fullSyncRequired = false;

    return juce::var (obj);
}

//==============================================================================
void UiBridge::tick()
{
    if (! uiReady || webComponent == nullptr)
        return;

    ++seq;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("seq", seq);

    const bool sendAll = fullSyncRequired;
    fullSyncRequired = false;

    obj->setProperty ("params", buildParamsObject (sendAll));
    obj->setProperty ("preset", buildPresetObject());

    if (userPresetsDirty || sendAll)
    {
        lastUserPresets = processor.getUserPresetNames();
        juce::Array<juce::var> userPresets;
        for (const auto& name : lastUserPresets)
            userPresets.add (name);
        obj->setProperty ("userPresets", userPresets);
        userPresetsDirty = false;
    }

    {
        auto* meters = new juce::DynamicObject();
        meters->setProperty ("in", juce::jmax (processor.getInputPeakLevel (0), processor.getInputPeakLevel (1)));
        meters->setProperty ("out", juce::jmax (processor.getPeakLevel (0), processor.getPeakLevel (1)));
        obj->setProperty ("meters", juce::var (meters));
    }

    obj->setProperty ("dynamicReduction", buildDynamicReductionArray());

    if (const auto analyzer = buildAnalyzerObject())
        obj->setProperty ("analyzer", analyzer);

    if (const auto resonance = buildResonanceObject())
        obj->setProperty ("resonance", resonance);

    const double sampleRate = processor.getDisplaySampleRate();
    if (sampleRate != lastSampleRate)
    {
        lastSampleRate = sampleRate;
        curveDirty = true;
    }

    if (curveDirty)
    {
        obj->setProperty ("curve", buildCurveArray());
        curveDirty = false;
    }

    webComponent->emitEventIfBrowserIsVisible ("metaleq.tick", juce::var (obj));
}

} // namespace metaleq::ui

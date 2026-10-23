/*
    MetalEQ - real-time safe DSP core.

    This header is deliberately free of any JUCE dependency so that it can be
    unit tested in isolation. It contains:

      * RBJ biquad coefficient design and magnitude evaluation.
      * The "chain spec" description of the whole EQ (user filters + macro filters).
      * A fixed-size, allocation-free, lock-free biquad processing engine with
        per-sample coefficient ramping so that parameter changes never click.
      * A pure function used by the GUI to draw exactly the response that the
        audio thread will produce.

    Nothing in here allocates, locks, throws or blocks.
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace metaleq
{

inline constexpr int    kNumBands     = 5;
// Worst case: 24 base/macro filters plus up to 18 filters from six shapes.
inline constexpr int    kMaxChainSize = 48;
inline constexpr int    kMaxChannels  = 8;   // we only ever advertise mono/stereo buses
inline constexpr double kPi           = 3.14159265358979323846;

inline const double kGraphMinFreq = 20.0;
inline const double kGraphMaxFreq = 20000.0;

template <typename T>
inline T clampValue (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline bool isFiniteValue (double v) noexcept { return std::isfinite (v); }

//==============================================================================
// Biquad coefficients, normalised (a0 == 1), stored in double precision.
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    bool operator== (const BiquadCoeffs& o) const noexcept
    {
        return b0 == o.b0 && b1 == o.b1 && b2 == o.b2 && a1 == o.a1 && a2 == o.a2;
    }

    bool operator!= (const BiquadCoeffs& o) const noexcept { return ! (*this == o); }
};

//==============================================================================
enum class FilterType : uint8_t
{
    highPass,
    lowPass,
    lowShelf,
    highShelf,
    peak
};

/** One section of the EQ chain. `gainDb` is ignored by high/low pass sections. */
struct FilterSpec
{
    FilterType type   = FilterType::peak;
    double     freq   = 1000.0;
    double     q      = 0.7071;
    double     gainDb = 0.0;
};

/** The complete, ordered description of the EQ. Fixed size, no allocation. */
struct ChainSpec
{
    std::array<FilterSpec, kMaxChainSize> filters {};
    int    numFilters = 0;
    double trimDb     = 0.0;

    void clear() noexcept { numFilters = 0; trimDb = 0.0; }

    void add (FilterType type, double freq, double q, double gainDb = 0.0) noexcept
    {
        if (numFilters >= kMaxChainSize)
            return;

        filters[static_cast<size_t> (numFilters++)] = FilterSpec { type, freq, q, gainDb };
    }

    const FilterSpec& operator[] (int i) const noexcept { return filters[static_cast<size_t> (i)]; }
};

//==============================================================================
/** Sanitise a design request so that no combination of parameters can produce
    invalid or unstable coefficients.
*/
inline BiquadCoeffs designBiquad (FilterType type,
                                  double sampleRate,
                                  double freq,
                                  double q,
                                  double gainDb) noexcept
{
    const BiquadCoeffs identity;

    if (! (isFiniteValue (sampleRate) && isFiniteValue (freq)
           && isFiniteValue (q) && isFiniteValue (gainDb)))
        return identity;

    if (sampleRate < 1000.0 || freq <= 0.0)
        return identity;

    const double nyquist = sampleRate * 0.5;

    freq   = clampValue (freq, 1.0, nyquist * 0.98);
    q      = clampValue (q, 0.05, 40.0);
    gainDb = clampValue (gainDb, -48.0, 48.0);

    // A flat shelf or peak is a straight-through: skip it entirely.
    if (type != FilterType::highPass && type != FilterType::lowPass && std::abs (gainDb) < 1.0e-4)
        return identity;

    const double w0    = 2.0 * kPi * freq / sampleRate;
    const double cosW0 = std::cos (w0);
    const double sinW0 = std::sin (w0);
    const double alpha = sinW0 / (2.0 * q);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type)
    {
        case FilterType::highPass:
            b0 = (1.0 + cosW0) * 0.5;
            b1 = -(1.0 + cosW0);
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha;
            break;

        case FilterType::lowPass:
            b0 = (1.0 - cosW0) * 0.5;
            b1 = 1.0 - cosW0;
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha;
            break;

        case FilterType::peak:
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosW0;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha / A;
            break;
        }

        case FilterType::lowShelf:
        {
            const double A     = std::pow (10.0, gainDb / 40.0);
            const double sqrtA = std::sqrt (A);
            const double tsa   = 2.0 * sqrtA * alpha;

            b0 = A * ((A + 1.0) - (A - 1.0) * cosW0 + tsa);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosW0 - tsa);
            a0 = (A + 1.0) + (A - 1.0) * cosW0 + tsa;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
            a2 = (A + 1.0) + (A - 1.0) * cosW0 - tsa;
            break;
        }

        case FilterType::highShelf:
        {
            const double A     = std::pow (10.0, gainDb / 40.0);
            const double sqrtA = std::sqrt (A);
            const double tsa   = 2.0 * sqrtA * alpha;

            b0 = A * ((A + 1.0) + (A - 1.0) * cosW0 + tsa);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosW0 - tsa);
            a0 = (A + 1.0) - (A - 1.0) * cosW0 + tsa;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
            a2 = (A + 1.0) - (A - 1.0) * cosW0 - tsa;
            break;
        }
    }

    if (! isFiniteValue (a0) || std::abs (a0) < 1.0e-12)
        return identity;

    const double invA0 = 1.0 / a0;
    const BiquadCoeffs c { b0 * invA0, b1 * invA0, b2 * invA0, a1 * invA0, a2 * invA0 };

    if (! (isFiniteValue (c.b0) && isFiniteValue (c.b1) && isFiniteValue (c.b2)
           && isFiniteValue (c.a1) && isFiniteValue (c.a2)))
        return identity;

    return c;
}

/** Magnitude of a biquad at a given frequency, in dB. Pure math, no state. */
inline double magnitudeDb (const BiquadCoeffs& c, double sampleRate, double freq) noexcept
{
    if (sampleRate <= 0.0 || ! isFiniteValue (freq) || freq <= 0.0)
        return 0.0;

    const double w  = 2.0 * kPi * freq / sampleRate;
    const double cw = std::cos (w);
    const double c2 = std::cos (2.0 * w);

    const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2
                     + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * cw
                     + 2.0 * c.b0 * c.b2 * c2;

    const double den = 1.0 + c.a1 * c.a1 + c.a2 * c.a2
                     + 2.0 * (c.a1 + c.a1 * c.a2) * cw
                     + 2.0 * c.a2 * c2;

    if (den < 1.0e-12 || num <= 0.0 || ! isFiniteValue (num / den))
        return -120.0;

    return 10.0 * std::log10 (num / den);
}

//==============================================================================
// Settings
//==============================================================================

/** One user parametric bell band. */
struct BandSettings
{
    double freq   = 1000.0;
    double gainDb = 0.0;
    double q      = 1.0;
    bool   on     = true;
};

/** All user-facing EQ parameters (no macros). */
struct EqSettings
{
    double hpFreq   = 80.0;
    double lpFreq   = 12000.0;
    int    hpSlope  = 0;   // 0 = off, 1 = 12, 2 = 24, 3 = 36 dB/oct
    int    lpSlope  = 0;
    // A low-pass is audible only when it is enabled AND its slope is > 0.
    // Default off: the transparent state must not roll off any top end.
    bool   lpEnabled = false;
    double lsFreq   = 140.0;
    double lsGainDb = 0.0;
    double hsFreq   = 7000.0;
    double hsGainDb = 0.0;
    std::array<BandSettings, kNumBands> bands {};
    // TIGHTEN, SCOOP, MID PUNCH, PICK ATTACK, FIZZ TAMER, BRIGHTEN.
    // Separate layers preserve the user's EQ and macro edits when toggled.
    std::array<bool, 6> shapes {};
    double trimDb   = 0.0;
};

/** The five macro controls. TIGHT/BODY are bipolar (0.5 = neutral), the rest
    are unipolar amounts (0 = off).
*/
struct MacroSettings
{
    double tight   = 0.0;
    double body    = 0.5;
    double scoop   = 0.0;
    double bite    = 0.0;
    double fizzCut = 0.0;
};

inline bool operator== (const BandSettings& a, const BandSettings& b) noexcept
{
    return a.freq == b.freq && a.gainDb == b.gainDb && a.q == b.q && a.on == b.on;
}

inline bool operator!= (const BandSettings& a, const BandSettings& b) noexcept { return ! (a == b); }

inline bool operator== (const EqSettings& a, const EqSettings& b) noexcept
{
    return a.hpFreq == b.hpFreq && a.lpFreq == b.lpFreq
        && a.hpSlope == b.hpSlope && a.lpSlope == b.lpSlope
        && a.lpEnabled == b.lpEnabled
        && a.lsFreq == b.lsFreq && a.lsGainDb == b.lsGainDb
        && a.hsFreq == b.hsFreq && a.hsGainDb == b.hsGainDb
        && a.bands == b.bands && a.shapes == b.shapes
        && a.trimDb == b.trimDb;
}

inline bool operator!= (const EqSettings& a, const EqSettings& b) noexcept { return ! (a == b); }

inline bool operator== (const MacroSettings& a, const MacroSettings& b) noexcept
{
    return a.tight == b.tight && a.body == b.body && a.scoop == b.scoop
        && a.bite == b.bite && a.fizzCut == b.fizzCut;
}

inline bool operator!= (const MacroSettings& a, const MacroSettings& b) noexcept { return ! (a == b); }

//==============================================================================
// One-click tonal shapes
//==============================================================================

inline constexpr double kButterworthQ = 0.70710678118654752440;

/** A single fixed filter contributed by a shape. Kept as data so buildChain()
    is the only place that turns a shape toggle into sound.
*/
struct ShapeFilterSpec
{
    FilterType type   = FilterType::peak;
    double     freq   = 1000.0;
    double     q      = 0.7071;
    double     gainDb = 0.0;
};

struct ShapeDefinition
{
    const char* id = "";
    const char* name = "";
    const char* tooltip = "";
    std::array<ShapeFilterSpec, 3> filters {};
    int numFilters = 0;
};

/** Order matches EqSettings::shapes and the shapeOn parameter array. */
inline constexpr std::array<ShapeDefinition, 6> shapeDefinitions {{
    { "tighten", "TIGHTEN",
      "Tightens excess lows and clears low-mid looseness.",
      {{ { FilterType::lowShelf, 130.0, kButterworthQ, -3.0 },
         { FilterType::peak,     300.0, 0.85,          -3.0 },
         { FilterType::peak,    1800.0, 1.0,            1.0 } }}, 3 },
    { "scoop", "SCOOP",
      "Applies a broad midrange scoop; use gently in dense mixes.",
      {{ { FilterType::peak, 550.0, 0.55, -4.5 },
         { FilterType::peak, 1800.0, 0.9, 1.5 },
         { FilterType::peak, 1000.0, 1.0, 0.0 } }}, 2 },
    { "midPunch", "MID PUNCH",
      "Adds forward midrange body and authority.",
      {{ { FilterType::peak, 320.0, 0.9, -3.0 },
         { FilterType::peak, 1600.0, 0.75, 4.0 },
         { FilterType::peak, 3000.0, 1.0, 1.5 } }}, 3 },
    { "pickAttack", "PICK ATTACK",
      "Adds pick definition around the attack region.",
      {{ { FilterType::peak, 350.0, 1.0, -2.0 },
         { FilterType::peak, 2600.0, 0.7, 4.0 },
         { FilterType::peak, 5000.0, 1.0, 1.5 } }}, 3 },
    { "fizzTamer", "FIZZ TAMER",
      "Reduces fizz and harshness while retaining pick attack.",
      {{ { FilterType::highShelf, 7000.0, kButterworthQ, -2.0 },
         { FilterType::peak,      5200.0, 1.0, -3.5 },
         { FilterType::peak,      8500.0, 0.8, -2.5 } }}, 3 },
    { "brighten", "BRIGHTEN",
      "Adds controlled presence to a dark guitar tone.",
      {{ { FilterType::highShelf, 8000.0, kButterworthQ, 3.0 },
         { FilterType::peak,      3000.0, 0.9, 2.0 },
         { FilterType::peak,     10000.0, 0.7, 1.5 } }}, 3 }
}};

inline const ShapeDefinition& getShapeDefinition (int index) noexcept
{
    return shapeDefinitions[static_cast<size_t> (
        clampValue (index, 0, static_cast<int> (shapeDefinitions.size()) - 1))];
}

inline int getNumShapes() noexcept { return static_cast<int> (shapeDefinitions.size()); }

//==============================================================================
// Chain construction
//==============================================================================

/** Cascaded Butterworth (Linkwitz-Riley even order) crossover sections.

    With `stages` identical sections the -3 dB point of the cascade sits below
    the cutoff of a single section, so the section frequency is compensated so
    that the labelled frequency stays at the true -3 dB point of the whole
    filter: stages 1 -> x1.000, 2 -> x1.557, 3 -> x1.993.
*/
inline double crossoverCompensation (int stages) noexcept
{
    switch (stages)
    {
        case 2:  return 1.5572;
        case 3:  return 1.9930;
        default: return 1.0;
    }
}

inline void addCrossover (ChainSpec& chain, FilterType type, double freq, int slopeChoice, double sampleRate) noexcept
{
    if (slopeChoice <= 0)
        return;

    const int    stages = clampValue (slopeChoice, 1, 3);
    const double f      = clampValue (freq * crossoverCompensation (stages), 1.0, sampleRate * 0.49);

    for (int i = 0; i < stages; ++i)
        chain.add (type, f, kButterworthQ, 0.0);
}

/** Build the complete EQ chain from the user settings and the macro settings.

    The macro filters use fixed, musically chosen frequencies so that a macro
    always does the same thing no matter where the user has placed their bands.
    They are ordinary filters, so the drawn curve includes them and every part
    of the sound stays visible and automatable.
*/
inline void buildChain (ChainSpec& chain,
                        const EqSettings& eq,
                        const MacroSettings& macros,
                        double sampleRate) noexcept
{
    chain.clear();

    const double tight   = clampValue (macros.tight,   0.0, 1.0);
    const double body    = clampValue (macros.body,    0.0, 1.0);
    const double scoop   = clampValue (macros.scoop,   0.0, 1.0);
    const double bite    = clampValue (macros.bite,    0.0, 1.0);
    const double fizzCut = clampValue (macros.fizzCut, 0.0, 1.0);

    //--- high pass -----------------------------------------------------------
    addCrossover (chain, FilterType::highPass, eq.hpFreq, eq.hpSlope, sampleRate);

    //--- user low shelf ------------------------------------------------------
    chain.add (FilterType::lowShelf, eq.lsFreq, kButterworthQ, eq.lsGainDb);

    //--- TIGHT: low-end weight and low-mid mud -------------------------------
    if (tight > 1.0e-3)
    {
        chain.add (FilterType::lowShelf, 130.0, kButterworthQ, -6.0 * tight);
        chain.add (FilterType::peak,     300.0, 0.85,          -3.5 * tight);
    }

    //--- BODY: fullness / low-mid weight (bipolar around 0.5) -----------------
    if (std::abs (body - 0.5) > 5.0e-3)
        chain.add (FilterType::lowShelf, 300.0, kButterworthQ, (body - 0.5) * 10.0);

    //--- the five user bands -------------------------------------------------
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = eq.bands[static_cast<size_t> (i)];

        if (b.on && std::abs (b.gainDb) > 1.0e-3)
            chain.add (FilterType::peak, b.freq, b.q, b.gainDb);
    }

    //--- SCOOP / BITE --------------------------------------------------------
    if (scoop > 1.0e-3)
        chain.add (FilterType::peak, 520.0, 0.50, -7.5 * scoop);

    if (bite > 1.0e-3)
        chain.add (FilterType::peak, 3000.0, 0.80, 4.5 * bite);

    //--- user high shelf -----------------------------------------------------
    chain.add (FilterType::highShelf, eq.hsFreq, kButterworthQ, eq.hsGainDb);

    //--- FIZZ CUT ------------------------------------------------------------
    if (fizzCut > 1.0e-3)
        chain.add (FilterType::highShelf, 7000.0, kButterworthQ, -5.5 * fizzCut);

    //--- independent, non-destructive tonal-shape layers -------------------
    // The filter table is the single source of truth: the graph, the audio
    // thread and the UI all derive the same sound from these entries.
    for (int shape = 0; shape < getNumShapes(); ++shape)
    {
        if (! eq.shapes[static_cast<size_t> (shape)])
            continue;

        const auto& definition = shapeDefinitions[static_cast<size_t> (shape)];
        for (int f = 0; f < definition.numFilters; ++f)
        {
            const auto& filter = definition.filters[static_cast<size_t> (f)];
            chain.add (filter.type, filter.freq, filter.q, filter.gainDb);
        }
    }

    //--- low pass ------------------------------------------------------------
    // Both switches matter: enabled without a slope is the transparent
    // default, and a slope without enabling must never filter anything.
    if (eq.lpEnabled && eq.lpSlope > 0)
        addCrossover (chain, FilterType::lowPass, eq.lpFreq, eq.lpSlope, sampleRate);

    chain.trimDb = eq.trimDb;
}

/** Response of a whole chain at one frequency. Pure function, used by the GUI
    so that the drawn curve is exactly what the audio thread will produce.
*/
inline double chainResponseDb (const ChainSpec& chain, double sampleRate, double freq) noexcept
{
    double total = 0.0;

    for (int i = 0; i < chain.numFilters; ++i)
    {
        const auto& spec = chain[i];
        const auto  c    = designBiquad (spec.type, sampleRate, spec.freq, spec.q, spec.gainDb);
        total += magnitudeDb (c, sampleRate, freq);
    }

    return total + chain.trimDb;
}

/** Convenience: response of a settings + macro combination at one frequency. */
inline double responseDb (const EqSettings& eq, const MacroSettings& macros,
                          double sampleRate, double freq) noexcept
{
    ChainSpec chain;
    buildChain (chain, eq, macros, sampleRate);
    return chainResponseDb (chain, sampleRate, freq);
}

//==============================================================================
// Real-time processing engine
//==============================================================================

/** A single biquad section with fixed-size per-channel state.

    Coefficients are never shared mutable state: every channel owns the
    coefficients it is currently using plus the step towards the new target, so
    left and right stay bit-identical and nothing can be swapped underneath the
    audio thread. No locking, no allocation, no reference counting.
*/
class BiquadSection
{
public:
    struct ChannelState
    {
        double z1 = 0.0, z2 = 0.0;                              // filter memory
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;  // current coefficients
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;  // step per coefficient
        int    remaining = 0;                                    // samples left in the ramp
        bool   primed    = false;                                // has ever been initialised
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
        setPassthrough();
    }

    /** Straight-through. Used for sections that are switched off. */
    void setPassthrough() noexcept
    {
        active_ = false;
        target_ = BiquadCoeffs {};

        for (auto& c : channels_)
        {
            c = ChannelState {};
            c.primed = true;
        }
    }

    /** Ramp from the coefficients currently in use to `target` over `rampLength`
        samples. The whole buffer is still processed, the tail runs on the
        settled coefficients.
    */
    void setTarget (const BiquadCoeffs& target, int rampLength) noexcept
    {
        if (! (isFiniteValue (target.b0) && isFiniteValue (target.b1) && isFiniteValue (target.b2)
               && isFiniteValue (target.a1) && isFiniteValue (target.a2)))
        {
            setPassthrough();
            return;
        }

        active_ = true;
        target_ = target;

        const int len = clampValue (rampLength, 2, 1 << 20);
        const double inv = 1.0 / static_cast<double> (len);

        for (auto& c : channels_)
        {
            if (! c.primed)
            {
                c = ChannelState {};
                c.primed = true;
                c.b0 = target.b0; c.b1 = target.b1; c.b2 = target.b2;
                c.a1 = target.a1; c.a2 = target.a2;
                c.remaining = 0;
                continue;
            }

            // Already there: stay in the fast path. Without this check a ramp
            // restarted on every block would never settle and the realised
            // response would never exactly match the requested one.
            if (c.b0 == target.b0 && c.b1 == target.b1 && c.b2 == target.b2
                 && c.a1 == target.a1 && c.a2 == target.a2)
            {
                c.remaining = 0;
                continue;
            }

            c.s0 = (target.b0 - c.b0) * inv;
            c.s1 = (target.b1 - c.b1) * inv;
            c.s2 = (target.b2 - c.b2) * inv;
            c.s3 = (target.a1 - c.a1) * inv;
            c.s4 = (target.a2 - c.a2) * inv;
            c.remaining = len;
        }
    }

    void reset() noexcept { setPassthrough(); }

    bool isActive() const noexcept { return active_; }

    /** True while any channel is still gliding towards its target. */
    bool isRamping() const noexcept
    {
        for (const auto& c : channels_)
            if (c.remaining > 0)
                return true;
        return false;
    }

    /** Ramp to identity without abruptly removing the section from the chain.
        Used when the chain shrinks; EqEngine keeps processing the retiring
        tail until every channel has settled, then switches it off.
    */
    void rampToPassthrough (int rampLength) noexcept
    {
        setTarget (BiquadCoeffs {}, rampLength);
    }

    BiquadCoeffs getCurrent() const noexcept
    {
        const auto& c = channels_[0];
        return { c.b0, c.b1, c.b2, c.a1, c.a2 };
    }

    /** Process one channel. Transposed direct form II with double precision
        state; the inner loop has no branches.
    */
    void processChannel (int channel, float* samples, int numSamples) noexcept
    {
        if (channel < 0 || channel >= kMaxChannels || samples == nullptr || numSamples <= 0)
            return;

        if (! active_)
        {
            auto& s = channels_[static_cast<size_t> (channel)];
            s.z1 = 0.0;
            s.z2 = 0.0;
            return;
        }

        auto& s = channels_[static_cast<size_t> (channel)];
        int i = 0;

        //--- coefficient ramp -------------------------------------------------
        if (s.remaining > 0)
        {
            const int n = std::min (numSamples, s.remaining);

            double b0 = s.b0 + s.s0, b1 = s.b1 + s.s1, b2 = s.b2 + s.s2;
            double a1 = s.a1 + s.s3, a2 = s.a2 + s.s4;
            double z1 = s.z1,  z2 = s.z2;

            for (; i < n; ++i)
            {
                const double x = samples[i];
                const double y = b0 * x + z1;

                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;

                samples[i] = static_cast<float> (y);

                b0 += s.s0; b1 += s.s1; b2 += s.s2; a1 += s.s3; a2 += s.s4;
            }

            s.b0 = b0; s.b1 = b1; s.b2 = b2; s.a1 = a1; s.a2 = a2;
            s.z1 = z1;  s.z2 = z2;
            s.remaining -= n;

            if (s.remaining == 0)
            {
                s.b0 = target_.b0; s.b1 = target_.b1; s.b2 = target_.b2;
                s.a1 = target_.a1; s.a2 = target_.a2;
            }
        }

        //--- steady state -----------------------------------------------------
        {
            const double b0 = s.b0, b1 = s.b1, b2 = s.b2, a1 = s.a1, a2 = s.a2;
            double z1 = s.z1, z2 = s.z2;

            for (; i < numSamples; ++i)
            {
                const double x = samples[i];
                const double y = b0 * x + z1;

                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;

                samples[i] = static_cast<float> (y);
            }

            s.z1 = z1;
            s.z2 = z2;
        }
    }

private:
    double     sampleRate_ = 44100.0;
    BiquadCoeffs target_ {};
    bool       active_ = false;
    std::array<ChannelState, kMaxChannels> channels_ {};
};

//==============================================================================
/** The full EQ engine: a fixed cascade of biquads plus a smoothed output trim.

    `process()` touches nothing but preallocated memory, which makes it safe to
    call from any real-time audio callback.
*/
class EqEngine
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;

        for (auto& f : filters_)
            f.prepare (sampleRate_);

        numActiveFilters_ = 0;
        numInstalledFilters_ = 0;
        trimGain_ = 1.0f;
        trimRampStart_ = 1.0f;

        for (auto& t : trim_)
            t = { 1.0f, 0.0f, 0, 1.0f };
    }

    void reset() noexcept
    {
        for (auto& f : filters_)
            f.reset();

        numActiveFilters_ = 0;
        numInstalledFilters_ = 0;
        trimGain_ = 1.0f;
        trimRampStart_ = 1.0f;

        for (auto& t : trim_)
            t = { 1.0f, 0.0f, 0, 1.0f };
    }

    double getSampleRate() const noexcept { return sampleRate_; }

    /** Design and install the chain. `rampLength` samples glide from the old to
        the new coefficients so that even a full preset change is click free.
    */
    void setChain (const ChainSpec& chain, int rampLength) noexcept
    {
        const int n = clampValue (chain.numFilters, 0, kMaxChainSize);
        const int previousInstalled = numInstalledFilters_;

        for (int i = 0; i < n; ++i)
        {
            const auto& spec = chain[i];
            const auto  c    = designBiquad (spec.type, sampleRate_, spec.freq, spec.q, spec.gainDb);

            filters_[static_cast<size_t> (i)].setTarget (c, rampLength);
        }

        // Do not drop removed sections instantly: ramp them to identity and
        // keep processing them until the glide has finished. This avoids the
        // click that a hard bypass would produce on preset changes.
        if (n < previousInstalled)
            for (int i = n; i < previousInstalled; ++i)
                filters_[static_cast<size_t> (i)].rampToPassthrough (rampLength);

        numInstalledFilters_ = std::max (previousInstalled, n);
        numActiveFilters_ = n;

        setTrimGain (chain.trimDb, rampLength);
    }

    void setTrimGain (double trimDb, int rampLength) noexcept
    {
        if (! isFiniteValue (trimDb))
            trimDb = 0.0;

        trimDb = clampValue (trimDb, -60.0, 24.0);

        const double target = std::pow (10.0, trimDb / 20.0);

        if (! isFiniteValue (target) || target <= 0.0)
            return;

        const float targetF = static_cast<float> (target);

        // Nothing to do: every channel has already reached the target. (The
        // exact equality check is safe because process() snaps to `target`.)
        bool settled = true;

        for (auto& t : trim_)
            if (t.gain != targetF || t.remaining != 0)
            {
                settled = false;
                break;
            }

        if (settled)
            return;

        trimGain_ = targetF;

        const int len = clampValue (rampLength, 2, 1 << 20);

        if (len <= 2)
        {
            for (auto& t : trim_)
                t = { targetF, 0.0f, 0, targetF };
            return;
        }

        trimRampStart_ = trim_[0].gain;
        const float step = (targetF - trimRampStart_) / static_cast<float> (len);

        for (auto& t : trim_)
            t = { trimRampStart_, step, len, targetF };
    }

    /** Process a block. `channelData[ch]` is modified in place. */
    void process (float* const* channelData, int numChannels, int numSamples) noexcept
    {
        if (channelData == nullptr || numChannels <= 0 || numSamples <= 0)
            return;

        numChannels = clampValue (numChannels, 0, kMaxChannels);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channelData[ch];

            if (data == nullptr)
                continue;

            for (int i = 0; i < numInstalledFilters_; ++i)
                filters_[static_cast<size_t> (i)].processChannel (ch, data, numSamples);

            //--- smoothed output trim --------------------------------------
            auto& t = trim_[static_cast<size_t> (ch)];
            int  i   = 0;

            if (t.remaining > 0)
            {
                const int n = std::min (numSamples, t.remaining);
                float g = t.gain;

                for (; i < n; ++i)
                {
                    data[i] *= g;
                    g += t.step;
                }

                t.remaining -= n;
                t.gain = (t.remaining == 0) ? t.target : g;
            }

            if (t.gain != 1.0f)
                for (; i < numSamples; ++i)
                    data[i] *= t.gain;
        }

        retireFinishedFilters();
    }

    /** Magnitude of the coefficients currently installed. Message thread only. */
    double getMagnitudeDb (double freq) const noexcept
    {
        double total = 0.0;

        for (int i = 0; i < numActiveFilters_; ++i)
            total += magnitudeDb (filters_[static_cast<size_t> (i)].getCurrent(), sampleRate_, freq);

        return total + 20.0 * std::log10 (static_cast<double> (trimGain_));
    }

    int getNumActiveFilters() const noexcept { return numActiveFilters_; }

private:
    /** Once every retiring section has reached identity, stop processing it. */
    void retireFinishedFilters() noexcept
    {
        if (numInstalledFilters_ <= numActiveFilters_)
            return;

        for (int i = numActiveFilters_; i < numInstalledFilters_; ++i)
            if (filters_[static_cast<size_t> (i)].isRamping())
                return;

        for (int i = numActiveFilters_; i < numInstalledFilters_; ++i)
            filters_[static_cast<size_t> (i)].setPassthrough();

        numInstalledFilters_ = numActiveFilters_;
    }

    struct TrimState
    {
        float gain      = 1.0f;
        float step      = 0.0f;
        int   remaining = 0;
        float target    = 1.0f;
    };

    double sampleRate_ = 44100.0;

    std::array<BiquadSection, kMaxChainSize> filters_ {};
    int numActiveFilters_ = 0;
    int numInstalledFilters_ = 0;

    std::array<TrimState, kMaxChannels> trim_ {};
    float trimGain_ = 1.0f;
    float trimRampStart_ = 1.0f;
};

} // namespace metaleq

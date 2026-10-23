/*
    MetalEQ - resonance analysis (V3).

    Finds narrow, stationary spectral peaks in a captured guitar performance.

    Two independent pieces of evidence have to agree, because either one alone
    is easy to fool:

      * LTAS prominence. A long-term average spectrum is smoothed over 1/12
        octave and compared with a 1/2-octave median baseline. A true resonance
        is a bump that is narrower than the baseline window. Shared harmonics
        can also show up here, which is why the second test exists.

      * Harmonic residual. For every frame the fundamental is estimated with an
        FFT autocorrelation, the amplitude of each harmonic is measured, and
        each harmonic is compared with the local median of its neighbours.
        A resonance lifts whichever harmonic happens to fall inside it, for
        every note; a naturally strong harmonic only lifts the residual for the
        notes that contain it. Across a varied performance a resonance builds a
        stable positive residual at a fixed frequency, while the harmonics move.

    A candidate must clear both gates, persist over the capture and have a
    plausible Q (a very high Q is almost always a single harmonic). The output
    is advisory: the UI lets the player audition and choose before any cut is
    applied to the dedicated surgical bands.

    This header is JUCE-free and allocates only while running on the analyzer
    worker thread, never on the audio thread.
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "MetalEqDsp.h"

namespace metaleq
{

inline constexpr int kMaxResonanceCandidates = 12;
/** How many candidates the UI receives. More than this is rarely actionable. */
inline constexpr int kMaxReportedResonances = 8;
inline constexpr int kMinResonanceFreq = 80;
inline constexpr int kMaxResonanceFreq = 14000;

struct ResonanceCandidate
{
    double freq           = 0.0;
    double q              = 3.0;
    double prominenceDb   = 0.0;   // LTAS peak height above the local envelope
    double residualDb     = 0.0;   // harmonic-residual evidence at this frequency
    double confidence     = 0.0;   // 0..1
    double suggestedCutDb = -2.0;  // negative: the scan only proposes cuts
};

struct ResonanceAnalysisResult
{
    std::array<ResonanceCandidate, kMaxResonanceCandidates> candidates {};
    int  numCandidates  = 0;
    int  framesAnalysed = 0;
    /** False when the capture was too static (e.g. one sustained note), which
        makes it impossible to tell a harmonic from a resonance.
    */
    bool enoughVariation = false;
};

namespace detail
{

inline void fftRadix2 (std::vector<double>& re, std::vector<double>& im)
{
    const int n = static_cast<int> (re.size());
    if (n < 2 || (n & (n - 1)) != 0)
        return;

    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
        {
            std::swap (re[static_cast<size_t> (i)], re[static_cast<size_t> (j)]);
            std::swap (im[static_cast<size_t> (i)], im[static_cast<size_t> (j)]);
        }
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * kPi / static_cast<double> (len);
        const double wRe   = std::cos (angle);
        const double wIm   = std::sin (angle);
        const int    half  = len >> 1;

        for (int start = 0; start < n; start += len)
        {
            double curRe = 1.0;
            double curIm = 0.0;

            for (int k = 0; k < half; ++k)
            {
                const int a = start + k;
                const int b = a + half;
                const double vRe = re[static_cast<size_t> (b)] * curRe
                                 - im[static_cast<size_t> (b)] * curIm;
                const double vIm = re[static_cast<size_t> (b)] * curIm
                                 + im[static_cast<size_t> (b)] * curRe;

                re[static_cast<size_t> (b)] = re[static_cast<size_t> (a)] - vRe;
                im[static_cast<size_t> (b)] = im[static_cast<size_t> (a)] - vIm;
                re[static_cast<size_t> (a)] += vRe;
                im[static_cast<size_t> (a)] += vIm;

                const double nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }
}

inline void ifftRadix2 (std::vector<double>& re, std::vector<double>& im)
{
    for (auto& value : im)
        value = -value;

    fftRadix2 (re, im);

    const double n = static_cast<double> (re.size());
    for (size_t i = 0; i < re.size(); ++i)
    {
        re[i] /= n;
        im[i] = -im[i] / n;
    }
}

inline double medianOf (std::vector<double>& values)
{
    if (values.empty())
        return 0.0;

    const size_t mid = values.size() / 2;
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (mid), values.end());

    double result = values[mid];

    if ((values.size() & 1u) == 0u)
    {
        std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (mid - 1), values.end());
        result = 0.5 * (result + values[mid - 1]);
    }

    return result;
}

inline double clampDouble (double v, double lo, double hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Perceptually weighted score contribution by frequency. The ear is most
    sensitive around 2-5 kHz, which is also where high-gain harshness lives.
*/
inline double frequencyWeight (double freq) noexcept
{
    if (freq < 150.0)  return 0.45;
    if (freq < 400.0)  return 0.75;
    if (freq < 700.0)  return 0.9;
    if (freq < 1600.0) return 1.0;
    if (freq < 6000.0) return 1.15;
    if (freq < 10000.0) return 1.0;
    return 0.75;
}

struct Peak
{
    double freq = 0.0;
    double q = 1.0;
    double prominenceDb = 0.0;
    double residualDb = 0.0;
    double coverage = 0.0;
    double score = 0.0;
    double confidence = 0.0;
};

} // namespace detail

/** Analyse a mono capture. Returns the most likely stationary resonances,
    strongest/most persistent first. Safe to call on a worker thread.
*/
inline void analyseResonances (const float* input,
                               int numSamples,
                               double sampleRate,
                               ResonanceAnalysisResult& result)
{
    result = ResonanceAnalysisResult {};

    if (input == nullptr || numSamples <= 0 || ! isFiniteValue (sampleRate)
        || sampleRate < 8000.0)
        return;

    // ~6 Hz resolution at every supported rate: 8192 at 44.1/48 kHz, 16384 at
    // 96 kHz, 32768 at 192 kHz. Cap the transform so a short capture still
    // produces a usable number of frames.
    int fftSize = 8192;
    while (fftSize < 32768 && sampleRate / static_cast<double> (fftSize) > 7.0)
        fftSize <<= 1;
    while (fftSize > 2048 && fftSize > numSamples)
        fftSize >>= 1;

    if (fftSize < 2048 || numSamples < fftSize)
        return;

    const int    hop     = fftSize / 2;
    const int    bins    = fftSize / 2;
    const double binHz   = sampleRate / static_cast<double> (fftSize);
    const double nyquist = sampleRate * 0.5;

    std::vector<double> window (static_cast<size_t> (fftSize));
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] =
            0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i)
                                  / static_cast<double> (fftSize - 1));

    std::vector<double> re (static_cast<size_t> (fftSize));
    std::vector<double> im (static_cast<size_t> (fftSize));
    std::vector<double> autocorrRe (static_cast<size_t> (fftSize));
    std::vector<double> autocorrIm (static_cast<size_t> (fftSize), 0.0);

    std::vector<double> powerSum (static_cast<size_t> (bins + 1), 0.0);
    std::vector<double> residualSum (static_cast<size_t> (bins + 1), 0.0);
    std::vector<double> residualWeight (static_cast<size_t> (bins + 1), 0.0);

    std::vector<double> previousShape;
    std::vector<double> currentShape (static_cast<size_t> (bins + 1), 0.0);
    std::vector<double> harmonicDb;
    std::vector<double> neighbours;

    int    frames          = 0;
    int    harmonicFrames  = 0;
    double variationTotal  = 0.0;
    int    variationPairs  = 0;

    for (int offset = 0; offset + fftSize <= numSamples; offset += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const double x = static_cast<double> (input[offset + i]);
            energy += x * x;
            re[static_cast<size_t> (i)] = x * window[static_cast<size_t> (i)];
            im[static_cast<size_t> (i)] = 0.0;
        }

        if (energy / static_cast<double> (fftSize) < 1.0e-9)
            continue;

        ++frames;

        detail::fftRadix2 (re, im);

        //--- average power spectrum and frame-to-frame variation ------------
        std::vector<double> framePower (static_cast<size_t> (bins + 1), 0.0);
        for (int bin = 0; bin <= bins; ++bin)
        {
            const double power = re[static_cast<size_t> (bin)] * re[static_cast<size_t> (bin)]
                               + im[static_cast<size_t> (bin)] * im[static_cast<size_t> (bin)];
            framePower[static_cast<size_t> (bin)] = power;
            powerSum[static_cast<size_t> (bin)] += power;

            currentShape[static_cast<size_t> (bin)] =
                10.0 * std::log10 (power + 1.0e-20);
        }

        if (! previousShape.empty())
        {
            double meanA = 0.0;
            double meanB = 0.0;
            int    count = 0;

            for (int bin = 2; bin < bins; ++bin)
            {
                meanA += previousShape[static_cast<size_t> (bin)];
                meanB += currentShape[static_cast<size_t> (bin)];
                ++count;
            }

            if (count > 0)
            {
                meanA /= static_cast<double> (count);
                meanB /= static_cast<double> (count);

                double diff = 0.0;
                for (int bin = 2; bin < bins; ++bin)
                    diff += std::abs ((previousShape[static_cast<size_t> (bin)] - meanA)
                                    - (currentShape[static_cast<size_t> (bin)] - meanB));

                variationTotal += diff / static_cast<double> (count);
                ++variationPairs;
            }
        }

        previousShape = currentShape;

        //--- fundamental estimate (FFT autocorrelation) ---------------------
        for (int bin = 0; bin <= bins; ++bin)
        {
            autocorrRe[static_cast<size_t> (bin)] = framePower[static_cast<size_t> (bin)];

            if (bin > 0 && bin < bins)
                autocorrRe[static_cast<size_t> (fftSize - bin)] = framePower[static_cast<size_t> (bin)];
        }

        std::fill (autocorrIm.begin(), autocorrIm.end(), 0.0);
        detail::ifftRadix2 (autocorrRe, autocorrIm);

        const int minLag = std::max (2, static_cast<int> (sampleRate / 1000.0));
        const int maxLag = std::min (fftSize / 2 - 1, static_cast<int> (sampleRate / 55.0));

        double bestCorrelation = 0.0;
        int    bestLag = minLag;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double value = autocorrRe[static_cast<size_t> (lag)]
                               / (autocorrRe[0] + 1.0e-20);
            if (value > bestCorrelation)
            {
                bestCorrelation = value;
                bestLag = lag;
            }
        }

        if (bestCorrelation < 0.30)
            continue;

        // Prefer the shortest strong periodicity: this avoids choosing a
        // multiple of the true period on a distorted, harmonic-rich note.
        int chosenLag = bestLag;
        for (int lag = minLag; lag <= bestLag; ++lag)
        {
            const double value = autocorrRe[static_cast<size_t> (lag)]
                               / (autocorrRe[0] + 1.0e-20);
            const double prev  = lag > minLag
                ? autocorrRe[static_cast<size_t> (lag - 1)] / (autocorrRe[0] + 1.0e-20) : -2.0;
            const double next  = lag < maxLag
                ? autocorrRe[static_cast<size_t> (lag + 1)] / (autocorrRe[0] + 1.0e-20) : -2.0;

            if (value >= 0.88 * bestCorrelation && value >= prev && value >= next)
            {
                chosenLag = lag;
                break;
            }
        }

        double lagOffset = 0.0;
        if (chosenLag > minLag && chosenLag < maxLag)
        {
            const double a = autocorrRe[static_cast<size_t> (chosenLag - 1)];
            const double b = autocorrRe[static_cast<size_t> (chosenLag)];
            const double c = autocorrRe[static_cast<size_t> (chosenLag + 1)];
            const double denom = a - 2.0 * b + c;

            if (std::abs (denom) > 1.0e-12)
                lagOffset = 0.5 * (a - c) / denom;

            lagOffset = detail::clampDouble (lagOffset, -0.5, 0.5);
        }

        const double f0 = sampleRate / (static_cast<double> (chosenLag) + lagOffset);
        if (f0 < 55.0 || f0 > 1000.0)
            continue;

        ++harmonicFrames;

        //--- harmonic amplitudes and local residual --------------------------
        harmonicDb.clear();
        for (int harmonic = 1; harmonic <= 60; ++harmonic)
        {
            const double freq = f0 * static_cast<double> (harmonic);
            if (freq > static_cast<double> (kMaxResonanceFreq))
                break;

            const int centre = static_cast<int> (std::lround (freq / binHz));
            const int radius = std::max (2, static_cast<int> (std::lround (freq * 0.006 / binHz)));

            double peakPower = 0.0;
            for (int bin = std::max (1, centre - radius);
                 bin <= std::min (bins, centre + radius); ++bin)
                peakPower = std::max (peakPower, framePower[static_cast<size_t> (bin)]);

            harmonicDb.push_back (10.0 * std::log10 (peakPower + 1.0e-20));
        }

        if (harmonicDb.size() < 8)
            continue;

        for (int index = 3; index + 3 < static_cast<int> (harmonicDb.size()); ++index)
        {
            neighbours.clear();
            for (int k = index - 3; k <= index + 3; ++k)
                if (k != index && k >= 0 && k < static_cast<int> (harmonicDb.size()))
                    neighbours.push_back (harmonicDb[static_cast<size_t> (k)]);

            const double smooth = detail::medianOf (neighbours);
            const double residual = harmonicDb[static_cast<size_t> (index)] - smooth;

            if (! std::isfinite (residual) || std::abs (residual) > 18.0)
                continue;

            const double freq = f0 * static_cast<double> (index + 1);
            const double logFreq = std::log2 (freq);
            const double sigma = 0.025; // ~1/40 octave

            for (int bin = 1; bin <= bins; ++bin)
            {
                const double binFreq = static_cast<double> (bin) * binHz;
                if (binFreq < 60.0 || binFreq > static_cast<double> (kMaxResonanceFreq))
                    continue;

                const double distance = (std::log2 (binFreq) - logFreq) / sigma;
                const double weight = std::exp (-0.5 * distance * distance);

                if (weight < 0.02)
                    continue;

                residualSum[static_cast<size_t> (bin)] += residual * weight;
                residualWeight[static_cast<size_t> (bin)] += weight;
            }
        }
    }

    result.framesAnalysed = frames;

    if (frames < 8)
        return;

    result.enoughVariation = variationPairs > 0
        && variationTotal / static_cast<double> (variationPairs) > 0.8;

    if (harmonicFrames < std::max (6, frames / 4))
        return;

    //--- long-term average spectrum -----------------------------------------
    for (auto& power : powerSum)
        power /= static_cast<double> (frames);

    std::vector<double> smoothed (static_cast<size_t> (bins + 1), 0.0);
    for (int bin = 2; bin < bins; ++bin)
    {
        // 1/12-octave power average: fills the gaps between harmonics so the
        // envelope, rather than the comb, is compared with the baseline.
        const int half = std::max (5, static_cast<int> (std::lround (
            static_cast<double> (bin) * (std::pow (2.0, 1.0 / 24.0) - 1.0))));
        const int lo = std::max (1, bin - half);
        const int hi = std::min (bins, bin + half);

        double sum = 0.0;
        for (int b = lo; b <= hi; ++b)
            sum += powerSum[static_cast<size_t> (b)];

        smoothed[static_cast<size_t> (bin)] =
            10.0 * std::log10 (sum / static_cast<double> (hi - lo + 1) + 1.0e-20);
    }

    std::vector<double> baseline (static_cast<size_t> (bins + 1), 0.0);
    std::vector<double> prominence (static_cast<size_t> (bins + 1), 0.0);
    std::vector<double> windowValues;

    for (int bin = 2; bin < bins; ++bin)
    {
        // 1/2-octave median baseline. Wider than any resonance we act on, so
        // a Q >= 3 bump survives, while the overall tonal balance does not.
        const int half = std::max (12, static_cast<int> (std::lround (
            static_cast<double> (bin) * (std::pow (2.0, 0.25) - 1.0))));
        const int lo = std::max (2, bin - half);
        const int hi = std::min (bins - 1, bin + half);

        windowValues.clear();
        for (int b = lo; b <= hi; ++b)
            windowValues.push_back (smoothed[static_cast<size_t> (b)]);

        baseline[static_cast<size_t> (bin)] = detail::medianOf (windowValues);
        prominence[static_cast<size_t> (bin)] =
            smoothed[static_cast<size_t> (bin)] - baseline[static_cast<size_t> (bin)];
    }

    std::vector<double> residualMap (static_cast<size_t> (bins + 1), -99.0);
    for (int bin = 2; bin < bins; ++bin)
        if (residualWeight[static_cast<size_t> (bin)] > 0.5)
            residualMap[static_cast<size_t> (bin)] =
                residualSum[static_cast<size_t> (bin)] / residualWeight[static_cast<size_t> (bin)];

    //--- collect candidates -------------------------------------------------
    std::vector<detail::Peak> peaks;

    for (int bin = 3; bin < bins - 2; ++bin)
    {
        const double prom = prominence[static_cast<size_t> (bin)];
        const double residual = residualMap[static_cast<size_t> (bin)];

        if (prom < 2.0 || residual < 1.5)
            continue;

        if (prom < prominence[static_cast<size_t> (bin - 1)]
            || prom < prominence[static_cast<size_t> (bin + 1)])
            continue;

        const double freq = static_cast<double> (bin) * binHz;
        if (freq < static_cast<double> (kMinResonanceFreq)
            || freq > static_cast<double> (kMaxResonanceFreq)
            || freq >= nyquist * 0.95)
            continue;

        // -3 dB width on the smoothed LTAS.
        const double threshold = smoothed[static_cast<size_t> (bin)] - 3.0;
        int left = bin;
        while (left > 2 && smoothed[static_cast<size_t> (left - 1)] > threshold)
            --left;
        int right = bin;
        while (right < bins - 1 && smoothed[static_cast<size_t> (right + 1)] > threshold)
            ++right;

        const double width = std::max (binHz, static_cast<double> (right - left) * binHz);
        const double q = freq / width;

        if (q < 2.0 || q > 16.0)
            continue;

        const double coverage = static_cast<double> (frames) > 0.0
            ? detail::clampDouble (residualWeight[static_cast<size_t> (bin)]
                                   / (static_cast<double> (frames) * 0.5), 0.0, 1.0)
            : 0.0;

        if (residualWeight[static_cast<size_t> (bin)] < 2.0)
            continue;

        detail::Peak peak;
        peak.freq = freq;
        peak.q = q;
        peak.prominenceDb = prom;
        peak.residualDb = residual;
        peak.coverage = coverage;

        const double narrowPenalty = q > 13.0
            ? detail::clampDouble (1.0 - (q - 13.0) / 6.0, 0.0, 1.0) : 1.0;

        peak.score = residual * prom
                   * (0.45 + 0.55 * coverage)
                   * detail::frequencyWeight (freq)
                   * narrowPenalty;

        peak.confidence = detail::clampDouble (
            0.45 * detail::clampDouble ((residual - 1.0) / 5.0, 0.0, 1.0)
            + 0.35 * detail::clampDouble ((prom - 1.5) / 6.0, 0.0, 1.0)
            + 0.20 * coverage, 0.0, 1.0);

        if (! result.enoughVariation)
            peak.confidence *= 0.65;

        peaks.push_back (peak);
    }

    if (peaks.empty())
        return;

    std::sort (peaks.begin(), peaks.end(),
               [] (const detail::Peak& a, const detail::Peak& b) { return a.score > b.score; });

    //--- cluster nearby bins, strongest first --------------------------------
    const double clusterRatio = std::pow (2.0, 1.0 / 6.0); // 1/6 octave

    for (const auto& peak : peaks)
    {
        if (result.numCandidates >= kMaxReportedResonances)
            break;

        bool duplicate = false;
        for (int i = 0; i < result.numCandidates; ++i)
        {
            const double other = result.candidates[static_cast<size_t> (i)].freq;
            const double ratio = peak.freq > other ? peak.freq / other : other / peak.freq;
            if (ratio < clusterRatio)
            {
                duplicate = true;
                break;
            }
        }

        if (duplicate)
            continue;

        auto& candidate = result.candidates[static_cast<size_t> (result.numCandidates++)];
        candidate.freq         = peak.freq;
        candidate.q            = detail::clampDouble (peak.q * 0.9, 3.0, 12.0);
        candidate.prominenceDb = peak.prominenceDb;
        candidate.residualDb   = peak.residualDb;
        candidate.confidence   = peak.confidence;

        // Conservative: propose the smallest cut likely to remove the peak.
        candidate.suggestedCutDb = -detail::clampDouble (
            0.65 * peak.prominenceDb + 0.20 * peak.residualDb, 1.0, 6.0);
    }
}

} // namespace metaleq

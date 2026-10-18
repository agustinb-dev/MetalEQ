#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "MetalEqDsp.h"

namespace metaleq::graph
{

inline constexpr std::array<double, 23> guitarLandmarks
    { 20.0, 40.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0, 400.0, 500.0,
      800.0, 1000.0, 1500.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0,
      8000.0, 10000.0, 12000.0, 16000.0, 20000.0 };

inline double frequencyToUnit (double frequency) noexcept
{
    frequency = clampValue (frequency, kGraphMinFreq, kGraphMaxFreq);
    return std::log (frequency / kGraphMinFreq) / std::log (kGraphMaxFreq / kGraphMinFreq);
}

inline double unitToFrequency (double unit) noexcept
{
    unit = clampValue (unit, 0.0, 1.0);
    return kGraphMinFreq * std::pow (kGraphMaxFreq / kGraphMinFreq, unit);
}

inline double dbToUnit (double db, double minDb = -18.0, double maxDb = 12.0) noexcept
{
    return clampValue ((db - minDb) / (maxDb - minDb), 0.0, 1.0);
}

/** Select a non-overlapping subset of frequency labels for the available width. */
inline std::vector<size_t> visibleLabelIndices (double width, double minLabelSpacing) 
{
    std::vector<size_t> selected;
    if (width <= 0.0 || minLabelSpacing <= 0.0)
        return selected;

    double lastX = 0.0;
    const double step = width;
    selected.push_back (0);
    for (size_t i = 0; i < guitarLandmarks.size(); ++i)
    {
        const double f = guitarLandmarks[i];
        if (f < kGraphMinFreq || f > kGraphMaxFreq)
            continue;
        if (f == kGraphMinFreq || f == kGraphMaxFreq)
            continue;
        const double x = frequencyToUnit (f) * step;
        if (x - lastX >= minLabelSpacing && x <= width - minLabelSpacing)
        {
            selected.push_back (i);
            lastX = x;
        }
    }
    const size_t last = guitarLandmarks.size() - 1;
    if (width - lastX < minLabelSpacing && selected.size() > 1)
        selected.pop_back();
    if (width >= minLabelSpacing)
        selected.push_back (last);
    return selected;
}

inline const char* frequencyLabel (double frequency) noexcept
{
    if (frequency >= 1000.0)
    {
        // Labels are stable literals so this helper remains allocation-free.
        if (frequency == 1000.0) return "1k";
        if (frequency == 1500.0) return "1.5k";
        if (frequency == 2000.0) return "2k";
        if (frequency == 3000.0) return "3k";
        if (frequency == 4000.0) return "4k";
        if (frequency == 5000.0) return "5k";
        if (frequency == 6000.0) return "6k";
        if (frequency == 8000.0) return "8k";
        if (frequency == 10000.0) return "10k";
        if (frequency == 12000.0) return "12k";
        if (frequency == 16000.0) return "16k";
        if (frequency == 20000.0) return "20k";
    }
    if (frequency == 20.0) return "20";
    if (frequency == 40.0) return "40";
    if (frequency == 60.0) return "60";
    if (frequency == 80.0) return "80";
    if (frequency == 100.0) return "100";
    if (frequency == 150.0) return "150";
    if (frequency == 200.0) return "200";
    if (frequency == 300.0) return "300";
    if (frequency == 400.0) return "400";
    if (frequency == 500.0) return "500";
    if (frequency == 800.0) return "800";
    return "";
}

} // namespace metaleq::graph

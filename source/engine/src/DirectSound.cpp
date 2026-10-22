#include "drenched/DirectSound.h"
#include "drenched/Biquad.h"

#include "Bands.h"

#include <algorithm>
#include <cmath>

namespace drenched
{

namespace
{
    constexpr double searchSeconds = 0.05;
    constexpr double beforeSeconds = 0.001, afterSeconds = 0.0015, edgeSeconds = 0.0005;
    constexpr double ringSeconds = 0.01; // room for the shelves to ring out

    std::size_t samplesFor (double seconds, double sampleRate) noexcept
    {
        return static_cast<std::size_t> (std::max (0.0, std::round (seconds * sampleRate)));
    }
}

std::size_t findDirectSound (const std::vector<float>& path, double sampleRate) noexcept
{
    const auto end = std::min (path.size(), samplesFor (searchSeconds, sampleRate));
    auto loudest = 0.0f;

    for (std::size_t n = 0; n < end; ++n)
        loudest = std::max (loudest, std::abs (path[n]));

    if (loudest <= 0.0f)
        return path.size();

    auto first = std::size_t { 0 };

    while (std::abs (path[first]) < 0.1f * loudest)
        ++first;

    // The arrival's own peak, within the next half millisecond.
    auto peak = first;

    for (auto n = first; n < std::min (end, first + samplesFor (edgeSeconds, sampleRate) + 1); ++n)
        if (std::abs (path[n]) > std::abs (path[peak]))
            peak = n;

    return peak;
}

void applyDirect (ImpulseResponseSet& responses, const ImpulseResponseRequest& request)
{
    const auto rate = responses.sampleRate;
    const auto neutral = request.directLevelDb == 0.0f && request.directDelayMs <= 0.0f && request.directColourDb == 0.0f;

    if (neutral || rate <= 0.0 || responses.length() == 0)
        return;

    const auto gain = request.directLevelDb <= minDirectLevelDb ? 0.0 : std::pow (10.0, request.directLevelDb / 20.0);
    const auto delay = samplesFor (std::clamp (request.directDelayMs, 0.0f, maxDirectDelayMs) / 1000.0, rate);
    const auto before = samplesFor (beforeSeconds, rate), after = samplesFor (afterSeconds, rate);
    const auto edge = std::max<std::size_t> (1, samplesFor (edgeSeconds, rate));
    const auto ring = samplesFor (ringSeconds, rate);

    const auto low = Biquad::lowShelf (rate, 250.0, -0.5 * request.directColourDb);
    const auto high = Biquad::highShelf (rate, 2500.0, 0.5 * request.directColourDb);

    std::vector<float> direct;

    for (auto& path : responses.paths)
    {
        const auto peak = findDirectSound (path, rate);

        if (peak >= path.size())
            continue;

        const auto start = peak > before ? peak - before : 0;
        const auto stop = std::min (path.size(), peak + after);
        const auto width = stop - start;

        // Lift the arrival out with soft edges, so what stays behind joins smoothly.
        direct.assign (width + ring, 0.0f);

        for (std::size_t i = 0; i < width; ++i)
        {
            const auto fadeIn = bands::raisedCosine (static_cast<double> (i + 1) / static_cast<double> (edge + 1));
            const auto fadeOut = bands::raisedCosine (static_cast<double> (width - i) / static_cast<double> (edge + 1));
            const auto w = static_cast<float> (fadeIn * fadeOut);

            direct[i] = w * path[start + i];
            path[start + i] -= direct[i];
        }

        if (request.directColourDb != 0.0f)
        {
            Biquad ls, hs;
            ls.setCoefficients (low);
            hs.setCoefficients (high);
            ls.process (direct.data(), direct.size());
            hs.process (direct.data(), direct.size());
        }

        for (std::size_t i = 0; i < direct.size() && start + delay + i < path.size(); ++i)
            path[start + delay + i] += static_cast<float> (gain) * direct[i];
    }
}

} // namespace drenched

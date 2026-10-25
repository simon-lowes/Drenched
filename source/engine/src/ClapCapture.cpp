#include "drenched/ClapCapture.h"
#include "Denormals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace drenched
{

namespace
{

constexpr double windowSeconds = 0.005;    // envelope resolution
constexpr double separationSeconds = 0.25; // claps closer than this are one event
constexpr double leadSeconds = 0.005;      // kept before the clap
constexpr double minimumAboveNoiseDb = 35.0;
constexpr float clipLevel = 0.99f;

struct Clap
{
    std::size_t peak = 0, onset = 0;
    float level = 0.0f;
    bool clipped = false;
};

std::size_t samplesOf (double seconds, double sampleRate) noexcept
{
    return static_cast<std::size_t> (std::lround (seconds * sampleRate));
}

} // namespace

std::vector<std::vector<float>> extractClap (const std::vector<std::vector<float>>& recordings, double sampleRate)
{
    const ScopedFlushDenormals flush;

    if (recordings.empty() || recordings.size() > 2 || ! (sampleRate >= 8000.0))
        throw std::invalid_argument ("A clap needs one or two recordings and a sample rate.");

    const auto length = std::min_element (recordings.begin(), recordings.end(),
                                          [] (const auto& a, const auto& b) { return a.size() < b.size(); })->size();
    const auto window = std::max<std::size_t> (1, samplesOf (windowSeconds, sampleRate));
    const auto windows = length / window;

    if (windows < 20)
        throw ClapProblem ("The recording is too short to find a clap in.");

    // The loudest microphone at every moment, and its energy per window.
    std::vector<float> loudest (length, 0.0f);

    for (const auto& r : recordings)
        for (std::size_t n = 0; n < length; ++n)
            loudest[n] = std::max (loudest[n], std::abs (r[n]));

    std::vector<double> energy (windows, 0.0);

    for (std::size_t w = 0; w < windows; ++w)
        for (std::size_t n = w * window; n < (w + 1) * window; ++n)
            energy[w] += static_cast<double> (loudest[n]) * loudest[n];

    // The background noise: the quietest tenth of the recording.
    auto sorted = energy;
    std::nth_element (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (windows / 10), sorted.end());
    const auto noise = std::max (sorted[windows / 10] / static_cast<double> (window), 1.0e-20); // mean square
    const auto threshold = static_cast<float> (std::sqrt (noise) * std::pow (10.0, minimumAboveNoiseDb / 20.0));

    // Every event that rises above the threshold, at least `separation` apart.
    const auto separation = samplesOf (separationSeconds, sampleRate);
    std::vector<Clap> claps;

    for (std::size_t n = 0; n < length;)
    {
        if (loudest[n] < threshold)
        {
            ++n;
            continue;
        }

        Clap clap;
        clap.onset = n;
        const auto end = std::min (length, n + samplesOf (0.01, sampleRate));

        for (auto m = n; m < end; ++m)
        {
            if (loudest[m] > clap.level)
            {
                clap.level = loudest[m];
                clap.peak = m;
            }

            clap.clipped = clap.clipped || loudest[m] >= clipLevel;
        }

        // The event lasts until it has stayed below the threshold for a while.
        auto quietFrom = end;

        for (auto m = end; m < length && m < quietFrom + separation; ++m)
        {
            if (loudest[m] >= threshold)
                quietFrom = m + 1;

            // A louder sound later in the same event (a double clap) counts as clipping it.
            if (m > clap.peak + samplesOf (0.03, sampleRate) && loudest[m] > 0.5f * clap.level)
                clap.clipped = true;
        }

        claps.push_back (clap);
        n = quietFrom + separation;
    }

    if (claps.empty())
        throw ClapProblem ("No clap stood out from the background noise. Clap louder or closer, or turn the mic gain up.");

    const Clap* best = nullptr;

    for (const auto& clap : claps)
        if (! clap.clipped && (best == nullptr || clap.level > best->level))
            best = &clap;

    if (best == nullptr)
        throw ClapProblem ("The clap clipped the microphone. Turn the mic gain down, or clap further away, and try again.");

    // From just before the clap to just before the next one.
    const auto lead = samplesOf (leadSeconds, sampleRate);
    const auto start = best->onset > lead ? best->onset - lead : 0;
    auto stop = length;

    for (const auto& clap : claps)
        if (clap.onset > best->onset)
        {
            stop = clap.onset > lead ? clap.onset - lead : clap.onset;
            break;
        }

    std::vector<std::vector<float>> channels;

    for (const auto& r : recordings)
        channels.emplace_back (r.begin() + static_cast<std::ptrdiff_t> (start), r.begin() + static_cast<std::ptrdiff_t> (stop));

    return channels;
}

} // namespace drenched

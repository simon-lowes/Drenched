#include "drenched/ResponseEnvelope.h"

#include "Bands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace drenched
{

namespace
{
    constexpr double reverseFloorDecibels = -60.0;
    constexpr double reverseBlockSeconds = 0.01;
    constexpr double crossFeedBlockSeconds = 0.01;

    std::size_t samplesFor (double seconds, double sampleRate) noexcept
    {
        return static_cast<std::size_t> (std::max (0.0, std::round (seconds * sampleRate)));
    }

    /** Cuts every path to length and fades the last fade samples out. */
    void cut (ImpulseResponseSet& responses, std::size_t length, std::size_t fade)
    {
        length = std::max<std::size_t> (1, std::min (length, responses.length()));
        fade = std::min (fade, length);

        for (auto& path : responses.paths)
        {
            path.resize (length);

            for (std::size_t i = 0; i < fade; ++i)
            {
                const auto x = static_cast<double> (i + 1) / static_cast<double> (fade + 1);
                path[length - 1 - i] *= static_cast<float> (bands::raisedCosine (x));
            }
        }
    }

    /** Where the response, summed over its paths, has decayed by 60 dB from
        its loudest moment, measured in short blocks. */
    std::size_t audibleLength (const ImpulseResponseSet& responses)
    {
        const auto length = responses.length();
        const auto block = std::max<std::size_t> (1, samplesFor (reverseBlockSeconds, responses.sampleRate));

        std::vector<double> energy ((length + block - 1) / block, 0.0);

        for (const auto& path : responses.paths)
            for (std::size_t n = 0; n < length; ++n)
                energy[n / block] += static_cast<double> (path[n]) * static_cast<double> (path[n]);

        const auto peak = energy.empty() ? 0.0 : *std::max_element (energy.begin(), energy.end());

        if (peak <= 0.0)
            return length;

        const auto floor = peak * std::pow (10.0, reverseFloorDecibels / 10.0);

        for (auto i = energy.size(); i > 0; --i)
            if (energy[i - 1] > floor)
                return std::min (length, i * block);

        return length;
    }
}

bool cutsTail (const ImpulseResponseRequest& request) noexcept
{
    return request.reverse || request.gateMs > 0.0f;
}

void applyEnvelope (ImpulseResponseSet& responses, const ImpulseResponseRequest& request)
{
    const auto rate = responses.sampleRate;

    if (rate <= 0.0 || responses.length() == 0)
        return;

    if (request.gateMs > 0.0f)
    {
        const auto open = samplesFor (std::max (minGateSeconds, request.gateMs / 1000.0), rate);
        const auto release = samplesFor (gateReleaseSeconds, rate);

        if (open + release < responses.length())
            cut (responses, open + release, release);
    }

    if (request.reverse)
    {
        if (request.gateMs <= 0.0f)
            cut (responses, audibleLength (responses), samplesFor (reverseBlockSeconds, rate));

        for (auto& path : responses.paths)
            std::reverse (path.begin(), path.end());
    }

    if (request.attackMs > 0.0f)
    {
        const auto attack = std::min (samplesFor (request.attackMs / 1000.0, rate), responses.length());

        for (auto& path : responses.paths)
            for (std::size_t n = 0; n < attack; ++n)
                path[n] *= static_cast<float> (bands::raisedCosine (static_cast<double> (n) / static_cast<double> (attack)));
    }
}

void applyCrossFeed (ImpulseResponseSet& responses, float crossFeedPercent)
{
    const auto amount = std::clamp (static_cast<double> (crossFeedPercent), 0.0, 100.0) / 100.0;
    const auto length = responses.length();

    if (amount >= 1.0 || length == 0 || responses.sampleRate <= 0.0)
        return;

    using P = ImpulseResponseSet;
    auto& paths = responses.paths;

    // The energy a centred source sends to both outputs, block by block.
    // Correlated paths (the direct sound and early reflections, or one source
    // recorded in stereo) add up in phase and decorrelated ones (a diffuse
    // tail) do not, so the make-up gain follows the response over time.
    const auto block = std::max<std::size_t> (1, samplesFor (crossFeedBlockSeconds, responses.sampleRate));
    const auto blocks = (length + block - 1) / block;
    std::vector<double> gain (blocks, 1.0);

    for (std::size_t b = 0; b < blocks; ++b)
    {
        auto before = 0.0, after = 0.0;

        for (const auto& [same, opposite] : { std::pair { P::leftToLeft, P::rightToLeft }, std::pair { P::rightToRight, P::leftToRight } })
        {
            for (auto n = b * block; n < std::min (length, (b + 1) * block); ++n)
            {
                const auto kept = static_cast<double> (paths[same][n]), fed = static_cast<double> (paths[opposite][n]);
                before += (kept + fed) * (kept + fed);
                after += (kept + amount * fed) * (kept + amount * fed);
            }
        }

        if (after > 0.0)
            gain[b] = std::clamp (std::sqrt (before / after), 0.5, 2.0);
    }

    // Glide between block centres so the gain never steps.
    const auto gainAt = [&] (std::size_t n)
    {
        const auto position = (static_cast<double> (n) + 0.5) / static_cast<double> (block) - 0.5;
        const auto first = static_cast<std::size_t> (std::clamp (std::floor (position), 0.0, static_cast<double> (blocks - 1)));
        const auto second = std::min (first + 1, blocks - 1);
        const auto t = std::clamp (position - static_cast<double> (first), 0.0, 1.0);
        return gain[first] + (gain[second] - gain[first]) * t;
    };

    for (std::size_t n = 0; n < length; ++n)
    {
        const auto g = gainAt (n);
        paths[P::leftToLeft][n] = static_cast<float> (paths[P::leftToLeft][n] * g);
        paths[P::rightToRight][n] = static_cast<float> (paths[P::rightToRight][n] * g);
        paths[P::leftToRight][n] = static_cast<float> (paths[P::leftToRight][n] * amount * g);
        paths[P::rightToLeft][n] = static_cast<float> (paths[P::rightToLeft][n] * amount * g);
    }
}

} // namespace drenched

#include "drenched/ResponseEnvelope.h"

#include "Bands.h"

#include <algorithm>
#include <cmath>

namespace drenched
{

namespace
{
    constexpr double reverseFloorDecibels = -60.0;
    constexpr double reverseBlockSeconds = 0.01;

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

} // namespace drenched

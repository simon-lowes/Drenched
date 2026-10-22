#include "drenched/SpeakerTrim.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace drenched
{

namespace
{

constexpr double glideSeconds = 0.02;

std::size_t nextPowerOfTwo (std::size_t n) noexcept
{
    std::size_t p = 1;

    while (p < n)
        p <<= 1;

    return p;
}

float gainOf (float levelDb) noexcept
{
    return levelDb <= SpeakerTrim::minLevelDb ? 0.0f : std::pow (10.0f, std::min (levelDb, SpeakerTrim::maxLevelDb) / 20.0f);
}

} // namespace

void SpeakerTrim::prepare (double newSampleRate, std::size_t numChannels)
{
    sampleRate = newSampleRate;
    channels = std::min (numChannels, maxChannels);

    const auto longest = static_cast<std::size_t> (std::ceil (maxDelayMs / 1000.0 * sampleRate));
    const auto size = nextPowerOfTwo (longest + 1);
    mask = size - 1;

    for (std::size_t c = 0; c < channels; ++c)
        lines[c].buffer.assign (size, 0.0f);

    for (std::size_t c = channels; c < maxChannels; ++c)
        lines[c].buffer = {};

    fadeStep = static_cast<float> (1.0 / std::max (1.0, glideSeconds * sampleRate));
    gainCoef = static_cast<float> (std::exp (-1.0 / (glideSeconds / 5.0 * sampleRate)));
    reset();
}

void SpeakerTrim::reset() noexcept
{
    write = 0;

    for (auto& line : lines)
    {
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.delay = line.previousDelay = 0;
        line.fade = 1.0f;
        line.gain = 1.0f;
    }
}

bool SpeakerTrim::isNeutral() const noexcept
{
    return std::all_of (lines.begin(), lines.begin() + static_cast<std::ptrdiff_t> (channels),
                        [] (const Line& line) { return line.delay == 0 && line.fade >= 1.0f && line.gain == 1.0f; });
}

void SpeakerTrim::process (float* const* audio, std::size_t numChannels, std::size_t n,
                           const float* levelsDb, const float* delaysMs) noexcept
{
    const auto count = std::min (numChannels, channels);
    const auto longest = static_cast<double> (mask);
    constexpr auto quarter = static_cast<float> (std::numbers::pi / 2.0);

    for (std::size_t c = 0; c < count; ++c)
    {
        auto& line = lines[c];
        auto* x = audio[c];
        auto w = write;

        const auto wanted = static_cast<std::size_t> (std::lround (std::clamp (static_cast<double> (delaysMs[c]), 0.0, static_cast<double> (maxDelayMs))
                                                                   / 1000.0 * sampleRate));
        const auto target = std::min (wanted, static_cast<std::size_t> (longest));

        // A new delay starts a crossfade once the last one has finished.
        if (target != line.delay && line.fade >= 1.0f)
        {
            line.previousDelay = line.delay;
            line.delay = target;
            line.fade = 0.0f;
        }

        const auto targetGain = gainOf (levelsDb[c]);

        for (std::size_t i = 0; i < n; ++i)
        {
            line.buffer[w] = x[i];

            auto y = line.buffer[(w - line.delay) & mask];

            if (line.fade < 1.0f)
            {
                line.fade = std::min (1.0f, line.fade + fadeStep);
                const auto old = line.buffer[(w - line.previousDelay) & mask];
                y = y * std::sin (line.fade * quarter) + old * std::cos (line.fade * quarter);
            }

            line.gain = targetGain + gainCoef * (line.gain - targetGain);

            if (std::abs (line.gain - targetGain) < 1.0e-6f)
                line.gain = targetGain;

            x[i] = y * line.gain;
            w = (w + 1) & mask;
        }
    }

    write = (write + n) & mask;
}

} // namespace drenched

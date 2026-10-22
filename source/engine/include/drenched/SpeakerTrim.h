#pragma once

#include "drenched/SpeakerLayout.h"

#include <array>
#include <cstddef>
#include <vector>

namespace drenched
{

/** Each speaker's own level and delay for the reverberation: to set the
    rears later and quieter for depth, or to line the reverb up with
    speakers at different distances. A changed delay crossfades from the old
    tap to the new one, and a changed level glides, both over 20 ms, so
    moving them while sound plays never clicks.

    process() and reset() are real-time safe. prepare() allocates. */
class SpeakerTrim
{
public:
    static constexpr float maxDelayMs = 100.0f;
    static constexpr float minLevelDb = -60.0f; // and below: silent
    static constexpr float maxLevelDb = 12.0f;

    void prepare (double newSampleRate, std::size_t numChannels);
    void reset() noexcept;

    /** In place on `n` samples of each channel. levelsDb and delaysMs hold
        one value per channel. */
    void process (float* const* channels, std::size_t numChannels, std::size_t n,
                  const float* levelsDb, const float* delaysMs) noexcept;

    /** True while every speaker plays at 0 dB with no delay and nothing is gliding. */
    bool isNeutral() const noexcept;

private:
    struct Line
    {
        std::vector<float> buffer;
        std::size_t delay = 0, previousDelay = 0;
        float fade = 1.0f; // 1 once settled on `delay`
        float gain = 1.0f;
    };

    double sampleRate = 48000.0;
    std::array<Line, maxChannels> lines;
    std::size_t channels = 0, mask = 0, write = 0;
    float fadeStep = 1.0f, gainCoef = 0.0f;
};

} // namespace drenched

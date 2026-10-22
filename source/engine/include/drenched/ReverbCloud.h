#pragma once

#include "drenched/SpeakerLayout.h"

#include <array>
#include <cstddef>

namespace drenched
{

/** Cloud size: lets the reverberation follow where the sound is panned.

    It listens to the input channels and finds the direction the sound comes
    from (each input speaker's direction, weighted by how loud it is over the
    last tenth of a second or so). The reverb in each output speaker is then
    turned up near that direction and down away from it, with the total level
    kept the same. At 100 % the reverb fills the room as captured; lower
    gathers it around the sound, and at 0 it comes mostly from the speakers
    nearest the sound.

    A sound spread evenly over the inputs has no one direction, so the reverb
    stays spread out whatever the size. When the input falls silent, or the
    room is frozen, the reverb holds its last place instead of drifting back.

    listen(), process() and reset() are real-time safe. prepare() does not
    allocate either, but call it while audio is stopped. */
class ReverbCloud
{
public:
    void prepare (double newSampleRate, const SpeakerLayout& inputs, const SpeakerLayout& outputs) noexcept;
    void reset() noexcept;

    /** Listens to n samples of each input channel. With hold set the
        direction stays where it is (while the room is frozen). */
    void listen (const float* const* inputs, std::size_t numInputs, std::size_t n, bool hold) noexcept;

    /** Scales each output's reverb, in place, for a cloud size of 0 .. 100 %.
        Gains glide from block to block, so nothing clicks. */
    void process (float* const* wet, std::size_t numOutputs, std::size_t n, float cloudPercent) noexcept;

    /** True while every output plays at unity and the size is settled at 100 %:
        process() then leaves the signal exactly as it was. */
    bool isNeutral() const noexcept;

    /** Where the sound comes from, as x (right), y (front) and z (up): a unit
        vector for a sound from one speaker, shorter the more it is spread. */
    std::array<float, 3> direction() const noexcept { return heading; }

    /** The gain each output's reverb plays at now. */
    float gain (std::size_t output) const noexcept { return output < maxChannels ? gains[output] : 0.0f; }

private:
    using Vector = std::array<float, 3>;

    double sampleRate = 48000.0;
    std::array<Vector, maxChannels> inputDirections {}, outputDirections {};
    std::array<bool, maxChannels> inputHeard {}, outputWet {};
    std::size_t numIn = 0, numOut = 0, numWet = 0;

    std::array<float, maxChannels> energy {};
    std::array<float, maxChannels> gains {};
    Vector heading {};
    float size = 100.0f;
};

} // namespace drenched

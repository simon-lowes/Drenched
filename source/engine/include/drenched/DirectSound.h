#pragma once

#include "drenched/ImpulseResponseSynth.h"

namespace drenched
{

/** The direct-sound controls' limits. At minDirectLevelDb the direct sound
    is removed altogether, leaving only the room. */
inline constexpr float minDirectLevelDb = -60.0f;
inline constexpr float maxDirectDelayMs = 50.0f;

/** Where the direct sound starts in one path: the first sample within 20 dB
    of the path's loudest moment in its first 50 ms, moved on to the peak of
    that arrival. Returns the path length for a silent path. */
std::size_t findDirectSound (const std::vector<float>& path, double sampleRate) noexcept;

/** Gives the direct sound its own level, delay and colour.

    In each path a short window around the direct arrival (1 ms before its
    peak to 1.5 ms after, with soft edges) is lifted out, turned up or down,
    tilted darker or brighter around 800 Hz with the same shelves as
    Brightness, and put back later by directDelayMs. Everything else, the
    reflections and the tail, stays where it was. With all three at their
    defaults the response is left exactly as it was. Run it on the builder
    thread. */
void applyDirect (ImpulseResponseSet& responses, const ImpulseResponseRequest& request);

} // namespace drenched

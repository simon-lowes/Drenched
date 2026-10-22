#pragma once

#include "drenched/ImpulseResponseSynth.h"

namespace drenched
{

/** The shortest gate and how long it takes to close. A gate shorter than a
    few milliseconds would only leave the direct sound. */
inline constexpr double minGateSeconds = 0.01;
inline constexpr double gateReleaseSeconds = 0.012;

/** True when the request cuts the response short (a gate) or turns it round
    (reverse). Either way the living tail cannot carry on the response's
    decay, so the builder leaves it out of the blend. */
bool cutsTail (const ImpulseResponseRequest& request) noexcept;

/** Shapes every path of the response the same way, in this order:

    - gate: keeps the first gateMs and closes over gateReleaseSeconds, so a
      long room stops dead, as with a gated drum room;
    - reverse: plays the response backwards, so the reverb swells up to the
      direct sound. Without a gate the response is first cut where it has
      decayed by 60 dB, so the swell does not start with seconds of noise;
    - attack: fades in over attackMs, so the reverb blooms rather than
      starting with the direct sound and first reflections.

    With all three off the response is left exactly as it was. Allocates
    nothing but may shorten the paths; run it on the builder thread. */
void applyEnvelope (ImpulseResponseSet& responses, const ImpulseResponseRequest& request);

} // namespace drenched

#pragma once

#include "drenched/ImpulseResponseSynth.h"
#include "drenched/SpeakerLayout.h"

#include <array>
#include <cstddef>
#include <vector>

namespace drenched
{

/** Impulse responses from the two stage sources (left and right input) to
    every speaker of an output layout. With a stereo layout it is exactly the
    true-stereo set. */
struct SpatialResponseSet
{
    double sampleRate = 0.0;
    std::array<std::vector<std::vector<float>>, 2> paths; // [source][speaker], all the same length

    std::size_t numOutputs() const noexcept { return paths[0].size(); }
    std::size_t length() const noexcept { return paths[0].empty() ? 0 : paths[0][0].size(); }
};

/** Spreads a true-stereo response over a speaker layout.

    Speakers at the stereo positions (30 degrees left and right, ear height)
    play the stereo paths as they are. Other front speakers (a centre, wides)
    get the direct sound and early reflections panned between them, so the
    front image holds, and a decorrelated copy of the late reverberation.
    Side, rear and height speakers get the late reverberation, decorrelated
    per speaker, with the early reflections of their side 6 dB down (height
    speakers 9 dB), so the room surrounds the listener without pulling the
    image away from the front. The LFE channel gets no reverberation.

    The total energy matches the stereo response, so switching a session from
    stereo to surround keeps the reverb at the same overall level. The result
    is the same for the same input (the decorrelation is seeded per speaker).
    Allocates; run it off the audio thread. */
SpatialResponseSet spreadResponses (const ImpulseResponseSet& stereo, const SpeakerLayout& outputs,
                                    double mixingTimeSeconds);

/** Plays a surround recording's own channels on an output layout.

    `channels` are the recording's speaker responses (already shaped like the
    stereo response, at its rate), `recorded` says where each was captured.
    An output speaker within 20 degrees of a captured one plays it. A captured
    channel no output speaker is near (heights on a 5.1 system, say) joins the
    nearest one. An output speaker with nothing captured near it (heights the
    recording lacks) gets what spreadResponses() would give it from `stereo`,
    the recording's fold-down. LFE outputs stay silent.

    The recording is one source, so both inputs feed every speaker; cross-feed
    below 100 % turns each input down in the speakers on the other side, its
    own speakers making up the level. The total energy matches `stereo`, as
    spreadResponses() does. Allocates; run it off the audio thread. */
SpatialResponseSet placeSpeakerResponses (const std::vector<std::vector<float>>& channels, const SpeakerLayout& recorded,
                                          const ImpulseResponseSet& stereo, const SpeakerLayout& outputs,
                                          double mixingTimeSeconds, float crossFeedPercent);

/** Where reflections blur into a diffuse tail, for a request: measured for a
    loaded response, estimated from the volume for a bundled space. */
double mixingTimeSeconds (const ImpulseResponseRequest& request) noexcept;

} // namespace drenched

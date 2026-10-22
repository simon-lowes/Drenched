#pragma once

#include <stdexcept>
#include <vector>

namespace drenched
{

/** Why a recording gave no usable clap, in words for the person capturing. */
struct ClapProblem : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Finds a clap (or a balloon pop, a starter pistol, any short sharp sound)
    in recordings of a room, one per microphone, and cuts out the room's
    response to it: from just before the clap to just before the next one,
    or to the end. With several claps it takes the loudest one that did not
    clip. The result goes to prepareRecordedResponse(), which trims the
    silence and the noise floor.

    A clap is not a perfect impulse: it has little bass, so the low bands of
    the result are quieter and noisier than a sweep's. Throws ClapProblem
    when no clap stands far enough above the background noise, or every clap
    clipped. Allocates; never on the audio thread. */
std::vector<std::vector<float>> extractClap (const std::vector<std::vector<float>>& recordings, double sampleRate);

} // namespace drenched

#pragma once

#include "drenched/Biquad.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace drenched
{

/** Why a recording could not be matched, in words for the person matching. */
struct RoomMatchProblem : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** What a recording says about the room it was made in. */
struct RoomEstimate
{
    std::array<float, 8> decaySeconds {}; // RT60 per octave band, 63 Hz .. 8 kHz
    std::array<int, 8> decaysHeard {};    // free decays each band was measured from; 0 = taken from its neighbours
};

/** Match a room: hears how long a room rings from an ordinary recording made
    in it (a voice, a band, a drum kit), with no test signal.

    Wherever the sound stops (a pause between phrases, a note cut short, a
    drum hit) the room carries on alone, and its level falls away at the
    room's own rate. The listener splits the recording into octave bands,
    follows each band's level, and finds those free decays: stretches where
    the level falls steadily by at least 10 dB, starting 5 dB down so the
    direct sound has gone. A straight line fitted to each gives a decay time.
    A sound can fade more slowly than the room but never faster, so each
    band's estimate comes from the quicker of its decays.

    It needs pauses: a recording that never stops, or is mostly silence,
    throws RoomMatchProblem. Allocates; never on the audio thread. */
class RoomListener
{
public:
    explicit RoomListener (double sampleRate);

    /** Listens to n more samples of the recording, mixed to one channel. */
    void listen (const float* samples, std::size_t n);

    /** Seconds of recording heard so far. */
    double secondsHeard() const noexcept;

    /** The room's decay by frequency. Throws RoomMatchProblem when the
        recording has too few pauses to hear the room in. */
    RoomEstimate estimate() const;

    /** How far the level is followed: one value per band every hop. */
    static constexpr double hopSeconds = 0.01;

private:
    double sampleRate;
    std::size_t hop;
    std::array<std::array<Biquad, 4>, 8> filters;
    std::array<double, 8> sum {};
    std::size_t filled = 0, total = 0;
    std::array<std::vector<float>, 8> frames; // mean square per hop
};

/** Listens to a whole recording, one vector per channel. */
RoomEstimate matchRoom (const std::vector<std::vector<float>>& channels, double sampleRate);

/** How different two decay curves sound: the root mean square of their ratio
    in decibels-of-time (20 log10), weighted to the middle bands where the ear
    judges a room's length. 0 for the same curve. */
float decayDistance (const std::array<float, 8>& a, const std::array<float, 8>& b) noexcept;

} // namespace drenched

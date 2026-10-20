#pragma once

#include "drenched/ImpulseResponseSynth.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace drenched
{

/** What an impulse response tells us about the room it was recorded in. */
struct RoomAnalysis
{
    std::array<float, 8> decaySeconds {}; // measured RT60 per octave band, 63 Hz .. 8 kHz
    float mixingTimeSeconds = 0.05f;      // where reflections blur into a diffuse tail
    float width = 10.0f, depth = 15.0f, height = 5.0f; // a shoebox of the estimated volume, metres

    float volume() const noexcept { return width * depth * height; }
};

/** An impulse response from a file, cleaned up and analysed, at its own
    sample rate. Made once when the file loads, then shared read-only between
    threads. */
struct RecordedResponse
{
    std::string name;
    int channels = 0;           // 1, 2 or 4, as the file had them
    ImpulseResponseSet responses; // four true-stereo paths, native rate, trimmed and normalised
    RoomAnalysis room;
};

/** Turns decoded audio into a RecordedResponse.

    Channels map onto the four true-stereo paths the usual way:
      1: the same response on each side, no cross-feed;
      2: one source captured in stereo, so both inputs feed both outputs;
      4: true stereo, in the order left-to-left, left-to-right,
         right-to-left, right-to-right.

    Silence before the direct sound is trimmed (keeping 1 ms), as is the
    noise floor the recording ends in, and the level is normalised like the
    bundled spaces. Then the decay per band, mixing time and room size are
    measured.

    Throws std::invalid_argument, with a message for the user, for audio
    that cannot be an impulse response. Allocates; never on the audio thread. */
std::shared_ptr<const RecordedResponse> prepareRecordedResponse (const std::vector<std::vector<float>>& channels,
                                                                 double sampleRate,
                                                                 std::string name);

/** Converts a recorded response's paths to another sample rate. */
ImpulseResponseSet resampleResponses (const ImpulseResponseSet& responses, double newRate);

/** Shapes a recorded response (already at the request's rate) by the
    request: each band's decay is stretched or shortened from the measured
    RT60 to the requested one, and early reflections are balanced against
    the tail. With the measured decay and the default balance the response
    comes back unchanged. */
ImpulseResponseSet shapeRecordedResponse (ImpulseResponseSet atRate, const RecordedResponse& recorded,
                                          const ImpulseResponseRequest& request);

/** The response a request asks for: the recorded one if it carries one,
    otherwise a synthesised bundled space. */
ImpulseResponseSet renderImpulseResponse (const ImpulseResponseRequest& request);

/** Measures RT60 per octave band from the Schroeder backward integral,
    fitted between -5 and -25 dB (T20, ISO 3382), after subtracting the noise
    floor. `paths` are summed by energy. */
std::array<float, 8> measureDecay (const std::array<std::vector<float>, 4>& paths, double sampleRate);

} // namespace drenched

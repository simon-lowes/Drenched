#pragma once

#include "drenched/SpeakerLayout.h"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace drenched
{

struct RecordedResponse;

/** Everything that shapes a synthesised impulse response. Two equal requests
    always produce identical responses (the noise is seeded per path, not per
    request), so a small edit changes the sound smoothly instead of
    re-rolling the tail. */
struct ImpulseResponseRequest
{
    double sampleRate = 48000.0;
    int space = 0;                         // index into bundledSpaces
    std::array<float, 8> decaySeconds {};  // RT60 per octave band, 63 Hz .. 8 kHz
    float sizePercent = 100.0f;            // 50 .. 150, scales the room
    float distancePercent = 40.0f;         // 0 .. 100, listener from near to far
    float listenerX = 0.5f;                // 0 .. 1, across the room
    float earlyLatePercent = 55.0f;        // 0 = all early, 100 = all late
    float livingPercent = 0.0f;            // 0 = the recorded tail, 100 = the living tail

    // How the response is played (see applyEnvelope()). These shape any
    // response, bundled or loaded.
    bool reverse = false;                  // play the response backwards
    float gateMs = 0.0f;                   // cut the response after this long, 0 = off
    float attackMs = 0.0f;                 // fade the response in over this long

    // The direct sound, the first arrival from each source (see applyDirect()).
    float directLevelDb = 0.0f;            // -60 = removed
    float directDelayMs = 0.0f;            // later than the room, 0 .. 50
    float directColourDb = 0.0f;           // tilt, darker below 0, brighter above

    // How much each input feeds the opposite output (see applyCrossFeed()).
    float crossFeedPercent = 100.0f;       // 0 = two separate reverbs, 100 = as captured

    // A loaded impulse response to play instead of the bundled space. Size,
    // distance and listener position only shape synthesised spaces.
    std::shared_ptr<const RecordedResponse> recorded {};

    // The speakers the kernel feeds. The response itself is always true
    // stereo; the builder spreads it over these (see spreadResponses()).
    SpeakerLayout outputs = layouts::stereo();

    bool operator== (const ImpulseResponseRequest&) const = default;
};

/** A true-stereo impulse response: four paths, each from one input channel to
    one output channel. All paths have the same length. */
struct ImpulseResponseSet
{
    enum Path { leftToLeft, leftToRight, rightToLeft, rightToRight };

    double sampleRate = 0.0;
    std::array<std::vector<float>, 4> paths;

    std::size_t length() const noexcept { return paths[0].size(); }
};

/** Builds a true-stereo impulse response for a bundled space.

    The early part is an image-source model of the room as a shoebox: two
    sources on the stage, two receivers at the listener position, with
    reflections up to the mixing time, each wall bounce attenuated per octave
    band by the absorption the band's RT60 implies (Sabine). The late part is
    decorrelated noise per path, split into octave bands with zero-phase,
    perfectly reconstructing crossovers, and each band decays to -60 dB at
    exactly its RT60. The late level follows diffuse-field theory (the
    critical distance), so moving the listener away makes the sound more
    reverberant, as in a real room.

    This stands in for captured responses until the space library is
    recorded. It allocates and takes tens to hundreds of milliseconds, so run
    it away from the audio thread. */
ImpulseResponseSet synthesiseImpulseResponse (const ImpulseResponseRequest& request);

/** The listener's distance from the front wall in metres, as the interface
    shows it: from near the source to most of the way down the room. */
float listenerDistanceMetres (int space, float sizePercent, float distancePercent) noexcept;

} // namespace drenched

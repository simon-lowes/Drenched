#pragma once

#include "drenched/Ambisonics.h"
#include "drenched/RecordedResponse.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace drenched
{

/** The impulse responses in a SOFA (AES69) file, and where each was heard
    from: what Drenched needs from room responses stored as SOFA
    (SingleRoomSRIR, SingleRoomMIMOSRIR, MultiSpeakerBRIR, AmbisonicsDRIR
    and the like). */
struct SofaSet
{
    /** One sound source: a measurement, or one emitter of a measurement
        made with several. */
    struct Source
    {
        int measurement = 0;
        int emitter = 0;
        int listener = 0;      // which listening position, numbered from 0
        float azimuth = 0.0f;  // seen from the listener, degrees, negative to the left
        float elevation = 0.0f;
    };

    int measurements = 0;
    int receivers = 0;
    int emitters = 1;
    int samples = 0;
    double sampleRate = 48000.0;

    std::vector<float> ir;           // [measurement][receiver][emitter][sample]
    std::vector<int> delays;         // samples, [measurement][receiver][emitter]; empty when none
    std::vector<Source> sources;
    std::vector<float> receiverSide; // per receiver: metres to the listener's left

    bool ambisonic = false; // receivers are ambisonic channels, not microphones
    AmbisonicFormat ambisonicFormat = AmbisonicFormat::ambiX;

    /** One receiver's response to one source, with its delay. */
    std::vector<float> response (const Source& source, int receiver) const;
};

/** Reads a SOFA file from memory. Throws std::invalid_argument, with a
    message for the user, for anything that isn't a SOFA file of impulse
    responses. Allocates; never on the audio thread. */
SofaSet readSofa (const void* data, std::size_t size);

/** Which of a SOFA file's responses Drenched plays. */
struct SofaChoice
{
    /** 1 (mono), 2 (stereo) or 4 (true stereo) channels, as
        prepareRecordedResponse() takes them; or the four first-order
        channels of an ambisonic file, in the file's own format. */
    std::vector<std::vector<float>> channels;

    // Indices into SofaSet::sources: the one played, or the stereo pair.
    int front = -1;
    int left = -1;
    int right = -1;
};

/** Picks what to play: a pair of sources about 30 degrees either side of the
    listener when the file has them, heard as true stereo by the receivers
    furthest to the left and right; otherwise the source closest to straight
    ahead. Throws std::invalid_argument when nothing can be played. */
SofaChoice chooseSofaChannels (const SofaSet& set);

/** readSofa(), chooseSofaChannels(), then prepareRecordedResponse() or
    prepareAmbisonicResponse(). Throws std::invalid_argument like them. */
std::shared_ptr<const RecordedResponse> prepareSofaResponse (const void* data, std::size_t size, std::string name);

} // namespace drenched

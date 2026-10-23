#pragma once

#include "drenched/Ambisonics.h"
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
    int channels = 0;           // as the file had them
    ImpulseResponseSet responses; // four true-stereo paths, native rate, trimmed and normalised
    RoomAnalysis room;

    // A surround recording keeps its own channels too, one per speaker, trimmed
    // and scaled like `responses` (which holds their fold-down to stereo).
    SpeakerLayout layout;
    std::vector<std::vector<float>> speakers;

    // A B-format recording keeps its four channels in `speakers` as AmbiX
    // (W Y Z X), with no layout: they are decoded to whatever speakers it
    // plays on (see decodeAmbisonic()), and `responses` holds its stereo decode.
    bool ambisonic = false;

    bool isSurround() const noexcept { return ! speakers.empty(); }
};

/** Turns decoded audio into a RecordedResponse.

    Channels map onto the four true-stereo paths the usual way:
      1: the same response on each side, no cross-feed;
      2: one source captured in stereo, so both inputs feed both outputs;
      4: true stereo, in the order left-to-left, left-to-right,
         right-to-left, right-to-right.
    Any other count (3, or 5 to 16) is one source captured in surround, with
    `layout` saying which speaker each channel is for. Its channels are kept
    for surround playback (see placeSpeakerResponses()), and folded down to
    stereo as a stereo capture for everything else: front channels panned
    between left and right, the rest 3 dB down on their side, the LFE left
    out.

    Silence before the direct sound is trimmed (keeping 1 ms), as is the
    noise floor the recording ends in, and the level is normalised like the
    bundled spaces. Then the decay per band, mixing time and room size are
    measured.

    Throws std::invalid_argument, with a message for the user, for audio
    that cannot be an impulse response. Allocates; never on the audio thread. */
std::shared_ptr<const RecordedResponse> prepareRecordedResponse (const std::vector<std::vector<float>>& channels,
                                                                 double sampleRate,
                                                                 std::string name,
                                                                 const SpeakerLayout& layout = {});

/** The same for a B-format (first-order ambisonic) recording, in any of the
    usual channel orders (see AmbisonicFormat); higher-order AmbiX files are
    heard at first order. `responses` is a stereo pair of virtual microphones
    (see ambisonicStereoAzimuth); surround layouts decode the four channels
    to their own speakers. Throws std::invalid_argument like
    prepareRecordedResponse(). */
std::shared_ptr<const RecordedResponse> prepareAmbisonicResponse (const std::vector<std::vector<float>>& channels,
                                                                  double sampleRate,
                                                                  std::string name,
                                                                  AmbisonicFormat format);

/** How a surround channel at this speaker folds down to stereo: its gains
    into the left and right. */
std::array<float, 2> foldDownGains (const Speaker& speaker) noexcept;

/** Converts a recorded response's paths to another sample rate. */
ImpulseResponseSet resampleResponses (const ImpulseResponseSet& responses, double newRate);

/** Shapes a recorded response (already at the request's rate) by the
    request: Size plays it back slower or faster, so reflections arrive
    later or sooner as in a bigger or smaller room, with each band's level
    put back; each band's decay is then stretched or shortened to the
    requested RT60, and early reflections are balanced against the tail. With the measured decay and the default balance the response
    comes back unchanged. `sides` says where each path plays (see decayFor()),
    so the left and right outputs can decay differently. */
ImpulseResponseSet shapeRecordedResponse (ImpulseResponseSet atRate, const RecordedResponse& recorded,
                                          const ImpulseResponseRequest& request,
                                          const std::array<double, 4>& sides = stereoPathSides);

/** The response a request asks for: the recorded one if it carries one,
    otherwise a synthesised bundled space. */
ImpulseResponseSet renderImpulseResponse (const ImpulseResponseRequest& request);

/** Measures RT60 per octave band from the Schroeder backward integral,
    fitted between -5 and -25 dB (T20, ISO 3382), after subtracting the noise
    floor. `paths` are summed by energy. */
std::array<float, 8> measureDecay (const std::array<std::vector<float>, 4>& paths, double sampleRate);

} // namespace drenched

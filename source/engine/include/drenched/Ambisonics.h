#pragma once

#include "drenched/SpeakerLayout.h"

#include <vector>

namespace drenched
{

/** How a B-format (first-order ambisonic) recording orders and scales its
    four channels.
      ambiX:    W Y Z X (ACN order), SN3D: the usual for files made since 2010;
      ambiXN3D: the same order with N3D scaling (Y, Z, X 4.8 dB louder);
      fuMa:     W X Y Z (Furse-Malham), W 3 dB down: older files, .amb files
                and the B-format downloads of most impulse response libraries. */
enum class AmbisonicFormat { ambiX, ambiXN3D, fuMa };

/** The first four channels of a B-format recording as AmbiX (W Y Z X, SN3D).
    Higher-order AmbiX files (9 or 16 channels) start with the same four, so
    they work too, heard at first order. Throws std::invalid_argument with
    fewer than four channels. */
std::vector<std::vector<float>> toAmbiX (const std::vector<std::vector<float>>& channels, AmbisonicFormat format);

/** What a first-order microphone at the recording position would have picked
    up, aimed at `direction` (a speaker's azimuth and elevation, negative
    azimuth to the left). `pattern` runs from 0 (omni) through 0.5 (cardioid)
    to 1 (figure of eight). */
std::vector<float> virtualMicrophone (const std::vector<std::vector<float>>& ambiX, const Speaker& direction, float pattern);

/** The pattern of the virtual microphones a B-format response is played
    through: a hypercardioid, so neighbouring speakers stay as distinct as a
    first-order recording allows. */
inline constexpr float ambisonicPattern = 0.75f;

/** Where the stereo pair of virtual microphones points: 60 degrees left and
    right. Hypercardioids this far apart pick up a diffuse room almost
    uncorrelated, so the reverb is as wide as from a spaced pair, while a
    sound from straight ahead still lands in the middle. */
inline constexpr float ambisonicStereoAzimuth = 60.0f;

/** A B-format response (AmbiX) played on a speaker layout: a virtual
    microphone aimed at each speaker, nothing in the LFE. */
std::vector<std::vector<float>> decodeAmbisonic (const std::vector<std::vector<float>>& ambiX, const SpeakerLayout& outputs);

} // namespace drenched

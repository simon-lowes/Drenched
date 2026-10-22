#pragma once

#include "drenched/ImpulseResponseSynth.h"

#include <array>
#include <cstddef>
#include <vector>

namespace drenched
{

/** Third-octave band centres from 20 Hz to 20 kHz, shared by the waterfall
    and the live spectrum. */
inline constexpr std::array<float, 31> analysisBands { 20.0f,   25.0f,   31.5f,   40.0f,   50.0f,   63.0f,   80.0f,   100.0f,
                                                       125.0f,  160.0f,  200.0f,  250.0f,  315.0f,  400.0f,  500.0f,  630.0f,
                                                       800.0f,  1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f,
                                                       5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f };

/** What the reverb's current response looks like, for the decay displays. */
struct ResponseAnalysis
{
    static constexpr std::size_t numSlices = 40;
    static constexpr float floorDb = -90.0f;

    /** The response's energy over time in each third-octave band: one row per
        slice, `sliceSeconds` apart from the start, in dB below the loudest
        band of any slice (so 0 dB is the peak, floorDb the bottom). */
    std::vector<std::array<float, analysisBands.size()>> waterfall;
    double sliceSeconds = 0.0;

    /** The broadband decay curve (Schroeder backward integral) in dB, one
        point every `decayStepSeconds`, from 0 dB down to the end shown. */
    std::vector<float> decayDb;
    double decayStepSeconds = 0.0;

    /** RT60 measured per octave band, 63 Hz .. 8 kHz (see measureDecay). */
    std::array<float, 8> rt60 {};

    /** How long the displays span: until the decay reaches -60 dB, or the
        response ends. */
    double seconds = 0.0;
};

/** Analyses a response with every path summed by energy. Allocates and runs
    over the whole response: call it away from the audio thread. */
ResponseAnalysis analyseResponse (const ImpulseResponseSet& responses);

} // namespace drenched

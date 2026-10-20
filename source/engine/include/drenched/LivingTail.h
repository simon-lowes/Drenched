#pragma once

#include "drenched/Biquad.h"
#include "drenched/ImpulseResponseSynth.h"

#include <array>
#include <cstddef>
#include <vector>

namespace drenched
{

/** Everything a LivingTail needs to sound like one room: its delay lengths,
    the loop filters that make each band decay at the room's RT60, and the
    output level that matches the room's late reverberation. Made off the
    audio thread by designLivingTail(), then copied in. */
struct LivingTailDesign
{
    static constexpr std::size_t numLines = 16;
    static constexpr std::size_t numShelves = 7;

    bool valid = false;
    double sampleRate = 0.0;
    std::array<std::size_t, numLines> delays {};

    // Loss per trip round the loop, per octave band, in dB (negative).
    std::array<std::array<float, 8>, numLines> bandLossDb {};

    // Normal (decaying) loop filter: a gain and shelves at the band edges.
    std::array<std::array<Biquad::Coefficients, numShelves>, numLines> shelves {};
    std::array<float, numLines> broadband {};

    float outputGain = 1.0f;
    float releaseSeconds = 1.0f; // how long a released freeze takes to fade
};

/** The living tail: a 16-line feedback delay network (Jot's structure, with a
    Hadamard feedback matrix) whose loop filters are fitted to the room's
    decay in every octave band.

    It runs alongside the convolution, following the same input, so at any
    moment it holds a tail that sounds like the room. Freezing it sets the
    loop lossless above the crossover, so the room sustains for ever, while
    the bass below keeps its natural decay and never builds into mud.

    prepare() allocates. Everything else is real-time safe. */
class LivingTail
{
public:
    static constexpr double maxDelaySeconds = 0.2;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Takes the room's delays and filters. Changing delay lengths while the
        tail is audible would click, so the caller does this only while it
        is silent. */
    void setDesign (const LivingTailDesign& design) noexcept;

    /** Lossless above crossoverHz when frozen; the room's decay otherwise. */
    void setFrozen (bool frozen, float crossoverHz) noexcept;

    bool hasDesign() const noexcept { return current.valid; }
    float releaseSeconds() const noexcept { return current.releaseSeconds; }

    /** Overwrites outL and outR. */
    void process (const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept;

private:
    static constexpr std::size_t N = LivingTailDesign::numLines;

    void applyLoopFilters() noexcept;

    double rate = 48000.0;
    LivingTailDesign current;
    bool frozen = false;
    float crossover = 320.0f;

    std::vector<float> buffer; // N lines, each `capacity` long
    std::size_t capacity = 0, mask = 0, write = 0;

    // Loop filters, stored shelf by shelf across the lines so the inner loop
    // runs all sixteen lines at once in SIMD.
    using Lines = std::array<float, N>;
    std::array<Lines, LivingTailDesign::numShelves> b0 {}, b1 {}, b2 {}, a1 {}, a2 {}, z1 {}, z2 {};
    Lines loopGain {};

    void setShelf (std::size_t shelf, std::size_t line, const Biquad::Coefficients& c) noexcept;
};

/** Fits a living tail to a room: delays from the room's mean free path,
    loop filters from its decay per band, and an output level matched to the
    late part of `responses`, the impulse response the convolution plays.
    Allocates; run it off the audio thread. */
LivingTailDesign designLivingTail (const ImpulseResponseRequest& request, const ImpulseResponseSet& responses);

} // namespace drenched

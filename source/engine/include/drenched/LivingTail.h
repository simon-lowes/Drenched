#pragma once

#include "drenched/Biquad.h"
#include "drenched/ImpulseResponseSynth.h"
#include "drenched/SpeakerLayout.h"

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

    // How much later than the convolution the tail should hear the input, so
    // its first echoes arrive at the mixing time, where the response's late
    // part begins, rather than on top of the early reflections.
    float inputDelaySeconds = 0.0f;
};

/** The living tail: a 16-line feedback delay network (Jot's structure, with a
    Hadamard feedback matrix) whose loop filters are fitted to the room's
    decay in every octave band.

    It runs alongside the convolution, following the same input, so at any
    moment it holds a tail that sounds like the room. Freezing it sets the
    loop lossless above the crossover, so the room sustains for ever, while
    the bass below keeps its natural decay and never builds into mud.

    Movement lets each line's length drift slowly, at its own speed, so the
    tail breathes the way air moving in a real room does. A frozen tail
    holds still, so it stays exactly as loud for as long as it is held. A new design
    glides the line lengths to theirs instead of jumping, so the room can
    change while the tail is audible.

    prepare() allocates. Everything else is real-time safe. */
class LivingTail
{
public:
    static constexpr double maxDelaySeconds = 0.2;
    static constexpr double maxInputDelaySeconds = 0.2;
    static constexpr double maxMovementSeconds = 0.0015; // how far a line's length drifts at full movement
    static constexpr double glideSeconds = 0.25;         // how long a new design takes to glide in

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Takes the room's delays and filters. The delays glide to their new
        lengths over glideSeconds, so this never clicks. */
    void setDesign (const LivingTailDesign& design) noexcept;

    /** Lossless above crossoverHz when frozen; the room's decay otherwise. */
    void setFrozen (bool frozen, float crossoverHz) noexcept;

    /** depth 0..1 of maxMovementSeconds; rateHz is the average speed of the
        drift (each line runs a little faster or slower than it). */
    void setMovement (float depth, float rateHz) noexcept;

    bool hasDesign() const noexcept { return current.valid; }
    float releaseSeconds() const noexcept { return current.releaseSeconds; }
    float inputDelaySeconds() const noexcept { return current.inputDelaySeconds; }

    /** Overwrites outL and outR. */
    void process (const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept;

    /** Overwrites numOutputs outputs (up to maxChannels), each decorrelated
        from the others; the first two are the stereo pair. scale multiplies
        the room-matched level. */
    void process (const float* inL, const float* inR, float* const* outputs, std::size_t numOutputs, float scale,
                  std::size_t numSamples) noexcept;

private:
    static constexpr std::size_t N = LivingTailDesign::numLines;

    void applyLoopFilters (bool glide) noexcept;
    void glideLoopFilters (std::size_t numSamples) noexcept;
    void readFixed (std::array<float, LivingTailDesign::numLines>& taps) noexcept;
    void readMoving (std::array<float, LivingTailDesign::numLines>& taps) noexcept;

    double rate = 48000.0;
    LivingTailDesign current;
    bool frozen = false;
    float crossover = 320.0f;

    std::vector<float> buffer; // N lines, each `capacity` long
    std::size_t capacity = 0, mask = 0, write = 0;

    // Loop filters, stored shelf by shelf across the lines so the inner loop
    // runs all sixteen lines at once in SIMD. A new design's filters and
    // output level glide in from the old ones block by block, so a new room
    // never clicks.
    using Lines = std::array<float, N>;

    struct LoopFilters
    {
        std::array<Lines, LivingTailDesign::numShelves> b0 {}, b1 {}, b2 {}, a1 {}, a2 {};
        Lines gain {};
        float output = 0.0f; // the room-matched output level
    };

    LoopFilters now, from, to;
    std::array<Lines, LivingTailDesign::numShelves> z1 {}, z2 {};
    std::size_t filterGlideLength = 0, filterGlideDone = 0;

    void setShelf (std::size_t shelf, std::size_t line, const Biquad::Coefficients& c) noexcept;

    // Line lengths in samples, fractional while gliding or moving.
    Lines delayNow {}, glideStep {}, allpass {};
    std::size_t glideRemaining = 0;

    // Movement: a sine per line, run as a rotating phasor.
    Lines lfoSin {}, lfoCos {}, rotSin {}, rotCos {};
    float depthNow = 0.0f, depthTarget = 0.0f, depthStep = 0.0f, maxExcursion = 0.0f, movementRate = -1.0f;
};

/** Fits a living tail to a room: delays from the room's mean free path,
    loop filters from its decay per band, and an output level matched to the
    late part of `responses`, the impulse response the convolution plays.
    Allocates; run it off the audio thread. */
LivingTailDesign designLivingTail (const ImpulseResponseRequest& request, const ImpulseResponseSet& responses);

/** Hands part of the late reverberation over to the living tail: the
    response's late part (from the mixing time, fully by twice it) is turned
    down by the cosine of the blend, and the returned level, its sine, is how
    loud the tail plays, so the late energy stays the same. The early
    reflections are untouched. Call after designLivingTail(), which matches
    the tail to the unblended response. */
float applyLivingBlend (ImpulseResponseSet& responses, float livingPercent, double mixingTimeSeconds);

} // namespace drenched

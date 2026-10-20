#pragma once

#include "drenched/Biquad.h"
#include "drenched/ImpulseResponseSynth.h"
#include "drenched/LivingTail.h"
#include "drenched/PartitionedConvolver.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace drenched
{

/** A true-stereo convolution kernel built from one impulse response set:
    left input to both outputs, right input to both outputs, plus the living
    tail fitted to the same room. Building it allocates and transforms the
    responses, so it is made on a background thread and handed to the audio
    thread ready to run. */
class TrueStereoKernel
{
public:
    explicit TrueStereoKernel (const ImpulseResponseSet& responses, const LivingTailDesign& tail = {});

    double sampleRate() const noexcept { return rate; }
    std::size_t length() const noexcept { return irLength; }
    const LivingTailDesign& livingTail() const noexcept { return tailDesign; }
    void reset() noexcept;

    /** Overwrites outL and outR. scratchL and scratchR are workspace of the
        same size. Real-time safe. */
    void process (const float* inL, const float* inR, float* outL, float* outR,
                  float* scratchL, float* scratchR, std::size_t numSamples) noexcept;

private:
    double rate;
    std::size_t irLength;
    PartitionedConvolver fromLeft, fromRight;
    LivingTailDesign tailDesign;
};

/** Block-rate controls for the wet path and the mix. */
struct ReverbControls
{
    float mix = 0.3f;            // 0 dry .. 1 wet, equal power
    float predelayMs = 0.0f;
    float brightnessDb = 0.0f;   // tilt: half up above 2.5 kHz, half down below 250 Hz
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    float widthPercent = 100.0f; // 0 mono .. 200 extra wide
    float monoBelowHz = 0.0f;    // 0 = off
    float ducking = 0.0f;        // 0 .. 1
    float outputGain = 1.0f;     // linear
    bool freeze = false;         // the room sustains for ever; new sound stays dry
    float crossoverHz = 320.0f;  // below this a frozen tail keeps decaying
};

/** The complete reverb: pre-delay, true-stereo convolution, the living
    tail, wet tone, width, ducking and the dry/wet mix. Zero latency.

    New kernels arrive from a background thread through submit(). The audio
    thread swaps them in without a gap: the outgoing kernel stops receiving
    input and rings out its own tail, so changing the decay or the space
    while sound is playing never cuts off what is already reverberating.
    Kernels the audio thread is finished with go back through takeRetired()
    to be freed off the audio thread.

    The living tail runs alongside, following the same input. Freezing
    crossfades from the convolution to the tail, which then holds the room
    for ever while new input stays out of the wet path. Releasing lets the
    tail die away at the room's own decay while the convolution takes new
    input again.

    process() and reset() are real-time safe. prepare() allocates. */
class ConvolutionReverb
{
public:
    static constexpr std::size_t chunkSize = 256;
    static constexpr double maxPredelaySeconds = 1.0;

    ConvolutionReverb();
    ~ConvolutionReverb();

    ConvolutionReverb (const ConvolutionReverb&) = delete;
    ConvolutionReverb& operator= (const ConvolutionReverb&) = delete;

    /** Call while audio is stopped. Kernels built for another sample rate are dropped. */
    void prepare (double sampleRate);

    void reset() noexcept;

    /** In place, any length. right may equal left for mono. */
    void process (float* left, float* right, std::size_t numSamples, const ReverbControls& controls) noexcept;

    /** From any thread except the audio thread. A kernel that was submitted
        but not yet picked up is replaced (and freed here). */
    void submit (std::unique_ptr<TrueStereoKernel> kernel);

    /** From the thread that frees kernels: returns one the audio thread no
        longer needs, or null. */
    std::unique_ptr<TrueStereoKernel> takeRetired() noexcept;

    /** The current tail in seconds (impulse response plus pre-delay), for
        the host; infinite while frozen. */
    double tailSeconds() const noexcept { return tail.load (std::memory_order_relaxed); }

    /** True once a kernel for the current sample rate is running. */
    bool isReady() const noexcept { return ready.load (std::memory_order_relaxed); }

private:
    void installIncoming() noexcept;
    void retire (TrueStereoKernel* kernel) noexcept;
    void updateFilters (const ReverbControls& controls) noexcept;
    void runKernels (std::size_t numSamples) noexcept;
    void updateFreeze (const ReverbControls& controls) noexcept;
    bool tailAudible() const noexcept { return frozen || tailPosition > 0.0f; }

    double sampleRate = 48000.0;

    // Kernels owned by the audio thread.
    TrueStereoKernel* current = nullptr;
    TrueStereoKernel* ringing = nullptr;
    TrueStereoKernel* fading = nullptr;
    std::size_t ringRemaining = 0, fadeRemaining = 0, fadeLength = 2048;

    std::atomic<TrueStereoKernel*> incoming { nullptr };

    // Single-producer (audio), single-consumer (builder) queue of retired kernels.
    static constexpr std::size_t retiredCapacity = 32;
    std::array<std::atomic<TrueStereoKernel*>, retiredCapacity> retired {};
    std::atomic<std::size_t> retiredWrite { 0 }, retiredRead { 0 };
    std::array<TrueStereoKernel*, 4> deferred {};

    std::atomic<double> tail { 0.0 };
    std::atomic<bool> ready { false };

    // Pre-delay lines.
    std::vector<float> delayL, delayR;
    std::size_t delayMask = 0, delayWrite = 0;
    float delaySamples = 0.0f, delayCoef = 0.0f;

    // Smoothed gains.
    float dryGain = 1.0f, wetGain = 0.0f, outGain = 1.0f, duckGain = 1.0f, gainCoef = 0.0f;
    float duckEnvelope = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
    bool firstBlock = true;

    // Wet tone.
    std::array<Biquad, 2> lowCut, highCut, lowShelf, highShelf;
    Biquad sideHighPass;
    ReverbControls lastFilterControls;
    bool filtersValid = false;

    // Living tail and freeze. Positions run 0..1 and become equal-power gains.
    LivingTail livingTail;
    bool frozen = false, tailDesignPending = false;
    float crossover = 320.0f;
    float convInputGain = 1.0f, tailInputGain = 1.0f, inputCoef = 0.0f;
    float convPosition = 1.0f, tailPosition = 0.0f;
    float freezeStep = 0.0f, returnStep = 0.0f, releaseStep = 0.0f;

    // Workspace, chunkSize each.
    std::vector<float> dryL, dryR, wetInL, wetInR, wetL, wetR, tmpL, tmpR, scratchL, scratchR, zeros,
        tailInL, tailInR, tailL, tailR;
};

} // namespace drenched

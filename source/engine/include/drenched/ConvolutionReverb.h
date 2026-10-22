#pragma once

#include "drenched/Biquad.h"
#include "drenched/ImpulseResponseSynth.h"
#include "drenched/LivingTail.h"
#include "drenched/PartitionedConvolver.h"
#include "drenched/ReverbCloud.h"
#include "drenched/SpeakerLayout.h"
#include "drenched/SpeakerTrim.h"
#include "drenched/SurroundResponse.h"
#include "drenched/WetEqualiser.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace drenched
{

/** A convolution kernel built from one impulse response set: each of the
    two stage sources (left and right input) to every output speaker, plus
    the living tail fitted to the same room. With a stereo layout this is
    true stereo. Building it allocates and transforms the responses, so it is
    made on a background thread and handed to the audio thread ready to run. */
class ReverbKernel
{
public:
    /** True stereo, for a stereo output. A latency (see PartitionedConvolver)
        makes the convolution cheaper and that many samples late; the reverb
        then delays its dry sound and living tail to match. */
    explicit ReverbKernel (const ImpulseResponseSet& responses, const LivingTailDesign& tail = {},
                           std::size_t latency = 0);

    /** Any output layout (see spreadResponses()). */
    explicit ReverbKernel (const SpatialResponseSet& responses, const LivingTailDesign& tail = {},
                           std::size_t latency = 0);

    double sampleRate() const noexcept { return rate; }
    std::size_t length() const noexcept { return irLength; }
    std::size_t numOutputs() const noexcept { return fromLeft.numOutputs(); }
    std::size_t latency() const noexcept { return fromLeft.latency(); }
    const LivingTailDesign& livingTail() const noexcept { return tailDesign; }

    /** How loud the living tail plays alongside this kernel, 0..1: the builder
        took the same share of the late part out of the response, so the
        late reverberation keeps its level (see applyLivingBlend()). */
    float livingLevel() const noexcept { return living; }
    void setLivingLevel (float level) noexcept { living = level; }

    void reset() noexcept;

    /** Overwrites numOutputs() outputs. scratch holds as many buffers of the
        same size as workspace. Real-time safe. */
    void process (const float* inL, const float* inR, float* const* outputs, float* const* scratch,
                  std::size_t numSamples) noexcept;

private:
    double rate;
    std::size_t irLength;
    PartitionedConvolver fromLeft, fromRight;
    LivingTailDesign tailDesign;
    float living = 0.0f;
};

/** Block-rate controls for the wet path and the mix. */
struct ReverbControls
{
    float mix = 0.3f;            // 0 dry .. 1 wet, equal power
    float predelayMs = 0.0f;
    float brightnessDb = 0.0f;   // tilt: half up above 2.5 kHz, half down below 250 Hz
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
    float widthPercent = 100.0f; // 0 mono .. 200 extra wide, across all the speakers
    float cloudPercent = 100.0f; // how far the reverb spreads from where the sound is panned (see ReverbCloud)
    float monoBelowHz = 0.0f;    // 0 = off
    float ducking = 0.0f;        // 0 .. 1
    float outputGain = 1.0f;     // linear
    bool freeze = false;         // the room sustains for ever; new sound stays dry
    float crossoverHz = 320.0f;  // below this a frozen tail keeps decaying
    float movement = 0.0f;       // 0 .. 1, how far the living tail drifts
    float movementRateHz = 0.7f; // how fast it drifts
    WetEqualiser::Bands eq = WetEqualiser::flat; // after the cuts and brightness

    // Each output speaker's reverb level and delay (see SpeakerTrim), in
    // the output layout's order.
    std::array<float, maxChannels> speakerLevelDb {};
    std::array<float, maxChannels> speakerDelayMs {};
};

/** The complete reverb: pre-delay, convolution from the two stage sources to
    every output speaker, the living tail, wet tone, width, ducking and the
    dry/wet mix. Mono, stereo and surround up to 9.1.6.

    Zero latency, unless the running kernel was built with one (low-CPU
    mode): then the dry sound and the living tail are delayed by the same
    amount so everything stays in time, and the host is told the latency
    (see lowCpuLatency()). Changing it crossfades the dry sound to its new
    delay over a few milliseconds.

    The input channels feed two stage sources: left-hand speakers the left
    one, right-hand speakers the right, centred ones both. The LFE input
    stays dry. Each output plays its dry input (the input of the same
    speaker, or for a mono input the centre, else the front pair) mixed with
    its own wet signal; the LFE output gets no reverberation.

    New kernels arrive from a background thread through submit(). The audio
    thread swaps them in without a gap: the outgoing kernel stops receiving
    input and rings out its own tail, so changing the decay or the space
    while sound is playing never cuts off what is already reverberating.
    Kernels the audio thread is finished with go back through takeRetired()
    to be freed off the audio thread.

    The living tail runs alongside, following the same input, and plays at
    the level the kernel asks for (none unless the living blend is up).
    Freezing crossfades from the convolution to the tail, which then holds
    the room for ever while new input stays out of the wet path. Releasing
    lets the tail die away at the room's own decay while the convolution
    takes new input again.

    process() and reset() are real-time safe. prepare() allocates. */
class ConvolutionReverb
{
public:
    static constexpr std::size_t chunkSize = 256;
    static constexpr double maxPredelaySeconds = 1.0;
    static constexpr std::size_t maxLatency = 8192;

    /** The latency low-CPU mode uses at a sample rate: about 21 ms, as a
        power of two (1024 samples at 44.1 and 48 kHz). */
    static std::size_t lowCpuLatency (double sampleRate) noexcept;

    /** The latency the dry sound is delayed by now: the running kernel's.
        Audio thread only. */
    std::size_t latency() const noexcept { return dryTap; }

    ConvolutionReverb();
    ~ConvolutionReverb();

    ConvolutionReverb (const ConvolutionReverb&) = delete;
    ConvolutionReverb& operator= (const ConvolutionReverb&) = delete;

    /** Call while audio is stopped. Kernels built for another sample rate or
        output layout are dropped. Up to maxChannels each way. */
    void prepare (double newSampleRate, const SpeakerLayout& inputs, const SpeakerLayout& outputs);

    /** Stereo in and out. */
    void prepare (double newSampleRate) { prepare (newSampleRate, layouts::stereo(), layouts::stereo()); }

    void reset() noexcept;

    /** In place: channels holds max(inputs, outputs) buffers; the first
        inputs.size() carry the input and all outputs.size() are written. */
    void process (float* const* channels, std::size_t numChannels, std::size_t numSamples,
                  const ReverbControls& controls) noexcept;

    /** Stereo layouts, in place, any length. right may equal left for a mono
        input, and then only left is written. */
    void process (float* left, float* right, std::size_t numSamples, const ReverbControls& controls) noexcept;

    /** From any thread except the audio thread. A kernel that was submitted
        but not yet picked up is replaced (and freed here). */
    void submit (std::unique_ptr<ReverbKernel> kernel);

    /** From the thread that frees kernels: returns one the audio thread no
        longer needs, or null. */
    std::unique_ptr<ReverbKernel> takeRetired() noexcept;

    /** The current tail in seconds (impulse response plus pre-delay), for
        the host; infinite while frozen. */
    double tailSeconds() const noexcept { return tail.load (std::memory_order_relaxed); }

    /** True once a kernel for the current sample rate is running. */
    bool isReady() const noexcept { return ready.load (std::memory_order_relaxed); }

    std::size_t numOutputs() const noexcept { return outputLayout.size(); }

private:
    void installIncoming() noexcept;
    void retire (ReverbKernel* kernel) noexcept;
    void updateFilters (const ReverbControls& controls) noexcept;
    void runKernels (std::size_t numSamples) noexcept;
    void delayDry (std::size_t wanted, std::size_t numSamples) noexcept;
    void updateFreeze (const ReverbControls& controls) noexcept;
    bool tailAudible() const noexcept { return frozen || tailPosition > 0.0f; }

    double sampleRate = 48000.0;

    // Routing, set in prepare().
    SpeakerLayout inputLayout, outputLayout;
    std::array<float, maxChannels> toLeftSource {}, toRightSource {}; // per input
    std::array<std::array<float, maxChannels>, maxChannels> dryRouting {}; // [output][input]
    std::array<bool, maxChannels> wetOutput {}; // false for LFE
    float tailScale = 1.0f; // the living tail's level per speaker, matched to spreadResponses()
    std::size_t numIn = 2, numOut = 2, numWet = 2;

    // Kernels owned by the audio thread.
    ReverbKernel* current = nullptr;
    ReverbKernel* ringing = nullptr;
    ReverbKernel* fading = nullptr;
    std::size_t ringRemaining = 0, fadeRemaining = 0, fadeLength = 2048;
    std::atomic<ReverbKernel*> incoming { nullptr };

    // Single-producer (audio), single-consumer (builder) queue of retired kernels.
    static constexpr std::size_t retiredCapacity = 32;
    std::array<std::atomic<ReverbKernel*>, retiredCapacity> retired {};
    std::atomic<std::size_t> retiredWrite { 0 }, retiredRead { 0 };
    std::array<ReverbKernel*, 4> deferred {};

    std::atomic<double> tail { 0.0 };
    std::atomic<bool> ready { false };

    // Pre-delay lines, one per stage source.
    std::vector<float> delayL, delayR;
    std::size_t delayMask = 0, delayWrite = 0;
    float delaySamples = 0.0f, delayCoef = 0.0f;
    float tailDelaySamples = 0.0f; // the living tail's extra wait, read from the same lines

    // Smoothed gains.
    float dryGain = 1.0f, wetGain = 0.0f, outGain = 1.0f, duckGain = 1.0f, gainCoef = 0.0f;
    float duckEnvelope = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
    bool firstBlock = true;

    // Wet tone, per output.
    std::array<Biquad, maxChannels> lowCut, highCut, lowShelf, highShelf, spreadHighPass;
    ReverbControls lastFilterControls;
    bool filtersValid = false;

    // Each speaker's reverb level and delay.
    SpeakerTrim speakerTrim;
    WetEqualiser equaliser;
    ReverbCloud cloud;

    // The dry sound's delay, matching the running kernel's latency: a line
    // per input, and a crossfade from the old tap when it changes.
    std::vector<float> dryLines;
    std::size_t dryMask = 0, dryWrite = 0, dryTap = 0, dryFrom = 0, dryFade = 0, dryFadeLength = 960;

    // Living tail and freeze. Positions run 0..1 and become equal-power gains.
    LivingTail livingTail;
    bool frozen = false, tailDesignPending = false;
    float crossover = 320.0f;
    float convInputGain = 1.0f, tailInputGain = 1.0f, inputCoef = 0.0f;
    float convPosition = 1.0f, tailPosition = 0.0f;
    float freezeStep = 0.0f, returnStep = 0.0f, releaseStep = 0.0f;
    float livingGain = 0.0f, livingStep = 0.0f; // the kernel's living level, gliding

    // Workspace: chunkSize each, or maxChannels blocks of chunkSize.
    std::vector<float> wetInL, wetInR, tailInL, tailInR, zeros, monoCopy;
    std::vector<float> dry, wet, tmp, scratch, tails;
    std::array<float*, maxChannels> wetPtr {}, tmpPtr {}, scratchPtr {}, tailPtr {};
};

} // namespace drenched

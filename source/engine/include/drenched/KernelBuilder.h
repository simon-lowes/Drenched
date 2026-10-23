#pragma once

#include "drenched/ImpulseResponseSynth.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace drenched
{

class ConvolutionReverb;

/** Builds impulse responses (synthesised, or shaped from a loaded file) and convolution kernels on its own thread and
    hands them to a ConvolutionReverb. It also frees the kernels the reverb
    retires, so nothing is ever allocated or freed on the audio thread.

    request() is debounced: while a control is being dragged, a new kernel is
    built only once the value has rested for a moment, then the reverb swaps
    it in seamlessly. Requests equal to the last build are ignored. */
class KernelBuilder
{
public:
    /** The most convolution a kernel may hold, as impulse response samples
        summed over its paths (a stereo kernel has four paths, a surround one
        two per speaker), or 0 for no limit. A 32-bit process has 2 to 4 GB to
        share with the host and every other plug-in, so there a long surround
        response at a high sample rate is shortened to fit (32 M samples peaks
        near 600 MB while building: 9.1.6 at 96 kHz keeps 10 s). Stereo is never
        shortened. 64-bit builds have no limit. */
    static constexpr std::size_t defaultSampleBudget = sizeof (void*) < 8 ? 32'000'000 : 0;

    explicit KernelBuilder (ConvolutionReverb& target,
                            std::chrono::milliseconds debounce = std::chrono::milliseconds (80),
                            std::size_t sampleBudget = defaultSampleBudget);
    ~KernelBuilder();

    KernelBuilder (const KernelBuilder&) = delete;
    KernelBuilder& operator= (const KernelBuilder&) = delete;

    /** From any thread but the audio thread. Never blocks for long. */
    void request (const ImpulseResponseRequest& wanted);

    /** Builds and submits on the calling thread, blocking until done. For
        prepareToPlay, so playback starts with the right response. */
    void buildNow (const ImpulseResponseRequest& wanted);

    /** The request the newest submitted kernel was built from, if any. */
    std::optional<ImpulseResponseRequest> lastBuilt() const;

    /** The length in seconds the newest kernel was shortened to by the
        sample budget, or 0 if it plays in full. From any thread. */
    double shortenedTo() const noexcept { return shortened.load (std::memory_order_relaxed); }

    /** The stereo response the newest kernel plays (for surround outputs, the
        stereo response its speakers are spread from), for the decay displays.
        From any thread. */
    std::shared_ptr<const ImpulseResponseSet> lastResponses() const;

    /** Changes every time a kernel is built. From any thread. */
    std::uint64_t buildCount() const noexcept { return builds.load (std::memory_order_acquire); }

private:
    void run();
    void build (const ImpulseResponseRequest& wanted);
    void freeRetired();

    ConvolutionReverb& reverb;
    const std::chrono::milliseconds debounceTime;
    const std::size_t budget;
    std::atomic<double> shortened { 0.0 };
    std::atomic<std::uint64_t> builds { 0 };

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::optional<ImpulseResponseRequest> pending, built;
    std::shared_ptr<const ImpulseResponseSet> builtResponses;
    std::chrono::steady_clock::time_point pendingSince;
    bool quit = false;

    std::mutex buildMutex; // one build at a time

    // Guarded by buildMutex: the last loaded response, at the session rate.
    std::shared_ptr<const RecordedResponse> resampledFrom;
    ImpulseResponseSet resampled;
    std::vector<ImpulseResponseSet> resampledSpeakers; // a surround recording's channels, four to a set
    std::thread worker;
};

} // namespace drenched

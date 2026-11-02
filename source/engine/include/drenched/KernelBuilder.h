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
        near 600 MB while building: 9.1.6 at 96 kHz keeps 10 s). Stereo is held to
        the same budget, 8 M samples a path, which only the longest responses reach
        at the highest rates (41.7 s at 192 kHz: a 30 s file at Size above 139 %,
        or 10.4 s at 768 kHz). 64-bit builds have no limit. */
    static constexpr std::size_t defaultSampleBudget = sizeof (void*) < 8 ? 32'000'000 : 0;

    explicit KernelBuilder (ConvolutionReverb& target,
                            std::chrono::milliseconds debounce = std::chrono::milliseconds (80),
                            std::size_t sampleBudget = defaultSampleBudget);
    ~KernelBuilder();

    KernelBuilder (const KernelBuilder&) = delete;
    KernelBuilder& operator= (const KernelBuilder&) = delete;

    /** From any thread but the audio thread. Never blocks for long. A request that
        doesn't settle (a scene recalled or a space chosen, rather than a control being
        dragged) builds without waiting for the value to rest. */
    void request (const ImpulseResponseRequest& wanted, bool settle = true);

    /** Builds and submits on the calling thread, blocking until done. For
        prepareToPlay, so playback starts with the right response. A request made
        before it and not yet built is dropped: this one is newer. */
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

    /** Waits until everything asked of the builder so far is done: no request waiting
        (to settle or to start) and no build running, on its thread or in buildNow().
        Returns whether that happened within the timeout. A request that turns out to
        equal the last build counts as done once the builder has seen it. For tests and
        tools that need to know a change has landed, or that none is coming; any thread
        but the audio thread. */
    bool waitUntilIdle (std::chrono::milliseconds timeout) const;

private:
    void run();
    void build (const ImpulseResponseRequest& wanted, std::uint64_t serial);
    void freeRetired();
    void finishBuilding (const ImpulseResponseRequest* builtNow, std::uint64_t serial);

    ConvolutionReverb& reverb;
    const std::chrono::milliseconds debounceTime;
    const std::size_t budget;
    std::atomic<double> shortened { 0.0 };
    std::atomic<std::uint64_t> builds { 0 };

    mutable std::mutex mutex;
    std::condition_variable wake;
    mutable std::condition_variable idle; // notified when a build finishes or a request is dropped
    std::optional<ImpulseResponseRequest> pending, built;
    std::shared_ptr<const ImpulseResponseSet> builtResponses;
    std::chrono::steady_clock::time_point pendingSince;
    int building = 0; // builds running, on the worker or in buildNow()
    bool quit = false;

    // Every request and buildNow() numbered in the order they were asked, so one asked
    // for earlier is never built after a later one: asked counts them, pendingAsked is
    // the pending request's number.
    std::uint64_t asked = 0, pendingAsked = 0;

    std::mutex buildMutex; // one build at a time
    std::uint64_t newestBuild = 0; // guarded by buildMutex: the number of the last build started

    // Guarded by buildMutex: the last loaded response, at the session rate.
    std::shared_ptr<const RecordedResponse> resampledFrom;
    ImpulseResponseSet resampled;
    std::vector<ImpulseResponseSet> resampledSpeakers; // a surround recording's channels, four to a set
    std::thread worker;
};

} // namespace drenched

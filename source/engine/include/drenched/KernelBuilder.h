#pragma once

#include "drenched/ImpulseResponseSynth.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace drenched
{

class ConvolutionReverb;

/** Builds impulse responses and convolution kernels on its own thread and
    hands them to a ConvolutionReverb. It also frees the kernels the reverb
    retires, so nothing is ever allocated or freed on the audio thread.

    request() is debounced: while a control is being dragged, a new kernel is
    built only once the value has rested for a moment, then the reverb swaps
    it in seamlessly. Requests equal to the last build are ignored. */
class KernelBuilder
{
public:
    explicit KernelBuilder (ConvolutionReverb& target,
                            std::chrono::milliseconds debounce = std::chrono::milliseconds (80));
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

private:
    void run();
    void build (const ImpulseResponseRequest& wanted);
    void freeRetired();

    ConvolutionReverb& reverb;
    const std::chrono::milliseconds debounceTime;

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::optional<ImpulseResponseRequest> pending, built;
    std::chrono::steady_clock::time_point pendingSince;
    bool quit = false;

    std::mutex buildMutex; // one build at a time
    std::thread worker;
};

} // namespace drenched

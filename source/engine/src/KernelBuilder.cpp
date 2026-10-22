#include "drenched/KernelBuilder.h"
#include "drenched/LivingTail.h"
#include "drenched/ConvolutionReverb.h"
#include "drenched/DirectSound.h"
#include "drenched/RecordedResponse.h"
#include "drenched/ResponseEnvelope.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace drenched
{

namespace
{
    /** Cuts every path to length, fading out over its last quarter so the
        tail ends smoothly rather than stopping dead. */
    void shorten (ImpulseResponseSet& responses, std::size_t length)
    {
        const auto fade = std::max<std::size_t> (1, length / 4);

        for (auto& path : responses.paths)
        {
            path.resize (length);

            for (std::size_t i = 0; i < fade; ++i)
            {
                const auto x = static_cast<double> (i + 1) / static_cast<double> (fade);
                path[length - 1 - i] *= static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * x));
            }
        }
    }
}

KernelBuilder::KernelBuilder (ConvolutionReverb& target, std::chrono::milliseconds debounce, std::size_t sampleBudget)
    : reverb (target), debounceTime (debounce), budget (sampleBudget), worker ([this] { run(); })
{
}

KernelBuilder::~KernelBuilder()
{
    {
        const std::lock_guard lock (mutex);
        quit = true;
    }

    wake.notify_all();
    worker.join();
    freeRetired();
}

void KernelBuilder::request (const ImpulseResponseRequest& wanted)
{
    {
        const std::lock_guard lock (mutex);

        if (pending == wanted)
            return;

        if (! pending.has_value() && built == wanted)
            return;

        pending = wanted;
        pendingSince = std::chrono::steady_clock::now();
    }

    wake.notify_all();
}

void KernelBuilder::buildNow (const ImpulseResponseRequest& wanted)
{
    build (wanted);

    const std::lock_guard lock (mutex);

    if (pending == wanted)
        pending.reset();
}

std::optional<ImpulseResponseRequest> KernelBuilder::lastBuilt() const
{
    const std::lock_guard lock (mutex);
    return built;
}

void KernelBuilder::build (const ImpulseResponseRequest& wanted)
{
    const std::lock_guard buildLock (buildMutex);

    ImpulseResponseSet responses;

    if (wanted.recorded != nullptr)
    {
        // Resampling a long file is the slow part, and it only depends on
        // the file and the rate, so it is kept between edits.
        if (resampledFrom != wanted.recorded || resampled.sampleRate != wanted.sampleRate)
        {
            resampled = resampleResponses (wanted.recorded->responses, wanted.sampleRate);
            resampledFrom = wanted.recorded;
        }

        responses = shapeRecordedResponse (resampled, *wanted.recorded, wanted);
    }
    else
    {
        responses = synthesiseImpulseResponse (wanted);
        resampledFrom.reset(); // let go of the file's audio
        resampled = {};
    }

    // The tail is fitted to the whole response, so Freeze still holds the
    // room when the response is gated or reversed.
    const auto tail = designLivingTail (wanted, responses);
    applyDirect (responses, wanted);
    applyCrossFeed (responses, wanted.crossFeedPercent);
    applyEnvelope (responses, wanted);

    const auto living = cutsTail (wanted) ? 0.0f
                                          : applyLivingBlend (responses, wanted.livingPercent, mixingTimeSeconds (wanted));

    const auto stereo = wanted.outputs == layouts::stereo();
    const auto paths = stereo ? responses.paths.size() : 2 * wanted.outputs.size();
    auto shortenedSeconds = 0.0;

    if (budget > 0 && responses.length() * paths > budget)
    {
        shorten (responses, budget / paths);
        shortenedSeconds = static_cast<double> (responses.length()) / responses.sampleRate;
    }

    auto kernel = stereo
                    ? std::make_unique<ReverbKernel> (responses, tail, wanted.latencySamples)
                    : std::make_unique<ReverbKernel> (spreadResponses (responses, wanted.outputs, mixingTimeSeconds (wanted)), tail,
                                                      wanted.latencySamples);
    kernel->setLivingLevel (living);
    reverb.submit (std::move (kernel));
    shortened.store (shortenedSeconds, std::memory_order_relaxed);

    const std::lock_guard lock (mutex);
    built = wanted;
}

void KernelBuilder::freeRetired()
{
    while (auto kernel = reverb.takeRetired())
        kernel.reset();
}

void KernelBuilder::run()
{
    std::unique_lock lock (mutex);

    while (! quit)
    {
        wake.wait_for (lock, std::chrono::milliseconds (25));

        if (quit)
            break;

        lock.unlock();
        freeRetired();
        lock.lock();

        if (! pending.has_value())
            continue;

        if (pending == built)
        {
            pending.reset();
            continue;
        }

        // Build straight away the first time; afterwards wait for the value to settle.
        const auto settled = std::chrono::steady_clock::now() - pendingSince >= debounceTime;

        if (built.has_value() && ! settled)
            continue;

        const auto wanted = *pending;
        pending.reset();

        lock.unlock();

        try
        {
            build (wanted);
        }
        catch (const std::exception&)
        {
            // Out of memory for a very long response: keep the current kernel.
        }

        lock.lock();
    }
}

} // namespace drenched

#include "drenched/KernelBuilder.h"
#include "drenched/LivingTail.h"
#include "drenched/ConvolutionReverb.h"
#include "drenched/RecordedResponse.h"

#include <exception>

namespace drenched
{

KernelBuilder::KernelBuilder (ConvolutionReverb& target, std::chrono::milliseconds debounce)
    : reverb (target), debounceTime (debounce), worker ([this] { run(); })
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

    const auto tail = designLivingTail (wanted, responses);
    const auto living = applyLivingBlend (responses, wanted.livingPercent, mixingTimeSeconds (wanted));
    auto kernel = wanted.outputs == layouts::stereo()
                    ? std::make_unique<ReverbKernel> (responses, tail)
                    : std::make_unique<ReverbKernel> (spreadResponses (responses, wanted.outputs, mixingTimeSeconds (wanted)), tail);
    kernel->setLivingLevel (living);
    reverb.submit (std::move (kernel));

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

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

    /** A surround recording's channels, four to a set, so the shaping steps
        (which treat every path alike) run on them as on a stereo response. */
    std::vector<ImpulseResponseSet> inSets (const std::vector<std::vector<float>>& channels, double sampleRate)
    {
        std::vector<ImpulseResponseSet> sets;
        const auto length = channels.empty() ? 0 : channels[0].size();

        for (std::size_t c = 0; c < channels.size(); c += 4)
        {
            auto& set = sets.emplace_back();
            set.sampleRate = sampleRate;

            for (std::size_t p = 0; p < 4; ++p)
                set.paths[p] = c + p < channels.size() ? channels[c + p] : std::vector<float> (length, 0.0f);
        }

        return sets;
    }

    std::vector<std::vector<float>> outOfSets (std::vector<ImpulseResponseSet>&& sets, std::size_t count)
    {
        std::vector<std::vector<float>> channels;

        for (auto& set : sets)
            for (auto& path : set.paths)
                if (channels.size() < count)
                    channels.push_back (std::move (path));

        return channels;
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
            resampledSpeakers.clear();

            for (const auto& set : inSets (wanted.recorded->speakers, wanted.recorded->responses.sampleRate))
                resampledSpeakers.push_back (resampleResponses (set, wanted.sampleRate));

            resampledFrom = wanted.recorded;
        }

        responses = shapeRecordedResponse (resampled, *wanted.recorded, wanted);
    }
    else
    {
        responses = synthesiseImpulseResponse (wanted);
        resampledFrom.reset(); // let go of the file's audio
        resampled = {};
        resampledSpeakers.clear();
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

    // A surround recording on surround speakers plays its own channels, shaped
    // the same way as its stereo fold-down (all but the cross-feed, which
    // placeSpeakerResponses() applies by side).
    const auto native = ! stereo && wanted.recorded != nullptr && wanted.recorded->isSurround();
    std::vector<ImpulseResponseSet> speakerSets;

    if (native)
    {
        for (const auto& set : resampledSpeakers)
        {
            auto& shaped = speakerSets.emplace_back (shapeRecordedResponse (set, *wanted.recorded, wanted));
            applyDirect (shaped, wanted);
            applyEnvelope (shaped, wanted);

            if (! cutsTail (wanted))
                applyLivingBlend (shaped, wanted.livingPercent, mixingTimeSeconds (wanted));
        }
    }

    if (budget > 0 && responses.length() * paths > budget)
    {
        shorten (responses, budget / paths);

        for (auto& set : speakerSets)
            if (set.length() > budget / paths)
                shorten (set, budget / paths);

        shortenedSeconds = static_cast<double> (responses.length()) / responses.sampleRate;
    }

    std::unique_ptr<ReverbKernel> kernel;

    if (stereo)
        kernel = std::make_unique<ReverbKernel> (responses, tail, wanted.latencySamples);
    else if (native)
    {
        // A B-format recording is decoded to the speakers it plays on, so each
        // has its own channel to play.
        auto channels = outOfSets (std::move (speakerSets), wanted.recorded->speakers.size());
        const auto& from = wanted.recorded->ambisonic ? wanted.outputs : wanted.recorded->layout;

        if (wanted.recorded->ambisonic)
            channels = decodeAmbisonic (channels, wanted.outputs);

        kernel = std::make_unique<ReverbKernel> (placeSpeakerResponses (channels, from, responses, wanted.outputs,
                                                                        mixingTimeSeconds (wanted), wanted.crossFeedPercent),
                                                 tail, wanted.latencySamples);
    }
    else
        kernel = std::make_unique<ReverbKernel> (spreadResponses (responses, wanted.outputs, mixingTimeSeconds (wanted)), tail,
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

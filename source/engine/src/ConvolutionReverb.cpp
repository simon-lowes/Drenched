#include "drenched/ConvolutionReverb.h"
#include "drenched/Mix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace drenched
{

namespace
{

std::vector<std::span<const float>> pathsFrom (const ImpulseResponseSet& set, ImpulseResponseSet::Path a, ImpulseResponseSet::Path b)
{
    return { std::span<const float> (set.paths[a]), std::span<const float> (set.paths[b]) };
}

std::vector<std::span<const float>> pathsFrom (const std::vector<std::vector<float>>& paths)
{
    std::vector<std::span<const float>> spans;

    for (const auto& p : paths)
        spans.emplace_back (p);

    return spans;
}

float onePole (double sampleRate, double seconds) noexcept
{
    return static_cast<float> (std::exp (-1.0 / (sampleRate * seconds)));
}

std::size_t nextPowerOfTwo (std::size_t n) noexcept
{
    std::size_t p = 1;

    while (p < n)
        p <<= 1;

    return p;
}

bool differs (float a, float b) noexcept { return std::abs (a - b) > 1.0e-4f; }

} // namespace

//==============================================================================
ReverbKernel::ReverbKernel (const ImpulseResponseSet& responses, const LivingTailDesign& tail, std::size_t latency)
    : rate (responses.sampleRate),
      irLength (responses.length()),
      fromLeft (pathsFrom (responses, ImpulseResponseSet::leftToLeft, ImpulseResponseSet::leftToRight), latency),
      fromRight (pathsFrom (responses, ImpulseResponseSet::rightToLeft, ImpulseResponseSet::rightToRight), latency),
      tailDesign (tail)
{
}

ReverbKernel::ReverbKernel (const SpatialResponseSet& responses, const LivingTailDesign& tail, std::size_t latency)
    : rate (responses.sampleRate),
      irLength (responses.length()),
      fromLeft (pathsFrom (responses.paths[0]), latency),
      fromRight (pathsFrom (responses.paths[1]), latency),
      tailDesign (tail)
{
}

void ReverbKernel::reset() noexcept
{
    fromLeft.reset();
    fromRight.reset();
}

void ReverbKernel::process (const float* inL, const float* inR, float* const* outputs, float* const* scratch,
                            std::size_t numSamples) noexcept
{
    fromLeft.process (inL, outputs, numSamples);
    fromRight.process (inR, scratch, numSamples);

    for (std::size_t k = 0; k < numOutputs(); ++k)
        for (std::size_t n = 0; n < numSamples; ++n)
            outputs[k][n] += scratch[k][n];
}

//==============================================================================
std::size_t ConvolutionReverb::lowCpuLatency (double sampleRate) noexcept
{
    return sampleRate <= 50000.0 ? 1024 : sampleRate <= 100000.0 ? 2048 : 4096;
}

ConvolutionReverb::ConvolutionReverb()
{
    for (auto* buffer : { &wetInL, &wetInR, &tailInL, &tailInR, &zeros, &monoCopy })
        buffer->assign (chunkSize, 0.0f);

    for (auto* buffer : { &dry, &wet, &tmp, &scratch, &tails })
        buffer->assign (chunkSize * maxChannels, 0.0f);

    for (std::size_t k = 0; k < maxChannels; ++k)
    {
        wetPtr[k] = wet.data() + k * chunkSize;
        tmpPtr[k] = tmp.data() + k * chunkSize;
        scratchPtr[k] = scratch.data() + k * chunkSize;
        tailPtr[k] = tails.data() + k * chunkSize;
    }

    prepare (sampleRate);
}

ConvolutionReverb::~ConvolutionReverb()
{
    delete current;
    delete ringing;
    delete fading;
    delete incoming.exchange (nullptr);

    for (auto* k : deferred)
        delete k;

    while (auto k = takeRetired())
        k.reset();
}

void ConvolutionReverb::prepare (double newSampleRate, const SpeakerLayout& inputs, const SpeakerLayout& outputs)
{
    sampleRate = newSampleRate;
    inputLayout.assign (inputs.begin(), inputs.begin() + static_cast<std::ptrdiff_t> (std::min (inputs.size(), maxChannels)));
    outputLayout.assign (outputs.begin(), outputs.begin() + static_cast<std::ptrdiff_t> (std::min (outputs.size(), maxChannels)));
    numIn = inputLayout.size();
    numOut = outputLayout.size();

    // Inputs to the two stage sources. A lone centred input (mono) feeds both
    // fully, as a mono source in the middle of the stage.
    const auto monoInput = layouts::countDirectional (inputLayout) == 1 && numIn == 1 && inputLayout[0].isCentred();
    toLeftSource.fill (0.0f);
    toRightSource.fill (0.0f);

    for (std::size_t i = 0; i < numIn; ++i)
    {
        const auto& in = inputLayout[i];

        if (in.lfe)
            continue;

        const auto centred = monoInput ? 1.0f : std::numbers::sqrt2_v<float> / 2.0f;
        toLeftSource[i] = in.isLeft() ? 1.0f : in.isCentred() ? centred : 0.0f;
        toRightSource[i] = in.isRight() ? 1.0f : in.isCentred() ? centred : 0.0f;
    }

    // Dry routing: each input to the output of the same speaker. An input
    // with no such output goes to the nearest one (a mono input with no
    // centre to the front pair, as stereo plays it); an LFE input with no
    // LFE output is dropped.
    for (auto& row : dryRouting)
        row.fill (0.0f);

    for (std::size_t i = 0; i < numIn; ++i)
    {
        const auto& in = inputLayout[i];
        bool routed = false;

        for (std::size_t o = 0; o < numOut; ++o)
        {
            const auto& out = outputLayout[o];

            if (out.lfe == in.lfe && (in.lfe || (std::abs (out.azimuth - in.azimuth) < 0.5f && std::abs (out.elevation - in.elevation) < 0.5f)))
            {
                dryRouting[o][i] = 1.0f;
                routed = true;
                break;
            }
        }

        if (routed || in.lfe)
            continue;

        if (monoInput)
        {
            for (std::size_t o = 0; o < numOut; ++o)
            {
                if (outputLayout[o].isFront() && std::abs (std::abs (outputLayout[o].azimuth) - 30.0f) < 0.5f)
                {
                    dryRouting[o][i] = 1.0f;
                    routed = true;
                }
            }

            if (routed)
                continue;
        }

        std::size_t nearest = numOut;
        float best = 1.0e9f;

        for (std::size_t o = 0; o < numOut; ++o)
        {
            const auto& out = outputLayout[o];

            if (out.lfe)
                continue;

            auto da = std::abs (out.azimuth - in.azimuth);
            da = std::min (da, 360.0f - da);
            const auto distance = da + std::abs (out.elevation - in.elevation);

            if (distance < best)
            {
                best = distance;
                nearest = o;
            }
        }

        if (nearest < numOut)
            dryRouting[nearest][i] = std::numbers::sqrt2_v<float> / 2.0f;
    }

    numWet = 0;

    for (std::size_t o = 0; o < maxChannels; ++o)
    {
        wetOutput[o] = o < numOut && ! outputLayout[o].lfe;
        numWet += wetOutput[o] ? 1 : 0;
    }

    // spreadResponses() keeps the stereo energy over all the speakers.
    tailScale = numWet > 0 ? std::sqrt (2.0f / static_cast<float> (numWet)) : 0.0f;

    // Audio is stopped, so kernels can be dropped here directly.
    for (auto** k : { &current, &ringing, &fading })
    {
        if (*k != nullptr && ((*k)->sampleRate() != sampleRate || (*k)->numOutputs() != numOut))
        {
            delete *k;
            *k = nullptr;
        }
    }

    ready.store (current != nullptr);
    tail.store (current != nullptr ? static_cast<double> (current->length()) / sampleRate : 0.0);

    const auto longest = maxPredelaySeconds + LivingTail::maxInputDelaySeconds;
    const auto capacity = nextPowerOfTwo (static_cast<std::size_t> (longest * sampleRate) + maxLatency + chunkSize + 4);
    delayL.assign (capacity, 0.0f);
    delayR.assign (capacity, 0.0f);
    delayMask = capacity - 1;

    const auto dryCapacity = nextPowerOfTwo (maxLatency + chunkSize + 1);
    dryLines.assign (dryCapacity * maxChannels, 0.0f);
    dryMask = dryCapacity - 1;
    dryFadeLength = static_cast<std::size_t> (0.02 * sampleRate);

    delayCoef = onePole (sampleRate, 0.05);
    gainCoef = onePole (sampleRate, 0.02);
    attackCoef = onePole (sampleRate, 0.01);
    releaseCoef = onePole (sampleRate, 0.25);
    fadeLength = static_cast<std::size_t> (0.04 * sampleRate);

    inputCoef = onePole (sampleRate, 0.01);
    freezeStep = static_cast<float> (1.0 / (0.15 * sampleRate));
    returnStep = static_cast<float> (1.0 / (0.05 * sampleRate));
    livingStep = static_cast<float> (1.0 / (0.05 * sampleRate));

    livingTail.prepare (sampleRate);
    speakerTrim.prepare (sampleRate, numOut);

    if (current != nullptr)
        livingTail.setDesign (current->livingTail());

    tailDesignPending = false;
    filtersValid = false;
    reset();
}

void ConvolutionReverb::reset() noexcept
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    delayWrite = 0;
    duckEnvelope = 0.0f;
    firstBlock = true;

    std::fill (dryLines.begin(), dryLines.end(), 0.0f);
    dryWrite = 0;
    dryTap = current != nullptr ? current->latency() : 0;
    dryFade = 0;

    for (auto* k : { current, ringing, fading })
        if (k != nullptr)
            k->reset();

    // A cleared room has nothing to hold; the next block re-freezes if asked.
    livingTail.reset();
    speakerTrim.reset();
    livingTail.setFrozen (false, crossover);
    frozen = false;
    convInputGain = tailInputGain = 1.0f;
    convPosition = 1.0f;
    tailPosition = 0.0f;
    livingGain = current != nullptr ? current->livingLevel() : 0.0f;
    tailDelaySamples = static_cast<float> (livingTail.inputDelaySeconds() * sampleRate + static_cast<double> (dryTap));

    if (tailDesignPending && current != nullptr)
        livingTail.setDesign (current->livingTail());

    tailDesignPending = false;

    for (auto* bank : { &lowCut, &highCut, &lowShelf, &highShelf, &spreadHighPass })
        for (auto& f : *bank)
            f.reset();
}

void ConvolutionReverb::submit (std::unique_ptr<ReverbKernel> kernel)
{
    delete incoming.exchange (kernel.release(), std::memory_order_acq_rel);
}

std::unique_ptr<ReverbKernel> ConvolutionReverb::takeRetired() noexcept
{
    const auto read = retiredRead.load (std::memory_order_relaxed);

    if (read == retiredWrite.load (std::memory_order_acquire))
        return nullptr;

    auto* k = retired[read % retiredCapacity].exchange (nullptr, std::memory_order_relaxed);
    retiredRead.store (read + 1, std::memory_order_release);
    return std::unique_ptr<ReverbKernel> (k);
}

void ConvolutionReverb::retire (ReverbKernel* kernel) noexcept
{
    if (kernel == nullptr)
        return;

    // Flush anything deferred first so the order is kept.
    for (auto*& waiting : deferred)
    {
        if (waiting == nullptr)
            continue;

        const auto write = retiredWrite.load (std::memory_order_relaxed);

        if (write - retiredRead.load (std::memory_order_acquire) >= retiredCapacity)
            break;

        retired[write % retiredCapacity].store (waiting, std::memory_order_relaxed);
        retiredWrite.store (write + 1, std::memory_order_release);
        waiting = nullptr;
    }

    const auto write = retiredWrite.load (std::memory_order_relaxed);

    if (write - retiredRead.load (std::memory_order_acquire) < retiredCapacity)
    {
        retired[write % retiredCapacity].store (kernel, std::memory_order_relaxed);
        retiredWrite.store (write + 1, std::memory_order_release);
        return;
    }

    // Queue full (the builder has stalled): park it and try again next time.
    for (auto*& waiting : deferred)
    {
        if (waiting == nullptr)
        {
            waiting = kernel;
            return;
        }
    }

    // Nowhere to put it. Leaking is better than freeing on the audio thread;
    // this needs 36 swaps without the builder running, which does not happen.
}

void ConvolutionReverb::installIncoming() noexcept
{
    auto* fresh = incoming.exchange (nullptr, std::memory_order_acq_rel);

    if (fresh == nullptr)
        return;

    if (fresh->sampleRate() != sampleRate || fresh->numOutputs() != numOut)
    {
        retire (fresh);
        return;
    }

    // The kernel that was ringing out gives way with a short fade.
    if (ringing != nullptr)
    {
        retire (fading);
        fading = ringing;
        fadeRemaining = fadeLength;
    }

    ringing = current;
    ringRemaining = ringing != nullptr ? ringing->length() : 0;
    current = fresh;

    // The living tail follows the new room at once, gliding to its new line
    // lengths. While it is holding a freeze it keeps the old room until the
    // freeze has faded.
    if (tailAudible())
        tailDesignPending = true;
    else
        livingTail.setDesign (current->livingTail());

    ready.store (true, std::memory_order_relaxed);
}

void ConvolutionReverb::updateFilters (const ReverbControls& c) noexcept
{
    const auto& last = lastFilterControls;

    if (filtersValid && ! differs (c.lowCutHz, last.lowCutHz) && ! differs (c.highCutHz, last.highCutHz)
        && ! differs (c.brightnessDb, last.brightnessDb) && ! differs (c.monoBelowHz, last.monoBelowHz))
        return;

    const auto lc = Biquad::highPass (sampleRate, c.lowCutHz);
    const auto hc = Biquad::lowPass (sampleRate, c.highCutHz);
    const auto ls = Biquad::lowShelf (sampleRate, 250.0, -0.5 * c.brightnessDb);
    const auto hs = Biquad::highShelf (sampleRate, 2500.0, 0.5 * c.brightnessDb);

    for (std::size_t ch = 0; ch < maxChannels; ++ch)
    {
        lowCut[ch].setCoefficients (lc);
        highCut[ch].setCoefficients (hc);
        lowShelf[ch].setCoefficients (ls);
        highShelf[ch].setCoefficients (hs);
    }

    if (c.monoBelowHz > 0.0f)
    {
        const auto spread = Biquad::highPass (sampleRate, c.monoBelowHz);

        for (auto& f : spreadHighPass)
            f.setCoefficients (spread);
    }

    lastFilterControls = c;
    filtersValid = true;
}

void ConvolutionReverb::updateFreeze (const ReverbControls& c) noexcept
{
    const auto xover = std::clamp (c.crossoverHz, 20.0f, 20000.0f);

    if (c.freeze != frozen)
    {
        frozen = c.freeze;
        crossover = xover;
        livingTail.setFrozen (frozen, crossover);
        releaseStep = static_cast<float> (1.0 / (static_cast<double> (livingTail.releaseSeconds()) * sampleRate));
    }
    else if (frozen && differs (xover, crossover))
    {
        crossover = xover;
        livingTail.setFrozen (true, crossover);
    }
    else
    {
        crossover = xover;
    }
}

void ConvolutionReverb::runKernels (std::size_t n) noexcept
{
    if (current != nullptr)
        current->process (wetInL.data(), wetInR.data(), wetPtr.data(), scratchPtr.data(), n);
    else
        for (std::size_t k = 0; k < numOut; ++k)
            std::fill_n (wetPtr[k], n, 0.0f);

    // The previous kernel keeps sounding its tail but takes no new input.
    if (ringing != nullptr)
    {
        ringing->process (zeros.data(), zeros.data(), tmpPtr.data(), scratchPtr.data(), n);

        for (std::size_t k = 0; k < numOut; ++k)
            for (std::size_t i = 0; i < n; ++i)
                wetPtr[k][i] += tmpPtr[k][i];

        ringRemaining = ringRemaining > n ? ringRemaining - n : 0;

        if (ringRemaining == 0)
        {
            retire (ringing);
            ringing = nullptr;
        }
    }

    if (fading != nullptr)
    {
        fading->process (zeros.data(), zeros.data(), tmpPtr.data(), scratchPtr.data(), n);

        const auto step = 1.0f / static_cast<float> (std::max<std::size_t> (fadeLength, 1));

        for (std::size_t k = 0; k < numOut; ++k)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                const auto g = static_cast<float> (fadeRemaining > i ? fadeRemaining - i : 0) * step;
                wetPtr[k][i] += tmpPtr[k][i] * g;
            }
        }

        fadeRemaining = fadeRemaining > n ? fadeRemaining - n : 0;

        if (fadeRemaining == 0)
        {
            retire (fading);
            fading = nullptr;
        }
    }
}

void ConvolutionReverb::delayDry (std::size_t wanted, std::size_t n) noexcept
{
    // Every input is written to its line, so a new delay always has history.
    if (wanted != dryTap && dryFade == 0)
    {
        dryFrom = dryTap;
        dryTap = wanted;
        dryFade = dryFadeLength;
    }

    const auto lineLength = dryMask + 1;
    constexpr auto quarter = static_cast<float> (std::numbers::pi / 2.0);
    const auto step = 1.0f / static_cast<float> (std::max<std::size_t> (dryFadeLength, 1));

    for (std::size_t in = 0; in < numIn; ++in)
    {
        auto* line = dryLines.data() + in * lineLength;
        auto* x = dry.data() + in * chunkSize;
        auto w = dryWrite;
        auto fade = dryFade;

        for (std::size_t i = 0; i < n; ++i)
        {
            line[w] = x[i];
            const auto now = line[(w - dryTap) & dryMask];

            if (fade > 0)
            {
                const auto g = static_cast<float> (dryFadeLength - fade) * step;
                x[i] = line[(w - dryFrom) & dryMask] * std::cos (g * quarter) + now * std::sin (g * quarter);
                --fade;
            }
            else
            {
                x[i] = now;
            }

            w = (w + 1) & dryMask;
        }
    }

    dryWrite = (dryWrite + n) & dryMask;
    dryFade = dryFade > n ? dryFade - n : 0;
}

void ConvolutionReverb::process (float* left, float* right, std::size_t numSamples, const ReverbControls& c) noexcept
{
    if (right != left)
    {
        float* channels[] { left, right };
        process (channels, 2, numSamples, c);
        return;
    }

    // Mono: the same signal on both inputs, and only the left output kept.
    for (std::size_t offset = 0; offset < numSamples; offset += chunkSize)
    {
        const auto n = std::min (chunkSize, numSamples - offset);
        std::copy_n (left + offset, n, monoCopy.data());
        float* channels[] { left + offset, monoCopy.data() };
        process (channels, 2, n, c);
    }
}

void ConvolutionReverb::process (float* const* channels, std::size_t numChannels, std::size_t numSamples,
                                 const ReverbControls& c) noexcept
{
    if (numChannels < std::max (numIn, numOut))
        return;

    installIncoming();
    updateFilters (c);
    updateFreeze (c);

    const auto mix = equalPowerMix (c.mix);
    const auto targetDelay = static_cast<float> (std::clamp (static_cast<double> (c.predelayMs) * 0.001, 0.0, maxPredelaySeconds) * sampleRate);
    const auto width = std::clamp (c.widthPercent, 0.0f, 200.0f) / 100.0f;
    const auto duckDepthDb = 24.0f * std::clamp (c.ducking, 0.0f, 1.0f);

    if (firstBlock)
    {
        dryGain = mix.dry;
        wetGain = mix.wet;
        outGain = c.outputGain;
        delaySamples = targetDelay;
        dryTap = current != nullptr ? current->latency() : 0;
        tailDelaySamples = static_cast<float> (livingTail.inputDelaySeconds() * sampleRate + static_cast<double> (dryTap));
        firstBlock = false;
    }

    for (std::size_t offset = 0; offset < numSamples; offset += chunkSize)
    {
        const auto n = std::min (chunkSize, numSamples - offset);

        for (std::size_t i = 0; i < numIn; ++i)
            std::copy_n (channels[i] + offset, n, dry.data() + i * chunkSize);

        // The stage sources, through the pre-delay, with the delay time
        // gliding so moving it never clicks. The living tail reads the same
        // lines a little further back, and later still by the kernel's
        // latency so it stays in time with the convolution.
        const auto kernelLatency = current != nullptr ? current->latency() : dryTap;
        const auto targetTailDelay = static_cast<float> (livingTail.inputDelaySeconds() * sampleRate + static_cast<double> (kernelLatency));

        for (std::size_t s = 0; s < n; ++s)
        {
            float l = 0.0f, r = 0.0f;

            for (std::size_t i = 0; i < numIn; ++i)
            {
                const auto x = dry[i * chunkSize + s];
                l += toLeftSource[i] * x;
                r += toRightSource[i] * x;
            }

            delayL[delayWrite] = l;
            delayR[delayWrite] = r;

            delaySamples = targetDelay + delayCoef * (delaySamples - targetDelay);
            const auto whole = static_cast<std::size_t> (delaySamples);
            const auto frac = delaySamples - static_cast<float> (whole);
            const auto a = (delayWrite - whole) & delayMask;
            const auto b = (a - 1) & delayMask;

            wetInL[s] = delayL[a] + frac * (delayL[b] - delayL[a]);
            wetInR[s] = delayR[a] + frac * (delayR[b] - delayR[a]);

            tailDelaySamples = targetTailDelay + delayCoef * (tailDelaySamples - targetTailDelay);
            const auto tailTotal = delaySamples + tailDelaySamples;
            const auto tailWhole = static_cast<std::size_t> (tailTotal);
            const auto tailFrac = tailTotal - static_cast<float> (tailWhole);
            const auto ta = (delayWrite - tailWhole) & delayMask;
            const auto tb = (ta - 1) & delayMask;

            tailInL[s] = delayL[ta] + tailFrac * (delayL[tb] - delayL[ta]);
            tailInR[s] = delayR[ta] + tailFrac * (delayR[tb] - delayR[ta]);
            delayWrite = (delayWrite + 1) & delayMask;
        }

        // While frozen, new sound reaches neither the convolution nor the
        // tail. Unless the tail is part of the sound, it starts following
        // the input again only once a released freeze has faded out.
        const auto livingTarget = current != nullptr ? current->livingLevel() : 0.0f;
        const auto convInputTarget = frozen ? 0.0f : 1.0f;
        const auto tailInputTarget = frozen || (tailPosition > 0.0f && livingTarget == 0.0f) ? 0.0f : 1.0f;

        for (std::size_t i = 0; i < n; ++i)
        {
            convInputGain = convInputTarget + inputCoef * (convInputGain - convInputTarget);
            tailInputGain = tailInputTarget + inputCoef * (tailInputGain - tailInputTarget);
            tailInL[i] *= tailInputGain;
            tailInR[i] *= tailInputGain;
            wetInL[i] *= convInputGain;
            wetInR[i] *= convInputGain;
        }

        runKernels (n);
        livingTail.setMovement (c.movement, c.movementRateHz);
        livingTail.process (tailInL.data(), tailInR.data(), tailPtr.data(), numOut, tailScale, n);

        // Crossfade between the convolution and the living tail. Out of a
        // freeze the tail still plays at the kernel's living level.
        if (frozen || tailPosition > 0.0f || convPosition < 1.0f || livingGain > 0.0f || livingTarget > 0.0f)
        {
            const auto convTarget = frozen ? 0.0f : 1.0f;
            const auto tailTarget = frozen ? 1.0f : 0.0f;
            const auto convStep = frozen ? freezeStep : returnStep;
            const auto tailStep = frozen ? freezeStep : releaseStep;
            constexpr auto quarter = static_cast<float> (std::numbers::pi / 2.0);
            const auto startConv = convPosition, startTail = tailPosition, startLiving = livingGain;

            for (std::size_t k = 0; k < numOut; ++k)
            {
                auto cp = startConv, tp = startTail, lg = startLiving;

                for (std::size_t i = 0; i < n; ++i)
                {
                    cp = cp < convTarget ? std::min (convTarget, cp + convStep) : std::max (convTarget, cp - convStep);
                    tp = tp < tailTarget ? std::min (tailTarget, tp + tailStep) : std::max (tailTarget, tp - tailStep);
                    lg = lg < livingTarget ? std::min (livingTarget, lg + livingStep) : std::max (livingTarget, lg - livingStep);
                    const auto held = std::sin (tp * quarter);
                    const auto tailWeight = lg > 0.0f ? 1.0f - (1.0f - held) * (1.0f - lg) : held;
                    wetPtr[k][i] = wetPtr[k][i] * std::sin (cp * quarter) + tailPtr[k][i] * tailWeight;
                }

                if (k + 1 == numOut)
                {
                    convPosition = cp;
                    tailPosition = tp;
                    livingGain = lg;
                }
            }

            if (! tailAudible() && tailDesignPending && current != nullptr)
            {
                livingTail.setDesign (current->livingTail());
                tailDesignPending = false;
            }
        }

        // Wet tone; the LFE channel carries no reverberation.
        for (std::size_t k = 0; k < numOut; ++k)
        {
            auto* w = wetPtr[k];

            if (! wetOutput[k])
            {
                std::fill_n (w, n, 0.0f);
                continue;
            }

            if (c.lowCutHz > 20.5f)
                lowCut[k].process (w, n);

            if (c.highCutHz < 19999.0f)
                highCut[k].process (w, n);

            if (std::abs (c.brightnessDb) > 0.01f)
            {
                lowShelf[k].process (w, n);
                highShelf[k].process (w, n);
            }
        }

        // Width and mono bass: each speaker's difference from the mean of all
        // of them is scaled (and high-passed). In stereo this is mid/side.
        if (numWet > 1)
        {
            const auto inverse = 1.0f / static_cast<float> (numWet);

            for (std::size_t i = 0; i < n; ++i)
            {
                float mean = 0.0f;

                for (std::size_t k = 0; k < numOut; ++k)
                    mean += wetOutput[k] ? wetPtr[k][i] : 0.0f;

                mean *= inverse;

                for (std::size_t k = 0; k < numOut; ++k)
                {
                    if (! wetOutput[k])
                        continue;

                    auto spread = wetPtr[k][i] - mean;

                    if (c.monoBelowHz > 0.0f)
                        spread = spreadHighPass[k].processSample (spread);

                    wetPtr[k][i] = mean + spread * width;
                }
            }
        }

        speakerTrim.process (wetPtr.data(), numOut, n, c.speakerLevelDb.data(), c.speakerDelayMs.data());

        // Ducking follows the dry level; then the mix and output gain, with
        // the per-sample gains kept in tmp for every output to share.
        auto* dryGains = tmpPtr[0];
        auto* wetGains = tmpPtr[1];
        auto* outGains = tmpPtr[2];

        for (std::size_t i = 0; i < n; ++i)
        {
            float level = 0.0f;

            for (std::size_t in = 0; in < numIn; ++in)
                level = std::max (level, std::abs (dry[in * chunkSize + i]));

            const auto coef = level > duckEnvelope ? attackCoef : releaseCoef;
            duckEnvelope = level + coef * (duckEnvelope - level);

            auto duckTarget = 1.0f;

            if (duckDepthDb > 0.0f)
            {
                const auto levelDb = 20.0f * std::log10 (std::max (duckEnvelope, 1.0e-6f));
                const auto amount = std::clamp ((levelDb + 40.0f) / 40.0f, 0.0f, 1.0f);
                duckTarget = std::pow (10.0f, -duckDepthDb * amount / 20.0f);
            }

            dryGain = mix.dry + gainCoef * (dryGain - mix.dry);
            wetGain = mix.wet + gainCoef * (wetGain - mix.wet);
            outGain = c.outputGain + gainCoef * (outGain - c.outputGain);
            duckGain = duckTarget + gainCoef * (duckGain - duckTarget);

            dryGains[i] = dryGain * outGain;
            wetGains[i] = wetGain * duckGain * outGain;
            outGains[i] = outGain;
        }

        delayDry (kernelLatency, n);

        for (std::size_t k = 0; k < numOut; ++k)
        {
            auto* out = channels[k] + offset;

            for (std::size_t i = 0; i < n; ++i)
            {
                float d = 0.0f;

                for (std::size_t in = 0; in < numIn; ++in)
                    d += dryRouting[k][in] * dry[in * chunkSize + i];

                out[i] = d * dryGains[i] + wetPtr[k][i] * wetGains[i];
            }
        }
    }

    if (std::abs (duckEnvelope) < 1.0e-20f)
        duckEnvelope = 0.0f;

    tail.store (frozen ? std::numeric_limits<double>::infinity()
                       : (current != nullptr ? static_cast<double> (current->length()) / sampleRate : 0.0) + maxPredelaySeconds * 0.25
                             + static_cast<double> (*std::max_element (c.speakerDelayMs.begin(), c.speakerDelayMs.end())) / 1000.0,
                std::memory_order_relaxed);
}

} // namespace drenched

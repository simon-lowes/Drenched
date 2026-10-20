#include "drenched/ConvolutionReverb.h"
#include "drenched/Mix.h"

#include <algorithm>
#include <cmath>

namespace drenched
{

namespace
{

std::vector<std::span<const float>> pathsFrom (const ImpulseResponseSet& set, ImpulseResponseSet::Path a, ImpulseResponseSet::Path b)
{
    return { std::span<const float> (set.paths[a]), std::span<const float> (set.paths[b]) };
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
TrueStereoKernel::TrueStereoKernel (const ImpulseResponseSet& responses)
    : rate (responses.sampleRate),
      irLength (responses.length()),
      fromLeft (pathsFrom (responses, ImpulseResponseSet::leftToLeft, ImpulseResponseSet::leftToRight)),
      fromRight (pathsFrom (responses, ImpulseResponseSet::rightToLeft, ImpulseResponseSet::rightToRight))
{
}

void TrueStereoKernel::reset() noexcept
{
    fromLeft.reset();
    fromRight.reset();
}

void TrueStereoKernel::process (const float* inL, const float* inR, float* outL, float* outR,
                                float* scratchL, float* scratchR, std::size_t numSamples) noexcept
{
    float* const toOut[] { outL, outR };
    float* const toScratch[] { scratchL, scratchR };

    fromLeft.process (inL, toOut, numSamples);
    fromRight.process (inR, toScratch, numSamples);

    for (std::size_t n = 0; n < numSamples; ++n)
    {
        outL[n] += scratchL[n];
        outR[n] += scratchR[n];
    }
}

//==============================================================================
ConvolutionReverb::ConvolutionReverb()
{
    for (auto* buffer : { &dryL, &dryR, &wetInL, &wetInR, &wetL, &wetR, &tmpL, &tmpR, &scratchL, &scratchR, &zeros })
        buffer->assign (chunkSize, 0.0f);

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

void ConvolutionReverb::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // Audio is stopped, so kernels can be dropped here directly.
    for (auto** k : { &current, &ringing, &fading })
    {
        if (*k != nullptr && (*k)->sampleRate() != sampleRate)
        {
            delete *k;
            *k = nullptr;
        }
    }

    ready.store (current != nullptr);
    tail.store (current != nullptr ? static_cast<double> (current->length()) / sampleRate : 0.0);

    const auto capacity = nextPowerOfTwo (static_cast<std::size_t> (maxPredelaySeconds * sampleRate) + chunkSize + 4);
    delayL.assign (capacity, 0.0f);
    delayR.assign (capacity, 0.0f);
    delayMask = capacity - 1;

    delayCoef = onePole (sampleRate, 0.05);
    gainCoef = onePole (sampleRate, 0.02);
    attackCoef = onePole (sampleRate, 0.01);
    releaseCoef = onePole (sampleRate, 0.25);
    fadeLength = static_cast<std::size_t> (0.04 * sampleRate);

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

    for (auto* k : { current, ringing, fading })
        if (k != nullptr)
            k->reset();

    for (auto* bank : { &lowCut, &highCut, &lowShelf, &highShelf })
        for (auto& f : *bank)
            f.reset();

    sideHighPass.reset();
}

void ConvolutionReverb::submit (std::unique_ptr<TrueStereoKernel> kernel)
{
    delete incoming.exchange (kernel.release(), std::memory_order_acq_rel);
}

std::unique_ptr<TrueStereoKernel> ConvolutionReverb::takeRetired() noexcept
{
    const auto read = retiredRead.load (std::memory_order_relaxed);

    if (read == retiredWrite.load (std::memory_order_acquire))
        return nullptr;

    auto* k = retired[read % retiredCapacity].exchange (nullptr, std::memory_order_relaxed);
    retiredRead.store (read + 1, std::memory_order_release);
    return std::unique_ptr<TrueStereoKernel> (k);
}

void ConvolutionReverb::retire (TrueStereoKernel* kernel) noexcept
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

    if (fresh->sampleRate() != sampleRate)
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

    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        lowCut[ch].setCoefficients (lc);
        highCut[ch].setCoefficients (hc);
        lowShelf[ch].setCoefficients (ls);
        highShelf[ch].setCoefficients (hs);
    }

    if (c.monoBelowHz > 0.0f)
        sideHighPass.setCoefficients (Biquad::highPass (sampleRate, c.monoBelowHz));

    lastFilterControls = c;
    filtersValid = true;
}

void ConvolutionReverb::runKernels (std::size_t n) noexcept
{
    if (current != nullptr)
        current->process (wetInL.data(), wetInR.data(), wetL.data(), wetR.data(), scratchL.data(), scratchR.data(), n);
    else
    {
        std::fill_n (wetL.data(), n, 0.0f);
        std::fill_n (wetR.data(), n, 0.0f);
    }

    // The previous kernel keeps sounding its tail but takes no new input.
    if (ringing != nullptr)
    {
        ringing->process (zeros.data(), zeros.data(), tmpL.data(), tmpR.data(), scratchL.data(), scratchR.data(), n);

        for (std::size_t i = 0; i < n; ++i)
        {
            wetL[i] += tmpL[i];
            wetR[i] += tmpR[i];
        }

        ringRemaining = ringRemaining > n ? ringRemaining - n : 0;

        if (ringRemaining == 0)
        {
            retire (ringing);
            ringing = nullptr;
        }
    }

    if (fading != nullptr)
    {
        fading->process (zeros.data(), zeros.data(), tmpL.data(), tmpR.data(), scratchL.data(), scratchR.data(), n);

        const auto step = 1.0f / static_cast<float> (std::max<std::size_t> (fadeLength, 1));

        for (std::size_t i = 0; i < n; ++i)
        {
            const auto g = static_cast<float> (fadeRemaining > i ? fadeRemaining - i : 0) * step;
            wetL[i] += tmpL[i] * g;
            wetR[i] += tmpR[i] * g;
        }

        fadeRemaining = fadeRemaining > n ? fadeRemaining - n : 0;

        if (fadeRemaining == 0)
        {
            retire (fading);
            fading = nullptr;
        }
    }
}

void ConvolutionReverb::process (float* left, float* right, std::size_t numSamples, const ReverbControls& c) noexcept
{
    installIncoming();
    updateFilters (c);

    const auto mix = equalPowerMix (c.mix);
    const auto targetDelay = static_cast<float> (std::clamp (static_cast<double> (c.predelayMs) * 0.001, 0.0, maxPredelaySeconds) * sampleRate);
    const auto width = std::clamp (c.widthPercent, 0.0f, 200.0f) / 100.0f;
    const auto duckDepthDb = 24.0f * std::clamp (c.ducking, 0.0f, 1.0f);
    const auto stereo = right != left;

    if (firstBlock)
    {
        dryGain = mix.dry;
        wetGain = mix.wet;
        outGain = c.outputGain;
        delaySamples = targetDelay;
        firstBlock = false;
    }

    for (std::size_t offset = 0; offset < numSamples; offset += chunkSize)
    {
        const auto n = std::min (chunkSize, numSamples - offset);
        auto* L = left + offset;
        auto* R = right + offset;

        std::copy_n (L, n, dryL.data());
        std::copy_n (stereo ? R : L, n, dryR.data());

        // Pre-delay, with the delay time gliding so moving it never clicks.
        for (std::size_t i = 0; i < n; ++i)
        {
            delayL[delayWrite] = dryL[i];
            delayR[delayWrite] = dryR[i];

            delaySamples = targetDelay + delayCoef * (delaySamples - targetDelay);
            const auto whole = static_cast<std::size_t> (delaySamples);
            const auto frac = delaySamples - static_cast<float> (whole);
            const auto a = (delayWrite - whole) & delayMask;
            const auto b = (a - 1) & delayMask;

            wetInL[i] = delayL[a] + frac * (delayL[b] - delayL[a]);
            wetInR[i] = delayR[a] + frac * (delayR[b] - delayR[a]);
            delayWrite = (delayWrite + 1) & delayMask;
        }

        runKernels (n);

        // Wet tone.
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            auto* w = ch == 0 ? wetL.data() : wetR.data();

            if (c.lowCutHz > 20.5f)
                lowCut[ch].process (w, n);

            if (c.highCutHz < 19999.0f)
                highCut[ch].process (w, n);

            if (std::abs (c.brightnessDb) > 0.01f)
            {
                lowShelf[ch].process (w, n);
                highShelf[ch].process (w, n);
            }
        }

        // Width and mono bass, in mid/side.
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto mid = 0.5f * (wetL[i] + wetR[i]);
            auto side = 0.5f * (wetL[i] - wetR[i]);

            if (c.monoBelowHz > 0.0f)
                side = sideHighPass.processSample (side);

            side *= width;
            wetL[i] = mid + side;
            wetR[i] = mid - side;
        }

        // Ducking follows the dry level; then the mix and output gain.
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto level = std::max (std::abs (dryL[i]), std::abs (dryR[i]));
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

            const auto wg = wetGain * duckGain;
            L[i] = (dryL[i] * dryGain + wetL[i] * wg) * outGain;

            if (stereo)
                R[i] = (dryR[i] * dryGain + wetR[i] * wg) * outGain;
        }
    }

    if (std::abs (duckEnvelope) < 1.0e-20f)
        duckEnvelope = 0.0f;

    tail.store ((current != nullptr ? static_cast<double> (current->length()) / sampleRate : 0.0) + maxPredelaySeconds * 0.25,
                std::memory_order_relaxed);
}

} // namespace drenched

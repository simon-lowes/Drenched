#pragma once

#include "drenched/ResponseAnalysis.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace drenched
{

/** A live spectrum of a signal: the audio thread pushes samples, and any
    other thread reads third-octave levels of the latest few thousand.

    push() is real-time safe and lock-free. read() allocates nothing after the
    first call but runs an FFT, so call it from a timer, not the audio thread.
    One thread pushes and one reads. */
class SpectrumTap
{
public:
    static constexpr std::size_t fftSize = 4096;
    static constexpr float floorDb = -100.0f;

    using Levels = std::array<float, analysisBands.size()>;

    SpectrumTap();
    ~SpectrumTap();

    SpectrumTap (const SpectrumTap&) = delete;
    SpectrumTap& operator= (const SpectrumTap&) = delete;

    /** The mean of `numChannels` channels of `n` samples. */
    void push (const float* const* channels, std::size_t numChannels, std::size_t n) noexcept;

    /** Each band's level in dB, where a full-scale sine reads 0 dB in its band. */
    Levels read (double sampleRate);

private:
    struct Fft;

    std::array<std::atomic<float>, fftSize> ring {};
    std::atomic<std::size_t> written { 0 };
    std::unique_ptr<Fft> fft;
};

} // namespace drenched

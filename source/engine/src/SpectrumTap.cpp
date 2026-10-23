#include "drenched/SpectrumTap.h"

#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace drenched
{

struct SpectrumTap::Fft
{
    Fft()
        : setup (pffft_new_setup (static_cast<int> (fftSize), PFFFT_REAL)),
          input (static_cast<float*> (pffft_aligned_malloc (fftSize * sizeof (float)))),
          output (static_cast<float*> (pffft_aligned_malloc (fftSize * sizeof (float)))),
          work (static_cast<float*> (pffft_aligned_malloc (fftSize * sizeof (float)))),
          window (fftSize)
    {
        double sumOfSquares = 0.0;

        for (std::size_t n = 0; n < fftSize; ++n)
        {
            window[n] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (n) / fftSize));
            sumOfSquares += static_cast<double> (window[n]) * window[n];
        }

        // A sine of amplitude A puts N * A^2 / 4 * sum(w^2) into the positive
        // bins (Parseval), so this scale makes a full-scale sine 1.
        scale = 4.0 / (static_cast<double> (fftSize) * sumOfSquares);
    }

    ~Fft()
    {
        pffft_aligned_free (input);
        pffft_aligned_free (output);
        pffft_aligned_free (work);
        pffft_destroy_setup (setup);
    }

    PFFFT_Setup* setup;
    float* input;
    float* output;
    float* work;
    std::vector<float> window;
    double scale = 1.0;
};

SpectrumTap::SpectrumTap() : fft (std::make_unique<Fft>()) {}
SpectrumTap::~SpectrumTap() = default;

void SpectrumTap::push (const float* const* channels, std::size_t numChannels, std::size_t n) noexcept
{
    if (numChannels == 0)
        return;

    auto at = written.load (std::memory_order_relaxed);
    const auto inverse = 1.0f / static_cast<float> (numChannels);

    for (std::size_t i = 0; i < n; ++i, ++at)
    {
        float sum = 0.0f;

        for (std::size_t c = 0; c < numChannels; ++c)
            sum += channels[c][i];

        ring[at % fftSize].store (sum * inverse, std::memory_order_relaxed);
    }

    written.store (at, std::memory_order_release);
}

SpectrumTap::Levels SpectrumTap::read (double sampleRate)
{
    const auto end = written.load (std::memory_order_acquire);

    for (std::size_t n = 0; n < fftSize; ++n)
        fft->input[n] = ring[(end + n) % fftSize].load (std::memory_order_relaxed) * fft->window[n];

    pffft_transform_ordered (fft->setup, fft->input, fft->output, fft->work, PFFFT_FORWARD);

    // Power per bin; pffft's ordered real output is DC, Nyquist, then (re, im) pairs.
    const auto binHz = sampleRate / static_cast<double> (fftSize);
    const auto powerOf = [this] (std::size_t k)
    {
        const auto re = static_cast<double> (fft->output[2 * k]), im = static_cast<double> (fft->output[2 * k + 1]);
        return re * re + im * im;
    };

    Levels levels;
    const auto edge = std::pow (2.0, 1.0 / 6.0);

    for (std::size_t b = 0; b < analysisBands.size(); ++b)
    {
        const auto centre = static_cast<double> (analysisBands[b]);
        const auto lo = static_cast<std::size_t> (std::ceil (centre / edge / binHz));
        const auto hi = std::min (fftSize / 2 - 1, static_cast<std::size_t> (std::floor (centre * edge / binHz)));
        double power = 0.0;

        if (centre >= sampleRate * 0.5)
            power = 0.0;
        else if (lo <= hi && lo >= 1)
            for (auto k = lo; k <= hi; ++k)
                power += powerOf (k);
        else
            // Narrower than a bin: the nearest bin's power, shared by the band's width.
            power = powerOf (std::clamp<std::size_t> (static_cast<std::size_t> (std::lround (centre / binHz)), 1, fftSize / 2 - 1))
                  * (centre * (edge - 1.0 / edge)) / binHz;

        const auto level = power * fft->scale;
        levels[b] = level > 0.0 ? std::max (static_cast<float> (10.0 * std::log10 (level)), floorDb) : floorDb;
    }

    return levels;
}

} // namespace drenched

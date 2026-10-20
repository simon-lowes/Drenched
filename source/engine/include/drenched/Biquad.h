#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace drenched
{

/** A second-order IIR section (transposed direct form II) with the usual
    Audio EQ Cookbook designs. Real-time safe; designing is a handful of trig
    calls, cheap enough to do once per block when a control moves. */
class Biquad
{
public:
    struct Coefficients
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    };

    static Coefficients lowPass (double sampleRate, double frequency, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        const auto [w, cosw, alpha] = prewarp (sampleRate, frequency, q);
        return normalise ((1.0 - cosw) / 2.0, 1.0 - cosw, (1.0 - cosw) / 2.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    static Coefficients highPass (double sampleRate, double frequency, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        const auto [w, cosw, alpha] = prewarp (sampleRate, frequency, q);
        return normalise ((1.0 + cosw) / 2.0, -(1.0 + cosw), (1.0 + cosw) / 2.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    static Coefficients lowShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto [w, cosw, alpha] = prewarp (sampleRate, frequency, std::numbers::sqrt2 / 2.0);
        const auto s = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1.0) - (A - 1.0) * cosw + s),
                          2.0 * A * ((A - 1.0) - (A + 1.0) * cosw),
                          A * ((A + 1.0) - (A - 1.0) * cosw - s),
                          (A + 1.0) + (A - 1.0) * cosw + s,
                          -2.0 * ((A - 1.0) + (A + 1.0) * cosw),
                          (A + 1.0) + (A - 1.0) * cosw - s);
    }

    static Coefficients highShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto [w, cosw, alpha] = prewarp (sampleRate, frequency, std::numbers::sqrt2 / 2.0);
        const auto s = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1.0) + (A - 1.0) * cosw + s),
                          -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw),
                          A * ((A + 1.0) + (A - 1.0) * cosw - s),
                          (A + 1.0) - (A - 1.0) * cosw + s,
                          2.0 * ((A - 1.0) - (A + 1.0) * cosw),
                          (A + 1.0) - (A - 1.0) * cosw - s);
    }

    void setCoefficients (const Coefficients& c) noexcept { coeffs = c; }
    void reset() noexcept { z1 = z2 = 0.0f; }

    float processSample (float x) noexcept
    {
        const auto y = coeffs.b0 * x + z1;
        z1 = coeffs.b1 * x - coeffs.a1 * y + z2;
        z2 = coeffs.b2 * x - coeffs.a2 * y;
        return y;
    }

    void process (float* data, std::size_t numSamples) noexcept
    {
        for (std::size_t n = 0; n < numSamples; ++n)
            data[n] = processSample (data[n]);

        // Flush denormals that a decaying tail can leave in the state.
        if (std::abs (z1) < 1.0e-20f) z1 = 0.0f;
        if (std::abs (z2) < 1.0e-20f) z2 = 0.0f;
    }

private:
    struct Warp { double w, cosw, alpha; };

    static Warp prewarp (double sampleRate, double frequency, double q) noexcept
    {
        const auto f = std::clamp (frequency, 1.0, sampleRate * 0.49);
        const auto w = 2.0 * std::numbers::pi * f / sampleRate;
        return { w, std::cos (w), std::sin (w) / (2.0 * q) };
    }

    static Coefficients normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { static_cast<float> (b0 / a0), static_cast<float> (b1 / a0), static_cast<float> (b2 / a0),
                 static_cast<float> (a1 / a0), static_cast<float> (a2 / a0) };
    }

    Coefficients coeffs;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace drenched

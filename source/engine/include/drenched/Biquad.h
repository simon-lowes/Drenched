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

    /** A bell around `frequency`: `gainDb` at the centre, `q` its narrowness. */
    static Coefficients peak (double sampleRate, double frequency, double gainDb, double q) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto [w, cosw, alpha] = prewarp (sampleRate, frequency, q);
        return normalise (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
    }

    void setCoefficients (const Coefficients& c) noexcept { coeffs = c; }
    void reset() noexcept { z1 = z2 = 0.0f; }

    /** Zeroes state too small to matter, so a decaying tail never leaves
        denormals behind. For filters run sample by sample. */
    void flushDenormals() noexcept
    {
        if (std::abs (z1) < 1.0e-20f) z1 = 0.0f;
        if (std::abs (z2) < 1.0e-20f) z2 = 0.0f;
    }

    /** The magnitude response in dB at one frequency. */
    static double magnitudeDb (const Coefficients& c, double sampleRate, double frequency) noexcept
    {
        const auto w = 2.0 * std::numbers::pi * frequency / sampleRate;
        const auto c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const auto nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
        const auto dr = 1.0 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
        return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

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
        flushDenormals();
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

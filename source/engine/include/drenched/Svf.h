#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace drenched
{

/** A state-variable filter (Andrew Simper's linear trapezoidal SVF) with the
    same responses as Biquad's designs. Its state is the signal inside the
    filter rather than the last few output samples, so its settings can move
    while sound plays without clicking: the wet tone and EQ use it for that.
    Real-time safe. */
class Svf
{
public:
    struct Coefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f; // the integrators
        float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f; // how much input, band and low pass make the output
    };

    static Coefficients lowPass (double sampleRate, double frequency, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        return make (warp (sampleRate, frequency), 1.0 / q, 0.0, 0.0, 1.0);
    }

    static Coefficients highPass (double sampleRate, double frequency, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        const auto k = 1.0 / q;
        return make (warp (sampleRate, frequency), k, 1.0, -k, -1.0);
    }

    static Coefficients lowShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto k = std::numbers::sqrt2;
        return make (warp (sampleRate, frequency) / std::sqrt (A), k, 1.0, k * (A - 1.0), A * A - 1.0);
    }

    static Coefficients highShelf (double sampleRate, double frequency, double gainDb) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto k = std::numbers::sqrt2;
        return make (warp (sampleRate, frequency) * std::sqrt (A), k, A * A, k * (1.0 - A) * A, 1.0 - A * A);
    }

    /** A bell around `frequency`: `gainDb` at the centre, `q` its narrowness. */
    static Coefficients peak (double sampleRate, double frequency, double gainDb, double q) noexcept
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto k = 1.0 / (q * A);
        return make (warp (sampleRate, frequency), k, 1.0, k * (A * A - 1.0), 0.0);
    }

    void setCoefficients (const Coefficients& c) noexcept
    {
        coeffs = c;
        remaining = 0;
    }

    /** Moves to new coefficients a little every sample over the next `samples`,
        so that even a large change in the response is smooth. */
    void glideTo (const Coefficients& c, std::size_t samples) noexcept
    {
        if (samples == 0)
        {
            setCoefficients (c);
            return;
        }

        const auto r = 1.0f / static_cast<float> (samples);
        target = c;
        step = { (c.a1 - coeffs.a1) * r, (c.a2 - coeffs.a2) * r, (c.a3 - coeffs.a3) * r,
                 (c.m0 - coeffs.m0) * r, (c.m1 - coeffs.m1) * r, (c.m2 - coeffs.m2) * r };
        remaining = samples;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** Zeroes state too small to matter, so a decaying tail never leaves denormals behind. */
    void flushDenormals() noexcept
    {
        if (std::abs (ic1) < 1.0e-20f) ic1 = 0.0f;
        if (std::abs (ic2) < 1.0e-20f) ic2 = 0.0f;
    }

    float processSample (float x) noexcept
    {
        if (remaining > 0)
            moveOn();

        const auto v3 = x - ic2;
        const auto v1 = coeffs.a1 * ic1 + coeffs.a2 * v3;
        const auto v2 = ic2 + coeffs.a2 * ic1 + coeffs.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return coeffs.m0 * x + coeffs.m1 * v1 + coeffs.m2 * v2;
    }

    void process (float* data, std::size_t numSamples) noexcept
    {
        for (std::size_t n = 0; n < numSamples; ++n)
            data[n] = processSample (data[n]);

        flushDenormals();
    }

private:
    void moveOn() noexcept
    {
        if (--remaining == 0)
        {
            coeffs = target;
            return;
        }

        coeffs.a1 += step.a1;
        coeffs.a2 += step.a2;
        coeffs.a3 += step.a3;
        coeffs.m0 += step.m0;
        coeffs.m1 += step.m1;
        coeffs.m2 += step.m2;
    }

    static double warp (double sampleRate, double frequency) noexcept
    {
        const auto f = std::clamp (frequency, 1.0, sampleRate * 0.49);
        return std::tan (std::numbers::pi * f / sampleRate);
    }

    static Coefficients make (double g, double k, double m0, double m1, double m2) noexcept
    {
        const auto a1 = 1.0 / (1.0 + g * (g + k));
        const auto a2 = g * a1;
        return { static_cast<float> (a1), static_cast<float> (a2), static_cast<float> (g * a2),
                 static_cast<float> (m0), static_cast<float> (m1), static_cast<float> (m2) };
    }

    Coefficients coeffs, target, step;
    std::size_t remaining = 0;
    float ic1 = 0.0f, ic2 = 0.0f;
};

} // namespace drenched

#pragma once

// Octave-band helpers shared by the impulse-response synthesiser and the
// recorded-response analysis. Internal to the engine, offline only: these
// allocate and run over whole responses.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace drenched::bands
{

/** Crossovers between the octave bands: geometric means of neighbouring centres. */
inline constexpr std::array<double, 7> crossovers { 88.39, 176.78, 353.55, 707.11, 1414.21, 2828.43, 5656.85 };

/** Second-order Butterworth low-pass (bilinear transform), run forwards then
    backwards. The result is zero phase with magnitude |H|^2, and since
    |H_lp|^2 + |H_hp|^2 = 1 for a Butterworth pair, x minus this low band is
    exactly the matching high band: the split reconstructs perfectly. */
inline void zeroPhaseLowPass (const std::vector<float>& in, std::vector<float>& out, double cutoff, double sampleRate)
{
    const auto k = std::tan (std::numbers::pi * std::min (cutoff, sampleRate * 0.45) / sampleRate);
    const auto q = std::numbers::sqrt2 / 2.0;
    const auto norm = 1.0 / (1.0 + k / q + k * k);
    const auto b0 = k * k * norm, b1 = 2.0 * b0, b2 = b0;
    const auto a1 = 2.0 * (k * k - 1.0) * norm, a2 = (1.0 - k / q + k * k) * norm;

    out.resize (in.size());

    double z1 = 0.0, z2 = 0.0;

    for (std::size_t n = 0; n < in.size(); ++n)
    {
        const double x = in[n];
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        out[n] = static_cast<float> (y);
    }

    z1 = z2 = 0.0;

    for (std::size_t n = in.size(); n-- > 0;)
    {
        const double x = out[n];
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        out[n] = static_cast<float> (y);
    }
}

/** Extracts one band of x. */
inline std::vector<float> bandOf (std::vector<float> x, std::size_t wanted, double sampleRate)
{
    std::vector<float> low;

    for (std::size_t band = 0; band < crossovers.size(); ++band)
    {
        zeroPhaseLowPass (x, low, crossovers[band], sampleRate);

        if (band == wanted)
            return low;

        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] -= low[n];
    }

    return x;
}

/** Splits x into all eight bands at once. The bands sum back to x exactly
    (to rounding). */
inline std::array<std::vector<float>, 8> split (std::vector<float> x, double sampleRate)
{
    std::array<std::vector<float>, 8> out;

    for (std::size_t band = 0; band < crossovers.size(); ++band)
    {
        zeroPhaseLowPass (x, out[band], crossovers[band], sampleRate);

        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] -= out[band][n];
    }

    out.back() = std::move (x);
    return out;
}

inline double raisedCosine (double t) noexcept
{
    t = std::clamp (t, 0.0, 1.0);
    return 0.5 - 0.5 * std::cos (std::numbers::pi * t);
}

inline double energyOf (const std::vector<float>& x) noexcept
{
    double sum = 0.0;

    for (auto v : x)
        sum += static_cast<double> (v) * v;

    return sum;
}

} // namespace drenched::bands

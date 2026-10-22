#include "drenched/ReverbCloud.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace drenched
{

namespace
{

constexpr double listenSeconds = 0.1; // how far back the loudness is weighed
constexpr double sizeSeconds = 0.05;  // how fast a change of size glides
constexpr float silence = 1.0e-8f;    // -80 dB: quieter than this, the direction holds
constexpr float sharpest = 8.0f;      // at 0 %, a speaker 60 degrees away plays 20 dB down

std::array<float, 3> directionOf (const Speaker& s) noexcept
{
    constexpr auto radians = static_cast<float> (std::numbers::pi / 180.0);
    const auto az = s.azimuth * radians, el = s.elevation * radians;
    return { std::sin (az) * std::cos (el), std::cos (az) * std::cos (el), std::sin (el) };
}

} // namespace

void ReverbCloud::prepare (double newSampleRate, const SpeakerLayout& inputs, const SpeakerLayout& outputs) noexcept
{
    sampleRate = newSampleRate;
    numIn = std::min (inputs.size(), maxChannels);
    numOut = std::min (outputs.size(), maxChannels);
    numWet = 0;

    for (std::size_t i = 0; i < numIn; ++i)
    {
        inputHeard[i] = ! inputs[i].lfe;
        inputDirections[i] = directionOf (inputs[i]);
    }

    for (std::size_t k = 0; k < numOut; ++k)
    {
        outputWet[k] = ! outputs[k].lfe;
        outputDirections[k] = directionOf (outputs[k]);
        numWet += outputWet[k] ? 1 : 0;
    }

    reset();
}

void ReverbCloud::reset() noexcept
{
    energy.fill (0.0f);
    gains.fill (1.0f);
    heading = {};
    size = 100.0f;
}

bool ReverbCloud::isNeutral() const noexcept
{
    return size >= 100.0f && std::all_of (gains.begin(), gains.begin() + static_cast<std::ptrdiff_t> (numOut), [] (float g) { return g == 1.0f; });
}

void ReverbCloud::listen (const float* const* inputs, std::size_t numInputs, std::size_t n, bool hold) noexcept
{
    if (n == 0)
        return;

    const auto coef = static_cast<float> (std::exp (-static_cast<double> (n) / (listenSeconds * sampleRate)));
    Vector sum {};
    float total = 0.0f;

    for (std::size_t i = 0; i < std::min (numInputs, numIn); ++i)
    {
        if (! inputHeard[i])
            continue;

        float meanSquare = 0.0f;

        for (std::size_t s = 0; s < n; ++s)
            meanSquare += inputs[i][s] * inputs[i][s];

        meanSquare /= static_cast<float> (n);
        energy[i] = meanSquare + coef * (energy[i] - meanSquare);
        total += energy[i];

        for (std::size_t axis = 0; axis < 3; ++axis)
            sum[axis] += energy[i] * inputDirections[i][axis];
    }

    if (hold || total < silence)
        return;

    for (std::size_t axis = 0; axis < 3; ++axis)
        heading[axis] = sum[axis] / total;
}

void ReverbCloud::process (float* const* wet, std::size_t numOutputs, std::size_t n, float cloudPercent) noexcept
{
    const auto target = std::clamp (cloudPercent, 0.0f, 100.0f);

    if (n == 0 || numWet == 0 || (target >= 100.0f && isNeutral()))
        return;

    const auto coef = static_cast<float> (std::exp (-static_cast<double> (n) / (sizeSeconds * sampleRate)));
    size = target + coef * (size - target);

    if (std::abs (size - target) < 0.01f)
        size = target;

    // How sharply the reverb gathers: not at all at 100 %, nor for a sound
    // that comes from everywhere.
    const auto focus = std::sqrt (heading[0] * heading[0] + heading[1] * heading[1] + heading[2] * heading[2]);
    const auto sharpness = focus > 1.0e-6f ? sharpest * (1.0f - size / 100.0f) * std::min (focus, 1.0f) : 0.0f;

    std::array<float, maxChannels> wanted {};
    float power = 0.0f;

    for (std::size_t k = 0; k < numOut; ++k)
    {
        if (! outputWet[k])
            continue;

        if (sharpness == 0.0f)
        {
            wanted[k] = 1.0f;
        }
        else
        {
            const auto& d = outputDirections[k];
            const auto facing = (d[0] * heading[0] + d[1] * heading[1] + d[2] * heading[2]) / focus;
            wanted[k] = std::pow (std::max (0.0f, 0.5f * (1.0f + facing)), sharpness);
        }

        power += wanted[k] * wanted[k];
    }

    // The same total level, however gathered.
    const auto scale = sharpness == 0.0f || power < 1.0e-12f ? 1.0f : std::sqrt (static_cast<float> (numWet) / power);

    for (std::size_t k = 0; k < std::min (numOutputs, numOut); ++k)
    {
        if (! outputWet[k])
            continue;

        const auto to = sharpness == 0.0f || power < 1.0e-12f ? 1.0f : wanted[k] * scale;
        const auto from = gains[k];

        if (from != to || from != 1.0f)
        {
            const auto step = (to - from) / static_cast<float> (n);

            for (std::size_t s = 0; s < n; ++s)
                wet[k][s] *= from + step * static_cast<float> (s + 1);
        }

        gains[k] = to;
    }
}

} // namespace drenched

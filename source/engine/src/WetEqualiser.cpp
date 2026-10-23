#include "drenched/WetEqualiser.h"

#include <algorithm>
#include <cmath>

namespace drenched
{

namespace
{

bool differs (const EqBand& a, const EqBand& b) noexcept
{
    return std::abs (a.frequencyHz - b.frequencyHz) > 1.0e-3f || std::abs (a.gainDb - b.gainDb) > 1.0e-4f
        || std::abs (a.q - b.q) > 1.0e-4f;
}

} // namespace

Biquad::Coefficients WetEqualiser::coefficientsOf (std::size_t band, const EqBand& settings, double sampleRate) noexcept
{
    const auto f = static_cast<double> (settings.frequencyHz);
    const auto g = static_cast<double> (std::clamp (settings.gainDb, minGainDb, maxGainDb));
    const auto q = static_cast<double> (std::clamp (settings.q, minQ, maxQ));

    switch (shapeOf (band))
    {
        case Shape::lowShelf:  return Biquad::lowShelf (sampleRate, f, g);
        case Shape::highShelf: return Biquad::highShelf (sampleRate, f, g);
        case Shape::bell:      break;
    }

    return Biquad::peak (sampleRate, f, g, q);
}

void WetEqualiser::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    valid = false;
    setBands (bands);
    reset();
}

void WetEqualiser::reset() noexcept
{
    for (auto& channel : filters)
        for (auto& f : channel)
            f.reset();
}

void WetEqualiser::setBands (const Bands& newBands) noexcept
{
    for (std::size_t b = 0; b < numBands; ++b)
    {
        if (valid && ! differs (newBands[b], bands[b]))
            continue;

        // A band coming back into use starts from silence, not from where it stopped.
        const auto waking = valid && ! isActive (bands[b]) && isActive (newBands[b]);

        bands[b] = newBands[b];
        coefficients[b] = coefficientsOf (b, bands[b], sampleRate);

        for (auto& channel : filters)
        {
            channel[b].setCoefficients (coefficients[b]);

            if (waking)
                channel[b].reset();
        }
    }

    valid = true;
}

void WetEqualiser::process (std::size_t channel, float* data, std::size_t n) noexcept
{
    auto& chain = filters[std::min (channel, maxChannels - 1)];

    for (std::size_t b = 0; b < numBands; ++b)
        if (isActive (bands[b]))
            chain[b].process (data, n);
}

bool WetEqualiser::isFlat() const noexcept
{
    return std::none_of (bands.begin(), bands.end(), isActive);
}

double WetEqualiser::magnitudeDb (double frequency) const noexcept
{
    double total = 0.0;

    for (std::size_t b = 0; b < numBands; ++b)
        if (isActive (bands[b]))
            total += Biquad::magnitudeDb (coefficients[b], sampleRate, frequency);

    return total;
}

bool WetEqualiser::isActive (const EqBand& band) noexcept
{
    return std::abs (band.gainDb) >= 0.01f;
}

} // namespace drenched

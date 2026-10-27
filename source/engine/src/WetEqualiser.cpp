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
    glideCoef = glide::coefficient (sampleRate, 0.02);
    reset();
}

void WetEqualiser::reset() noexcept
{
    bands = targets;

    for (std::size_t b = 0; b < numBands; ++b)
        design (b, 0);

    for (auto& channel : filters)
        for (auto& f : channel)
            f.reset();

    heardSinceReset = false;
}

void WetEqualiser::setBands (const Bands& newBands) noexcept
{
    targets = newBands;

    if (! heardSinceReset)
    {
        for (std::size_t b = 0; b < numBands; ++b)
        {
            if (differs (bands[b], targets[b]))
            {
                bands[b] = targets[b];
                design (b, 0);
            }
        }
    }
}

void WetEqualiser::advance() noexcept
{
    for (std::size_t b = 0; b < numBands; ++b)
    {
        auto& now = bands[b];
        const auto& goal = targets[b];

        if (now.frequencyHz == goal.frequencyHz && now.gainDb == goal.gainDb && now.q == goal.q)
            continue;

        // A band coming back into use starts from silence, not from where it
        // stopped, and from 0 dB, where its frequency and Q make no difference.
        if (! isActive (now))
        {
            now.frequencyHz = goal.frequencyHz;
            now.q = goal.q;

            for (auto& channel : filters)
                channel[b].reset();
        }

        now.gainDb = glide::towards (now.gainDb, goal.gainDb, glideCoef);
        now.frequencyHz = glide::towardsRatio (now.frequencyHz, goal.frequencyHz, glideCoef);
        now.q = glide::towardsRatio (now.q, goal.q, glideCoef);
        design (b, glide::block);
    }
}

void WetEqualiser::design (std::size_t band, std::size_t ramp) noexcept
{
    const auto& b = bands[band];
    coefficients[band] = coefficientsOf (band, b, sampleRate);

    // The same response, played by filters whose settings can glide.
    const auto f = static_cast<double> (b.frequencyHz);
    const auto g = static_cast<double> (std::clamp (b.gainDb, minGainDb, maxGainDb));
    const auto q = static_cast<double> (std::clamp (b.q, minQ, maxQ));
    const auto shape = shapeOf (band);
    const auto svf = shape == Shape::lowShelf ? Svf::lowShelf (sampleRate, f, g)
                   : shape == Shape::highShelf ? Svf::highShelf (sampleRate, f, g)
                                               : Svf::peak (sampleRate, f, g, q);

    for (auto& channel : filters)
        channel[band].glideTo (svf, ramp);
}

void WetEqualiser::process (std::size_t channel, float* data, std::size_t n) noexcept
{
    auto& chain = filters[std::min (channel, maxChannels - 1)];
    heardSinceReset = true;

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

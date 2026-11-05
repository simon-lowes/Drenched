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
        || std::abs (a.q - b.q) > 1.0e-4f || a.shape != b.shape;
}

} // namespace

Biquad::Coefficients WetEqualiser::coefficientsOf (const EqBand& settings, double sampleRate) noexcept
{
    const auto f = static_cast<double> (settings.frequencyHz);
    const auto g = static_cast<double> (std::clamp (settings.gainDb, minGainDb, maxGainDb));
    const auto q = static_cast<double> (std::clamp (settings.q, minQ, maxQ));

    switch (settings.shape)
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

    for (auto& f : soloFilters)
        f.reset();

    soloHeard = soloFrom = solo == noSolo ? 0.0f : 1.0f;
    designSolo (0);
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

void WetEqualiser::setSolo (int band) noexcept
{
    solo = band >= 0 && band < static_cast<int> (numBands) ? band : noSolo;

    if (! heardSinceReset)
    {
        soloHeard = soloFrom = solo == noSolo ? 0.0f : 1.0f;
        designSolo (0);
    }
}

void WetEqualiser::advance() noexcept
{
    for (std::size_t b = 0; b < numBands; ++b)
    {
        auto& now = bands[b];
        const auto& goal = targets[b];

        if (now.frequencyHz == goal.frequencyHz && now.gainDb == goal.gainDb && now.q == goal.q && now.shape == goal.shape)
            continue;

        // A band coming back into use starts from silence, not from where it
        // stopped, and from 0 dB, where its frequency, Q and shape make no difference.
        if (! isActive (now))
        {
            now.frequencyHz = goal.frequencyHz;
            now.q = goal.q;
            now.shape = goal.shape;

            for (auto& channel : filters)
                channel[b].reset();
        }
        else if (now.shape != goal.shape)
        {
            // A new shape at the same gain: the filter morphs over the block.
            now.shape = goal.shape;
        }

        now.gainDb = glide::towards (now.gainDb, goal.gainDb, glideCoef);
        now.frequencyHz = glide::towardsRatio (now.frequencyHz, goal.frequencyHz, glideCoef);
        now.q = glide::towardsRatio (now.q, goal.q, glideCoef);
        design (b, glide::block);
    }

    // The solo fades in and out; its filter follows the band as heard.
    soloFrom = soloHeard;
    const auto soloGoal = solo == noSolo ? 0.0f : 1.0f;

    if (soloHeard != soloGoal)
    {
        soloHeard = glide::towards (soloHeard, soloGoal, glideCoef);

        if (std::abs (soloHeard - soloGoal) < 1.0e-3f)
            soloHeard = soloGoal;
    }

    if (solo != noSolo)
    {
        // A solo starting from silence takes its filter at once, with clean state.
        const auto fresh = soloFrom <= 0.0f && soloDesigned == noSolo;

        if (fresh)
            for (auto& f : soloFilters)
                f.reset();

        designSolo (fresh ? 0 : glide::block);
    }
    else if (soloHeard <= 0.0f)
    {
        soloDesigned = noSolo;
    }
}

void WetEqualiser::design (std::size_t band, std::size_t ramp) noexcept
{
    const auto& b = bands[band];
    coefficients[band] = coefficientsOf (b, sampleRate);

    // The same response, played by filters whose settings can glide.
    const auto f = static_cast<double> (b.frequencyHz);
    const auto g = static_cast<double> (std::clamp (b.gainDb, minGainDb, maxGainDb));
    const auto q = static_cast<double> (std::clamp (b.q, minQ, maxQ));
    const auto svf = b.shape == Shape::lowShelf ? Svf::lowShelf (sampleRate, f, g)
                   : b.shape == Shape::highShelf ? Svf::highShelf (sampleRate, f, g)
                                                 : Svf::peak (sampleRate, f, g, q);

    for (auto& channel : filters)
        channel[band].glideTo (svf, ramp);
}

void WetEqualiser::designSolo (std::size_t ramp) noexcept
{
    if (solo == noSolo)
        return;

    const auto& b = bands[static_cast<std::size_t> (solo)];
    const auto f = static_cast<double> (b.frequencyHz);
    const auto q = static_cast<double> (std::clamp (b.q, minQ, maxQ));
    const auto svf = b.shape == Shape::lowShelf ? Svf::lowPass (sampleRate, f)
                   : b.shape == Shape::highShelf ? Svf::highPass (sampleRate, f)
                                                 : Svf::bandPass (sampleRate, f, q);

    for (auto& filter : soloFilters)
        filter.glideTo (svf, ramp);

    soloDesigned = solo;
}

void WetEqualiser::process (std::size_t channel, float* data, std::size_t n) noexcept
{
    const auto k = std::min (channel, maxChannels - 1);
    auto& chain = filters[k];
    heardSinceReset = true;

    for (std::size_t b = 0; b < numBands; ++b)
        if (isActive (bands[b]))
            chain[b].process (data, n);

    if (soloFrom <= 0.0f && soloHeard <= 0.0f)
        return;

    // Between the whole reverberation and the soloed band's part of it.
    auto& filter = soloFilters[k];
    const auto step = n > 0 ? (soloHeard - soloFrom) / static_cast<float> (n) : 0.0f;
    auto amount = soloFrom;

    for (std::size_t i = 0; i < n; ++i)
    {
        amount += step;
        const auto x = data[i];
        data[i] = x + amount * (filter.processSample (x) - x);
    }

    filter.flushDenormals();
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

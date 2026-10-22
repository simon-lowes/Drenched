#include "drenched/ResponseAnalysis.h"

#include "drenched/Biquad.h"
#include "drenched/RecordedResponse.h"

#include <algorithm>
#include <cmath>

namespace drenched
{

namespace
{

constexpr std::size_t decayPoints = 200;
constexpr double minimumWindowSeconds = 0.01;

double toDb (double ratio, float floorDb) noexcept
{
    return ratio > 0.0 ? std::max (10.0 * std::log10 (ratio), static_cast<double> (floorDb)) : floorDb;
}

/** One third-octave band of a signal: fourth-order high-pass and low-pass at the band edges. */
std::vector<float> thirdOctave (const std::vector<float>& signal, std::size_t length, double sampleRate, double centre)
{
    std::vector<float> band (signal.begin(), signal.begin() + static_cast<std::ptrdiff_t> (std::min (length, signal.size())));
    const auto edge = std::pow (2.0, 1.0 / 6.0);

    for (int pass = 0; pass < 2; ++pass)
    {
        Biquad hp, lp;
        hp.setCoefficients (Biquad::highPass (sampleRate, centre / edge));
        hp.process (band.data(), band.size());

        if (centre * edge < sampleRate * 0.45)
        {
            lp.setCoefficients (Biquad::lowPass (sampleRate, centre * edge));
            lp.process (band.data(), band.size());
        }
    }

    return band;
}

} // namespace

ResponseAnalysis analyseResponse (const ImpulseResponseSet& responses)
{
    ResponseAnalysis analysis;
    const auto length = responses.length();
    const auto rate = responses.sampleRate;

    if (length == 0 || rate <= 0.0)
        return analysis;

    // The broadband decay: energy summed over the paths, integrated backwards.
    std::vector<double> schroeder (length, 0.0);

    for (const auto& path : responses.paths)
        for (std::size_t n = 0; n < std::min (length, path.size()); ++n)
            schroeder[n] += static_cast<double> (path[n]) * path[n];

    for (std::size_t n = length - 1; n-- > 0;)
        schroeder[n] += schroeder[n + 1];

    const auto total = schroeder[0];

    if (total <= 0.0)
        return analysis;

    // The displays stop where the decay reaches -60 dB (or the response ends).
    std::size_t end = length;

    for (std::size_t n = 0; n < length; ++n)
    {
        if (schroeder[n] < total * 1.0e-6)
        {
            end = std::max<std::size_t> (n, 1);
            break;
        }
    }

    analysis.seconds = static_cast<double> (end) / rate;
    analysis.decayStepSeconds = analysis.seconds / static_cast<double> (decayPoints - 1);
    analysis.decayDb.resize (decayPoints);

    for (std::size_t i = 0; i < decayPoints; ++i)
    {
        const auto n = std::min (length - 1, static_cast<std::size_t> (std::lround (static_cast<double> (i) * analysis.decayStepSeconds * rate)));
        analysis.decayDb[i] = static_cast<float> (toDb (schroeder[n] / total, ResponseAnalysis::floorDb));
    }

    // The waterfall: each band's mean energy over a window from each slice.
    constexpr auto slices = ResponseAnalysis::numSlices;
    analysis.sliceSeconds = analysis.seconds / static_cast<double> (slices);
    const auto hop = std::max<std::size_t> (1, end / slices);
    const auto window = std::max (hop, static_cast<std::size_t> (minimumWindowSeconds * rate));

    const auto used = std::min (length, (slices - 1) * hop + window); // nothing past the last window is filtered
    std::vector<std::array<double, analysisBands.size()>> energy (slices);

    for (const auto& path : responses.paths)
    {
        if (path.empty())
            continue;

        for (std::size_t b = 0; b < analysisBands.size(); ++b)
        {
            if (analysisBands[b] >= rate * 0.5)
                continue;

            const auto band = thirdOctave (path, used, rate, analysisBands[b]);

            for (std::size_t s = 0; s < slices; ++s)
            {
                const auto from = std::min (band.size(), s * hop);
                const auto to = std::min (band.size(), from + window);
                double sum = 0.0;

                for (std::size_t n = from; n < to; ++n)
                    sum += static_cast<double> (band[n]) * band[n];

                energy[s][b] += to > from ? sum / static_cast<double> (to - from) : 0.0;
            }
        }
    }

    double peak = 0.0;

    for (const auto& slice : energy)
        peak = std::max (peak, *std::max_element (slice.begin(), slice.end()));

    analysis.waterfall.resize (slices);

    for (std::size_t s = 0; s < slices; ++s)
        for (std::size_t b = 0; b < analysisBands.size(); ++b)
            analysis.waterfall[s][b] = static_cast<float> (toDb (peak > 0.0 ? energy[s][b] / peak : 0.0, ResponseAnalysis::floorDb));

    analysis.rt60 = measureDecay (responses.paths, rate);
    return analysis;
}

} // namespace drenched

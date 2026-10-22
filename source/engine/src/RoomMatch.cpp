#include "drenched/RoomMatch.h"

#include "drenched/Spaces.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace drenched
{

namespace
{

constexpr double fitStartDb = 5.0;   // below the start of a decay, where the fit begins: past the direct sound
constexpr double fitRangeDb = 10.0;  // the least fall a decay needs over its fit
constexpr double wobbleDb = 3.0;     // how far a decaying level may rise and still be decaying
constexpr double floorMarginDb = 5.0;// a decay ends this far above the background noise
constexpr double fitQuality = 0.9;   // R squared of the line through a decay
constexpr int decaysNeeded = 3;      // in a band, to measure it

/** Frames each band's level is averaged over: enough cycles of the band that
    its level is steady, at least 50 ms. A decay keeps its slope through the
    averaging; only its start blurs. */
std::size_t smoothingFrames (std::size_t band) noexcept
{
    const auto bandwidth = static_cast<double> (decayBandCentres[band]) * (std::numbers::sqrt2 - 1.0 / std::numbers::sqrt2);
    return static_cast<std::size_t> (std::max (5.0, std::round (12.0 / bandwidth / RoomListener::hopSeconds)));
}

struct Line
{
    double slope = 0.0, r2 = 0.0;
};

Line fit (const std::vector<double>& level, std::size_t from, std::size_t to)
{
    const auto n = static_cast<double> (to - from + 1);
    double mt = 0.0, ml = 0.0;

    for (auto i = from; i <= to; ++i)
    {
        mt += static_cast<double> (i);
        ml += level[i];
    }

    mt /= n;
    ml /= n;
    double tl = 0.0, tt = 0.0, ll = 0.0;

    for (auto i = from; i <= to; ++i)
    {
        const auto dt = static_cast<double> (i) - mt, dl = level[i] - ml;
        tl += dt * dl;
        tt += dt * dt;
        ll += dl * dl;
    }

    if (tt <= 0.0 || ll <= 0.0)
        return {};

    return { tl / tt / RoomListener::hopSeconds, tl * tl / (tt * ll) };
}

/** Decay times (RT60, seconds) of every free decay in one band's level. */
std::vector<double> freeDecays (const std::vector<float>& meanSquare, std::size_t smoothing)
{
    const auto n = meanSquare.size();

    if (n < smoothing + 4)
        return {};

    // The level in dB, averaged over the smoothing frames, centred.
    std::vector<double> prefix (n + 1, 0.0), level (n);

    for (std::size_t i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + meanSquare[i];

    for (std::size_t i = 0; i < n; ++i)
    {
        const auto from = i >= smoothing / 2 ? i - smoothing / 2 : 0;
        const auto to = std::min (n, from + smoothing);
        level[i] = 10.0 * std::log10 (std::max (0.0, prefix[to] - prefix[from]) / static_cast<double> (to - from) + 1.0e-30);
    }

    // The background: the level the quietest moments of the recording sit at,
    // but no more than 90 dB below its loudest.
    auto sorted = level;
    std::sort (sorted.begin(), sorted.end());
    const auto floor = std::max (sorted[n / 50], sorted.back() - 90.0);
    const auto stop = floor + floorMarginDb;

    std::vector<double> decays;
    std::size_t i = 1;

    while (i + 1 < n)
    {
        // A decay starts at a peak.
        if (! (level[i] >= level[i - 1] && level[i + 1] < level[i]) || level[i] < stop + fitStartDb + fitRangeDb)
        {
            ++i;
            continue;
        }

        auto j = i, lowest = i;

        while (j + 1 < n && level[j + 1] < level[lowest] + wobbleDb && level[j + 1] > stop)
        {
            ++j;

            if (level[j] < level[lowest])
                lowest = j;
        }

        auto from = i;

        while (from < lowest && level[from] > level[i] - fitStartDb)
            ++from;

        const auto fall = level[from] - level[lowest];

        if (lowest >= from + 3 && fall >= fitRangeDb)
        {
            const auto line = fit (level, from, lowest);

            if (line.slope < 0.0 && line.r2 >= fitQuality)
                decays.push_back (-60.0 / line.slope);
        }
        else if (lowest > from && fall >= fitRangeDb)
        {
            // Gone too fast to fit a line: a dry room. At most this long.
            decays.push_back (60.0 * static_cast<double> (lowest - from) * RoomListener::hopSeconds / fall);
        }

        i = std::max (j, i + 1);
    }

    return decays;
}

} // namespace

RoomListener::RoomListener (double newSampleRate)
    : sampleRate (newSampleRate),
      hop (std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (hopSeconds * newSampleRate))))
{
    for (std::size_t b = 0; b < 8; ++b)
    {
        const double centre = decayBandCentres[b];
        const auto top = std::min (centre * std::numbers::sqrt2, 0.45 * sampleRate);

        // Fourth-order edges, so a neighbouring band's longer decay does not leak in.
        filters[b][0].setCoefficients (Biquad::highPass (sampleRate, centre / std::numbers::sqrt2));
        filters[b][1].setCoefficients (Biquad::highPass (sampleRate, centre / std::numbers::sqrt2));
        filters[b][2].setCoefficients (Biquad::lowPass (sampleRate, top));
        filters[b][3].setCoefficients (Biquad::lowPass (sampleRate, top));
    }
}

void RoomListener::listen (const float* samples, std::size_t n)
{
    for (std::size_t s = 0; s < n; ++s)
    {
        const auto x = std::isfinite (samples[s]) ? samples[s] : 0.0f;

        for (std::size_t b = 0; b < 8; ++b)
        {
            auto y = x;

            for (auto& f : filters[b])
                y = f.processSample (y);

            sum[b] += static_cast<double> (y) * y;
        }

        if (++filled == hop)
        {
            for (std::size_t b = 0; b < 8; ++b)
            {
                frames[b].push_back (static_cast<float> (sum[b] / static_cast<double> (hop)));
                sum[b] = 0.0;
            }

            filled = 0;
        }
    }

    total += n;
}

double RoomListener::secondsHeard() const noexcept
{
    return static_cast<double> (total) / sampleRate;
}

RoomEstimate RoomListener::estimate() const
{
    const auto peak = frames[4].empty() ? 0.0f : *std::max_element (frames[4].begin(), frames[4].end());

    if (! (peak > 1.0e-12f))
        throw RoomMatchProblem ("This recording is silent.");

    RoomEstimate out;
    std::array<double, 8> rt {};

    for (std::size_t b = 0; b < 8; ++b)
    {
        auto decays = freeDecays (frames[b], smoothingFrames (b));
        out.decaysHeard[b] = static_cast<int> (decays.size());

        if (out.decaysHeard[b] < decaysNeeded)
        {
            rt[b] = std::nan ("");
            continue;
        }

        // The quicker third of the decays: the room's own, not a sound fading
        // more slowly than it. Their median, so one odd one does not decide.
        std::sort (decays.begin(), decays.end());
        const auto quick = std::max<std::size_t> (1, decays.size() / 3);
        rt[b] = decays[(quick - 1) / 2];
    }

    // The middle bands carry most of a room; without them there is nothing to go on.
    if (std::isnan (rt[3]) && std::isnan (rt[4]) && std::isnan (rt[5]))
        throw RoomMatchProblem ("Too few pauses to hear the room in. Choose a recording where the sound stops now and then.");

    // A band that could not be measured takes its nearest measured neighbour's value.
    for (std::size_t b = 0; b < 8; ++b)
    {
        if (! std::isnan (rt[b]))
            continue;

        for (std::size_t d = 1; d < 8; ++d)
        {
            if (b >= d && ! std::isnan (rt[b - d]) && out.decaysHeard[b - d] >= decaysNeeded)
            {
                rt[b] = rt[b - d];
                break;
            }

            if (b + d < 8 && ! std::isnan (rt[b + d]) && out.decaysHeard[b + d] >= decaysNeeded)
            {
                rt[b] = rt[b + d];
                break;
            }
        }
    }

    for (std::size_t b = 0; b < 8; ++b)
    {
        out.decaySeconds[b] = static_cast<float> (std::clamp (rt[b], 0.1, 20.0));

        if (out.decaysHeard[b] < decaysNeeded)
            out.decaysHeard[b] = 0;
    }

    return out;
}

RoomEstimate matchRoom (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    RoomListener listener (sampleRate);

    if (channels.empty())
        return listener.estimate();

    const auto length = std::max_element (channels.begin(), channels.end(),
                                          [] (const auto& a, const auto& b) { return a.size() < b.size(); })->size();
    constexpr std::size_t block = 4096;
    std::vector<float> mono (block);

    for (std::size_t pos = 0; pos < length; pos += block)
    {
        const auto n = std::min (block, length - pos);
        std::fill (mono.begin(), mono.end(), 0.0f);

        for (const auto& ch : channels)
            for (std::size_t s = 0; s < n && pos + s < ch.size(); ++s)
                mono[s] += ch[pos + s] / static_cast<float> (channels.size());

        listener.listen (mono.data(), n);
    }

    return listener.estimate();
}

float decayDistance (const std::array<float, 8>& a, const std::array<float, 8>& b) noexcept
{
    constexpr std::array<double, 8> weight { 0.25, 0.5, 1.0, 1.0, 1.0, 1.0, 0.5, 0.25 };
    double sum = 0.0, weights = 0.0;

    for (std::size_t k = 0; k < 8; ++k)
    {
        const auto ratio = 20.0 * std::log10 (std::max (a[k], 0.01f) / std::max (b[k], 0.01f));
        sum += weight[k] * ratio * ratio;
        weights += weight[k];
    }

    return static_cast<float> (std::sqrt (sum / weights));
}

} // namespace drenched

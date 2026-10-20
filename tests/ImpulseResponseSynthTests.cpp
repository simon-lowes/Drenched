#include <drenched/Biquad.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/Spaces.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace drenched;

namespace
{
    ImpulseResponseRequest requestFor (int space, double sampleRate = 48000.0)
    {
        ImpulseResponseRequest r;
        r.sampleRate = sampleRate;
        r.space = space;
        r.decaySeconds = bundledSpaces[static_cast<std::size_t> (space)].decaySeconds;
        return r;
    }

    /** RT60 of one octave band, from the Schroeder backward integral fitted
        between -5 and -25 dB (T20), the standard ISO 3382 measurement. */
    double measureRt60 (const std::vector<float>& ir, double sampleRate, double centre)
    {
        // Octave band: fourth-order high-pass and low-pass at the band edges,
        // steep enough that a neighbouring band's longer decay does not leak in.
        std::vector<float> band (ir);
        for (int pass = 0; pass < 2; ++pass)
        {
            Biquad hp, lp;
            hp.setCoefficients (Biquad::highPass (sampleRate, centre / std::sqrt (2.0)));
            lp.setCoefficients (Biquad::lowPass (sampleRate, centre * std::sqrt (2.0)));
            hp.process (band.data(), band.size());
            lp.process (band.data(), band.size());
        }

        std::vector<double> schroeder (band.size());
        double sum = 0.0;
        for (std::size_t n = band.size(); n-- > 0;)
        {
            sum += static_cast<double> (band[n]) * band[n];
            schroeder[n] = sum;
        }

        const auto total = schroeder[0];
        std::vector<double> t, db;

        for (std::size_t n = 0; n < band.size(); ++n)
        {
            const auto level = 10.0 * std::log10 (schroeder[n] / total);

            if (level <= -5.0 && level >= -25.0)
            {
                t.push_back (static_cast<double> (n) / sampleRate);
                db.push_back (level);
            }
        }

        const auto meanT = std::accumulate (t.begin(), t.end(), 0.0) / static_cast<double> (t.size());
        const auto meanD = std::accumulate (db.begin(), db.end(), 0.0) / static_cast<double> (db.size());
        double num = 0.0, den = 0.0;

        for (std::size_t i = 0; i < t.size(); ++i)
        {
            num += (t[i] - meanT) * (db[i] - meanD);
            den += (t[i] - meanT) * (t[i] - meanT);
        }

        return -60.0 / (num / den);
    }

    double energy (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        double e = 0.0;
        const auto end = std::min (to, x.size());
        for (auto it = x.begin() + static_cast<std::ptrdiff_t> (std::min (from, end)); it != x.begin() + static_cast<std::ptrdiff_t> (end); ++it)
            e += static_cast<double> (*it) * *it;
        return e;
    }
}

TEST_CASE ("Each band decays at the RT60 the decay graph asks for", "[irsynth]")
{
    const auto space = GENERATE (0, 1, 2, 3);
    const auto request = requestFor (space);
    const auto ir = synthesiseImpulseResponse (request);

    for (std::size_t band = 1; band < 8; ++band)
    {
        const auto target = request.decaySeconds[band];
        const auto measured = measureRt60 (ir.paths[ImpulseResponseSet::leftToLeft], request.sampleRate, decayBandCentres[band]);

        INFO (bundledSpaces[static_cast<std::size_t> (space)].name << ", " << decayBandCentres[band] << " Hz: target "
                                                                 << target << " s, measured " << measured << " s");
        REQUIRE (std::abs (measured - target) <= 0.2 * target);
    }
}

TEST_CASE ("Editing one band changes that band's decay only", "[irsynth]")
{
    auto request = requestFor (0);
    request.decaySeconds[6] = 4.0f; // 4 kHz much longer

    const auto ir = synthesiseImpulseResponse (request);
    const auto& path = ir.paths[ImpulseResponseSet::rightToRight];

    REQUIRE (std::abs (measureRt60 (path, request.sampleRate, 4000.0) - 4.0) <= 0.8);
    REQUIRE (std::abs (measureRt60 (path, request.sampleRate, 500.0) - request.decaySeconds[3]) <= 0.2 * request.decaySeconds[3]);
}

TEST_CASE ("The response is deterministic and the four paths are decorrelated", "[irsynth]")
{
    const auto request = requestFor (1);
    const auto a = synthesiseImpulseResponse (request);
    const auto b = synthesiseImpulseResponse (request);

    for (std::size_t p = 0; p < 4; ++p)
        REQUIRE (a.paths[p] == b.paths[p]);

    // Correlation of the left and right tails, after the early part.
    const auto& l = a.paths[ImpulseResponseSet::leftToLeft];
    const auto& r = a.paths[ImpulseResponseSet::leftToRight];
    const auto start = static_cast<std::size_t> (0.3 * request.sampleRate);
    double lr = 0.0, ll = 0.0, rr = 0.0;

    for (std::size_t n = start; n < l.size(); ++n)
    {
        lr += static_cast<double> (l[n]) * r[n];
        ll += static_cast<double> (l[n]) * l[n];
        rr += static_cast<double> (r[n]) * r[n];
    }

    REQUIRE (std::abs (lr / std::sqrt (ll * rr)) < 0.1);
}

TEST_CASE ("Level is normalised, so every space plays at a similar loudness", "[irsynth]")
{
    const auto space = GENERATE (0, 1, 2, 3);
    const auto ir = synthesiseImpulseResponse (requestFor (space));

    double total = 0.0;
    for (const auto& p : ir.paths)
        total += energy (p);

    REQUIRE (std::abs (total - 1.0) < 1.0e-3);

    for (const auto& p : ir.paths)
        REQUIRE (p.size() == ir.length());
}

TEST_CASE ("Moving the listener away makes the sound more reverberant", "[irsynth]")
{
    auto near = requestFor (0);
    near.distancePercent = 0.0f;
    auto far = near;
    far.distancePercent = 100.0f;

    const auto directToReverberant = [] (const ImpulseResponseSet& ir)
    {
        const auto& p = ir.paths[ImpulseResponseSet::leftToLeft];
        const auto edge = static_cast<std::size_t> (0.004 * ir.sampleRate); // direct sound sits at 1 ms
        return energy (p, 0, edge) / energy (p, edge);
    };

    REQUIRE (directToReverberant (synthesiseImpulseResponse (near)) > 4.0 * directToReverberant (synthesiseImpulseResponse (far)));
}

TEST_CASE ("Early/Late moves energy between the reflections and the tail", "[irsynth]")
{
    auto early = requestFor (0);
    early.earlyLatePercent = 10.0f;
    auto late = early;
    late.earlyLatePercent = 90.0f;

    const auto tailShare = [] (const ImpulseResponseSet& ir)
    {
        const auto& p = ir.paths[ImpulseResponseSet::rightToLeft];
        const auto edge = static_cast<std::size_t> (0.3 * ir.sampleRate);
        return energy (p, edge) / energy (p);
    };

    REQUIRE (tailShare (synthesiseImpulseResponse (late)) > 1.5 * tailShare (synthesiseImpulseResponse (early)));
}

TEST_CASE ("The response starts with the direct sound and ends near silence", "[irsynth]")
{
    const auto request = requestFor (3, 44100.0);
    const auto ir = synthesiseImpulseResponse (request);
    const auto& p = ir.paths[ImpulseResponseSet::leftToLeft];

    const auto loudest = std::distance (p.begin(), std::max_element (p.begin(), p.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
    REQUIRE (loudest < static_cast<long> (0.004 * request.sampleRate));

    const auto slowest = *std::max_element (request.decaySeconds.begin(), request.decaySeconds.end());
    REQUIRE (static_cast<double> (ir.length()) / request.sampleRate >= slowest);
    REQUIRE (std::abs (p.back()) < 1.0e-5f);
}

TEST_CASE ("Listener distance matches the interface's metres", "[irsynth]")
{
    // Grand Hall, 42 m deep: 1.5 m at the front, 35.7 m at the back.
    REQUIRE (std::abs (listenerDistanceMetres (0, 100.0f, 0.0f) - 1.5f) < 1.0e-4f);
    REQUIRE (std::abs (listenerDistanceMetres (0, 100.0f, 100.0f) - 35.7f) < 1.0e-3f);
    REQUIRE (std::abs (listenerDistanceMetres (0, 100.0f, 40.0f) - 15.18f) < 1.0e-2f);
}

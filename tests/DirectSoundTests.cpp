#include "TestHelpers.h"

#include <drenched/Biquad.h>
#include <drenched/DirectSound.h>
#include <drenched/Spaces.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double sr = 48000.0;

    ImpulseResponseRequest room()
    {
        ImpulseResponseRequest r;
        r.sampleRate = sr;
        r.space = 3; // Live Room B: short, so the tests stay quick
        r.decaySeconds = bundledSpaces[3].decaySeconds;
        return r;
    }

    std::size_t at (double seconds) { return static_cast<std::size_t> (std::round (seconds * sr)); }

    double energy (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double e = 0.0;
        for (auto n = from; n < std::min (to, x.size()); ++n)
            e += static_cast<double> (x[n]) * x[n];
        return e;
    }

    std::vector<float> above (std::vector<float> x, double frequency)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            Biquad hp;
            hp.setCoefficients (Biquad::highPass (sr, frequency));
            hp.process (x.data(), x.size());
        }
        return x;
    }

    // The samples the direct-sound window can touch around a peak.
    bool outside (std::size_t n, std::size_t peak) { return n + at (0.0012) < peak || n > peak + at (0.0017); }
}

TEST_CASE ("The direct sound is found at the start of each path", "[direct]")
{
    const auto responses = synthesiseImpulseResponse (room());

    for (const auto& path : responses.paths)
    {
        const auto peak = findDirectSound (path, sr);
        REQUIRE (peak < at (0.02));

        const auto loudest = *std::max_element (path.begin(), path.begin() + static_cast<std::ptrdiff_t> (at (0.05)),
                                                [] (float a, float b) { return std::abs (a) < std::abs (b); });

        for (std::size_t n = 0; n < peak; ++n)
            CHECK (std::abs (path[n]) < std::abs (loudest)); // nothing louder before it
    }

    CHECK (findDirectSound (std::vector<float> (1000, 0.0f), sr) == 1000);
}

TEST_CASE ("With the direct controls at their defaults the response is untouched", "[direct]")
{
    const auto request = room();
    const auto original = synthesiseImpulseResponse (request);
    auto shaped = original;

    applyDirect (shaped, request);

    CHECK (shaped.paths == original.paths);
}

TEST_CASE ("Direct level turns the direct sound up, down or off and leaves the room", "[direct]")
{
    auto request = room();
    const auto original = synthesiseImpulseResponse (request);

    for (const auto level : { -60.0f, -6.0f, 6.0f })
    {
        request.directLevelDb = level;
        auto shaped = original;
        applyDirect (shaped, request);

        const auto expected = level <= minDirectLevelDb ? 0.0 : std::pow (10.0, level / 20.0);

        for (std::size_t p = 0; p < 4; ++p)
        {
            const auto peak = findDirectSound (original.paths[p], sr);
            INFO ("level " << level << " dB, path " << p);
            CHECK (shaped.paths[p][peak] == Catch::Approx (expected * original.paths[p][peak]).margin (1.0e-6));

            for (std::size_t n = 0; n < original.length(); ++n)
                if (outside (n, peak))
                    REQUIRE (shaped.paths[p][n] == original.paths[p][n]);
        }
    }
}

TEST_CASE ("Direct delay moves the direct sound later and leaves the room", "[direct]")
{
    auto request = room();
    request.directDelayMs = 10.0f;
    const auto original = synthesiseImpulseResponse (request);
    auto shaped = original;

    applyDirect (shaped, request);

    for (std::size_t p = 0; p < 4; ++p)
    {
        const auto peak = findDirectSound (original.paths[p], sr);
        const auto moved = peak + at (0.01);

        CHECK (std::abs (shaped.paths[p][peak]) < 1.0e-6f);
        CHECK (shaped.paths[p][moved] == Catch::Approx (original.paths[p][moved] + original.paths[p][peak]).margin (1.0e-6));

        for (std::size_t n = 0; n < original.length(); ++n)
            if (outside (n, peak) && outside (n, moved))
                REQUIRE (shaped.paths[p][n] == original.paths[p][n]);
    }
}

TEST_CASE ("Direct colour tilts the direct sound brighter or darker", "[direct]")
{
    auto request = room();
    const auto original = synthesiseImpulseResponse (request);
    const auto peak = findDirectSound (original.paths[0], sr);
    const auto from = peak - at (0.001), to = peak + at (0.003);

    const auto highs = [&] (float colour)
    {
        request.directColourDb = colour;
        auto shaped = original;
        applyDirect (shaped, request);
        return 10.0 * std::log10 (energy (above (shaped.paths[0], 5000.0), from, to));
    };

    const auto flat = highs (0.0f), bright = highs (12.0f), dark = highs (-12.0f);
    INFO ("highs: flat " << flat << " dB, bright " << bright << " dB, dark " << dark << " dB");
    CHECK (bright > flat + 3.0);
    CHECK (dark < flat - 3.0);
}

#include <drenched/ImpulseResponseSynth.h>
#include <drenched/PartitionedConvolver.h>
#include <drenched/RoomMatch.h>
#include <drenched/Spaces.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    ImpulseResponseSet spaceResponse (std::size_t space)
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = static_cast<int> (space);
        r.decaySeconds = bundledSpaces[space].decaySeconds;
        return synthesiseImpulseResponse (r);
    }

    /** Something playing in fits and starts: bursts of noise from a fifth of a
        second to half a second long, with pauses up to a second and a half,
        at levels that vary by 12 dB; with fadeHalf every other one fades away
        over a second and a half. Dry, as it left the instrument. */
    std::vector<float> phrases (double seconds, unsigned seed, double longestPause = 1.6, bool fadeHalf = false)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<double> length (0.15, 0.5), pause (0.6, longestPause), level (-12.0, 0.0);
        std::vector<float> x (static_cast<std::size_t> (seconds * rate), 0.0f);
        const auto source = testing::noise (x.size(), seed + 1);
        const auto fadeLength = static_cast<std::size_t> (1.5 * rate);
        auto pos = static_cast<std::size_t> (0.1 * rate);
        bool fade = false;

        while (pos < x.size())
        {
            const auto n = static_cast<std::size_t> (length (rng) * rate);
            const auto gain = static_cast<float> (0.3 * std::pow (10.0, level (rng) / 20.0));
            fade = fadeHalf && ! fade;

            for (std::size_t s = 0; s < n + (fade ? fadeLength : 0) && pos + s < x.size(); ++s)
            {
                const auto faded = s < n ? 1.0 : std::pow (10.0, -2.0 * static_cast<double> (s - n) / static_cast<double> (fadeLength));
                x[pos + s] = gain * static_cast<float> (faded) * source[pos + s];
            }

            pos += n + (fade ? fadeLength : 0) + static_cast<std::size_t> (pause (rng) * rate);
        }

        return x;
    }

    /** The dry sound played in a room, as its left microphone hears it. */
    std::vector<float> playIn (const std::vector<float>& dry, const ImpulseResponseSet& room)
    {
        PartitionedConvolver convolver ({ std::span<const float> (room.paths[ImpulseResponseSet::leftToLeft]) });
        std::vector<float> wet (dry.size() + room.length(), 0.0f);
        auto padded = dry;
        padded.resize (wet.size(), 0.0f);
        float* out[] { wet.data() };
        convolver.process (padded.data(), out, padded.size());
        return wet;
    }

    /** The room's own decay, measured from its response. */
    std::array<double, 8> trueDecay (const ImpulseResponseSet& room)
    {
        std::array<double, 8> rt {};

        for (std::size_t b = 0; b < 8; ++b)
            rt[b] = testing::measureRt60 (room.paths[ImpulseResponseSet::leftToLeft], rate, decayBandCentres[b]);

        return rt;
    }
}

TEST_CASE ("Match a room hears each bundled space's decay in a recording made there", "[match]")
{
    for (std::size_t space = 0; space < bundledSpaces.size(); ++space)
    {
        const auto room = spaceResponse (space);
        const auto truth = trueDecay (room);
        const auto heard = matchRoom ({ playIn (phrases (40.0, 11u + static_cast<unsigned> (space)), room) }, rate);

        for (std::size_t b = 0; b < 8; ++b)
        {
            INFO (bundledSpaces[space].name << " at " << decayBandCentres[b] << " Hz: heard " << heard.decaySeconds[b]
                  << " s from " << heard.decaysHeard[b] << " decays, room " << truth[b] << " s");
            const auto error = std::abs (heard.decaySeconds[b] / truth[b] - 1.0);
            CHECK (error < (b >= 2 && b <= 6 ? 0.12 : 0.2));
            CHECK (heard.decaysHeard[b] >= 10);
        }
    }
}

TEST_CASE ("Match a room hears a stereo recording with background noise", "[match]")
{
    const auto room = spaceResponse (0);
    const auto truth = trueDecay (room);
    auto left = playIn (phrases (40.0, 21u), room);
    auto right = playIn (phrases (40.0, 21u), room);

    // Hiss 50 dB below the loudest phrases.
    const auto hiss = testing::noise (left.size(), 22u);

    for (std::size_t n = 0; n < left.size(); ++n)
    {
        left[n] += 0.001f * hiss[n];
        right[n] += 0.001f * hiss[(n + 7919) % hiss.size()];
    }

    const auto heard = matchRoom ({ left, right }, rate);

    for (std::size_t b = 2; b <= 6; ++b)
    {
        INFO (decayBandCentres[b] << " Hz: heard " << heard.decaySeconds[b] << " s, room " << truth[b] << " s");
        CHECK (std::abs (heard.decaySeconds[b] / truth[b] - 1.0) < 0.15);
    }
}

TEST_CASE ("Match a room is not fooled by sounds that fade out slowly", "[match]")
{
    // Every other phrase fades away over a second and a half, slower than the
    // room; after the others the room still rings on at its own rate.
    const auto dry = phrases (40.0, 31u, 1.6, true);
    const auto room = spaceResponse (3);
    const auto truth = trueDecay (room);
    const auto heard = matchRoom ({ playIn (dry, room) }, rate);

    for (std::size_t b = 2; b <= 6; ++b)
    {
        INFO (decayBandCentres[b] << " Hz: heard " << heard.decaySeconds[b] << " s, room " << truth[b] << " s");
        CHECK (std::abs (heard.decaySeconds[b] / truth[b] - 1.0) < 0.15);
    }
}

TEST_CASE ("Match a room hears a dry recording as a short decay", "[match]")
{
    const auto heard = matchRoom ({ phrases (30.0, 41u) }, rate);

    for (std::size_t b = 2; b < 8; ++b)
    {
        INFO (decayBandCentres[b] << " Hz: heard " << heard.decaySeconds[b] << " s");
        CHECK (heard.decaySeconds[b] < 0.3f);
    }
}

TEST_CASE ("Match a room says why when a recording has nothing to hear", "[match]")
{
    using Catch::Matchers::ContainsSubstring;

    // Silence.
    CHECK_THROWS_WITH (matchRoom ({ std::vector<float> (static_cast<std::size_t> (10.0 * rate), 0.0f) }, rate),
                       ContainsSubstring ("silent"));
    CHECK_THROWS_AS (matchRoom ({}, rate), RoomMatchProblem);

    // A sound that never stops.
    const auto held = playIn (testing::noise (static_cast<std::size_t> (20.0 * rate), 51u), spaceResponse (0));
    CHECK_THROWS_WITH (matchRoom ({ std::vector<float> (held.begin(), held.begin() + static_cast<std::ptrdiff_t> (19.0 * rate)) }, rate),
                       ContainsSubstring ("Too few pauses"));
}

TEST_CASE ("Decay distance finds the closest space", "[match]")
{
    for (const auto& space : bundledSpaces)
        CHECK (decayDistance (space.decaySeconds, space.decaySeconds) == 0.0f);

    const auto& hall = bundledSpaces[0].decaySeconds;
    const auto& room = bundledSpaces[3].decaySeconds;
    CHECK (decayDistance (hall, room) == decayDistance (room, hall));
    CHECK (decayDistance (hall, room) > 10.0f);

    // A slightly longer hall is still nearer the hall than the chapel or the car park.
    auto longer = hall;

    for (auto& v : longer)
        v *= 1.2f;

    const auto nearest = std::min_element (bundledSpaces.begin(), bundledSpaces.end(), [&] (const Space& a, const Space& b)
                                           { return decayDistance (longer, a.decaySeconds) < decayDistance (longer, b.decaySeconds); });
    CHECK (nearest == bundledSpaces.begin());
}

#include "TestHelpers.h"

#include <drenched/WetEqualiser.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <utility>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    /** The gain in dB the equaliser gives a steady sine, measured after it settles. */
    double measuredGainDb (WetEqualiser& eq, double frequency)
    {
        eq.reset();
        std::vector<float> x (24000);

        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] = static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (n) / rate));

        const auto in = x;

        for (std::size_t pos = 0; pos < x.size(); pos += 256)
            eq.process (0, x.data() + pos, std::min<std::size_t> (256, x.size() - pos));

        double a = 0.0, b = 0.0;

        for (std::size_t n = 12000; n < x.size(); ++n)
        {
            a += static_cast<double> (x[n]) * x[n];
            b += static_cast<double> (in[n]) * in[n];
        }

        return 10.0 * std::log10 (a / b);
    }
}

TEST_CASE ("A flat equaliser leaves the sound exactly as it was", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);
    REQUIRE (eq.isFlat());

    const auto in = testing::noise (4800, 3);
    auto out = in;

    eq.process (0, out.data(), out.size());
    REQUIRE (out == in);
    REQUIRE (eq.magnitudeDb (1000.0) == 0.0);
}

TEST_CASE ("A bell lifts its own frequency by its gain and leaves distant ones alone", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);

    auto bands = WetEqualiser::flat;
    bands[2] = { 700.0f, 6.0f, 0.7f };
    eq.setBands (bands);

    REQUIRE_FALSE (eq.isFlat());
    REQUIRE (measuredGainDb (eq, 700.0) == Catch::Approx (6.0).margin (0.1));
    REQUIRE (std::abs (measuredGainDb (eq, 14000.0)) < 0.2);
    REQUIRE (std::abs (measuredGainDb (eq, 35.0)) < 0.2);

    // A narrower bell has fallen further an octave away.
    const auto wide = measuredGainDb (eq, 1400.0);
    bands[2].q = 6.0f;
    eq.setBands (bands);
    REQUIRE (measuredGainDb (eq, 1400.0) < wide - 1.0);
}

TEST_CASE ("The shelves cut or lift everything beyond their frequency", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);

    auto bands = WetEqualiser::flat;
    bands[0].gainDb = -9.0f;
    bands[5].gainDb = 4.0f;
    eq.setBands (bands);

    REQUIRE (measuredGainDb (eq, 30.0) == Catch::Approx (-9.0).margin (0.3));
    REQUIRE (measuredGainDb (eq, 18000.0) == Catch::Approx (4.0).margin (0.3));
    REQUIRE (std::abs (measuredGainDb (eq, 1000.0)) < 0.3);
}

TEST_CASE ("The drawn curve is the sound the equaliser makes", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);

    auto bands = WetEqualiser::flat;
    bands[0].gainDb = 3.0f;
    bands[1] = { 300.0f, -5.0f, 1.2f };
    bands[3] = { 2200.0f, 7.5f, 0.7f };
    bands[4] = { 6000.0f, -12.0f, 4.0f };
    bands[5].gainDb = -2.0f;
    eq.setBands (bands);

    for (const auto f : { 50.0, 300.0, 1000.0, 2200.0, 6000.0, 12000.0 })
        REQUIRE (measuredGainDb (eq, f) == Catch::Approx (eq.magnitudeDb (f)).margin (0.1));
}

TEST_CASE ("Equaliser settings are kept in range", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);

    auto bands = WetEqualiser::flat;
    bands[2] = { 700.0f, 40.0f, 0.0f };
    eq.setBands (bands);

    REQUIRE (eq.magnitudeDb (700.0) == Catch::Approx (WetEqualiser::maxGainDb).margin (0.01));
    REQUIRE (std::isfinite (measuredGainDb (eq, 700.0)));
}

TEST_CASE ("A band switched back on starts fresh, and nothing allocates", "[eq][realtime]")
{
    WetEqualiser eq;
    eq.prepare (rate);

    auto noise = testing::noise (48000, 4);
    auto bands = WetEqualiser::flat;

    testing::AllocationCounter counter;

    for (std::size_t pos = 0; pos + 480 <= noise.size(); pos += 480)
    {
        // Sweep one bell up and down through 0 dB, off and on again.
        bands[3].gainDb = static_cast<float> (12.0 * std::sin (static_cast<double> (pos) / 4000.0));
        bands[3].frequencyHz = 500.0f + static_cast<float> (pos) * 0.1f;
        eq.setBands (bands);

        for (std::size_t from = 0; from < 480; from += 32)
        {
            eq.advance();
            eq.process (0, noise.data() + pos + from, std::min<std::size_t> (32, 480 - from));
            eq.process (1, noise.data() + pos + from, std::min<std::size_t> (32, 480 - from));
        }
    }

    REQUIRE (counter.count() == 0);

    for (const auto s : noise)
        REQUIRE (std::isfinite (s));
}

TEST_CASE ("Any band can be a shelf or a bell, and the later bands start out flat bells", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);
    REQUIRE (WetEqualiser::numBands == 12);

    for (std::size_t b = 6; b < WetEqualiser::numBands; ++b)
    {
        REQUIRE (WetEqualiser::flat[b].shape == WetEqualiser::Shape::bell);
        REQUIRE (WetEqualiser::flat[b].gainDb == 0.0f);
    }

    // A bell slot made a high shelf lifts everything above it, and the shelf
    // slot made a bell leaves distant frequencies alone.
    auto bands = WetEqualiser::flat;
    bands[8] = { 4000.0f, 5.0f, 0.9f, WetEqualiser::Shape::highShelf };
    bands[0] = { 100.0f, -8.0f, 1.0f, WetEqualiser::Shape::bell };
    eq.setBands (bands);

    REQUIRE (measuredGainDb (eq, 16000.0) == Catch::Approx (5.0).margin (0.3));
    REQUIRE (measuredGainDb (eq, 100.0) == Catch::Approx (-8.0).margin (0.3));
    REQUIRE (std::abs (measuredGainDb (eq, 1000.0)) < 0.3);

    for (const auto f : { 50.0, 100.0, 1000.0, 4000.0, 16000.0 })
        REQUIRE (measuredGainDb (eq, f) == Catch::Approx (eq.magnitudeDb (f)).margin (0.1));
}

TEST_CASE ("A soloed bell is heard alone through its own band, and the solo fades", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);

    auto bands = WetEqualiser::flat;
    bands[3] = { 2000.0f, 3.0f, 2.0f, WetEqualiser::Shape::bell };
    eq.setBands (bands);
    eq.setSolo (3);
    REQUIRE (eq.isSoloing());

    // At the band's frequency the band-pass is transparent: just the bell's gain is heard.
    REQUIRE (measuredGainDb (eq, 2000.0) == Catch::Approx (3.0).margin (0.3));
    // Far away, almost nothing.
    REQUIRE (measuredGainDb (eq, 100.0) < -20.0);
    REQUIRE (measuredGainDb (eq, 16000.0) < -15.0);

    // A soloed low shelf hears everything below it, a high shelf everything above.
    bands[0].gainDb = 2.0f;
    eq.setBands (bands);
    eq.setSolo (0);
    REQUIRE (measuredGainDb (eq, 30.0) == Catch::Approx (2.0).margin (0.5));
    REQUIRE (measuredGainDb (eq, 8000.0) < -30.0);

    eq.setSolo (5);
    REQUIRE (measuredGainDb (eq, 18000.0) == Catch::Approx (0.0).margin (0.5));
    REQUIRE (measuredGainDb (eq, 50.0) < -30.0);

    // Switching the solo on while sound plays fades in rather than jumping: a
    // low tone keeps some level through the first block after the switch.
    eq.setSolo (WetEqualiser::noSolo);
    eq.reset();
    std::vector<float> x (4096);

    for (std::size_t n = 0; n < x.size(); ++n)
        x[n] = static_cast<float> (std::sin (2.0 * std::numbers::pi * 100.0 * static_cast<double> (n) / rate));

    for (std::size_t pos = 0; pos < 2048; pos += 32)
    {
        eq.advance();
        eq.process (0, x.data() + pos, 32);
    }

    eq.setSolo (5);
    float firstPeak = 0.0f, lastPeak = 0.0f;

    for (std::size_t pos = 2048; pos < x.size(); pos += 32)
    {
        eq.advance();
        eq.process (0, x.data() + pos, 32);
        float peak = 0.0f;

        for (std::size_t i = 0; i < 32; ++i)
            peak = std::max (peak, std::abs (x[pos + i]));

        if (pos == 2048)
            firstPeak = peak;

        lastPeak = peak;
    }

    REQUIRE (firstPeak > 0.5f);
    REQUIRE (lastPeak < 0.05f);

    // Out of range is no solo at all.
    eq.setSolo (99);
    eq.setBands (WetEqualiser::flat);
    eq.reset();
    REQUIRE_FALSE (eq.isSoloing());
    REQUIRE (std::abs (measuredGainDb (eq, 100.0)) < 0.3);
}

TEST_CASE ("A band-pass state-variable filter passes its own frequency at full level", "[eq]")
{
    for (const auto q : { 0.5, 2.0, 8.0 })
    {
        Svf filter;
        filter.setCoefficients (Svf::bandPass (rate, 1000.0, q));
        std::vector<float> x (24000);

        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] = static_cast<float> (std::sin (2.0 * std::numbers::pi * 1000.0 * static_cast<double> (n) / rate));

        const auto in = x;
        filter.process (x.data(), x.size());
        double a = 0.0, b = 0.0;

        for (std::size_t n = 12000; n < x.size(); ++n)
        {
            a += static_cast<double> (x[n]) * x[n];
            b += static_cast<double> (in[n]) * in[n];
        }

        REQUIRE (10.0 * std::log10 (a / b) == Catch::Approx (0.0).margin (0.05));
    }
}

namespace
{
    /** How rough a run of samples is (the largest third difference), the measure ClickTests uses. */
    float roughness (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float most = 0.0f;

        for (auto i = std::max<std::size_t> (from, 3); i < to; ++i)
            most = std::max (most, std::abs (x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3]));

        return most;
    }

    /** A low tone through the equaliser, with `change` made half way. The click it makes, in dB
        against the tone's level, and how rough the settled tone is anyway. */
    std::pair<double, double> clickOf (const std::function<void (WetEqualiser&)>& setUp,
                                       const std::function<void (WetEqualiser&)>& change)
    {
        WetEqualiser eq;
        eq.prepare (rate);
        setUp (eq);

        const std::size_t length = 96000, at = 48000;
        std::vector<float> y (length);

        for (std::size_t n = 0; n < length; ++n)
            y[n] = 0.25f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 110.0 * static_cast<double> (n) / rate));

        for (std::size_t pos = 0; pos < length; pos += 32)
        {
            if (pos == at)
                change (eq);

            eq.advance();
            eq.process (0, y.data() + pos, 32);
        }

        float level = 0.0f;

        for (auto i = at - 12000; i < length; ++i)
            level = std::max (level, std::abs (y[i]));

        const auto dB = [level] (float v) { return 20.0 * std::log10 (std::max (static_cast<double> (v / level), 1.0e-12)); };
        return { dB (roughness (y, at, at + 24000)), dB (roughness (y, length - 12000, length)) };
    }

    void setBand (WetEqualiser& eq, std::size_t band, EqBand settings)
    {
        auto bands = WetEqualiser::flat;
        bands[band] = settings;
        eq.setBands (bands);
    }
}

TEST_CASE ("A boosted or cut band changing shape, or the solo moving, doesn't click", "[eq]")
{
    using S = EqBand::Shape;
    const auto quiet = [] (std::pair<double, double> click) { return click.first < std::max (-40.0, click.second + 6.0); };

    SECTION ("a boosted low shelf becomes a high shelf")
    {
        REQUIRE (quiet (clickOf ([] (auto& eq) { setBand (eq, 0, { 300.0f, 18.0f, 0.71f, S::lowShelf }); },
                                 [] (auto& eq) { setBand (eq, 0, { 300.0f, 18.0f, 0.71f, S::highShelf }); })));
    }

    SECTION ("a boosted bell becomes a high shelf")
    {
        REQUIRE (quiet (clickOf ([] (auto& eq) { setBand (eq, 1, { 110.0f, 18.0f, 0.9f, S::bell }); },
                                 [] (auto& eq) { setBand (eq, 1, { 110.0f, 18.0f, 0.9f, S::highShelf }); })));
    }

    SECTION ("a cut band is removed: back to its first shape, flat")
    {
        REQUIRE (quiet (clickOf ([] (auto& eq) { setBand (eq, 2, { 200.0f, -18.0f, 0.71f, S::lowShelf }); },
                                 [] (auto& eq) { eq.setBands (WetEqualiser::flat); })));
    }

    SECTION ("the solo moves from a low shelf to a high shelf")
    {
        REQUIRE (quiet (clickOf ([] (auto& eq)
                                 {
                                     auto bands = WetEqualiser::flat;
                                     bands[0] = { 100.0f, 6.0f, 0.71f, S::lowShelf };
                                     bands[6] = { 10000.0f, 6.0f, 0.71f, S::highShelf };
                                     eq.setBands (bands);
                                     eq.setSolo (0);
                                 },
                                 [] (auto& eq) { eq.setSolo (6); })));
    }

    SECTION ("the soloed band changes shape")
    {
        REQUIRE (quiet (clickOf ([] (auto& eq) { setBand (eq, 0, { 150.0f, 6.0f, 0.71f, S::lowShelf }); eq.setSolo (0); },
                                 [] (auto& eq) { setBand (eq, 0, { 150.0f, 6.0f, 0.71f, S::highShelf }); })));
    }
}

TEST_CASE ("A band given a zero or broken frequency or Q while playing stays finite", "[eq]")
{
    WetEqualiser eq;
    eq.prepare (rate);
    setBand (eq, 1, { 700.0f, 6.0f, 0.9f, EqBand::Shape::bell });

    auto x = testing::noise (4800, 5);
    eq.advance();
    eq.process (0, x.data(), 32);

    setBand (eq, 1, { 0.0f, 6.0f, -1.0f, EqBand::Shape::bell });
    for (std::size_t pos = 32; pos + 32 <= x.size(); pos += 32)
    {
        eq.advance();
        eq.process (0, x.data() + pos, 32);
    }

    for (auto v : x)
        REQUIRE (std::isfinite (v));
    REQUIRE (std::isfinite (eq.magnitudeDb (1000.0)));
}

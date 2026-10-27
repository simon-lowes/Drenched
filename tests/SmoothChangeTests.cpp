#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/Glide.h>
#include <drenched/Spaces.h>
#include <drenched/Svf.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

using namespace drenched;

/*  Controls can jump from one end of their range to the other in a single block, and
    the sound must follow without a click. A click shows as something sharp: the third
    difference of the output, which a steady 110 Hz tone has next to none of. */

namespace
{
    constexpr double rate = 48000.0;

    std::vector<float> tone (std::size_t length, double frequency = 110.0, float level = 0.25f)
    {
        std::vector<float> x (length);

        for (std::size_t n = 0; n < length; ++n)
            x[n] = level * static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (n) / rate));

        return x;
    }

    float roughness (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float most = 0.0f;

        for (auto i = std::max<std::size_t> (from, 3); i < std::min (to, x.size()); ++i)
            most = std::max (most, std::abs (x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3]));

        return most;
    }

    float peak (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        float most = 0.0f;

        for (auto i = from; i < std::min (to, x.size()); ++i)
            most = std::max (most, std::abs (x[i]));

        return most;
    }

    double decibels (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

    ImpulseResponseRequest room()
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = 3; // Live Room B: short, so the tests stay quick
        r.decaySeconds = bundledSpaces[3].decaySeconds;
        return r;
    }

    /** How far under the tone's level the sharpest moment after `changeAt` is, in dB:
        the tone plays through the reverb with `before`, then `after` from that block on. */
    double clickUnderLevel (ConvolutionReverb& reverb, const ReverbControls& before, const ReverbControls& after,
                            const std::function<void()>& atChange = {})
    {
        constexpr std::size_t changeAt = 48000, length = 96000, block = 480;
        auto l = tone (length), r = l;

        for (std::size_t pos = 0; pos < length; pos += block)
        {
            if (pos == changeAt && atChange)
                atChange();

            reverb.process (l.data() + pos, r.data() + pos, block, pos < changeAt ? before : after);
        }

        const auto level = std::max (peak (l, changeAt - 12000, changeAt), peak (l, length - 12000, length));
        return decibels (std::max (roughness (l, changeAt, length), roughness (r, changeAt, length)) / level);
    }
}

TEST_CASE ("Glides land exactly where they are sent", "[glide]")
{
    const auto coef = glide::coefficient (rate, 0.02);
    auto gain = 18.0f, frequency = 20.0f;

    for (int step = 0; step < 2000 && (gain != -3.0f || frequency != 500.0f); ++step)
    {
        gain = glide::towards (gain, -3.0f, coef);
        frequency = glide::towardsRatio (frequency, 500.0f, coef);
    }

    REQUIRE (gain == -3.0f);
    REQUIRE (frequency == 500.0f);
}

TEST_CASE ("The state-variable filters have the cookbook responses", "[svf]")
{
    struct Design { Svf::Coefficients svf; Biquad::Coefficients biquad; };
    const std::vector<Design> designs {
        { Svf::lowPass (rate, 500.0), Biquad::lowPass (rate, 500.0) },
        { Svf::highPass (rate, 200.0), Biquad::highPass (rate, 200.0) },
        { Svf::lowShelf (rate, 250.0, 12.0), Biquad::lowShelf (rate, 250.0, 12.0) },
        { Svf::lowShelf (rate, 250.0, -9.0), Biquad::lowShelf (rate, 250.0, -9.0) },
        { Svf::highShelf (rate, 2500.0, 12.0), Biquad::highShelf (rate, 2500.0, 12.0) },
        { Svf::highShelf (rate, 10000.0, -18.0), Biquad::highShelf (rate, 10000.0, -18.0) },
        { Svf::peak (rate, 1000.0, 18.0, 0.7), Biquad::peak (rate, 1000.0, 18.0, 0.7) },
        { Svf::peak (rate, 700.0, -12.0, 4.0), Biquad::peak (rate, 700.0, -12.0, 4.0) },
    };

    for (const auto& d : designs)
    {
        for (const auto frequency : { 50.0, 250.0, 700.0, 1000.0, 4000.0, 12000.0 })
        {
            Svf filter;
            filter.setCoefficients (d.svf);
            auto x = tone (19200, frequency, 0.5f);
            const auto in = x;
            filter.process (x.data(), x.size());

            double a = 0.0, b = 0.0;

            for (std::size_t n = 9600; n < x.size(); ++n)
            {
                a += static_cast<double> (x[n]) * x[n];
                b += static_cast<double> (in[n]) * in[n];
            }

            INFO (frequency << " Hz");
            REQUIRE (std::abs (10.0 * std::log10 (a / b) - Biquad::magnitudeDb (d.biquad, rate, frequency)) < 0.05);
        }
    }
}

TEST_CASE ("A filter whose response jumps glides there without a click", "[svf]")
{
    // A bell on the tone's own frequency, from +18 dB to -18 dB.
    auto x = tone (48000);
    Svf filter;
    filter.setCoefficients (Svf::peak (rate, 110.0, 18.0, 1.0));
    filter.process (x.data(), 24000);

    const auto coef = glide::coefficient (rate, 0.02);
    auto gain = 18.0f;

    for (std::size_t pos = 24000; pos < x.size(); pos += glide::block)
    {
        gain = glide::towards (gain, -18.0f, coef);
        filter.glideTo (Svf::peak (rate, 110.0, gain, 1.0), glide::block);
        filter.process (x.data() + pos, glide::block);
    }

    REQUIRE (gain == -18.0f);
    REQUIRE (decibels (roughness (x, 24000, x.size()) / peak (x, 0, 24000)) < -60.0);
}

TEST_CASE ("Changing the room while a note holds never clicks", "[reverb]")
{
    // From the room as recorded to the same room reversed, with a loud direct sound.
    auto changed = room();
    changed.reverse = true;
    changed.directLevelDb = 6.0f;

    ConvolutionReverb reverb;
    reverb.prepare (rate);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (room())));

    ReverbControls c;
    c.mix = 1.0f;
    const auto click = clickUnderLevel (reverb, c, c, [&]
    {
        reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (changed)));
    });

    REQUIRE (click < -45.0);
}

TEST_CASE ("The wet tone, EQ and width move without a click", "[reverb][eq]")
{
    using Change = std::function<void (ReverbControls&)>;
    const std::vector<std::pair<const char*, Change>> changes {
        { "low cut", [] (ReverbControls& c) { c.lowCutHz = 800.0f; } },
        { "high cut", [] (ReverbControls& c) { c.highCutHz = 300.0f; } },
        { "brightness", [] (ReverbControls& c) { c.brightnessDb = 12.0f; } },
        { "width", [] (ReverbControls& c) { c.widthPercent = 0.0f; } },
        { "mono bass", [] (ReverbControls& c) { c.monoBelowHz = 400.0f; } },
        { "EQ low shelf", [] (ReverbControls& c) { c.eq[0].gainDb = 18.0f; } },
        { "EQ bell on the tone", [] (ReverbControls& c) { c.eq[1] = { 110.0f, 18.0f, 2.0f }; } },
        { "EQ bell moving", [] (ReverbControls& c) { c.eq[2] = { 100.0f, -18.0f, 0.5f }; } },
        { "EQ high shelf", [] (ReverbControls& c) { c.eq[5].gainDb = 18.0f; } },
    };

    for (const auto& [name, change] : changes)
    {
        for (const auto backwards : { false, true })
        {
            ConvolutionReverb reverb;
            reverb.prepare (rate);
            reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (room())));

            ReverbControls plain, moved;
            plain.mix = moved.mix = 1.0f;
            change (moved);

            INFO (name << (backwards ? ", back" : ""));
            REQUIRE (clickUnderLevel (reverb, backwards ? moved : plain, backwards ? plain : moved) < -45.0);
        }
    }
}

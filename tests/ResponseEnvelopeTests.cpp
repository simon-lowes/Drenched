#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/ResponseEnvelope.h>
#include <drenched/Spaces.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
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

    /** Plays an impulse through a reverb built from the request, wet only. */
    std::vector<float> play (const ImpulseResponseRequest& request, double seconds, ReverbControls c = {})
    {
        c.mix = 1.0f;

        ConvolutionReverb reverb;
        reverb.prepare (sr);
        KernelBuilder builder (reverb);
        builder.buildNow (request);

        std::vector<float> l (at (seconds), 0.0f), r (l.size(), 0.0f);
        l[0] = r[0] = 1.0f;

        for (std::size_t pos = 0; pos < l.size(); pos += 480)
            reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (480, l.size() - pos), c);

        return l;
    }

    double decibels (double e) { return 10.0 * std::log10 (std::max (e, 1.0e-30)); }
}

TEST_CASE ("With reverse, gate and attack off the response is untouched", "[envelope]")
{
    const auto request = room();
    const auto original = synthesiseImpulseResponse (request);
    auto shaped = original;

    applyEnvelope (shaped, request);

    CHECK_FALSE (cutsTail (request));
    CHECK (shaped.paths == original.paths);
}

TEST_CASE ("A gate keeps the start of the room and then stops it dead", "[envelope]")
{
    auto request = room();
    request.gateMs = 200.0f;
    const auto original = synthesiseImpulseResponse (request);
    auto gated = original;

    applyEnvelope (gated, request);

    CHECK (cutsTail (request));
    REQUIRE (gated.length() == at (0.2) + at (gateReleaseSeconds));

    for (std::size_t p = 0; p < 4; ++p)
    {
        REQUIRE (gated.paths[p].size() == gated.length());

        for (std::size_t n = 0; n < at (0.2); ++n)
            CHECK (gated.paths[p][n] == original.paths[p][n]);

        // The release closes smoothly to nothing.
        CHECK (std::abs (gated.paths[p].back()) < 0.01f * std::abs (original.paths[p][gated.length() - 1]) + 1.0e-9f);
    }

    // A gate longer than the room changes nothing.
    request.gateMs = 1.0e5f;
    auto long_ = original;
    applyEnvelope (long_, request);
    CHECK (long_.paths == original.paths);
}

TEST_CASE ("Reverse plays the room backwards, swelling up to the direct sound", "[envelope]")
{
    auto request = room();
    request.reverse = true;
    const auto original = synthesiseImpulseResponse (request);
    auto reversed = original;

    applyEnvelope (reversed, request);

    CHECK (cutsTail (request));
    const auto length = reversed.length();
    REQUIRE (length > at (0.1));
    CHECK (length <= original.length());

    const auto fade = at (0.01);

    for (std::size_t p = 0; p < 4; ++p)
    {
        // Backwards sample for sample, apart from the fade at the start.
        for (std::size_t n = 0; n + fade < length; ++n)
            CHECK (reversed.paths[p][length - 1 - n] == original.paths[p][n]);

        // The end of the swell is far louder than its start.
        CHECK (energy (reversed.paths[p], length - at (0.05), length) > 1000.0 * energy (reversed.paths[p], 0, at (0.05)));
    }

    // What was cut was below -60 dB of the loudest moment.
    double kept = 0.0, dropped = 0.0;
    for (const auto& path : original.paths)
    {
        kept += energy (path, 0, length);
        dropped += energy (path, length, path.size());
    }
    CHECK (dropped < 1.0e-5 * kept);

    // With a gate, the gated length is what plays backwards.
    request.gateMs = 120.0f;
    auto reverseGate = original;
    applyEnvelope (reverseGate, request);
    REQUIRE (reverseGate.length() == at (0.12) + at (gateReleaseSeconds));
    CHECK (reverseGate.paths[0].back() == original.paths[0].front());
}

TEST_CASE ("Attack fades the room in", "[envelope]")
{
    auto request = room();
    request.attackMs = 100.0f;
    const auto original = synthesiseImpulseResponse (request);
    auto soft = original;

    applyEnvelope (soft, request);

    CHECK_FALSE (cutsTail (request));
    REQUIRE (soft.length() == original.length());

    for (std::size_t p = 0; p < 4; ++p)
    {
        CHECK (soft.paths[p][0] == 0.0f);
        CHECK (energy (soft.paths[p], 0, at (0.02)) < 0.05 * energy (original.paths[p], 0, at (0.02)));

        for (auto n = at (0.1); n < original.length(); ++n)
            CHECK (soft.paths[p][n] == original.paths[p][n]);
    }
}

TEST_CASE ("A gated reverb is silent after the gate, even with the living tail up", "[envelope][reverb]")
{
    auto request = room();
    request.gateMs = 150.0f;
    request.livingPercent = 100.0f;

    const auto out = play (request, 1.0);
    const auto open = energy (out, 0, at (0.15));
    const auto after = energy (out, at (0.15 + gateReleaseSeconds + 0.005), out.size());

    INFO ("open " << decibels (open) << " dB, after the gate " << decibels (after) << " dB");
    CHECK (open > 0.0);
    CHECK (decibels (after) < decibels (open) - 100.0);
}

TEST_CASE ("A reversed reverb swells and then stops", "[envelope][reverb]")
{
    auto request = room();
    request.reverse = true;

    const auto out = play (request, 2.0);
    const auto peak = static_cast<std::size_t> (std::distance (out.begin(), std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); })));

    INFO ("loudest at " << static_cast<double> (peak) / sr << " s");
    CHECK (peak > at (0.1)); // the direct sound comes at the end of the swell
    CHECK (energy (out, peak - at (0.05), peak) > 100.0 * energy (out, 0, at (0.05)));
    CHECK (decibels (energy (out, peak + at (0.05), out.size())) < decibels (energy (out, 0, peak)) - 60.0);
}

TEST_CASE ("Freeze still holds the room when it is gated", "[envelope][reverb][freeze]")
{
    auto request = room();
    request.gateMs = 100.0f;

    ReverbControls c;
    c.mix = 1.0f;

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    KernelBuilder builder (reverb);
    builder.buildNow (request);

    auto l = testing::noise (at (3.0), 7), r = l;
    const auto stop = at (1.0);
    std::fill (l.begin() + static_cast<std::ptrdiff_t> (stop), l.end(), 0.0f);
    std::fill (r.begin() + static_cast<std::ptrdiff_t> (stop), r.end(), 0.0f);

    for (std::size_t pos = 0; pos < l.size(); pos += 480)
    {
        c.freeze = pos >= at (0.9);
        reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (480, l.size() - pos), c);
    }

    const auto playing = energy (l, at (0.7), at (0.9)), held = energy (l, at (2.6), at (2.8));
    INFO ("playing " << decibels (playing) << " dB, held " << decibels (held) << " dB");
    CHECK (std::abs (decibels (held) - decibels (playing)) < 6.0);
}

TEST_CASE ("Cross-feed turns the opposite-side paths down and keeps a centred sound's level", "[envelope][crossfeed]")
{
    using P = ImpulseResponseSet;

    // A centred source: what reaches each output from both inputs together.
    const auto centred = [] (const ImpulseResponseSet& s, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        auto total = 0.0;
        for (auto n = from; n < std::min (to, s.length()); ++n)
        {
            const auto l = static_cast<double> (s.paths[P::leftToLeft][n]) + s.paths[P::rightToLeft][n];
            const auto r = static_cast<double> (s.paths[P::rightToRight][n]) + s.paths[P::leftToRight][n];
            total += l * l + r * r;
        }
        return total;
    };

    // True stereo (decorrelated paths), and one source recorded in stereo
    // (each output's two paths identical), as a stereo file loads.
    auto correlated = synthesiseImpulseResponse (room());
    correlated.paths[P::rightToLeft] = correlated.paths[P::leftToLeft];
    correlated.paths[P::leftToRight] = correlated.paths[P::rightToRight];

    for (const auto& original : { synthesiseImpulseResponse (room()), correlated })
    {
        auto full = original;
        applyCrossFeed (full, 100.0f);
        CHECK (full.paths == original.paths);

        for (const auto amount : { 0.0f, 50.0f })
        {
            auto shaped = original;
            applyCrossFeed (shaped, amount);

            INFO ("cross-feed " << amount << "%");

            // A centred sound keeps its level early and late.
            for (const auto [from, to] : { std::pair { 0.0, 0.05 }, std::pair { 0.05, 0.5 } })
            {
                INFO ("from " << from << " s to " << to << " s");
                CHECK (centred (shaped, at (from), at (to)) == Catch::Approx (centred (original, at (from), at (to))).epsilon (0.03));
            }

            // At any moment the cross paths are their originals turned down relative to the same-side paths.
            for (std::size_t n = 0; n < original.length(); n += 97)
            {
                if (original.paths[P::leftToLeft][n] == 0.0f)
                    continue;

                const auto gain = shaped.paths[P::leftToLeft][n] / original.paths[P::leftToLeft][n];
                REQUIRE (shaped.paths[P::rightToRight][n] == Catch::Approx (gain * original.paths[P::rightToRight][n]).margin (1.0e-6));
                REQUIRE (shaped.paths[P::leftToRight][n] == Catch::Approx (gain * amount / 100.0 * original.paths[P::leftToRight][n]).margin (1.0e-6));
                REQUIRE (shaped.paths[P::rightToLeft][n] == Catch::Approx (gain * amount / 100.0 * original.paths[P::rightToLeft][n]).margin (1.0e-6));
            }
        }
    }
}

TEST_CASE ("With no cross-feed the left input only reaches the left output", "[envelope][crossfeed][reverb]")
{
    auto request = room();
    request.crossFeedPercent = 0.0f;

    ReverbControls c;
    c.mix = 1.0f;

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    KernelBuilder builder (reverb);
    builder.buildNow (request);

    std::vector<float> l (at (1.0), 0.0f), r (l.size(), 0.0f);
    l[0] = 1.0f;

    for (std::size_t pos = 0; pos < l.size(); pos += 480)
        reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (480, l.size() - pos), c);

    INFO ("left " << decibels (energy (l, 0, l.size())) << " dB, right " << decibels (energy (r, 0, r.size())) << " dB");
    CHECK (decibels (energy (r, 0, r.size())) < decibels (energy (l, 0, l.size())) - 100.0);
}

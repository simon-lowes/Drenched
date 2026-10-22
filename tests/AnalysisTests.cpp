#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/RecordedResponse.h>
#include <drenched/ResponseAnalysis.h>
#include <drenched/Spaces.h>
#include <drenched/SpectrumTap.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    ImpulseResponseRequest room (std::array<float, 8> decay)
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = 3;
        r.decaySeconds = decay;
        return r;
    }

    std::size_t bandOf (float centre)
    {
        return static_cast<std::size_t> (std::find (analysisBands.begin(), analysisBands.end(), centre) - analysisBands.begin());
    }

    std::vector<float> sine (double frequency, float amplitude, std::size_t length)
    {
        std::vector<float> x (length);

        for (std::size_t n = 0; n < length; ++n)
            x[n] = amplitude * static_cast<float> (std::sin (2.0 * std::numbers::pi * frequency * static_cast<double> (n) / rate));

        return x;
    }
}

TEST_CASE ("The decay curve falls from 0 dB to -60 dB over the time shown", "[analysis]")
{
    const auto responses = synthesiseImpulseResponse (room ({ 1.6f, 1.6f, 1.5f, 1.4f, 1.3f, 1.2f, 1.0f, 0.8f }));
    const auto analysis = analyseResponse (responses);

    REQUIRE (analysis.decayDb.size() == 200);
    REQUIRE (analysis.decayDb.front() == Catch::Approx (0.0f).margin (0.01));
    REQUIRE (analysis.decayDb.back() < -50.0f);
    REQUIRE (std::is_sorted (analysis.decayDb.rbegin(), analysis.decayDb.rend()));
    REQUIRE (analysis.seconds > 0.8);
    REQUIRE (analysis.seconds < static_cast<double> (responses.length()) / rate + 1.0e-9);
    REQUIRE (analysis.decayStepSeconds * 199.0 == Catch::Approx (analysis.seconds));

    // The band decays are the same measurement the graph is set from.
    REQUIRE (analysis.rt60 == measureDecay (responses.paths, rate));
}

TEST_CASE ("The waterfall shows low bands outlasting high ones in a dark room", "[analysis]")
{
    const auto analysis = analyseResponse (synthesiseImpulseResponse (room ({ 2.4f, 2.4f, 2.2f, 1.8f, 1.2f, 0.8f, 0.5f, 0.35f })));

    REQUIRE (analysis.waterfall.size() == ResponseAnalysis::numSlices);
    REQUIRE (analysis.sliceSeconds * ResponseAnalysis::numSlices == Catch::Approx (analysis.seconds));

    float peak = ResponseAnalysis::floorDb;

    for (const auto& slice : analysis.waterfall)
        for (const auto level : slice)
        {
            REQUIRE (level <= 0.0f);
            REQUIRE (level >= ResponseAnalysis::floorDb);
            peak = std::max (peak, level);
        }

    REQUIRE (peak == 0.0f);

    // Halfway through, each band's fall from where it started.
    const auto fall = [&analysis] (float centre)
    {
        const auto b = bandOf (centre);
        return analysis.waterfall[1][b] - analysis.waterfall[ResponseAnalysis::numSlices / 2][b];
    };

    REQUIRE (fall (4000.0f) > fall (200.0f) + 10.0f);
}

TEST_CASE ("An empty response has nothing to show", "[analysis]")
{
    ImpulseResponseSet empty;
    empty.sampleRate = rate;
    const auto analysis = analyseResponse (empty);

    REQUIRE (analysis.waterfall.empty());
    REQUIRE (analysis.decayDb.empty());
    REQUIRE (analysis.seconds == 0.0);
}

TEST_CASE ("The live spectrum reads a sine in its own band at its level", "[analysis][spectrum]")
{
    SpectrumTap tap;
    const auto x = sine (1000.0, 0.5f, 6000);
    const float* channels[] { x.data(), x.data() };

    tap.push (channels, 2, x.size());
    const auto levels = tap.read (rate);

    REQUIRE (levels[bandOf (1000.0f)] == Catch::Approx (-6.02f).margin (1.0));
    REQUIRE (levels[bandOf (250.0f)] < -60.0f);
    REQUIRE (levels[bandOf (4000.0f)] < -60.0f);

    // The lowest bands are narrower than an FFT bin and still read about right.
    SpectrumTap low;
    const auto y = sine (25.0, 1.0f, 8192);
    const float* mono[] { y.data() };
    low.push (mono, 1, y.size());
    REQUIRE (low.read (rate)[bandOf (25.0f)] > -6.0f);
}

TEST_CASE ("The live spectrum of silence is at the floor, and pushing never allocates", "[analysis][spectrum][realtime]")
{
    SpectrumTap tap;

    for (const auto level : tap.read (rate))
        REQUIRE (level == SpectrumTap::floorDb);

    const auto x = testing::noise (48000, 21);
    const float* channels[] { x.data() };

    testing::AllocationCounter counter;

    for (std::size_t pos = 0; pos + 480 <= x.size(); pos += 480)
    {
        const float* block[] { channels[0] + pos };
        tap.push (block, 1, 480);
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("The reverb's spectrum is the wet signal, whatever the mix", "[analysis][spectrum]")
{
    ConvolutionReverb reverb;
    reverb.prepare (rate);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (room (bundledSpaces[3].decaySeconds))));

    auto l = testing::noise (24000, 22), r = testing::noise (24000, 23);
    ReverbControls c;
    c.mix = 0.0f;

    for (std::size_t pos = 0; pos < l.size(); pos += 480)
        reverb.process (l.data() + pos, r.data() + pos, 480, c);

    const auto levels = reverb.wetSpectrum().read (rate);
    REQUIRE (levels[bandOf (1000.0f)] > -60.0f);
}

TEST_CASE ("The builder keeps the response it built for the displays", "[analysis]")
{
    ConvolutionReverb reverb;
    reverb.prepare (rate);
    KernelBuilder builder (reverb);

    REQUIRE (builder.lastResponses() == nullptr);
    const auto before = builder.buildCount();

    builder.buildNow (room (bundledSpaces[3].decaySeconds));

    REQUIRE (builder.buildCount() == before + 1);
    REQUIRE (builder.lastResponses() != nullptr);
    REQUIRE (builder.lastResponses()->length() > 0);
}

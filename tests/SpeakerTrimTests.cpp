#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/LivingTail.h>
#include <drenched/Mix.h>
#include <drenched/Spaces.h>
#include <drenched/SpeakerTrim.h>
#include <drenched/SurroundResponse.h>

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

    /** Runs a trim over two channels in blocks, with the same settings throughout. */
    void run (SpeakerTrim& trim, std::vector<float>& a, std::vector<float>& b, const std::array<float, 2>& levels,
              const std::array<float, 2>& delays, std::size_t from = 0, std::size_t to = 0, std::size_t block = 300)
    {
        to = to == 0 ? a.size() : to;

        for (auto pos = from; pos < to; pos += block)
        {
            const auto n = std::min (block, to - pos);
            float* channels[] { a.data() + pos, b.data() + pos };
            trim.process (channels, 2, n, levels.data(), delays.data());
        }
    }
}

TEST_CASE ("At 0 dB with no delay every speaker passes unchanged", "[speakertrim]")
{
    SpeakerTrim trim;
    trim.prepare (rate, 2);

    auto a = testing::noise (5000, 1u), b = testing::noise (5000, 2u);
    const auto originalA = a, originalB = b;
    run (trim, a, b, { 0.0f, 0.0f }, { 0.0f, 0.0f });

    CHECK (a == originalA);
    CHECK (b == originalB);
    CHECK (trim.isNeutral());
}

TEST_CASE ("A speaker's delay and level move and scale its signal", "[speakertrim]")
{
    SpeakerTrim trim;
    trim.prepare (rate, 2);

    // Let the new settings settle on silence, then send an impulse.
    std::vector<float> a (20000, 0.0f), b (20000, 0.0f);
    run (trim, a, b, { -6.0f, 0.0f }, { 30.0f, 0.0f }, 0, 4800);
    a[4800] = 1.0f;
    b[4800] = 1.0f;
    run (trim, a, b, { -6.0f, 0.0f }, { 30.0f, 0.0f }, 4800);

    const auto peakOf = [] (const std::vector<float>& x) { return std::max_element (x.begin(), x.end()) - x.begin(); };

    CHECK (peakOf (a) == 4800 + 1440);
    CHECK (a[4800 + 1440] == Catch::Approx (std::pow (10.0, -6.0 / 20.0)).epsilon (1.0e-4));
    CHECK (peakOf (b) == 4800);
    CHECK (b[4800] == 1.0f);
    CHECK_FALSE (trim.isNeutral());
}

TEST_CASE ("The lowest level silences a speaker", "[speakertrim]")
{
    SpeakerTrim trim;
    trim.prepare (rate, 2);

    auto a = testing::noise (20000, 3u), b = testing::noise (20000, 4u);
    run (trim, a, b, { SpeakerTrim::minLevelDb, 0.0f }, { 0.0f, 0.0f });

    const auto late = std::max_element (a.begin() + 10000, a.end(), [] (float x, float y) { return std::abs (x) < std::abs (y); });
    CHECK (*late == 0.0f);
}

TEST_CASE ("Changing a speaker's delay or level while it plays does not click", "[speakertrim]")
{
    SpeakerTrim trim;
    trim.prepare (rate, 2);

    // A 200 Hz sine.
    std::vector<float> a (48000), b (48000);

    for (std::size_t n = 0; n < a.size(); ++n)
        a[n] = b[n] = static_cast<float> (std::sin (2.0 * std::numbers::pi * 200.0 * static_cast<double> (n) / rate));

    run (trim, a, b, { 0.0f, 0.0f }, { 0.0f, 0.0f }, 0, 9600);
    run (trim, a, b, { 0.0f, -12.0f }, { 37.3f, 0.0f }, 9600, 24000);
    run (trim, a, b, { 6.0f, 0.0f }, { 2.0f, 0.0f }, 24000);

    // A click is a sudden bend: the sine's own bend between samples is below
    // 0.001, a step of the signal would be hundreds of times that.
    for (const auto* x : { &a, &b })
    {
        auto biggest = 0.0f;

        for (std::size_t n = 2; n < x->size(); ++n)
            biggest = std::max (biggest, std::abs ((*x)[n] - 2.0f * (*x)[n - 1] + (*x)[n - 2]));

        CHECK (biggest < 0.02f);
    }
}

TEST_CASE ("Each surround speaker's reverb gets its own delay and level, and the dry sound none", "[speakertrim][surround]")
{
    const auto layout = layouts::fivePointOne();

    ImpulseResponseRequest request;
    request.sampleRate = rate;
    request.space = 3;
    request.decaySeconds = bundledSpaces[3].decaySeconds;
    request.outputs = layout;

    const auto render = [&] (const ReverbControls& c)
    {
        const auto stereo = synthesiseImpulseResponse (request);
        ConvolutionReverb reverb;
        reverb.prepare (rate, layout, layout);
        reverb.submit (std::make_unique<ReverbKernel> (spreadResponses (stereo, layout, mixingTimeSeconds (request)),
                                                       designLivingTail (request, stereo)));

        std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (48000, 0.0f));
        std::vector<float*> pointers (layout.size());
        const auto start = std::size_t (4800); // after the trim has settled
        channels[0][start] = 1.0f;

        for (std::size_t pos = 0; pos < 48000; pos += 512)
        {
            for (std::size_t k = 0; k < layout.size(); ++k)
                pointers[k] = channels[k].data() + pos;

            reverb.process (pointers.data(), pointers.size(), std::min<std::size_t> (512, 48000 - pos), c);
        }

        for (auto& ch : channels)
            ch.erase (ch.begin(), ch.begin() + static_cast<std::ptrdiff_t> (start));

        return channels;
    };

    ReverbControls c;
    c.mix = 0.5f;
    c.predelayMs = 0.0f;
    const auto plain = render (c);

    constexpr std::size_t leftSurround = 4;
    c.speakerDelayMs[leftSurround] = 25.0f;
    c.speakerLevelDb[leftSurround] = -6.0f;
    c.speakerLevelDb[0] = 3.0f;
    const auto trimmed = render (c);

    // The left surround is the same reverb, 25 ms later and 6 dB down.
    const auto shift = std::size_t (1200);
    const auto gain = static_cast<float> (std::pow (10.0, -6.0 / 20.0));
    auto error = 0.0, total = 0.0;

    for (std::size_t n = 0; n + shift < plain[leftSurround].size(); ++n)
    {
        const auto d = static_cast<double> (trimmed[leftSurround][n + shift] - gain * plain[leftSurround][n]);
        error += d * d;
        total += static_cast<double> (plain[leftSurround][n]) * plain[leftSurround][n];
    }

    CHECK (error / total < 1.0e-8);

    for (std::size_t n = 0; n < shift; ++n)
        REQUIRE (trimmed[leftSurround][n] == 0.0f);

    // The left speaker's dry impulse is untouched; only its reverb is 3 dB up.
    const auto dry = equalPowerMix (0.5f).dry;
    CHECK (trimmed[0][0] - plain[0][0] == Catch::Approx ((std::pow (10.0, 3.0 / 20.0) - 1.0) * (plain[0][0] - dry)).margin (1.0e-5));

    // Speakers left alone are unchanged.
    CHECK (trimmed[1] == plain[1]);
    CHECK (trimmed[2] == plain[2]);
}

TEST_CASE ("Speaker trims never allocate on the audio thread", "[speakertrim][realtime]")
{
    SpeakerTrim trim;
    trim.prepare (rate, 2);
    auto a = testing::noise (20000, 5u), b = testing::noise (20000, 6u);

    testing::AllocationCounter counter;
    run (trim, a, b, { -3.0f, 4.0f }, { 12.0f, 80.0f }, 0, 10000);
    run (trim, a, b, { 0.0f, -70.0f }, { 0.0f, 100.0f }, 10000);

    REQUIRE (counter.count() == 0);
}

#include <drenched/ConvolutionReverb.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/KernelBuilder.h>
#include <drenched/LivingTail.h>
#include <drenched/Mix.h>
#include <drenched/Spaces.h>
#include <drenched/SurroundResponse.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    ImpulseResponseRequest roomRequest (const SpeakerLayout& outputs = layouts::stereo())
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = 3; // the live room: short, so the tests stay quick
        r.decaySeconds = bundledSpaces[3].decaySeconds;
        r.outputs = outputs;
        return r;
    }

    double energyOf (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
    {
        double e = 0.0;

        for (auto n = from; n < (to == 0 ? x.size() : to); ++n)
            e += static_cast<double> (x[n]) * x[n];

        return e;
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;

        for (auto n = from; n < to; ++n)
        {
            ab += static_cast<double> (a[n]) * b[n];
            aa += static_cast<double> (a[n]) * a[n];
            bb += static_cast<double> (b[n]) * b[n];
        }

        return ab / std::sqrt (aa * bb);
    }

    std::unique_ptr<ReverbKernel> kernelFor (const ImpulseResponseRequest& request)
    {
        const auto stereo = synthesiseImpulseResponse (request);
        return std::make_unique<ReverbKernel> (spreadResponses (stereo, request.outputs, mixingTimeSeconds (request)),
                                               designLivingTail (request, stereo));
    }

    /** Runs a reverb over channel buffers in blocks. */
    void run (ConvolutionReverb& reverb, std::vector<std::vector<float>>& channels, const ReverbControls& c,
              std::size_t block = 512)
    {
        std::vector<float*> pointers (channels.size());
        const auto length = channels[0].size();

        for (std::size_t pos = 0; pos < length; pos += block)
        {
            const auto n = std::min (block, length - pos);

            for (std::size_t k = 0; k < channels.size(); ++k)
                pointers[k] = channels[k].data() + pos;

            reverb.process (pointers.data(), pointers.size(), n, c);
        }
    }
}

TEST_CASE ("Spreading over a stereo layout changes nothing", "[surround]")
{
    const auto stereo = synthesiseImpulseResponse (roomRequest());
    const auto spread = spreadResponses (stereo, layouts::stereo(), 0.05);

    REQUIRE (spread.numOutputs() == 2);
    CHECK (spread.paths[0][0] == stereo.paths[ImpulseResponseSet::leftToLeft]);
    CHECK (spread.paths[0][1] == stereo.paths[ImpulseResponseSet::leftToRight]);
    CHECK (spread.paths[1][0] == stereo.paths[ImpulseResponseSet::rightToLeft]);
    CHECK (spread.paths[1][1] == stereo.paths[ImpulseResponseSet::rightToRight]);
}

TEST_CASE ("A room spreads over 7.1.4 with its level, decay and front image kept", "[surround]")
{
    const auto request = roomRequest();
    const auto stereo = synthesiseImpulseResponse (request);
    const auto layout = layouts::sevenPointOnePointFour();
    const auto tm = mixingTimeSeconds (request);
    const auto spread = spreadResponses (stereo, layout, tm);

    REQUIRE (spread.numOutputs() == layout.size());

    // The same overall level as stereo.
    double total = 0.0, reference = 0.0;

    for (const auto& source : spread.paths)
        for (const auto& p : source)
            total += energyOf (p);

    for (const auto& p : stereo.paths)
        reference += energyOf (p);

    CHECK (std::abs (total / reference - 1.0) < 1.0e-4);

    // No reverberation in the LFE channel.
    CHECK (energyOf (spread.paths[0][3]) == 0.0);
    CHECK (energyOf (spread.paths[1][3]) == 0.0);

    // The front pair are the stereo paths, only scaled.
    const auto lateFrom = static_cast<std::size_t> (2.0 * tm * rate);
    const auto lateTo = stereo.length();
    CHECK (correlation (spread.paths[0][0], stereo.paths[ImpulseResponseSet::leftToLeft], 0, lateTo) > 0.9999);

    // The centre carries the panned direct sound: correlated with the pair early on.
    const auto earlyTo = static_cast<std::size_t> (tm * rate);
    std::vector<float> pair (stereo.length());

    for (std::size_t n = 0; n < pair.size(); ++n)
        pair[n] = stereo.paths[ImpulseResponseSet::leftToLeft][n] + stereo.paths[ImpulseResponseSet::leftToRight][n];

    CHECK (correlation (spread.paths[0][2], pair, 0, earlyTo) > 0.95);

    // Every other speaker's tail is decorrelated from the front left and from each other.
    for (std::size_t a = 2; a < layout.size(); ++a)
    {
        if (layout[a].lfe)
            continue;

        INFO ("speaker " << a);
        CHECK (std::abs (correlation (spread.paths[0][a], spread.paths[0][0], lateFrom, lateTo)) < 0.2);

        for (std::size_t b = a + 1; b < layout.size(); ++b)
            if (! layout[b].lfe)
                CHECK (std::abs (correlation (spread.paths[0][a], spread.paths[0][b], lateFrom, lateTo)) < 0.2);
    }

    // Each speaker decays like the room.
    const auto front = testing::measureRt60 (spread.paths[0][0], rate, 1000.0);

    for (const std::size_t k : { 2u, 4u, 6u, 8u, 10u })
    {
        INFO ("speaker " << k);
        CHECK (std::abs (testing::measureRt60 (spread.paths[0][k], rate, 1000.0) / front - 1.0) < 0.1);
    }

    // Early reflections stay mostly in front: the sides carry less of them, the heights less again.
    const auto earlyShare = [&] (std::size_t k) { return energyOf (spread.paths[0][k], 0, earlyTo) / energyOf (spread.paths[0][k]); };
    CHECK (earlyShare (4) < earlyShare (0));
    CHECK (earlyShare (8) < earlyShare (4));

    // The same request always gives the same result.
    CHECK (spreadResponses (stereo, layout, tm).paths[1][9] == spread.paths[1][9]);
}

TEST_CASE ("Surround routing: dry to its own speaker, LFE kept dry, wet everywhere else", "[surround]")
{
    const auto layout = layouts::sevenPointOnePointFour();
    const auto request = roomRequest (layout);

    ConvolutionReverb reverb;
    reverb.prepare (rate, layout, layout);
    reverb.submit (kernelFor (request));

    std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (24000, 0.0f));
    channels[0][0] = 1.0f; // left
    channels[3][0] = 1.0f; // LFE

    ReverbControls c;
    c.mix = 0.5f;
    c.predelayMs = 0.0f;
    run (reverb, channels, c);

    REQUIRE (reverb.isReady());

    const auto dry = equalPowerMix (0.5f).dry;

    // The LFE output is its own dry input, and nothing else.
    CHECK (std::abs (channels[3][0] - dry) < 1.0e-6f);
    CHECK (energyOf (channels[3], 1) == 0.0);

    // Every other speaker has the room; the left one has its dry impulse too.
    for (std::size_t k = 0; k < layout.size(); ++k)
    {
        if (k == 3)
            continue;

        INFO ("speaker " << k);
        CHECK (energyOf (channels[k], 1) > 1.0e-6);
    }

    CHECK (channels[0][0] > dry * 0.99f);
}

TEST_CASE ("Mono and stereo inputs reach the right surround speakers dry", "[surround]")
{
    const auto layout = layouts::fivePointOne();
    ReverbControls c;
    c.mix = 0.0f;

    SECTION ("mono goes to the centre")
    {
        ConvolutionReverb reverb;
        reverb.prepare (rate, layouts::mono(), layout);
        reverb.submit (kernelFor (roomRequest (layout)));

        std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (2048, 0.0f));
        channels[0] = testing::noise (2048, 4);
        const auto input = channels[0];
        run (reverb, channels, c);

        for (std::size_t k = 0; k < layout.size(); ++k)
        {
            INFO ("speaker " << k);
            const auto expected = k == 2 ? energyOf (input) : 0.0;
            CHECK (std::abs (energyOf (channels[k]) - expected) < 1.0e-6 * (1.0 + expected));
        }
    }

    SECTION ("stereo goes to the front pair")
    {
        ConvolutionReverb reverb;
        reverb.prepare (rate, layouts::stereo(), layout);

        std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (2048, 0.0f));
        channels[0] = testing::noise (2048, 5);
        channels[1] = testing::noise (2048, 6);
        const auto l = channels[0], r = channels[1];
        run (reverb, channels, c);

        CHECK (channels[0] == l);
        CHECK (channels[1] == r);

        for (std::size_t k = 2; k < layout.size(); ++k)
            CHECK (energyOf (channels[k]) == 0.0);
    }

    SECTION ("stereo folded into mono keeps both sides")
    {
        ConvolutionReverb reverb;
        reverb.prepare (rate, layouts::stereo(), layouts::mono());

        std::vector<std::vector<float>> channels (2, std::vector<float> (64, 0.0f));
        channels[0][10] = 1.0f;
        channels[1][20] = 1.0f;
        run (reverb, channels, c);

        CHECK (channels[0][10] > 0.5f);
        CHECK (channels[0][20] > 0.5f);
    }
}

TEST_CASE ("A kernel for another layout is not played", "[surround]")
{
    ConvolutionReverb reverb;
    reverb.prepare (rate, layouts::stereo(), layouts::fivePointOne());
    reverb.submit (kernelFor (roomRequest (layouts::stereo())));

    std::vector<std::vector<float>> channels (6, std::vector<float> (512, 0.0f));
    ReverbControls c;
    run (reverb, channels, c);

    CHECK_FALSE (reverb.isReady());

    while (reverb.takeRetired() != nullptr) {}
}

TEST_CASE ("Width at zero makes every surround speaker play the same reverb", "[surround]")
{
    const auto layout = layouts::fivePointOne();
    ConvolutionReverb reverb;
    reverb.prepare (rate, layouts::stereo(), layout);
    reverb.submit (kernelFor (roomRequest (layout)));

    std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (12000, 0.0f));
    channels[0][0] = 1.0f;

    ReverbControls c;
    c.mix = 1.0f;
    c.widthPercent = 0.0f;
    run (reverb, channels, c);

    for (const std::size_t k : { 1u, 2u, 4u, 5u })
        for (std::size_t n = 0; n < channels[0].size(); n += 37)
            REQUIRE (std::abs (channels[k][n] - channels[0][n]) < 1.0e-5f);
}

TEST_CASE ("The builder makes kernels for the layout asked for", "[surround]")
{
    ConvolutionReverb reverb;
    const auto layout = layouts::fivePointOne();
    reverb.prepare (rate, layout, layout);

    KernelBuilder builder (reverb);
    builder.buildNow (roomRequest (layout));

    std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (512, 0.0f));
    ReverbControls c;
    run (reverb, channels, c);

    CHECK (reverb.isReady());
}

TEST_CASE ("A 16-channel reverb never allocates on the audio thread", "[surround][realtime]")
{
    SpeakerLayout layout = layouts::sevenPointOnePointFour();
    layout.push_back ({ -60.0f, 0.0f });
    layout.push_back ({ 60.0f, 0.0f });
    layout.push_back ({ -90.0f, 45.0f });
    layout.push_back ({ 90.0f, 45.0f }); // 9.1.6
    REQUIRE (layout.size() == maxChannels);

    ConvolutionReverb reverb;
    reverb.prepare (rate, layout, layout);
    reverb.submit (kernelFor (roomRequest (layout)));

    std::vector<std::vector<float>> channels (layout.size(), testing::noise (8192, 9));
    std::vector<float*> pointers (layout.size());

    ReverbControls c;
    c.mix = 0.4f;
    c.monoBelowHz = 120.0f;
    c.lowCutHz = 80.0f;
    c.brightnessDb = 2.0f;
    c.ducking = 0.3f;

    testing::AllocationCounter counter;

    for (std::size_t pos = 0; pos < 8192; pos += 300)
    {
        const auto n = std::min<std::size_t> (300, 8192 - pos);

        for (std::size_t k = 0; k < layout.size(); ++k)
            pointers[k] = channels[k].data() + pos;

        c.freeze = pos > 4000;
        reverb.process (pointers.data(), pointers.size(), n, c);
    }

    CHECK (counter.count() == 0);
}

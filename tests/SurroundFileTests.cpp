#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/RecordedResponse.h>
#include <drenched/SurroundResponse.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    /** A surround recording of a room: 30 ms of silence, then for each speaker
        its own decaying noise (half a second RT60), with the direct sound in
        the front speakers. The LFE channel carries a low rumble. */
    std::vector<std::vector<float>> surroundFile (const SpeakerLayout& layout)
    {
        const auto pad = static_cast<std::size_t> (0.03 * rate), length = static_cast<std::size_t> (0.8 * rate);
        std::vector<std::vector<float>> file;

        for (std::size_t k = 0; k < layout.size(); ++k)
        {
            const auto noise = testing::noise (length, 101u + static_cast<unsigned> (k));
            std::vector<float> c (pad + length, 0.0f);

            for (std::size_t n = 0; n < length; ++n)
                c[pad + n] = 0.3f * noise[n] * static_cast<float> (std::pow (10.0, -3.0 * static_cast<double> (n) / (0.5 * rate)));

            if (layout[k].isFront())
                c[pad] = 0.9f;

            file.push_back (std::move (c));
        }

        return file;
    }

    double energyOf (const std::vector<float>& x)
    {
        double e = 0.0;

        for (auto v : x)
            e += static_cast<double> (v) * v;

        return e;
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, std::size_t to = 0)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        const auto end = to == 0 ? std::min (a.size(), b.size()) : std::min ({ a.size(), b.size(), to });

        for (std::size_t n = 0; n < end; ++n)
        {
            ab += static_cast<double> (a[n]) * b[n];
            aa += static_cast<double> (a[n]) * a[n];
            bb += static_cast<double> (b[n]) * b[n];
        }

        return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
    }

    double totalEnergy (const SpatialResponseSet& set)
    {
        double e = 0.0;

        for (const auto& source : set.paths)
            for (const auto& p : source)
                e += energyOf (p);

        return e;
    }

    double totalEnergy (const ImpulseResponseSet& set)
    {
        double e = 0.0;

        for (const auto& p : set.paths)
            e += energyOf (p);

        return e;
    }

    ImpulseResponseRequest requestFor (const std::shared_ptr<const RecordedResponse>& recorded, const SpeakerLayout& outputs)
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.recorded = recorded;
        r.decaySeconds = recorded->room.decaySeconds;
        r.outputs = outputs;
        return r;
    }
}

TEST_CASE ("A surround file keeps its channels and folds down to stereo", "[surround][recorded]")
{
    const auto layout = layouts::fivePointOne(); // L R C LFE Ls Rs
    const auto recorded = prepareRecordedResponse (surroundFile (layout), rate, "5.1 room", layout);

    REQUIRE (recorded->isSurround());
    CHECK (recorded->channels == 6);
    CHECK (recorded->layout == layout);
    REQUIRE (recorded->speakers.size() == 6);

    // The fold-down is one source heard in stereo: both inputs feed both sides.
    const auto& paths = recorded->responses.paths;
    CHECK (paths[ImpulseResponseSet::leftToLeft] == paths[ImpulseResponseSet::rightToLeft]);
    CHECK (paths[ImpulseResponseSet::leftToRight] == paths[ImpulseResponseSet::rightToRight]);

    // Left: L in full, C and Ls 3 dB down, nothing of the right or the LFE.
    const auto& sp = recorded->speakers;
    const auto half = std::sqrt (0.5f);
    float worst = 0.0f;

    for (std::size_t n = 0; n < paths[0].size(); ++n)
        worst = std::max (worst, std::abs (paths[ImpulseResponseSet::leftToLeft][n] - (sp[0][n] + half * sp[2][n] + half * sp[4][n])));

    CHECK (worst < 1.0e-5f);
    CHECK (recorded->room.decaySeconds[4] > 0.4f);
    CHECK (recorded->room.decaySeconds[4] < 0.6f);

    // The trim before the direct sound is the same on every channel.
    CHECK (std::abs (sp[0][static_cast<std::size_t> (0.001 * rate)]) > 0.0f);
}

TEST_CASE ("A file with channels Drenched can't place is refused with a reason", "[surround][recorded]")
{
    using Catch::Matchers::ContainsSubstring;
    const auto layout = layouts::fivePointOne();
    const auto file = surroundFile (layout);

    CHECK_THROWS_WITH (prepareRecordedResponse (file, rate, "no layout"), ContainsSubstring ("which speaker"));
    CHECK_THROWS_WITH (prepareRecordedResponse ({ file[0], file[1], file[2] }, rate, "wrong layout", layout), ContainsSubstring ("which speaker"));
    const Speaker lfe { 0.0f, 0.0f, true };
    CHECK_THROWS_AS (prepareRecordedResponse ({ file[3], file[3], file[3] }, rate, "lfe only", { lfe, lfe, lfe }), std::invalid_argument);
}

TEST_CASE ("A surround recording plays its own channels on the same speakers", "[surround]")
{
    const auto layout = layouts::fivePointOne();
    const auto recorded = prepareRecordedResponse (surroundFile (layout), rate, "5.1 room", layout);
    const auto placed = placeSpeakerResponses (recorded->speakers, layout, recorded->responses, layout, 0.05, 100.0f);

    REQUIRE (placed.numOutputs() == 6);

    for (std::size_t k = 0; k < layout.size(); ++k)
    {
        for (std::size_t s = 0; s < 2; ++s)
        {
            INFO ("speaker " << k << ", source " << s);

            if (layout[k].lfe)
                CHECK (energyOf (placed.paths[s][k]) == 0.0);
            else
                CHECK (correlation (placed.paths[s][k], recorded->speakers[k]) > 0.9999);
        }
    }

    CHECK (std::abs (totalEnergy (placed) / totalEnergy (recorded->responses) - 1.0) < 1.0e-6);
}

TEST_CASE ("A 7.1.4 recording on 5.1 folds its heights and sides into the nearest speakers", "[surround]")
{
    const auto from = layouts::sevenPointOnePointFour(); // L R C LFE Lss Rss Lsr Rsr Ltf Rtf Ltr Rtr
    const auto to = layouts::fivePointOne();             // L R C LFE Ls Rs
    const auto recorded = prepareRecordedResponse (surroundFile (from), rate, "7.1.4 room", from);
    const auto placed = placeSpeakerResponses (recorded->speakers, from, recorded->responses, to, 0.05, 100.0f);
    const auto& sp = recorded->speakers;

    // Ls plays the side and the rear left; L plays the front left and the height above it.
    CHECK (correlation (placed.paths[0][4], sp[4]) > 0.5);
    CHECK (correlation (placed.paths[0][4], sp[6]) > 0.5);
    CHECK (correlation (placed.paths[0][0], sp[8]) > 0.3);
    CHECK (correlation (placed.paths[0][0], sp[1]) < 0.1);
    CHECK (energyOf (placed.paths[0][3]) == 0.0);
    CHECK (std::abs (totalEnergy (placed) / totalEnergy (recorded->responses) - 1.0) < 1.0e-6);
}

TEST_CASE ("A 5.1 recording on 7.1.4 fills the speakers it lacks from its fold-down", "[surround]")
{
    const auto from = layouts::fivePointOne();
    const auto to = layouts::sevenPointOnePointFour();
    const auto recorded = prepareRecordedResponse (surroundFile (from), rate, "5.1 room", from);
    const auto placed = placeSpeakerResponses (recorded->speakers, from, recorded->responses, to, 0.05, 100.0f);
    const auto& sp = recorded->speakers;

    REQUIRE (placed.numOutputs() == 12);

    // The side speakers are near the recording's surrounds and play them.
    CHECK (correlation (placed.paths[0][4], sp[4]) > 0.9999);

    // The heights and rears get reverb of their own, not a copy of a captured channel.
    for (const std::size_t k : { 6u, 7u, 8u, 9u, 10u, 11u })
    {
        INFO ("speaker " << k);
        CHECK (energyOf (placed.paths[0][k]) > 0.0);

        for (const auto& captured : sp)
            CHECK (std::abs (correlation (placed.paths[0][k], captured)) < 0.5);
    }

    CHECK (std::abs (totalEnergy (placed) / totalEnergy (recorded->responses) - 1.0) < 1.0e-6);
}

TEST_CASE ("Cross-feed keeps each input of a surround recording on its own side", "[surround]")
{
    const auto layout = layouts::fivePointOne();
    const auto recorded = prepareRecordedResponse (surroundFile (layout), rate, "5.1 room", layout);
    const auto full = placeSpeakerResponses (recorded->speakers, layout, recorded->responses, layout, 0.05, 100.0f);
    const auto none = placeSpeakerResponses (recorded->speakers, layout, recorded->responses, layout, 0.05, 0.0f);

    // The left input no longer reaches R or Rs; the right input no longer reaches L or Ls.
    CHECK (energyOf (none.paths[0][1]) == 0.0);
    CHECK (energyOf (none.paths[0][5]) == 0.0);
    CHECK (energyOf (none.paths[1][0]) == 0.0);
    CHECK (energyOf (none.paths[1][4]) == 0.0);
    CHECK (energyOf (none.paths[0][2]) > 0.0); // both still reach the centre

    // Each input keeps its level.
    double left = 0.0, right = 0.0;

    for (std::size_t k = 0; k < layout.size(); ++k)
    {
        left += energyOf (none.paths[0][k]);
        right += energyOf (none.paths[1][k]);
    }

    CHECK (std::abs (left / right - 1.0) < 1.0e-6);
    CHECK (std::abs (totalEnergy (none) / totalEnergy (full) - 1.0) < 1.0e-6);
}

TEST_CASE ("The builder plays a surround recording's own channels", "[surround][builder]")
{
    const auto layout = layouts::fivePointOne();
    const auto recorded = prepareRecordedResponse (surroundFile (layout), rate, "5.1 room", layout);

    ConvolutionReverb reverb;
    reverb.prepare (rate, layout, layout);
    KernelBuilder builder (reverb);
    builder.buildNow (requestFor (recorded, layout));

    // An impulse into the left input comes out of Ls as the recording's Ls.
    std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (static_cast<std::size_t> (0.5 * rate), 0.0f));
    channels[0][0] = 1.0f;
    std::vector<float*> pointers;

    for (auto& ch : channels)
        pointers.push_back (ch.data());

    ReverbControls c;
    c.mix = 1.0f;

    for (std::size_t pos = 0; pos < channels[0].size(); pos += 256)
    {
        std::vector<float*> block;

        for (auto* p : pointers)
            block.push_back (p + pos);

        reverb.process (block.data(), block.size(), std::min<std::size_t> (256, channels[0].size() - pos), c);
    }

    REQUIRE (reverb.isReady());
    const auto window = static_cast<std::size_t> (0.2 * rate);
    CHECK (correlation (channels[4], recorded->speakers[4], window) > 0.95);
    CHECK (correlation (channels[5], recorded->speakers[5], window) > 0.95);
    CHECK (correlation (channels[4], recorded->speakers[5], window) < 0.2);
}

TEST_CASE ("On stereo a surround recording plays its fold-down", "[surround][builder]")
{
    const auto layout = layouts::fivePointOne();
    const auto recorded = prepareRecordedResponse (surroundFile (layout), rate, "5.1 room", layout);

    ConvolutionReverb reverb;
    reverb.prepare (rate);
    KernelBuilder builder (reverb);
    builder.buildNow (requestFor (recorded, layouts::stereo()));

    std::vector<float> l (static_cast<std::size_t> (0.5 * rate), 0.0f), r (l.size(), 0.0f);
    l[0] = 1.0f;
    ReverbControls c;
    c.mix = 1.0f;

    for (std::size_t pos = 0; pos < l.size(); pos += 256)
        reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (256, l.size() - pos), c);

    REQUIRE (reverb.isReady());
    const auto window = static_cast<std::size_t> (0.2 * rate);
    CHECK (correlation (l, recorded->responses.paths[ImpulseResponseSet::leftToLeft], window) > 0.95);
    CHECK (correlation (r, recorded->responses.paths[ImpulseResponseSet::leftToRight], window) > 0.95);
}

TEST_CASE ("Editing the decay reshapes every channel of a surround recording", "[surround][builder]")
{
    const auto layout = layouts::fivePointOne();
    const auto recorded = prepareRecordedResponse (surroundFile (layout), rate, "5.1 room", layout);

    ConvolutionReverb reverb;
    reverb.prepare (rate, layout, layout);
    KernelBuilder builder (reverb);
    auto request = requestFor (recorded, layout);

    for (auto& d : request.decaySeconds)
        d *= 0.5f;

    builder.buildNow (request);

    std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (static_cast<std::size_t> (0.8 * rate), 0.0f));
    channels[0][0] = 1.0f;
    ReverbControls c;
    c.mix = 1.0f;

    for (std::size_t pos = 0; pos < channels[0].size(); pos += 256)
    {
        std::vector<float*> block;

        for (auto& ch : channels)
            block.push_back (ch.data() + pos);

        reverb.process (block.data(), block.size(), std::min<std::size_t> (256, channels[0].size() - pos), c);
    }

    for (const std::size_t k : { 0u, 2u, 4u, 5u })
    {
        const auto rt = testing::measureRt60 (channels[k], rate, 1000.0);
        INFO ("speaker " << k << ": " << rt << " s, asked " << request.decaySeconds[4] << " s");
        CHECK (std::abs (rt / request.decaySeconds[4] - 1.0) < 0.15);
    }
}

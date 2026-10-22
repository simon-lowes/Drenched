#include <drenched/Ambisonics.h>
#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/RecordedResponse.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    /** A sound from one direction (a speaker's azimuth and elevation) in AmbiX, SN3D. */
    std::array<float, 4> encode (const Speaker& from)
    {
        constexpr auto radians = static_cast<float> (std::numbers::pi / 180.0);
        const auto a = -from.azimuth * radians, e = from.elevation * radians;
        return { 1.0f, std::sin (a) * std::cos (e), std::sin (e), std::cos (a) * std::cos (e) };
    }

    /** A B-format recording of a room, AmbiX: 20 ms of silence, the direct sound from
        `source`, then from 5 ms later a diffuse tail (half a second RT60) made of
        independent noise in each component, each of X, Y and Z at a third of W's
        energy, as in a diffuse field. */
    std::vector<std::vector<float>> roomInAmbiX (const Speaker& source = { 0.0f, 0.0f })
    {
        const auto pad = static_cast<std::size_t> (0.02 * rate), gap = static_cast<std::size_t> (0.005 * rate);
        const auto length = static_cast<std::size_t> (0.8 * rate);
        const auto direct = encode (source);
        std::vector<std::vector<float>> file;

        for (std::size_t c = 0; c < 4; ++c)
        {
            const auto noise = testing::noise (length, 301u + static_cast<unsigned> (c));
            const auto level = c == 0 ? 0.3f : 0.3f / std::sqrt (3.0f);
            std::vector<float> ch (pad + gap + length, 0.0f);

            for (std::size_t n = 0; n < length; ++n)
                ch[pad + gap + n] = level * noise[n] * static_cast<float> (std::pow (10.0, -3.0 * static_cast<double> (n) / (0.5 * rate)));

            ch[pad] += 0.9f * direct[c];
            file.push_back (std::move (ch));
        }

        return file;
    }

    /** The same recording as FuMa: W X Y Z, W 3 dB down. */
    std::vector<std::vector<float>> asFuMa (const std::vector<std::vector<float>>& ambiX)
    {
        auto w = ambiX[0];

        for (auto& v : w)
            v /= std::numbers::sqrt2_v<float>;

        return { w, ambiX[3], ambiX[1], ambiX[2] };
    }

    double energyOf (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
    {
        double e = 0.0;
        const auto end = to == 0 ? x.size() : std::min (x.size(), to);

        for (auto n = from; n < end; ++n)
            e += static_cast<double> (x[n]) * x[n];

        return e;
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, std::size_t from = 0, std::size_t to = 0)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        const auto end = to == 0 ? std::min (a.size(), b.size()) : std::min ({ a.size(), b.size(), to });

        for (auto n = from; n < end; ++n)
        {
            ab += static_cast<double> (a[n]) * b[n];
            aa += static_cast<double> (a[n]) * a[n];
            bb += static_cast<double> (b[n]) * b[n];
        }

        return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
    }
}

TEST_CASE ("B-format files in FuMa and N3D become AmbiX", "[ambisonic]")
{
    const std::vector<std::vector<float>> fuMa { { 1.0f }, { 2.0f }, { 3.0f }, { 4.0f } }; // W X Y Z
    const auto fromFuMa = toAmbiX (fuMa, AmbisonicFormat::fuMa);
    REQUIRE (fromFuMa.size() == 4);
    CHECK (std::abs (fromFuMa[0][0] - std::numbers::sqrt2_v<float>) < 1.0e-6f);
    CHECK (fromFuMa[1][0] == 3.0f); // Y
    CHECK (fromFuMa[2][0] == 4.0f); // Z
    CHECK (fromFuMa[3][0] == 2.0f); // X

    const std::vector<std::vector<float>> n3d { { 1.0f }, { std::sqrt (3.0f) }, { 0.0f }, { 0.0f } };
    CHECK (std::abs (toAmbiX (n3d, AmbisonicFormat::ambiXN3D)[1][0] - 1.0f) < 1.0e-6f);

    // A second-order AmbiX file starts with the first-order four.
    std::vector<std::vector<float>> secondOrder (9, std::vector<float> { 0.5f });
    CHECK (toAmbiX (secondOrder, AmbisonicFormat::ambiX).size() == 4);

    CHECK_THROWS_AS (toAmbiX ({ { 1.0f }, { 1.0f } }, AmbisonicFormat::ambiX), std::invalid_argument);
}

TEST_CASE ("A virtual microphone hears a sound from where it points", "[ambisonic]")
{
    for (const auto& from : { Speaker { 0.0f, 0.0f }, Speaker { -110.0f, 0.0f }, Speaker { 45.0f, 45.0f }, Speaker { 180.0f, 0.0f } })
    {
        const auto e = encode (from);
        const std::vector<std::vector<float>> sound { { e[0] }, { e[1] }, { e[2] }, { e[3] } };
        const Speaker opposite { from.azimuth > 0.0f ? from.azimuth - 180.0f : from.azimuth + 180.0f, -from.elevation };

        for (const auto pattern : { 0.0f, 0.5f, 0.75f, 1.0f })
        {
            INFO ("from " << from.azimuth << "/" << from.elevation << ", pattern " << pattern);
            CHECK (std::abs (virtualMicrophone (sound, from, pattern)[0] - 1.0f) < 1.0e-5f);
            CHECK (std::abs (virtualMicrophone (sound, opposite, pattern)[0] - (1.0f - 2.0f * pattern)) < 1.0e-5f);
        }
    }
}

TEST_CASE ("A B-format response decodes loudest to the speakers facing its sound", "[ambisonic]")
{
    const auto layout = layouts::sevenPointOnePointFour();
    const auto e = encode ({ -90.0f, 0.0f }); // from the left
    const std::vector<std::vector<float>> sound { { e[0] }, { e[1] }, { e[2] }, { e[3] } };
    const auto decoded = decodeAmbisonic (sound, layout);
    REQUIRE (decoded.size() == layout.size());

    CHECK (decoded[3][0] == 0.0f); // the LFE
    const auto loudest = std::max_element (decoded.begin(), decoded.end(), [] (const auto& a, const auto& b) { return a[0] < b[0]; });
    CHECK (loudest - decoded.begin() == 4); // Lss, at -90
    CHECK (decoded[4][0] > 1.9f * std::abs (decoded[5][0])); // Lss 6 dB above Rss, the most first order gives
}

TEST_CASE ("A B-format response plays as a wide stereo pair with the front in the middle", "[ambisonic][recorded]")
{
    const auto recorded = prepareAmbisonicResponse (roomInAmbiX(), rate, "B-format room", AmbisonicFormat::ambiX);
    REQUIRE (recorded->ambisonic);
    REQUIRE (recorded->speakers.size() == 4);
    CHECK (recorded->channels == 4);
    CHECK (recorded->layout.empty());

    using P = ImpulseResponseSet::Path;
    const auto& left = recorded->responses.paths[P::leftToLeft];
    const auto& right = recorded->responses.paths[P::leftToRight];

    // The direct sound from straight ahead reaches both sides equally.
    const auto lead = static_cast<std::size_t> (0.004 * rate); // the direct sound, before the tail
    CHECK (std::abs (energyOf (left, 0, lead) / energyOf (right, 0, lead) - 1.0) < 0.05);

    // The diffuse tail is hardly correlated between the sides: a wide room.
    const auto from = static_cast<std::size_t> (0.05 * rate), to = static_cast<std::size_t> (0.4 * rate);
    CHECK (std::abs (correlation (left, right, from, to)) < 0.3);

    // A sound from the left is louder on the left.
    const auto fromLeft = prepareAmbisonicResponse (roomInAmbiX ({ -60.0f, 0.0f }), rate, "left", AmbisonicFormat::ambiX);
    CHECK (energyOf (fromLeft->responses.paths[P::leftToLeft], 0, lead) > 4.0 * energyOf (fromLeft->responses.paths[P::leftToRight], 0, lead));

    // The decay is measured as for any other file.
    for (std::size_t b = 2; b < 7; ++b)
        CHECK (std::abs (recorded->room.decaySeconds[b] / 0.5f - 1.0f) < 0.15f);
}

TEST_CASE ("A FuMa file sounds the same as its AmbiX twin", "[ambisonic][recorded]")
{
    const auto ambiX = prepareAmbisonicResponse (roomInAmbiX(), rate, "AmbiX", AmbisonicFormat::ambiX);
    const auto fuMa = prepareAmbisonicResponse (asFuMa (roomInAmbiX()), rate, "FuMa", AmbisonicFormat::fuMa);

    for (std::size_t p = 0; p < 4; ++p)
        CHECK (correlation (ambiX->responses.paths[p], fuMa->responses.paths[p]) > 0.9999);

    for (std::size_t c = 0; c < 4; ++c)
        CHECK (correlation (ambiX->speakers[c], fuMa->speakers[c]) > 0.9999);
}

TEST_CASE ("The builder decodes a B-format response to the session's speakers", "[ambisonic][builder]")
{
    const auto layout = layouts::fivePointOne();
    const auto recorded = prepareAmbisonicResponse (roomInAmbiX ({ -110.0f, 0.0f }), rate, "B-format room", AmbisonicFormat::ambiX);

    ConvolutionReverb reverb;
    reverb.prepare (rate, layout, layout);
    KernelBuilder builder (reverb);

    ImpulseResponseRequest request;
    request.sampleRate = rate;
    request.recorded = recorded;
    request.decaySeconds = recorded->room.decaySeconds;
    request.outputs = layout;
    builder.buildNow (request);

    // An impulse into the left input: Ls, facing the direct sound, carries it,
    // and each speaker plays its own decode.
    std::vector<std::vector<float>> channels (layout.size(), std::vector<float> (static_cast<std::size_t> (0.5 * rate), 0.0f));
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

    REQUIRE (reverb.isReady());
    const auto decoded = decodeAmbisonic (recorded->speakers, layout);
    const auto window = static_cast<std::size_t> (0.2 * rate);

    for (const std::size_t k : { 0u, 1u, 2u, 4u, 5u })
    {
        INFO ("speaker " << k);
        CHECK (correlation (channels[k], decoded[k], 0, window) > 0.95);
    }

    CHECK (energyOf (channels[3]) == 0.0); // the LFE

    const auto lead = static_cast<std::size_t> (0.004 * rate);
    CHECK (energyOf (channels[4], 0, lead) > 4.0 * energyOf (channels[5], 0, lead)); // Ls above Rs
}

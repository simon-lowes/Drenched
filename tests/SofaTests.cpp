#include <drenched/SofaResponse.h>

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    /** A room measured from one listening position with sources at these
        azimuths, each receiver's response a decaying noise tagged by its
        first sample: 100 * source + receiver + 1. */
    SofaSet room (const std::vector<float>& azimuths, std::vector<float> receiverSides)
    {
        SofaSet set;
        set.measurements = static_cast<int> (azimuths.size());
        set.receivers = static_cast<int> (receiverSides.size());
        set.samples = static_cast<int> (0.5 * rate);
        set.sampleRate = rate;
        set.receiverSide = std::move (receiverSides);

        for (std::size_t m = 0; m < azimuths.size(); ++m)
        {
            SofaSet::Source s;
            s.measurement = static_cast<int> (m);
            s.azimuth = azimuths[m];
            set.sources.push_back (s);

            for (int r = 0; r < set.receivers; ++r)
            {
                auto noise = testing::noise (static_cast<std::size_t> (set.samples), 7u + static_cast<unsigned> (m * 10) + static_cast<unsigned> (r));

                for (std::size_t n = 0; n < noise.size(); ++n)
                    noise[n] *= 0.5f * std::exp (-6.9f * static_cast<float> (n) / static_cast<float> (rate * 0.4));

                noise[0] = static_cast<float> (100 * m) + static_cast<float> (r) + 1.0f;
                set.ir.insert (set.ir.end(), noise.begin(), noise.end());
            }
        }

        return set;
    }

    float tag (const std::vector<float>& channel) { return channel.front(); }

    std::vector<char> readFile (const std::string& name, const std::string& folder = DRENCHED_SOFA_TEST_DIR)
    {
        std::ifstream in (folder + "/" + name, std::ios::binary);
        return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
    }

    /** One of the small SOFA files made for these tests (tests/data/make_sofa_files.py). */
    std::vector<char> readTestFile (const std::string& name)
    {
        return readFile (name, DRENCHED_TEST_DATA_DIR);
    }
} // namespace

TEST_CASE ("A SOFA pair either side of the listener plays as true stereo", "[sofa]")
{
    // Sources ahead, 30 degrees left and right, and hard right; receivers listed right first.
    const auto set = room ({ 0.0f, -30.0f, 30.0f, 90.0f }, { -0.09f, 0.09f });
    const auto choice = chooseSofaChannels (set);

    REQUIRE (choice.channels.size() == 4);
    CHECK (choice.left == 1);
    CHECK (choice.right == 2);

    // Left to left, left to right, right to left, right to right.
    CHECK (tag (choice.channels[0]) == 102.0f);
    CHECK (tag (choice.channels[1]) == 101.0f);
    CHECK (tag (choice.channels[2]) == 202.0f);
    CHECK (tag (choice.channels[3]) == 201.0f);
}

TEST_CASE ("Without a pair, the SOFA source closest to ahead plays in stereo", "[sofa]")
{
    const auto set = room ({ 120.0f, 8.0f, 90.0f }, { 0.09f, -0.09f });
    const auto choice = chooseSofaChannels (set);

    REQUIRE (choice.channels.size() == 2);
    CHECK (choice.front == 1);
    CHECK (tag (choice.channels[0]) == 101.0f);
    CHECK (tag (choice.channels[1]) == 102.0f);

    const auto mono = chooseSofaChannels (room ({ 30.0f, -30.0f, 4.0f }, { 0.0f }));
    REQUIRE (mono.channels.size() == 1);
    CHECK (tag (mono.channels[0]) == 201.0f);
}

TEST_CASE ("A stereo pair is only taken from one listening position", "[sofa]")
{
    auto set = room ({ 0.0f, -30.0f, 30.0f }, { 0.09f, -0.09f });
    set.sources[2].listener = 1;

    CHECK (chooseSofaChannels (set).channels.size() == 2);
}

TEST_CASE ("SOFA delays move each response later", "[sofa]")
{
    auto set = room ({ 0.0f }, { 0.09f, -0.09f });
    set.delays = { 0, 24 };
    const auto choice = chooseSofaChannels (set);

    REQUIRE (choice.channels.size() == 2);
    CHECK (choice.channels[0].size() == static_cast<std::size_t> (set.samples));
    REQUIRE (choice.channels[1].size() == static_cast<std::size_t> (set.samples) + 24);
    CHECK (choice.channels[1][23] == 0.0f);
    CHECK (choice.channels[1][24] == 2.0f);
}

TEST_CASE ("An ambisonic SOFA file gives its first four channels", "[sofa]")
{
    auto set = room ({ 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
    set.ambisonic = true;
    const auto choice = chooseSofaChannels (set);

    REQUIRE (choice.channels.size() == 4);
    CHECK (tag (choice.channels[3]) == 4.0f);

    auto tooFew = room ({ 0.0f }, { 0.0f, 0.0f });
    tooFew.ambisonic = true;
    CHECK_THROWS_AS (chooseSofaChannels (tooFew), std::invalid_argument);
}

TEST_CASE ("A measured binaural room SOFA file loads as true stereo", "[sofa]")
{
    // 50 loudspeakers around a KU100 dummy head in a room, 0.3 s each.
    const auto file = readFile ("D1_48K_24bit_0.3s_FIR_SOFA.sofa");
    REQUIRE (! file.empty());

    const auto set = readSofa (file.data(), file.size());
    CHECK (set.measurements == 50);
    CHECK (set.receivers == 2);
    CHECK (set.sampleRate == 48000.0);
    CHECK (set.samples == 14400);

    const auto choice = chooseSofaChannels (set);
    REQUIRE (choice.channels.size() == 4);

    const auto& left = set.sources[static_cast<std::size_t> (choice.left)];
    const auto& right = set.sources[static_cast<std::size_t> (choice.right)];
    CHECK (left.azimuth < -10.0f);
    CHECK (right.azimuth > 10.0f);
    CHECK (std::abs (left.azimuth + right.azimuth) < 20.0f);

    // The left ear hears the left speaker first and louder.
    const auto energy = [] (const std::vector<float>& x) { double e = 0; for (auto v : x) e += double (v) * v; return e; };
    CHECK (energy (choice.channels[0]) > energy (choice.channels[1]));
    CHECK (energy (choice.channels[3]) > energy (choice.channels[2]));

    const auto response = prepareSofaResponse (file.data(), file.size(), "D1");
    REQUIRE (response != nullptr);
    CHECK (response->channels == 4);
    CHECK (response->name == "D1");
}

TEST_CASE ("SOFA files that aren't room responses are refused with a reason", "[sofa]")
{
    const auto hrtf = readFile ("MIT_KEMAR_normal_pinna.sofa");
    REQUIRE (! hrtf.empty());
    CHECK_THROWS_WITH (readSofa (hrtf.data(), hrtf.size()), Catch::Matchers::ContainsSubstring ("without a room"));

    const std::string text = "RIFF this is not a SOFA file at all";
    CHECK_THROWS_WITH (readSofa (text.data(), text.size()), Catch::Matchers::ContainsSubstring ("isn't a SOFA file"));

    // Cut short anywhere, a file is refused, never read past its end.
    const auto file = readFile ("D1_48K_24bit_0.3s_FIR_SOFA.sofa");

    for (const auto fraction : { 0.01, 0.1, 0.5, 0.9, 0.999 })
        CHECK_THROWS_AS (readSofa (file.data(), static_cast<std::size_t> (fraction * static_cast<double> (file.size()))),
                         std::invalid_argument);
}

TEST_CASE ("SOFA receivers placed by azimuth and distance are told left from right", "[sofa]")
{
    // Two receivers 9 cm either side, the right one (azimuth -90) listed first; sources
    // ahead and 30 degrees left and right. Each response's first sample is 100 m + r + 1.
    const auto file = readTestFile ("spherical-receivers.sofa");
    REQUIRE (! file.empty());

    const auto set = readSofa (file.data(), file.size());
    REQUIRE (set.receivers == 2);
    REQUIRE (set.measurements == 3);
    REQUIRE (set.receiverSide.size() == 2);
    CHECK (set.receiverSide[0] == Catch::Approx (-0.09f).margin (1.0e-6));
    CHECK (set.receiverSide[1] == Catch::Approx (0.09f).margin (1.0e-6));

    // SOFA's azimuth runs anticlockwise, Drenched's clockwise.
    CHECK (set.sources[0].azimuth == Catch::Approx (0.0f).margin (1.0e-4));
    CHECK (set.sources[1].azimuth == Catch::Approx (-30.0f).margin (1.0e-4));
    CHECK (set.sources[2].azimuth == Catch::Approx (30.0f).margin (1.0e-4));

    const auto choice = chooseSofaChannels (set);
    REQUIRE (choice.channels.size() == 4);
    CHECK (choice.left == 1);
    CHECK (choice.right == 2);

    // Left source to the left receiver (the second), to the right one, then the right source.
    CHECK (tag (choice.channels[0]) == 102.0f);
    CHECK (tag (choice.channels[1]) == 101.0f);
    CHECK (tag (choice.channels[2]) == 202.0f);
    CHECK (tag (choice.channels[3]) == 201.0f);
}

TEST_CASE ("SOFA emitters placed by azimuth and distance around their source are heard where they are", "[sofa]")
{
    // A loudspeaker 2 m ahead with two drivers 1 m to its left (azimuth 90) and right.
    // Each response's first sample is 10 e + r + 1; receiver 0 is on the left.
    const auto file = readTestFile ("spherical-emitters.sofa");
    REQUIRE (! file.empty());

    const auto set = readSofa (file.data(), file.size());
    REQUIRE (set.emitters == 2);
    REQUIRE (set.sources.size() == 2);

    const auto apart = static_cast<float> (std::atan2 (1.0, 2.0) * 180.0 / std::numbers::pi);
    CHECK (set.sources[0].emitter == 0);
    CHECK (set.sources[0].azimuth == Catch::Approx (-apart).margin (1.0e-3));
    CHECK (set.sources[1].azimuth == Catch::Approx (apart).margin (1.0e-3));
    CHECK (set.sources[0].elevation == Catch::Approx (0.0f).margin (1.0e-3));

    const auto choice = chooseSofaChannels (set);
    REQUIRE (choice.channels.size() == 4);
    CHECK (choice.left == 0);
    CHECK (choice.right == 1);
    CHECK (tag (choice.channels[0]) == 1.0f);  // left driver, left receiver
    CHECK (tag (choice.channels[1]) == 2.0f);  // left driver, right receiver
    CHECK (tag (choice.channels[2]) == 11.0f); // right driver, left receiver
    CHECK (tag (choice.channels[3]) == 12.0f);
}

TEST_CASE ("An ambisonic SOFA file says whether it is AmbiX, FuMa or N3D, and each plays the same", "[sofa][ambisonic]")
{
    // One B-format room response written three ways (tests/data/make_sofa_files.py).
    struct Written
    {
        const char* file;
        AmbisonicFormat format;
    };

    std::vector<std::shared_ptr<const RecordedResponse>> loaded;

    for (const auto& [name, format] : { Written { "ambisonic-ambix.sofa", AmbisonicFormat::ambiX },
                                        Written { "ambisonic-fuma.sofa", AmbisonicFormat::fuMa },
                                        Written { "ambisonic-n3d.sofa", AmbisonicFormat::ambiXN3D } })
    {
        INFO (name);
        const auto file = readTestFile (name);
        REQUIRE (! file.empty());

        const auto set = readSofa (file.data(), file.size());
        CHECK (set.ambisonic);
        CHECK (set.ambisonicFormat == format);
        CHECK (set.receivers == 4);

        const auto response = prepareSofaResponse (file.data(), file.size(), name);
        REQUIRE (response != nullptr);
        CHECK (response->ambisonic);
        REQUIRE (response->speakers.size() == 4);
        loaded.push_back (response);
    }

    // Read in its own format, each is the same sound field: the same AmbiX channels, and
    // the same stereo decode. (FuMa's W was written 3 dB down and so rounded differently.)
    const auto& reference = *loaded.front();

    for (std::size_t k = 1; k < loaded.size(); ++k)
    {
        const auto& other = *loaded[k];

        const auto compare = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            REQUIRE (a.size() == b.size());
            float worst = 0.0f, peak = 0.0f;

            for (std::size_t n = 0; n < a.size(); ++n)
            {
                worst = std::max (worst, std::abs (a[n] - b[n]));
                peak = std::max (peak, std::abs (a[n]));
            }

            INFO ("differs by " << worst << " of a peak of " << peak);
            CHECK (peak > 0.0f);
            CHECK (worst <= 1.0e-4f * peak);
        };

        for (std::size_t c = 0; c < 4; ++c)
        {
            INFO (other.name << ", AmbiX channel " << c);
            compare (other.speakers[c], reference.speakers[c]);
        }

        for (std::size_t p = 0; p < 4; ++p)
        {
            INFO (other.name << ", stereo path " << p);
            compare (other.responses.paths[p], reference.responses.paths[p]);
        }
    }
}

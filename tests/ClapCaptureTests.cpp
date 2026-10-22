#include <drenched/ClapCapture.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/PartitionedConvolver.h>
#include <drenched/RecordedResponse.h>
#include <drenched/Spaces.h>
#include <drenched/SweepCapture.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    /** The live room's two left-input paths. */
    ImpulseResponseSet liveRoom()
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = 3;
        r.decaySeconds = bundledSpaces[3].decaySeconds;
        return synthesiseImpulseResponse (r);
    }

    /** A hand clap: a couple of milliseconds of noise, dying away fast. */
    std::vector<float> clapSound (float level)
    {
        auto x = testing::noise (static_cast<std::size_t> (0.003 * rate), 9u);

        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] *= level * static_cast<float> (std::exp (-static_cast<double> (n) / (0.0006 * rate)));

        return x;
    }

    /** What two microphones in the room hear: claps at the given times and
        levels, over a little background noise. */
    std::vector<std::vector<float>> roomHears (const std::vector<std::pair<double, float>>& claps, double seconds = 4.0)
    {
        const auto room = liveRoom();
        const auto length = static_cast<std::size_t> (seconds * rate);
        std::vector<std::vector<float>> mics;

        for (const auto path : { ImpulseResponseSet::leftToLeft, ImpulseResponseSet::leftToRight })
        {
            std::vector<float> dry (length, 0.0f);

            for (const auto& [at, level] : claps)
            {
                const auto sound = clapSound (level);
                const auto start = static_cast<std::size_t> (at * rate);

                for (std::size_t n = 0; n < sound.size() && start + n < length; ++n)
                    dry[start + n] += sound[n];
            }

            const auto& h = room.paths[path];
            PartitionedConvolver convolver ({ std::span<const float> (h) });
            std::vector<float> wet (length);
            float* outputs[] = { wet.data() };
            convolver.process (dry.data(), outputs, length);

            // Rooms gain about 20 dB on a clap's level; keep the result in range.
            auto peak = 0.0f;

            for (const auto v : wet)
                peak = std::max (peak, std::abs (v));

            const auto loudestClap = std::max_element (claps.begin(), claps.end(), [] (const auto& a, const auto& b) { return a.second < b.second; })->second;
            const auto scale = loudestClap / peak;
            const auto hiss = testing::noise (length, static_cast<unsigned> (path) + 30u);

            for (std::size_t n = 0; n < length; ++n)
                wet[n] = wet[n] * scale + hiss[n] * 1.0e-4f; // background noise about 80 dB down

            mics.push_back (std::move (wet));
        }

        return mics;
    }
}

TEST_CASE ("A clap recorded in a room gives back the room's decay", "[clap]")
{
    const auto mics = roomHears ({ { 1.0, 0.3f }, { 2.6, 0.2f } });
    const auto channels = extractClap (mics, rate);

    REQUIRE (channels.size() == 2);

    // From 5 ms before the clap to 5 ms before the next one.
    CHECK (std::abs (static_cast<double> (channels[0].size()) / rate - 1.6) < 0.02);

    const auto recorded = prepareRecordedResponse (channels, rate, "clap");

    for (std::size_t b = 2; b < 7; ++b)
    {
        const auto wanted = static_cast<double> (bundledSpaces[3].decaySeconds[b]);
        const auto measured = static_cast<double> (recorded->room.decaySeconds[b]);
        INFO ("band " << decayBandCentres[b] << " Hz: room " << wanted << " s, from the clap " << measured << " s");
        CHECK (std::abs (measured - wanted) / wanted < 0.2);
    }
}

TEST_CASE ("Of several claps the loudest one that did not clip is used", "[clap]")
{
    // The first clap clips; the second is clean; the third is quieter.
    auto mics = roomHears ({ { 0.5, 0.5f }, { 1.6, 0.4f }, { 2.8, 0.2f } });

    for (auto& m : mics)
        for (std::size_t n = static_cast<std::size_t> (0.5 * rate); n < static_cast<std::size_t> (0.6 * rate); ++n)
            m[n] = std::clamp (m[n] * 4.0f, -1.0f, 1.0f);

    const auto channels = extractClap (mics, rate);

    // The second clap runs until just before the third.
    CHECK (std::abs (static_cast<double> (channels[0].size()) / rate - 1.2) < 0.02);
}

TEST_CASE ("A recording with no usable clap says why", "[clap]")
{
    SECTION ("only background noise")
    {
        const std::vector<std::vector<float>> mics { testing::noise (static_cast<std::size_t> (3.0 * rate), 4u) };
        CHECK_THROWS_WITH (extractClap (mics, rate), Catch::Matchers::ContainsSubstring ("No clap"));
    }

    SECTION ("every clap clipped")
    {
        auto mics = roomHears ({ { 1.0, 0.3f } });

        for (auto& m : mics)
            for (auto& v : m)
                v = std::clamp (v * 8.0f, -1.0f, 1.0f);

        CHECK_THROWS_WITH (extractClap (mics, rate), Catch::Matchers::ContainsSubstring ("clipped"));
    }

    SECTION ("too short")
    {
        const std::vector<std::vector<float>> mics { std::vector<float> (100, 0.0f) };
        CHECK_THROWS_AS (extractClap (mics, rate), ClapProblem);
    }
}

TEST_CASE ("Listening for a clap records with the speakers silent and without allocating", "[clap][capture][realtime]")
{
    CaptureRecorder recorder;
    recorder.prepareListening (0.5, rate, 2);
    recorder.start();

    REQUIRE (recorder.sweep() == nullptr);
    REQUIRE (recorder.sampleRate() == rate);

    const auto left = testing::noise (24000, 1u), right = testing::noise (24000, 2u);
    std::vector<float> a (left), b (right);
    std::size_t done = 0;

    {
        testing::AllocationCounter counter;

        while (done < a.size())
        {
            const auto n = std::min<std::size_t> (512, a.size() - done);
            float* channels[] = { a.data() + done, b.data() + done };
            recorder.process (channels, 2, channels, 2, n);
            done += n;
        }

        CHECK (counter.count() == 0);
    }

    CHECK (recorder.state() == CaptureRecorder::State::finished);
    CHECK (std::all_of (a.begin(), a.end(), [] (float v) { return v == 0.0f; }));
    CHECK (std::all_of (b.begin(), b.end(), [] (float v) { return v == 0.0f; }));
    CHECK (recorder.recordings()[0] == left);
    CHECK (recorder.recordings()[1] == right);
}

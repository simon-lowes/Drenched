#include <drenched/ConvolutionReverb.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/KernelBuilder.h>
#include <drenched/LivingTail.h>
#include <drenched/RecordedResponse.h>
#include <drenched/Spaces.h>
#include <drenched/SurroundResponse.h>

#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace drenched;
using testing::measureRt60;

namespace
{
    ImpulseResponseRequest requestFor (int space, double sampleRate = 48000.0)
    {
        ImpulseResponseRequest r;
        r.sampleRate = sampleRate;
        r.space = space;
        r.decaySeconds = bundledSpaces[static_cast<std::size_t> (space)].decaySeconds;
        return r;
    }

    /** A bundled space rendered as if it were a four-channel file, with some
        silence before it and a noise floor after, as a real recording has. */
    std::vector<std::vector<float>> fileOf (const ImpulseResponseSet& set, double silenceSeconds = 0.03,
                                            double noiseDb = -200.0, double noiseSeconds = 0.0)
    {
        const auto pad = static_cast<std::size_t> (silenceSeconds * set.sampleRate);
        const auto extra = static_cast<std::size_t> (noiseSeconds * set.sampleRate);
        const auto level = static_cast<float> (std::pow (10.0, noiseDb / 20.0));
        std::vector<std::vector<float>> file;

        for (std::size_t p = 0; p < 4; ++p)
        {
            auto noise = testing::noise (pad + set.length() + extra, 11u + static_cast<unsigned> (p));
            std::vector<float> c (pad + set.length() + extra);

            for (std::size_t n = 0; n < c.size(); ++n)
                c[n] = noise[n] * level + (n >= pad && n - pad < set.length() ? 4.0f * set.paths[p][n - pad] : 0.0f);

            file.push_back (std::move (c));
        }

        return file;
    }

    double energy (const std::vector<float>& x)
    {
        double e = 0.0;

        for (auto v : x)
            e += static_cast<double> (v) * v;

        return e;
    }

    double totalEnergy (const ImpulseResponseSet& set)
    {
        double e = 0.0;

        for (const auto& p : set.paths)
            e += energy (p);

        return e;
    }

    ImpulseResponseRequest requestForRecorded (const std::shared_ptr<const RecordedResponse>& recorded, double sampleRate)
    {
        ImpulseResponseRequest r;
        r.sampleRate = sampleRate;
        r.recorded = recorded;
        r.decaySeconds = recorded->room.decaySeconds;
        return r;
    }
}

TEST_CASE ("Channel counts map onto the true-stereo paths", "[recorded]")
{
    const auto ir = testing::noise (4800, 1);
    std::vector<float> decaying (ir.size());

    for (std::size_t n = 0; n < ir.size(); ++n)
        decaying[n] = ir[n] * static_cast<float> (std::exp (-static_cast<double> (n) / 400.0));

    SECTION ("mono plays on each side with no cross-feed")
    {
        const auto r = prepareRecordedResponse ({ decaying }, 48000.0, "mono");
        CHECK (r->channels == 1);
        CHECK (r->responses.paths[ImpulseResponseSet::leftToLeft] == r->responses.paths[ImpulseResponseSet::rightToRight]);
        CHECK (energy (r->responses.paths[ImpulseResponseSet::leftToRight]) == 0.0);
        CHECK (energy (r->responses.paths[ImpulseResponseSet::rightToLeft]) == 0.0);
    }

    SECTION ("stereo feeds both inputs to both outputs")
    {
        const auto noise = testing::noise (decaying.size(), 7);
        std::vector<float> other (decaying.size());

        for (std::size_t n = 0; n < other.size(); ++n)
            other[n] = noise[n] * static_cast<float> (std::exp (-static_cast<double> (n) / 300.0));

        const auto r = prepareRecordedResponse ({ decaying, other }, 48000.0, "stereo");
        CHECK (r->responses.paths[ImpulseResponseSet::leftToLeft] == r->responses.paths[ImpulseResponseSet::rightToLeft]);
        CHECK (r->responses.paths[ImpulseResponseSet::leftToRight] == r->responses.paths[ImpulseResponseSet::rightToRight]);
        CHECK (r->responses.paths[ImpulseResponseSet::leftToLeft] != r->responses.paths[ImpulseResponseSet::leftToRight]);
    }

    SECTION ("the level matches the bundled spaces")
    {
        const auto r = prepareRecordedResponse ({ decaying, decaying, decaying, decaying }, 48000.0, "four");
        CHECK (std::abs (totalEnergy (r->responses) - 1.0) < 1.0e-4);
    }
}

TEST_CASE ("Files that can't be impulse responses are refused with a reason", "[recorded]")
{
    const auto ir = testing::noise (48000, 2);

    CHECK_THROWS_AS (prepareRecordedResponse ({ ir, ir, ir }, 48000.0, "three"), std::invalid_argument);
    CHECK_THROWS_AS (prepareRecordedResponse ({ std::vector<float> (48000, 0.0f) }, 48000.0, "silent"), std::invalid_argument);
    CHECK_THROWS_AS (prepareRecordedResponse ({ std::vector<float> (100, 0.5f) }, 48000.0, "short"), std::invalid_argument);

    // Steady noise never decays: music or a room tone, not a response.
    CHECK_THROWS_AS (prepareRecordedResponse ({ ir }, 48000.0, "noise"), std::invalid_argument);

    auto damaged = ir;
    damaged[100] = std::nanf ("");
    CHECK_THROWS_AS (prepareRecordedResponse ({ damaged }, 48000.0, "nan"), std::invalid_argument);
}

TEST_CASE ("Loading measures the room's decay in every band", "[recorded]")
{
    const auto space = GENERATE (0, 1, 2, 3);
    const auto noisy = GENERATE (false, true);
    const auto source = synthesiseImpulseResponse (requestFor (space));

    // A noise floor 80 dB under the peak, a second of it, as a recording ends.
    const auto file = noisy ? fileOf (source, 0.03, -80.0, 1.0) : fileOf (source);
    const auto recorded = prepareRecordedResponse (file, source.sampleRate, "space");
    const auto& wanted = bundledSpaces[static_cast<std::size_t> (space)].decaySeconds;

    for (std::size_t b = 1; b < 8; ++b)
    {
        INFO (bundledSpaces[static_cast<std::size_t> (space)].name << (noisy ? ", noisy" : "") << ", band " << decayBandCentres[b]
                                                                   << " Hz: wanted " << wanted[b] << " s, measured " << recorded->room.decaySeconds[b] << " s");
        CHECK (std::abs (recorded->room.decaySeconds[b] - wanted[b]) / wanted[b] < 0.12);
    }

    // The silence before the direct sound is gone, bar a millisecond.
    const auto& left = recorded->responses.paths[0];
    const auto first = std::find_if (left.begin(), left.end(), [] (float s) { return s != 0.0f; });
    CHECK (std::distance (left.begin(), first) <= 48);

    // Larger rooms have later mixing times and larger estimated volumes.
    CHECK (recorded->room.mixingTimeSeconds >= 0.01f);
    CHECK (recorded->room.mixingTimeSeconds <= 0.2f);

    if (noisy)
    {
        // The noise floor is trimmed: the response ends before the added second of noise does.
        CHECK (recorded->responses.length() < source.length() + static_cast<std::size_t> (0.5 * source.sampleRate));
    }
}

TEST_CASE ("A loaded room is estimated larger than a small one", "[recorded]")
{
    const auto hall = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (0))), 48000.0, "hall");
    const auto room = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (3))), 48000.0, "room");
    INFO ("hall " << hall->room.volume() << " m3, mixing " << hall->room.mixingTimeSeconds << " s; room "
                  << room->room.volume() << " m3, mixing " << room->room.mixingTimeSeconds << " s");
    CHECK (hall->room.volume() > room->room.volume());
}

TEST_CASE ("With its own decay a loaded response plays back unchanged", "[recorded]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (1))), 48000.0, "chapel");
    const auto out = renderImpulseResponse (requestForRecorded (recorded, 48000.0));

    REQUIRE (out.length() == recorded->responses.length());

    double error = 0.0;

    for (std::size_t p = 0; p < 4; ++p)
        for (std::size_t n = 0; n < out.length(); ++n)
            error = std::max (error, static_cast<double> (std::abs (out.paths[p][n] - recorded->responses.paths[p][n])));

    CHECK (20.0 * std::log10 (error + 1.0e-30) < -120.0);
}

TEST_CASE ("Loaded responses are resampled to the session rate", "[recorded]")
{
    const auto fileRate = GENERATE (44100.0, 96000.0);
    const auto source = synthesiseImpulseResponse (requestFor (3, fileRate));
    const auto recorded = prepareRecordedResponse (fileOf (source), fileRate, "room");

    const auto out = renderImpulseResponse (requestForRecorded (recorded, 48000.0));

    CHECK (out.sampleRate == 48000.0);
    const auto expected = static_cast<double> (recorded->responses.length()) * 48000.0 / fileRate;
    CHECK (std::abs (static_cast<double> (out.length()) - expected) <= 2.0);
    CHECK (std::abs (totalEnergy (out) - 1.0) < 1.0e-3);

    for (std::size_t b = 1; b < 7; ++b)
    {
        const auto measured = measureRt60 (out.paths[0], 48000.0, decayBandCentres[b]);
        const auto wanted = static_cast<double> (bundledSpaces[3].decaySeconds[b]);
        INFO (fileRate << " Hz file, band " << decayBandCentres[b] << " Hz: wanted " << wanted << " s, measured " << measured << " s");
        CHECK (std::abs (measured - wanted) / wanted < 0.12);
    }
}

TEST_CASE ("Editing the decay reshapes a loaded response band by band", "[recorded]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (0))), 48000.0, "hall");
    auto request = requestForRecorded (recorded, 48000.0);

    // Darker: the top three bands shortened by 40%, the bottom three
    // lengthened by 30%, 500 Hz and 1 kHz left alone.
    for (std::size_t b : { 5, 6, 7 })
        request.decaySeconds[b] *= 0.6f;

    for (std::size_t b : { 0, 1, 2 })
        request.decaySeconds[b] *= 1.3f;

    const auto out = renderImpulseResponse (request);
    const auto measured = measureDecay (out.paths, 48000.0);

    for (std::size_t b = 1; b < 8; ++b)
    {
        const auto wanted = static_cast<double> (request.decaySeconds[b]);
        INFO ("band " << decayBandCentres[b] << " Hz: wanted " << wanted << " s, measured " << measured[b] << " s");
        CHECK (std::abs (measured[b] - wanted) / wanted < 0.15);
    }

    CHECK (std::abs (totalEnergy (out) - 1.0) < 1.0e-3);
}

TEST_CASE ("A loaded response's left and right outputs take their own decay", "[recorded][channels]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (0))), 48000.0, "hall");
    auto request = requestForRecorded (recorded, 48000.0);
    request.rightDecaySeconds = request.decaySeconds;

    for (auto& d : request.rightDecaySeconds)
        d *= 0.6f;

    const auto out = renderImpulseResponse (request);
    using P = ImpulseResponseSet;
    const auto left = measureDecay ({ out.paths[P::leftToLeft], out.paths[P::rightToLeft], out.paths[P::leftToLeft], out.paths[P::rightToLeft] }, 48000.0);
    const auto right = measureDecay ({ out.paths[P::leftToRight], out.paths[P::rightToRight], out.paths[P::leftToRight], out.paths[P::rightToRight] }, 48000.0);

    for (std::size_t b = 1; b < 8; ++b)
    {
        const auto wantedLeft = static_cast<double> (request.decaySeconds[b]);
        const auto wantedRight = static_cast<double> (request.rightDecaySeconds[b]);
        INFO ("band " << decayBandCentres[b] << " Hz: left " << left[b] << " s (wanted " << wantedLeft << "), right "
                      << right[b] << " s (wanted " << wantedRight << ")");
        CHECK (std::abs (left[b] - wantedLeft) / wantedLeft < 0.15);
        CHECK (std::abs (right[b] - wantedRight) / wantedRight < 0.15);
    }
}

TEST_CASE ("Early/late balance works on a loaded response", "[recorded]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (0))), 48000.0, "hall");
    auto request = requestForRecorded (recorded, 48000.0);
    const auto late = static_cast<std::size_t> (3.0 * recorded->room.mixingTimeSeconds * 48000.0);

    const auto lateShare = [late] (const ImpulseResponseSet& set)
    {
        double tail = 0.0;

        for (const auto& p : set.paths)
            for (std::size_t n = late; n < p.size(); ++n)
                tail += static_cast<double> (p[n]) * p[n];

        return tail / totalEnergy (set);
    };

    const auto normal = lateShare (renderImpulseResponse (request));
    request.earlyLatePercent = 10.0f;
    const auto early = lateShare (renderImpulseResponse (request));
    request.earlyLatePercent = 95.0f;
    const auto moreLate = lateShare (renderImpulseResponse (request));

    CHECK (early < 0.5 * normal);
    CHECK (moreLate > normal);
}

TEST_CASE ("Size moves a loaded room's reflections later or sooner", "[recorded][size]")
{
    // A direct sound, one strong reflection 20 ms later, then a diffuse tail.
    constexpr double sr = 48000.0;
    const auto at = [] (double seconds) { return static_cast<std::size_t> (seconds * sr); };
    std::vector<std::vector<float>> file;

    for (unsigned c = 0; c < 2; ++c)
    {
        auto x = testing::noise (at (2.0), 5u + c);

        for (std::size_t n = 0; n < x.size(); ++n)
            x[n] *= n < at (0.06) ? 0.0f : 0.05f * static_cast<float> (std::exp (-6.9 * static_cast<double> (n) / sr));

        x[at (0.01)] = 1.0f;
        x[at (0.03)] = 0.6f;
        file.push_back (std::move (x));
    }

    const auto recorded = prepareRecordedResponse (file, sr, "two clicks");
    auto request = requestForRecorded (recorded, sr);

    const auto peakIn = [&] (const ImpulseResponseSet& set, double from, double to)
    {
        const auto& p = set.paths[ImpulseResponseSet::leftToLeft];
        const auto first = p.begin() + static_cast<std::ptrdiff_t> (at (from));
        const auto last = p.begin() + static_cast<std::ptrdiff_t> (std::min (p.size(), at (to)));
        return static_cast<double> (std::max_element (first, last, [] (float a, float b) { return std::abs (a) < std::abs (b); }) - p.begin()) / sr;
    };

    const auto original = renderImpulseResponse (request);
    const auto direct = peakIn (original, 0.0, 0.005);
    const auto reflection = peakIn (original, 0.01, 0.045) - direct;
    REQUIRE (reflection == Catch::Approx (0.02).margin (0.0002));

    for (const auto size : { 50.0f, 150.0f })
    {
        request.sizePercent = size;
        const auto out = renderImpulseResponse (request);
        INFO ("size " << size << "%");

        CHECK (peakIn (out, 0.0, 0.005) == Catch::Approx (direct).margin (0.0001));
        CHECK (peakIn (out, 0.005, 0.045) - direct == Catch::Approx (0.02 * size / 100.0).margin (0.0003));
        CHECK (mixingTimeSeconds (request) == Catch::Approx (recorded->room.mixingTimeSeconds * size / 100.0));
        CHECK (std::abs (totalEnergy (out) - 1.0) < 1.0e-3);
    }
}

TEST_CASE ("Size keeps a loaded room's decay and colour", "[recorded][size]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (0))), 48000.0, "hall");
    auto request = requestForRecorded (recorded, 48000.0);

    // Each band's share of the first 100 ms.
    const auto colour = [] (const ImpulseResponseSet& set, double seconds)
    {
        std::array<double, 8> share {};
        auto total = 0.0;

        for (std::size_t b = 0; b < 8; ++b)
        {
            const auto band = testing::octaveBand ({ set.paths[0].begin(), set.paths[0].begin() + static_cast<std::ptrdiff_t> (seconds * set.sampleRate) },
                                                   set.sampleRate, decayBandCentres[b]);
            share[b] = energy (band);
            total += share[b];
        }

        for (auto& v : share)
            v /= total;

        return share;
    };

    const auto before = colour (renderImpulseResponse (request), 0.1);

    for (const auto size : { 60.0f, 140.0f })
    {
        request.sizePercent = size;
        const auto out = renderImpulseResponse (request);
        const auto measured = measureDecay (out.paths, 48000.0);
        const auto after = colour (out, 0.1 * size / 100.0);

        for (std::size_t b = 1; b < 7; ++b)
        {
            const auto wanted = static_cast<double> (request.decaySeconds[b]);
            INFO ("size " << size << "%, band " << decayBandCentres[b] << " Hz: wanted " << wanted << " s, measured " << measured[b]
                          << " s; level " << 10.0 * std::log10 (after[b] / before[b]) << " dB");
            CHECK (std::abs (measured[b] - wanted) / wanted < 0.15);
            CHECK (std::abs (10.0 * std::log10 (after[b] / before[b])) < 2.0);
        }
    }
}

TEST_CASE ("The living tail fits a loaded room", "[recorded][livingtail]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (1))), 48000.0, "chapel");
    const auto request = requestForRecorded (recorded, 48000.0);
    const auto responses = renderImpulseResponse (request);
    const auto design = designLivingTail (request, responses);

    REQUIRE (design.valid);

    LivingTail tail;
    tail.prepare (48000.0);
    tail.setDesign (design);

    const auto length = static_cast<std::size_t> (5.0 * 48000.0);
    std::vector<float> in (length, 0.0f), silent (length, 0.0f), l (length), r (length);
    in[0] = 1.0f;
    tail.process (in.data(), silent.data(), l.data(), r.data(), length);

    for (std::size_t b = 2; b < 7; ++b)
    {
        const auto measured = measureRt60 (l, 48000.0, decayBandCentres[b]);
        const auto wanted = static_cast<double> (recorded->room.decaySeconds[b]);
        INFO ("band " << decayBandCentres[b] << " Hz: wanted " << wanted << " s, measured " << measured << " s");
        CHECK (std::abs (measured - wanted) / wanted < 0.12);
    }
}

TEST_CASE ("The builder plays a loaded response", "[recorded][builder]")
{
    const auto recorded = prepareRecordedResponse (fileOf (synthesiseImpulseResponse (requestFor (3, 44100.0))), 44100.0, "room");

    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    KernelBuilder builder (reverb);
    builder.buildNow (requestForRecorded (recorded, 48000.0));

    REQUIRE (builder.lastBuilt().has_value());
    CHECK (builder.lastBuilt()->recorded == recorded);

    // An impulse through the reverb comes back as the loaded room.
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    l[0] = r[0] = 1.0f;
    ReverbControls controls;
    controls.mix = 1.0f;

    for (std::size_t pos = 0; pos < l.size(); pos += 256)
        reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (256, l.size() - pos), controls);

    CHECK (energy (l) > 0.0);
    CHECK (std::all_of (l.begin(), l.end(), [] (float s) { return std::isfinite (s); }));
}

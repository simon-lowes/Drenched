#include <drenched/Biquad.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/LivingTail.h>
#include <drenched/Spaces.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
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

    struct Rendered
    {
        std::vector<float> left, right;
    };

    /** Runs a tail over `input` (fed to both channels) followed by silence. */
    Rendered render (LivingTail& tail, const std::vector<float>& input, std::size_t totalLength, std::size_t block = 512)
    {
        Rendered out { std::vector<float> (totalLength), std::vector<float> (totalLength) };
        std::vector<float> in (block), silent (block, 0.0f);

        for (std::size_t pos = 0; pos < totalLength; pos += block)
        {
            const auto n = std::min (block, totalLength - pos);

            for (std::size_t i = 0; i < n; ++i)
                in[i] = pos + i < input.size() ? input[pos + i] : 0.0f;

            tail.process (in.data(), silent.data(), out.left.data() + pos, out.right.data() + pos, n);
        }

        return out;
    }

    std::vector<float> octaveBand (std::vector<float> x, double sampleRate, double centre)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            Biquad hp, lp;
            hp.setCoefficients (Biquad::highPass (sampleRate, centre / std::sqrt (2.0)));
            lp.setCoefficients (Biquad::lowPass (sampleRate, centre * std::sqrt (2.0)));
            hp.process (x.data(), x.size());
            lp.process (x.data(), x.size());
        }

        return x;
    }

    double energyDb (const std::vector<float>& x, double fromSeconds, double toSeconds, double sampleRate)
    {
        double e = 0.0;
        const auto end = std::min (x.size(), static_cast<std::size_t> (toSeconds * sampleRate));

        for (auto n = static_cast<std::size_t> (fromSeconds * sampleRate); n < end; ++n)
            e += static_cast<double> (x[n]) * x[n];

        return 10.0 * std::log10 (e + 1.0e-30);
    }

    LivingTail tailFor (const ImpulseResponseRequest& request, LivingTailDesign* designOut = nullptr)
    {
        const auto design = designLivingTail (request, synthesiseImpulseResponse (request));

        if (designOut != nullptr)
            *designOut = design;

        LivingTail tail;
        tail.prepare (request.sampleRate);
        tail.setDesign (design);
        return tail;
    }
}

TEST_CASE ("Living tail decays at the room's RT60 in every band", "[livingtail]")
{
    const auto space = GENERATE (0, 1, 2, 3);
    const auto request = requestFor (space);
    auto tail = tailFor (request);

    const auto slowest = *std::max_element (request.decaySeconds.begin(), request.decaySeconds.end());
    const auto length = static_cast<std::size_t> ((slowest + 0.5) * request.sampleRate);
    std::vector<float> impulse (1, 1.0f);
    const auto out = render (tail, impulse, length);

    // 63 Hz is left out: an octave filter that low rings too long to measure
    // short decays honestly, the same as in the impulse-response tests.
    for (std::size_t b = 1; b < 8; ++b)
    {
        const auto measured = measureRt60 (out.left, request.sampleRate, decayBandCentres[b]);
        const auto wanted = static_cast<double> (request.decaySeconds[b]);
        INFO (bundledSpaces[static_cast<std::size_t> (space)].name << " band " << decayBandCentres[b] << " Hz: wanted "
                                                                   << wanted << " s, measured " << measured << " s");
        CHECK (std::abs (measured - wanted) / wanted < 0.12);
    }
}

TEST_CASE ("Living tail matches the room's late level and is decorrelated", "[livingtail]")
{
    const auto space = GENERATE (0, 1, 2, 3);
    const auto request = requestFor (space);
    const auto responses = synthesiseImpulseResponse (request);
    const auto design = designLivingTail (request, responses);

    LivingTail tail;
    tail.prepare (request.sampleRate);
    tail.setDesign (design);

    const auto out = render (tail, std::vector<float> (1, 1.0f), responses.length());

    // A window later than the calibration window, so this also checks that
    // the two decay together.
    const auto rtMid = 0.5 * (request.decaySeconds[3] + request.decaySeconds[4]);
    const auto from = 0.25 * rtMid, to = from + 0.15;

    const auto tailDb = 10.0 * std::log10 (std::pow (10.0, energyDb (out.left, from, to, request.sampleRate) / 10.0)
                                           + std::pow (10.0, energyDb (out.right, from, to, request.sampleRate) / 10.0));
    const auto roomDb = 10.0 * std::log10 (std::pow (10.0, energyDb (responses.paths[0], from, to, request.sampleRate) / 10.0)
                                           + std::pow (10.0, energyDb (responses.paths[1], from, to, request.sampleRate) / 10.0));
    INFO (bundledSpaces[static_cast<std::size_t> (space)].name << ": tail " << tailDb << " dB, room " << roomDb << " dB");
    CHECK (std::abs (tailDb - roomDb) < 2.0);

    double lr = 0.0, ll = 0.0, rr = 0.0;

    for (std::size_t n = static_cast<std::size_t> (0.05 * request.sampleRate); n < out.left.size(); ++n)
    {
        lr += static_cast<double> (out.left[n]) * out.right[n];
        ll += static_cast<double> (out.left[n]) * out.left[n];
        rr += static_cast<double> (out.right[n]) * out.right[n];
    }

    CHECK (std::abs (lr) / std::sqrt (ll * rr) < 0.2);
}

TEST_CASE ("A frozen tail sustains above the crossover and lets the bass fade", "[livingtail]")
{
    const auto request = requestFor (0);
    const auto sr = request.sampleRate;
    auto tail = tailFor (request);

    // Fill the room with noise, then freeze it and stop the input.
    const auto noise = testing::noise (static_cast<std::size_t> (1.0 * sr), 3);
    const auto blockSize = std::size_t { 512 };
    const auto total = static_cast<std::size_t> (31.0 * sr);
    std::vector<float> left (total), right (total), in (blockSize), silent (blockSize, 0.0f);

    for (std::size_t pos = 0; pos < total; pos += blockSize)
    {
        if (pos >= noise.size() && pos < noise.size() + blockSize)
            tail.setFrozen (true, 320.0f);

        const auto n = std::min (blockSize, total - pos);

        for (std::size_t i = 0; i < n; ++i)
            in[i] = pos + i < noise.size() ? noise[pos + i] : 0.0f;

        tail.process (in.data(), silent.data(), left.data() + pos, right.data() + pos, n);
    }

    const auto high = octaveBand (left, sr, 2000.0);
    const auto low = octaveBand (left, sr, 63.0);

    const auto highEarly = energyDb (high, 2.0, 3.0, sr), highLate = energyDb (high, 30.0, 31.0, sr);
    const auto lowEarly = energyDb (low, 2.0, 3.0, sr), lowLate = energyDb (low, 20.0, 21.0, sr);

    INFO ("2 kHz: " << highEarly << " dB at 2 s, " << highLate << " dB at 30 s; 63 Hz: " << lowEarly << " dB at 2 s, "
                    << lowLate << " dB at 20 s");
    CHECK (std::abs (highLate - highEarly) < 1.0); // sustains, and never grows
    CHECK (lowEarly - lowLate > 20.0);             // the bass keeps decaying

    CHECK (std::all_of (left.begin(), left.end(), [] (float s) { return std::isfinite (s); }));
}

TEST_CASE ("Unfreezing returns the tail to the room's decay", "[livingtail]")
{
    const auto request = requestFor (3);
    const auto sr = request.sampleRate;
    auto tail = tailFor (request);

    const auto noise = testing::noise (static_cast<std::size_t> (0.5 * sr), 5);
    tail.setFrozen (true, 320.0f);
    auto frozen = render (tail, noise, static_cast<std::size_t> (2.0 * sr));
    tail.setFrozen (false, 320.0f);
    const auto released = render (tail, {}, static_cast<std::size_t> (4.0 * sr));

    const auto start = energyDb (frozen.left, 1.5, 2.0, sr);
    const auto end = energyDb (released.left, 3.5, 4.0, sr);
    INFO ("frozen " << start << " dB, 3.5 s after release " << end << " dB");
    CHECK (start - end > 60.0);
}

TEST_CASE ("Living tail is real-time safe", "[livingtail][realtime]")
{
    const auto request = requestFor (1);
    LivingTailDesign design;
    auto tail = tailFor (request, &design);
    std::vector<float> in (256, 0.1f), outL (256), outR (256);

    testing::AllocationCounter counter;

    for (int i = 0; i < 50; ++i)
    {
        tail.setFrozen (i % 2 == 0, 100.0f + 10.0f * static_cast<float> (i));
        tail.setDesign (design);
        tail.process (in.data(), in.data(), outL.data(), outR.data(), in.size());
    }

    CHECK (counter.count() == 0);
}

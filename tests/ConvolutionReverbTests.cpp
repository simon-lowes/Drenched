#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/LivingTail.h>
#include <drenched/Spaces.h>
#include <drenched/SurroundResponse.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <thread>
#include <vector>

using namespace drenched;

namespace
{
    ImpulseResponseRequest hall (double sampleRate)
    {
        ImpulseResponseRequest r;
        r.sampleRate = sampleRate;
        r.space = 3; // Live Room B: short, so the tests stay quick
        r.decaySeconds = bundledSpaces[3].decaySeconds;
        return r;
    }

    double energy (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        double e = 0.0;
        const auto end = std::min (to, x.size());
        for (auto it = x.begin() + static_cast<std::ptrdiff_t> (std::min (from, end)); it != x.begin() + static_cast<std::ptrdiff_t> (end); ++it)
            e += static_cast<double> (*it) * *it;
        return e;
    }

    std::unique_ptr<ReverbKernel> kernelWithTail (const ImpulseResponseRequest& request)
    {
        const auto responses = synthesiseImpulseResponse (request);
        return std::make_unique<ReverbKernel> (responses, designLivingTail (request, responses));
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
        const auto from = static_cast<std::size_t> (fromSeconds * sampleRate);
        const auto to = static_cast<std::size_t> (toSeconds * sampleRate);
        return 10.0 * std::log10 (energy (x, from, to) + 1.0e-30);
    }

    /** Plays `input` for its length, then silence, with freeze switched on and
        off at the given times. Blocks are cut at those times exactly. */
    std::vector<float> playWithFreeze (ConvolutionReverb& reverb, const std::vector<float>& input, double seconds,
                                       double freezeAt, double releaseAt, double sampleRate, ReverbControls c = {})
    {
        const auto total = static_cast<std::size_t> (seconds * sampleRate);
        std::vector<float> l (total, 0.0f), r (total, 0.0f);
        std::copy_n (input.begin(), std::min (input.size(), total), l.begin());
        std::copy_n (input.begin(), std::min (input.size(), total), r.begin());

        // Clamped before the cast: "never release" (1e9 s) overflows a 32-bit size_t.
        const auto sampleAt = [sampleRate, total] (double t)
        {
            return static_cast<std::size_t> (std::min (t * sampleRate, static_cast<double> (total)));
        };

        const auto freezeSample = sampleAt (freezeAt);
        const auto releaseSample = sampleAt (releaseAt);
        constexpr std::size_t block = 480;

        for (std::size_t pos = 0; pos < total;)
        {
            auto n = std::min (block, total - pos);

            for (const auto edge : { freezeSample, releaseSample })
                if (edge > pos && edge < pos + n)
                    n = edge - pos;

            c.freeze = pos >= freezeSample && pos < releaseSample;
            reverb.process (l.data() + pos, r.data() + pos, n, c);
            pos += n;
        }

        return l;
    }

    void run (ConvolutionReverb& reverb, std::vector<float>& l, std::vector<float>& r, const ReverbControls& c, std::size_t block = 480)
    {
        for (std::size_t pos = 0; pos < l.size(); pos += block)
        {
            const auto n = std::min (block, l.size() - pos);
            reverb.process (l.data() + pos, r.data() + pos, n, c);
        }
    }
}

TEST_CASE ("Fully dry passes the input through untouched", "[reverb]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

    auto l = testing::noise (20000, 1), r = testing::noise (20000, 2);
    const auto inL = l, inR = r;

    ReverbControls c;
    c.mix = 0.0f;
    run (reverb, l, r, c);

    for (std::size_t n = 0; n < l.size(); ++n)
    {
        REQUIRE (std::abs (l[n] - inL[n]) < 1.0e-6f);
        REQUIRE (std::abs (r[n] - inR[n]) < 1.0e-6f);
    }
}

TEST_CASE ("Fully wet plays the impulse response, after the pre-delay", "[reverb]")
{
    constexpr double sr = 48000.0;
    const auto set = synthesiseImpulseResponse (hall (sr));

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (std::make_unique<ReverbKernel> (set));

    std::vector<float> l (set.length() + 4800, 0.0f), r (l.size(), 0.0f);
    l[0] = 1.0f;

    ReverbControls c;
    c.mix = 1.0f;
    c.predelayMs = 10.0f;
    run (reverb, l, r, c);

    const auto delay = static_cast<std::size_t> (0.010 * sr);

    for (std::size_t n = 0; n < delay; ++n)
        REQUIRE (std::abs (l[n]) < 1.0e-6f);

    // Left input to left output, delayed by exactly the pre-delay.
    const auto& ll = set.paths[ImpulseResponseSet::leftToLeft];
    const auto& lr = set.paths[ImpulseResponseSet::leftToRight];

    for (std::size_t n = 0; n + delay < l.size() && n < ll.size(); n += 7)
    {
        REQUIRE (std::abs (l[n + delay] - ll[n]) < 1.0e-4f);
        REQUIRE (std::abs (r[n + delay] - lr[n]) < 1.0e-4f);
    }
}

TEST_CASE ("Swapping kernels mid-sound keeps the old tail ringing", "[reverb]")
{
    constexpr double sr = 48000.0;
    auto request = hall (sr);

    ReverbControls c;
    c.mix = 1.0f;

    // A burst, then silence; swap to a new kernel 50 ms into the silence.
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    for (std::size_t n = 0; n < 2400; ++n)
        l[n] = r[n] = std::sin (static_cast<float> (n) * 0.05f);

    std::vector<float> reference (l);
    {
        ConvolutionReverb unswapped;
        unswapped.prepare (sr);
        unswapped.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (request)));
        std::vector<float> rr (r);
        run (unswapped, reference, rr, c);
    }

    ConvolutionReverb swapped;
    swapped.prepare (sr);
    swapped.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (request)));

    std::vector<float> sl (24000, 0.0f), sr2 (24000, 0.0f);
    for (std::size_t n = 0; n < 2400; ++n)
        sl[n] = sr2[n] = std::sin (static_cast<float> (n) * 0.05f);

    request.decaySeconds[4] = 0.2f; // a different response
    for (std::size_t pos = 0; pos < sl.size(); pos += 480)
    {
        if (pos == 4800)
            swapped.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (request)));
        swapped.process (sl.data() + pos, sr2.data() + pos, 480, c);
    }

    // The new kernel gets no input after the swap (the burst is over), so the
    // output must be exactly the old kernel's tail: identical to no swap at all.
    for (std::size_t n = 0; n < sl.size(); ++n)
        REQUIRE (std::abs (sl[n] - reference[n]) < 1.0e-5f);

    // Retired kernels come back for freeing once their tail is done.
    std::vector<float> silenceL (48000, 0.0f), silenceR (48000, 0.0f);
    run (swapped, silenceL, silenceR, c);
    REQUIRE (swapped.takeRetired() != nullptr);
}

TEST_CASE ("A kernel for the wrong sample rate is never played", "[reverb]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (44100.0))));

    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    l[0] = 1.0f;
    ReverbControls c;
    c.mix = 1.0f;
    run (reverb, l, r, c);

    REQUIRE_FALSE (reverb.isReady());
    REQUIRE (energy (l) < 1.0e-12); // only the (numerically tiny) dry share at full wet
    REQUIRE (reverb.takeRetired() != nullptr);
}

TEST_CASE ("Width zero makes the wet signal mono", "[reverb]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

    auto l = testing::noise (24000, 5), r = testing::noise (24000, 6);
    ReverbControls c;
    c.mix = 1.0f;
    c.widthPercent = 0.0f;
    run (reverb, l, r, c);

    for (std::size_t n = 4800; n < l.size(); ++n)
        REQUIRE (std::abs (l[n] - r[n]) < 1.0e-5f);
}

TEST_CASE ("Ducking lowers the wet signal while the input is loud", "[reverb]")
{
    const auto wetLevelWith = [] (float ducking)
    {
        ConvolutionReverb reverb;
        reverb.prepare (48000.0);
        reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

        auto l = testing::noise (24000, 7), r = testing::noise (24000, 8);
        ReverbControls c;
        c.mix = 1.0f;
        c.ducking = ducking;
        run (reverb, l, r, c);
        return energy (l, 12000);
    };

    REQUIRE (wetLevelWith (1.0f) < 0.1 * wetLevelWith (0.0f));
}

TEST_CASE ("The audio path never allocates, including kernel swaps", "[reverb][realtime]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

    auto request = hall (48000.0);
    request.decaySeconds[2] = 1.5f;
    auto second = std::make_unique<ReverbKernel> (synthesiseImpulseResponse (request));

    auto l = testing::noise (48000, 9), r = testing::noise (48000, 10);
    ReverbControls c;
    c.mix = 0.5f;
    c.predelayMs = 20.0f;
    c.brightnessDb = 3.0f;
    c.lowCutHz = 120.0f;
    c.highCutHz = 9000.0f;
    c.monoBelowHz = 150.0f;
    c.ducking = 0.5f;

    reverb.process (l.data(), r.data(), 512, c); // install the first kernel
    reverb.submit (std::move (second));          // submitting is the builder's job, outside the count

    testing::AllocationCounter counter;

    for (std::size_t pos = 512; pos + 517 <= l.size(); pos += 517)
    {
        c.predelayMs = pos > 20000 ? 40.0f : 20.0f;
        reverb.process (l.data() + pos, r.data() + pos, 517, c);
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("The builder builds once the request settles and frees retired kernels", "[reverb][builder]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);

    {
        KernelBuilder builder (reverb, std::chrono::milliseconds (30));
        auto request = hall (48000.0);
        builder.buildNow (request);
        REQUIRE (builder.lastBuilt() == request);

        std::vector<float> l (480, 0.0f), r (480, 0.0f);
        ReverbControls c;
        reverb.process (l.data(), r.data(), l.size(), c);
        REQUIRE (reverb.isReady());

        request.decaySeconds[3] = 0.9f;
        builder.request (request);

        for (int i = 0; i < 200 && builder.lastBuilt() != request; ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));

        REQUIRE (builder.lastBuilt() == request);
    }
}

TEST_CASE ("Freeze holds the room above the crossover, for as long as it is on", "[reverb][freeze]")
{
    constexpr double sr = 48000.0;
    auto chapel = hall (sr);
    chapel.space = 1;
    chapel.decaySeconds = bundledSpaces[1].decaySeconds;

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (kernelWithTail (chapel));

    ReverbControls c;
    c.mix = 1.0f;
    c.crossoverHz = 320.0f;

    const auto noise = testing::noise (static_cast<std::size_t> (1.0 * sr), 21);
    const auto out = playWithFreeze (reverb, noise, 20.0, 1.0, 1.0e9, sr, c);
    const auto high = octaveBand (out, sr, 2000.0);
    const auto low = octaveBand (out, sr, 125.0);

    const auto heldEarly = energyDb (high, 2.0, 3.0, sr), heldLate = energyDb (high, 19.0, 20.0, sr);
    const auto bassEarly = energyDb (low, 2.0, 3.0, sr), bassLate = energyDb (low, 19.0, 20.0, sr);
    INFO ("2 kHz " << heldEarly << " -> " << heldLate << " dB; 125 Hz " << bassEarly << " -> " << bassLate << " dB");

    // The chapel decays by 60 dB in about 3 s at 2 kHz, so anything still
    // there after 19 s is the freeze.
    CHECK (std::abs (heldLate - heldEarly) < 1.0);
    CHECK (bassEarly - bassLate > 20.0);
    CHECK (std::isinf (reverb.tailSeconds()));

    // The level as the freeze takes over stays close to the room's own.
    const auto justBefore = energyDb (high, 0.9, 1.0, sr), justAfter = energyDb (high, 1.2, 1.3, sr);
    INFO ("2 kHz just before " << justBefore << " dB, just after " << justAfter << " dB");
    CHECK (std::abs (justAfter - justBefore) < 6.0);
}

TEST_CASE ("While frozen, new sound stays out of the wet signal", "[reverb][freeze]")
{
    constexpr double sr = 48000.0;
    ReverbControls c;
    c.mix = 1.0f;

    const auto noise = testing::noise (static_cast<std::size_t> (1.0 * sr), 22);
    auto withBurst = noise;
    withBurst.resize (static_cast<std::size_t> (4.0 * sr), 0.0f);

    for (std::size_t n = static_cast<std::size_t> (3.0 * sr); n < withBurst.size(); ++n)
        withBurst[n] = (n % 97 == 0) ? 1.0f : 0.0f;

    ConvolutionReverb a, b;

    for (auto* reverb : { &a, &b })
    {
        reverb->prepare (sr);
        reverb->submit (kernelWithTail (hall (sr)));
    }

    const auto quiet = playWithFreeze (a, noise, 4.0, 1.0, 1.0e9, sr, c);
    const auto busy = playWithFreeze (b, withBurst, 4.0, 1.0, 1.0e9, sr, c);

    double difference = 0.0;

    for (auto n = static_cast<std::size_t> (3.0 * sr); n < quiet.size(); ++n)
        difference += static_cast<double> (busy[n] - quiet[n]) * (busy[n] - quiet[n]);

    const auto held = energy (quiet, static_cast<std::size_t> (3.0 * sr));
    INFO ("difference " << difference << " against held " << held);
    CHECK (difference < held * 1.0e-9);
}

TEST_CASE ("Releasing a freeze lets the room die away naturally", "[reverb][freeze]")
{
    constexpr double sr = 48000.0;
    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (kernelWithTail (hall (sr)));

    ReverbControls c;
    c.mix = 1.0f;

    const auto noise = testing::noise (static_cast<std::size_t> (1.0 * sr), 23);
    const auto out = playWithFreeze (reverb, noise, 7.0, 1.0, 3.0, sr, c);

    const auto held = energyDb (out, 2.99, 3.0, sr);
    const auto justAfter = energyDb (out, 3.0, 3.01, sr);
    const auto later = energyDb (out, 6.5, 7.0, sr) - 10.0 * std::log10 (50.0); // per 10 ms
    INFO ("held " << held << " dB, first 10 ms after release " << justAfter << " dB, 3.5 s later " << later << " dB");

    // The room is decaying at 60 dB in about half a second: a little over
    // 1 dB in 10 ms, nothing sudden.
    CHECK (held - justAfter < 3.0);
    CHECK (held - later > 60.0);
    CHECK (std::isfinite (reverb.tailSeconds()));

    // New sound is reverberated again once released.
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    l[0] = r[0] = 1.0f;
    reverb.process (l.data(), r.data(), l.size(), c);
    CHECK (energy (l, 1) > 1.0e-4);
}

TEST_CASE ("The living blend hands only the late part to the tail", "[reverb][living]")
{
    const auto request = hall (48000.0);
    const auto original = synthesiseImpulseResponse (request);
    const auto tm = mixingTimeSeconds (request);
    const auto at = [&] (double seconds) { return static_cast<std::size_t> (seconds * request.sampleRate); };

    auto none = original;
    CHECK (applyLivingBlend (none, 0.0f, tm) == 0.0f);
    CHECK (none.paths == original.paths);

    auto half = original;
    CHECK (applyLivingBlend (half, 50.0f, tm) == Catch::Approx (std::sqrt (0.5)).margin (1.0e-6));

    auto all = original;
    CHECK (applyLivingBlend (all, 100.0f, tm) == Catch::Approx (1.0).margin (1.0e-6));

    for (std::size_t p = 0; p < 4; ++p)
    {
        for (auto n = std::size_t { 0 }; n < at (tm); ++n)
        {
            CHECK (all.paths[p][n] == original.paths[p][n]); // early reflections untouched
            CHECK (half.paths[p][n] == original.paths[p][n]);
        }

        for (auto n = at (2.0 * tm); n < original.length(); ++n)
        {
            CHECK (std::abs (all.paths[p][n]) < 1.0e-6f);
            CHECK (half.paths[p][n] == Catch::Approx (original.paths[p][n] * std::sqrt (0.5)).margin (1.0e-6));
        }
    }
}

TEST_CASE ("With the living tail up, the late reverberation keeps its level", "[reverb][living]")
{
    constexpr double sr = 48000.0;
    ReverbControls c;
    c.mix = 1.0f;

    const auto play = [&] (float living)
    {
        auto request = hall (sr);
        request.livingPercent = living;

        ConvolutionReverb reverb;
        reverb.prepare (sr);
        KernelBuilder builder (reverb);
        builder.buildNow (request);

        std::vector<float> l (static_cast<std::size_t> (1.5 * sr), 0.0f), r (l.size(), 0.0f);
        l[0] = r[0] = 1.0f;

        for (std::size_t pos = 0; pos < l.size(); pos += 480)
            reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (480, l.size() - pos), c);

        return l;
    };

    const auto recorded = play (0.0f), blended = play (50.0f), living = play (100.0f);
    const auto rt = 0.5 * (bundledSpaces[3].decaySeconds[3] + bundledSpaces[3].decaySeconds[4]);
    const auto from = 0.25 * rt, to = from + 0.15;

    const auto early = energyDb (recorded, 0.0, 0.01, sr);
    const auto late = energyDb (recorded, from, to, sr);
    INFO ("late: recorded " << late << " dB, half " << energyDb (blended, from, to, sr) << " dB, living "
                            << energyDb (living, from, to, sr) << " dB");
    CHECK (std::abs (energyDb (blended, from, to, sr) - late) < 2.0);
    CHECK (std::abs (energyDb (living, from, to, sr) - late) < 2.0);
    CHECK (std::abs (energyDb (living, 0.0, 0.01, sr) - early) < 1.0);

    // And it is a different tail, not the recorded one at the same level.
    double a = 0.0, b = 0.0, ab = 0.0;

    for (auto n = static_cast<std::size_t> (from * sr); n < static_cast<std::size_t> (to * sr); ++n)
    {
        a += static_cast<double> (recorded[n]) * recorded[n];
        b += static_cast<double> (living[n]) * living[n];
        ab += static_cast<double> (recorded[n]) * living[n];
    }

    CHECK (std::abs (ab) / std::sqrt (a * b) < 0.3);
}

TEST_CASE ("Changing the living blend while sound plays is smooth", "[reverb][living]")
{
    constexpr double sr = 48000.0;
    ReverbControls c;
    c.mix = 1.0f;
    c.movement = 0.5f;

    auto request = hall (sr);
    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (kernelWithTail (request));

    auto l = testing::noise (static_cast<std::size_t> (2.0 * sr), 41), r = l;
    const auto switchAt = static_cast<std::size_t> (1.0 * sr);

    for (std::size_t pos = 0; pos < l.size(); pos += 480)
    {
        if (pos == switchAt)
        {
            request.livingPercent = 100.0f;
            const auto responses = synthesiseImpulseResponse (request);
            auto blended = responses;
            const auto design = designLivingTail (request, responses);
            const auto level = applyLivingBlend (blended, request.livingPercent, mixingTimeSeconds (request));
            auto kernel = std::make_unique<ReverbKernel> (blended, design);
            kernel->setLivingLevel (level);
            reverb.submit (std::move (kernel));
        }

        reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (480, l.size() - pos), c);
    }

    const auto before = energyDb (l, 0.8, 1.0, sr), after = energyDb (l, 1.0, 1.2, sr), settled = energyDb (l, 1.8, 2.0, sr);
    INFO ("before " << before << " dB, during the change " << after << " dB, settled " << settled << " dB");
    CHECK (std::abs (after - before) < 2.0);
    CHECK (std::abs (settled - before) < 2.0);
    CHECK (std::all_of (l.begin(), l.end(), [] (float x) { return std::isfinite (x); }));
}

TEST_CASE ("Freeze and crossover moves never allocate", "[reverb][freeze][realtime]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (kernelWithTail (hall (48000.0)));

    auto second = hall (48000.0);
    second.decaySeconds[4] = 1.2f;
    auto pending = kernelWithTail (second);

    auto l = testing::noise (96000, 31), r = testing::noise (96000, 32);
    ReverbControls c;
    c.mix = 0.5f;
    reverb.process (l.data(), r.data(), 512, c);

    testing::AllocationCounter counter;

    for (std::size_t pos = 512, i = 0; pos + 517 <= l.size(); pos += 517, ++i)
    {
        c.freeze = (i / 20) % 2 == 0;
        c.crossoverHz = 150.0f + 5.0f * static_cast<float> (i % 50);
        reverb.process (l.data() + pos, r.data() + pos, 517, c);

        if (i == 30)
            reverb.submit (std::move (pending)); // a swap while the tail is busy
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("Benchmark: CPU use per space", "[.benchmark]")
{
    for (const double sr : { 48000.0, 96000.0 })
    {
        for (int space = 0; space < 4; ++space)
        {
            ImpulseResponseRequest request;
            request.sampleRate = sr;
            request.space = space;
            request.decaySeconds = bundledSpaces[static_cast<std::size_t> (space)].decaySeconds;

            const auto buildStart = std::chrono::steady_clock::now();
            auto kernel = kernelWithTail (request);
            const auto buildTime = std::chrono::duration<double> (std::chrono::steady_clock::now() - buildStart).count();

            ConvolutionReverb reverb;
            reverb.prepare (sr);
            reverb.submit (std::move (kernel));

            auto l = testing::noise (static_cast<std::size_t> (sr * 10.0), 1), r = testing::noise (l.size(), 2);
            ReverbControls c;
            c.mix = 0.5f;

            const auto start = std::chrono::steady_clock::now();
            run (reverb, l, r, c, 256);
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

            std::printf ("%-18s %6.0f Hz  build %.2f s  CPU %.2f%% of one core\n", bundledSpaces[static_cast<std::size_t> (space)].name, sr, buildTime, 100.0 * seconds / 10.0);
        }
    }
}

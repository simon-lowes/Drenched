#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/DirectConvolver.h>
#include <drenched/KernelBuilder.h>
#include <drenched/LivingTail.h>
#include <drenched/Mix.h>
#include <drenched/Spaces.h>
#include <drenched/SurroundResponse.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <thread>
#include <utility>
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
    auto first = std::make_unique<ReverbKernel> (synthesiseImpulseResponse (request));
    const auto* const firstKernel = first.get();
    swapped.submit (std::move (first));

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

    // The old kernel comes back for freeing once its tail is done, and only it: the new
    // one plays on.
    std::vector<float> silenceL (48000, 0.0f), silenceR (48000, 0.0f);
    run (swapped, silenceL, silenceR, c);
    const auto retired = swapped.takeRetired();
    REQUIRE (retired.get() == firstKernel);
    REQUIRE (swapped.takeRetired() == nullptr);
}

TEST_CASE ("Prepared again, the reverb plays the room built next from its first block, with nothing of the one before", "[reverb][swap]")
{
    // A host restarting audio (prepareToPlay) is followed by the room it is to play,
    // built before the first block (a session just loaded, say). Nothing rings in the
    // old room after prepare(), so the new one plays alone at once: no handover, which
    // would pass the first 40 ms of input through a room nobody asked for, to ring for
    // its whole length. So a bounce sounds the same whatever was loaded before.
    constexpr double sr = 48000.0;
    auto wanted = hall (sr);
    auto before = wanted;
    before.space = 0; // a much longer room
    before.decaySeconds = bundledSpaces[0].decaySeconds;

    ReverbControls c;
    c.mix = 0.5f;

    // Played before with the reverb ducked all the way, which must not carry over either.
    auto ducked = c;
    ducked.ducking = 1.0f;

    const auto played = [&] (bool wasPlaying)
    {
        ConvolutionReverb reverb;
        reverb.prepare (sr);

        if (wasPlaying)
        {
            reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (before)));
            auto l = testing::noise (24000, 7), r = testing::noise (24000, 8);
            run (reverb, l, r, ducked);
            reverb.prepare (sr);
        }

        reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (wanted)));
        auto l = testing::noise (48000, 9), r = testing::noise (48000, 10);
        run (reverb, l, r, c);
        return l;
    };

    const auto fresh = played (false);
    const auto again = played (true);

    auto difference = 0.0f, loudest = 0.0f;

    for (std::size_t n = 0; n < fresh.size(); ++n)
    {
        difference = std::max (difference, std::abs (again[n] - fresh[n]));
        loudest = std::max (loudest, std::abs (fresh[n]));
    }

    INFO ("loudest " << loudest << ", differs by up to " << difference);
    CHECK (difference <= 1.0e-5f * loudest);
}

TEST_CASE ("A kernel for the wrong sample rate is never played", "[reverb]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    auto wrongRate = std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (44100.0)));
    const auto* const wrongKernel = wrongRate.get();
    reverb.submit (std::move (wrongRate));

    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    l[0] = 1.0f;
    ReverbControls c;
    c.mix = 1.0f;
    run (reverb, l, r, c);

    REQUIRE_FALSE (reverb.isReady());
    REQUIRE (energy (l) < 1.0e-12); // only the (numerically tiny) dry share at full wet

    // Handed straight back to be freed.
    const auto retired = reverb.takeRetired();
    REQUIRE (retired.get() == wrongKernel);
    REQUIRE (reverb.takeRetired() == nullptr);
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

TEST_CASE ("The wet EQ shapes the reverberation and never the dry sound", "[reverb][eq]")
{
    const auto render = [] (float mix, float highShelfDb)
    {
        ConvolutionReverb reverb;
        reverb.prepare (48000.0);
        reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

        auto l = testing::noise (24000, 11), r = testing::noise (24000, 12);
        ReverbControls c;
        c.mix = mix;
        c.highCutHz = 20000.0f;
        c.eq[5].gainDb = highShelfDb;
        run (reverb, l, r, c);
        return l;
    };

    const auto flat = render (1.0f, 0.0f), cut = render (1.0f, -12.0f);
    const auto band = [] (const std::vector<float>& x) { return energy (octaveBand (x, 48000.0, 16000.0), 12000); };
    REQUIRE (band (cut) < 0.1 * band (flat));

    REQUIRE (render (0.0f, -12.0f) == render (0.0f, 0.0f));
}

TEST_CASE ("Soloing an EQ band mutes the dry sound and keeps only that band's reverberation", "[reverb][eq]")
{
    const auto render = [] (float mix, int solo)
    {
        ConvolutionReverb reverb;
        reverb.prepare (48000.0);
        reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

        auto l = testing::noise (48000, 21), r = testing::noise (48000, 22);
        ReverbControls c;
        c.mix = mix;
        c.highCutHz = 20000.0f;
        c.eq[3] = { 2000.0f, 0.0f, 2.0f, WetEqualiser::Shape::bell };
        c.soloBand = solo;
        run (reverb, l, r, c);
        return l;
    };

    // Fully dry, the solo still plays the reverberation (wet, at half mix at least),
    // and nothing of the dry noise comes through: the output is far from the input.
    const auto dryInput = testing::noise (48000, 21);
    const auto soloed = render (0.0f, 3);
    double diff = 0.0, same = 0.0;

    for (std::size_t n = 24000; n < 48000; ++n)
    {
        diff += static_cast<double> (soloed[n] - dryInput[n]) * (soloed[n] - dryInput[n]);
        same += static_cast<double> (dryInput[n]) * dryInput[n];
    }

    REQUIRE (diff > 0.5 * same);
    REQUIRE (energy (soloed, 24000) > 0.0);

    // Only the soloed band's region is left of the reverberation (at half mix: half the power).
    const auto all = render (1.0f, WetEqualiser::noSolo);
    const auto band = [] (const std::vector<float>& x, double centre) { return energy (octaveBand (x, 48000.0, centre), 24000); };
    REQUIRE (band (soloed, 2000.0) > 0.2 * band (all, 2000.0));
    REQUIRE (band (soloed, 125.0) < 0.05 * band (all, 125.0));
    REQUIRE (band (soloed, 16000.0) < 0.05 * band (all, 16000.0));

    // No solo plays as before.
    REQUIRE (render (0.5f, WetEqualiser::noSolo) == render (0.5f, WetEqualiser::noSolo));
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
        c.eq[2].gainDb = pos > 30000 ? -4.0f : 0.0f;
        reverb.process (l.data() + pos, r.data() + pos, 517, c);
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("Low-CPU mode plays the same sound, dry and wet, exactly its latency later", "[reverb][latency]")
{
    constexpr double sr = 48000.0;
    const auto latency = ConvolutionReverb::lowCpuLatency (sr);
    REQUIRE (latency == 1024);
    REQUIRE (ConvolutionReverb::lowCpuLatency (96000.0) == 2048);

    ReverbControls c;
    c.mix = 0.5f;
    c.predelayMs = 12.0f;

    const auto play = [&] (std::size_t samples, float living)
    {
        auto request = hall (sr);
        request.livingPercent = living;
        request.latencySamples = samples;

        ConvolutionReverb reverb;
        reverb.prepare (sr);
        KernelBuilder builder (reverb);
        builder.buildNow (request);

        auto l = testing::noise (static_cast<std::size_t> (1.2 * sr), 3), r = testing::noise (l.size(), 4);
        std::fill (l.begin() + 4800, l.end(), 0.0f);
        std::fill (r.begin() + 4800, r.end(), 0.0f);
        run (reverb, l, r, c, 300);
        CHECK (reverb.latency() == samples);
        return std::make_pair (l, r);
    };

    // The convolution matches to rounding. The living tail reads its input
    // further back in a delay line whose fractional read rounds a little
    // differently, so with it up the match is to about -70 dB.
    const auto living = GENERATE (0.0f, 50.0f);
    const auto [zeroL, zeroR] = play (0, living);
    const auto [lowL, lowR] = play (latency, living);

    for (std::size_t n = 0; n < latency; ++n)
        REQUIRE (lowL[n] == 0.0f);

    double error = 0.0, total = 0.0;

    for (std::size_t n = 0; n + latency < zeroL.size(); ++n)
    {
        for (const auto& [low, zero] : { std::pair (&lowL, &zeroL), std::pair (&lowR, &zeroR) })
        {
            const auto d = static_cast<double> ((*low)[n + latency] - (*zero)[n]);
            error += d * d;
            total += static_cast<double> ((*zero)[n]) * (*zero)[n];
        }
    }

    INFO ("living " << living << "%: difference " << 10.0 * std::log10 (error / total) << " dB");
    CHECK (error / total < (living > 0.0f ? 1.0e-6 : 1.0e-12));
}

TEST_CASE ("Switching to low-CPU mode while sound plays moves the dry sound without a click", "[reverb][latency]")
{
    constexpr double sr = 48000.0;
    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (sr))));

    // A 300 Hz sine, fully dry, so only the dry delay is heard.
    std::vector<float> l (static_cast<std::size_t> (sr)), r;

    for (std::size_t n = 0; n < l.size(); ++n)
        l[n] = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 300.0 * static_cast<double> (n) / sr));

    r = l;
    const auto original = l;
    ReverbControls c;
    c.mix = 0.0f;

    for (std::size_t pos = 0; pos < l.size(); pos += 480)
    {
        if (pos == 24000)
            reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (sr)), LivingTailDesign {}, 1024));

        reverb.process (l.data() + pos, r.data() + pos, std::min<std::size_t> (480, l.size() - pos), c);
    }

    // Untouched before, 1024 samples late once the 20 ms crossfade is over.
    for (std::size_t n = 0; n < 24000; ++n)
        REQUIRE (l[n] == Catch::Approx (original[n]).margin (1.0e-6));

    for (std::size_t n = 24000 + 960 + 480; n < l.size(); ++n)
        REQUIRE (l[n] == Catch::Approx (original[n - 1024]).margin (1.0e-6));

    auto biggest = 0.0f;

    for (std::size_t n = 2; n < l.size(); ++n)
        biggest = std::max (biggest, std::abs (l[n] - 2.0f * l[n - 1] + l[n - 2]));

    CHECK (biggest < 0.02f); // the sine's own bend is 0.0019
}

TEST_CASE ("Low-CPU mode never allocates on the audio thread, including switching to it", "[reverb][latency][realtime]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));
    auto low = std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0)), LivingTailDesign {}, 1024);

    auto l = testing::noise (48000, 19), r = testing::noise (48000, 20);
    ReverbControls c;
    c.mix = 0.5f;
    reverb.process (l.data(), r.data(), 512, c);
    reverb.submit (std::move (low));

    testing::AllocationCounter counter;

    for (std::size_t pos = 512; pos + 517 <= l.size(); pos += 517)
        reverb.process (l.data() + pos, r.data() + pos, 517, c);

    REQUIRE (counter.count() == 0);
    REQUIRE (reverb.latency() == 1024);
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
        const auto builds = builder.buildCount();
        builder.request (request);

        REQUIRE (builder.waitUntilIdle (testing::buildTimeout));
        REQUIRE (builder.lastBuilt() == request);
        CHECK (builder.buildCount() == builds + 1);
    }
}

TEST_CASE ("A request that needn't settle is built at once", "[reverb][builder]")
{
    // A scene recalled or a space chosen is a single change, not a control being dragged,
    // so it shouldn't wait for the value to rest. The rest here is far longer than the
    // test waits for, so only a request built without it can land.
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);

    KernelBuilder builder (reverb, std::chrono::minutes (10));
    auto request = hall (48000.0);
    builder.buildNow (request);

    request.decaySeconds[3] = 0.9f;
    builder.request (request);
    CHECK_FALSE (builder.waitUntilIdle (std::chrono::milliseconds (0))); // still settling
    CHECK (builder.lastBuilt() != request);

    // The same request again, now not to settle, goes ahead.
    builder.request (request, false);
    CHECK (builder.waitUntilIdle (testing::buildTimeout));
    CHECK (builder.lastBuilt() == request);

    request.decaySeconds[3] = 1.7f;
    builder.request (request, false);
    CHECK (builder.waitUntilIdle (testing::buildTimeout));
    CHECK (builder.lastBuilt() == request);
}

TEST_CASE ("A room built at once replaces one asked for before it, which is never built", "[reverb][builder]")
{
    // The timer asks for the room as the settings were at its last tick; prepareToPlay
    // then builds the room as they are now. The older request must not be built after
    // it, or the room would go back to what it was a moment before.
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    KernelBuilder builder (reverb, std::chrono::minutes (10)); // so the request waits
    auto request = hall (48000.0);
    builder.buildNow (request);

    auto older = request;
    older.decaySeconds[3] = 0.6f;
    builder.request (older);

    auto now = request;
    now.decaySeconds[3] = 1.2f;
    builder.buildNow (now);

    CHECK (builder.waitUntilIdle (std::chrono::milliseconds (0))); // nothing left to build
    CHECK (builder.lastBuilt() == now);

    // A request after it is built as usual.
    auto later = request;
    later.decaySeconds[3] = 1.5f;
    builder.request (later, false);
    REQUIRE (builder.waitUntilIdle (testing::buildTimeout));
    CHECK (builder.lastBuilt() == later);
}

TEST_CASE ("The builder is idle only once everything asked of it is built", "[reverb][builder]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    KernelBuilder builder (reverb, std::chrono::milliseconds (30));

    // Nothing asked yet.
    CHECK (builder.waitUntilIdle (std::chrono::milliseconds (0)));

    auto request = hall (48000.0);
    builder.buildNow (request);
    CHECK (builder.waitUntilIdle (std::chrono::milliseconds (0)));
    const auto builds = builder.buildCount();

    // The room already built, asked for again: nothing to build, and idle once seen.
    builder.request (request);
    CHECK (builder.waitUntilIdle (testing::buildTimeout));
    CHECK (builder.buildCount() == builds);

    // Several changes in a row while one settles: only the last is built, and the
    // builder is not idle until it has been.
    for (const auto decay : { 0.5f, 0.7f, 0.9f })
    {
        request.decaySeconds[3] = decay;
        builder.request (request);
    }

    CHECK_FALSE (builder.waitUntilIdle (std::chrono::milliseconds (0)));
    REQUIRE (builder.waitUntilIdle (testing::buildTimeout));
    CHECK (builder.lastBuilt() == request);
    CHECK (builder.buildCount() == builds + 1);

    // A request built at once by buildNow() is not built again. (The builder's thread may
    // have taken it up meanwhile, in which case it lets it go once buildNow() is done.)
    request.decaySeconds[3] = 1.3f;
    builder.request (request);
    builder.buildNow (request);
    const auto afterNow = builder.buildCount();
    REQUIRE (builder.waitUntilIdle (testing::buildTimeout));
    CHECK (builder.buildCount() == afterNow);
    CHECK (builder.lastBuilt() == request);
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

TEST_CASE ("Input that isn't a number is heard as silence", "[reverb]")
{
    constexpr double sr = 48000.0;
    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (sr))));

    const auto bad = GENERATE (std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity());

    std::vector<float> l (sr, 0.0f), r (l.size(), 0.0f);
    l[100] = bad;
    r[2000] = bad;
    l[24000] = 1.0f;

    ReverbControls c;
    c.mix = 0.5f;
    run (reverb, l, r, c);

    for (std::size_t n = 0; n < l.size(); ++n)
    {
        REQUIRE (std::isfinite (l[n]));
        REQUIRE (std::isfinite (r[n]));
    }

    // The click later on still rings in the room.
    REQUIRE (energy (l, 24480) > 1.0e-4);
    REQUIRE (reverb.recoveries() == 0);
}

TEST_CASE ("A reverb that stops making numbers starts again from silence", "[reverb]")
{
    constexpr double sr = 48000.0;
    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (sr))));

    // A kernel that isn't numbers poisons the wet path from inside.
    auto poisoned = synthesiseImpulseResponse (hall (sr));
    poisoned.paths[ImpulseResponseSet::leftToLeft][100] = std::numeric_limits<float>::quiet_NaN();
    reverb.submit (std::make_unique<ReverbKernel> (poisoned));

    auto l = testing::noise (24000, 1), r = testing::noise (24000, 2);
    const auto inL = l;

    ReverbControls broken;
    broken.mix = 0.5f;
    run (reverb, l, r, broken);

    REQUIRE (reverb.recoveries() > 0);

    // The dry sound plays on, finite, at the equal-power dry level.
    const auto dryGain = equalPowerMix (0.5f).dry;

    for (std::size_t n = 1000; n < l.size(); ++n)
    {
        REQUIRE (std::isfinite (l[n]));
        REQUIRE (std::isfinite (r[n]));
        REQUIRE (std::abs (l[n] - inL[n] * dryGain) < 1.0e-3f);
    }

    // With a sound kernel back, the room rings again: the broken one, handing over, is
    // dropped at once rather than left to ring out.
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (sr))));
    std::vector<float> l2 (sr, 0.0f), r2 (l2.size(), 0.0f);
    l2[960] = 1.0f;
    ReverbControls good;
    good.mix = 1.0f;
    const auto before = reverb.recoveries();
    run (reverb, l2, r2, good);

    for (std::size_t n = 0; n < l2.size(); ++n)
        REQUIRE (std::isfinite (l2[n]));

    REQUIRE (energy (l2, 1440) > 1.0e-4);
    CHECK (reverb.recoveries() <= before + 1);
}

TEST_CASE ("A control that isn't a number keeps the last one that was", "[reverb]")
{
    constexpr double sr = 48000.0;
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();

    using Break = void (*) (ReverbControls&);
    const auto broken = GENERATE (as<Break> {},
        [] (ReverbControls& c) { c.mix = nan; },             [] (ReverbControls& c) { c.predelayMs = inf; },
        [] (ReverbControls& c) { c.brightnessDb = nan; },    [] (ReverbControls& c) { c.lowCutHz = -inf; },
        [] (ReverbControls& c) { c.highCutHz = nan; },       [] (ReverbControls& c) { c.widthPercent = nan; },
        [] (ReverbControls& c) { c.cloudPercent = inf; },    [] (ReverbControls& c) { c.monoBelowHz = nan; },
        [] (ReverbControls& c) { c.ducking = nan; },         [] (ReverbControls& c) { c.outputGain = inf; },
        [] (ReverbControls& c) { c.crossoverHz = nan; },     [] (ReverbControls& c) { c.movement = nan; },
        [] (ReverbControls& c) { c.movementRateHz = inf; },  [] (ReverbControls& c) { c.eq[0].frequencyHz = nan; },
        [] (ReverbControls& c) { c.eq[2].gainDb = inf; },    [] (ReverbControls& c) { c.eq[3].q = nan; },
        [] (ReverbControls& c) { c.speakerLevelDb[0] = nan; }, [] (ReverbControls& c) { c.speakerDelayMs[1] = inf; });

    ReverbControls good;
    good.mix = 0.5f;
    good.predelayMs = 12.0f;
    good.brightnessDb = 2.0f;
    good.lowCutHz = 80.0f;
    good.highCutHz = 9000.0f;
    good.widthPercent = 140.0f;
    good.monoBelowHz = 120.0f;
    good.ducking = 0.3f;
    good.movement = 0.4f;
    good.eq[2].gainDb = 3.0f;

    // Half a second as asked, then half a second with one control broken, against a
    // second of the controls as asked.
    const auto play = [&] (bool breakOne)
    {
        ConvolutionReverb reverb;
        reverb.prepare (sr);
        reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (sr))));
        auto l = testing::noise (48000, 3), r = testing::noise (48000, 4);
        auto c = good;

        for (std::size_t pos = 0; pos < l.size(); pos += 480)
        {
            if (breakOne && pos == 24000)
                broken (c);

            reverb.process (l.data() + pos, r.data() + pos, 480, c);
        }

        CHECK (reverb.recoveries() == 0);
        return std::pair { l, r };
    };

    const auto [wantedL, wantedR] = play (false);
    const auto [l, r] = play (true);

    for (std::size_t n = 0; n < l.size(); ++n)
    {
        REQUIRE (l[n] == wantedL[n]);
        REQUIRE (r[n] == wantedR[n]);
    }
}

TEST_CASE ("The tail reported to hosts covers the pre-delay, the response and the latency", "[reverb][tail]")
{
    // A host may stop calling a plugin once the reported tail has passed after its input
    // went quiet, so the tail must not be shorter than the sound really goes on for.
    const auto reported = [] (bool lowCpu, float predelayMs, float speakerDelayMs)
    {
        ImpulseResponseRequest request;
        request.sampleRate = 48000.0;
        request.space = 3;
        request.decaySeconds = bundledSpaces[3].decaySeconds;
        request.latencySamples = lowCpu ? static_cast<int> (ConvolutionReverb::lowCpuLatency (48000.0)) : 0;

        ConvolutionReverb reverb;
        reverb.prepare (48000.0);
        KernelBuilder builder (reverb);
        builder.buildNow (request);

        ReverbControls c;
        c.predelayMs = predelayMs;
        c.speakerDelayMs[0] = speakerDelayMs;
        std::vector<float> l (512, 0.0f), r (512, 0.0f);

        for (int b = 0; b < 20; ++b)
            reverb.process (l.data(), r.data(), l.size(), c);

        return reverb.tailSeconds();
    };

    const auto plain = reported (false, 0.0f, 0.0f);
    CHECK (plain > 0.0);
    CHECK (reported (false, 1000.0f, 40.0f) >= plain + 1.04 - 1.0e-6);
    CHECK (reported (true, 0.0f, 0.0f) >= plain + ConvolutionReverb::lowCpuLatency (48000.0) / 48000.0 - 1.0e-6);
    CHECK (reported (false, 0.0f, 1.0e6f) <= plain + SpeakerTrim::maxDelayMs / 1000.0 + 1.0e-6);
}

//==============================================================================
// The whole reverb nulled against DirectConvolver, and kernels changing faster
// than their tails ring out.

namespace
{
    /** Noise under an exponential envelope, scaled to unit energy: a response
        nothing could mistake for one built from another seed. */
    std::vector<float> noiseResponse (std::size_t length, unsigned seed, double decaySamples)
    {
        auto h = testing::noise (length, seed);
        double e = 0.0;

        for (std::size_t n = 0; n < length; ++n)
        {
            h[n] *= static_cast<float> (std::exp (-static_cast<double> (n) / decaySamples));
            e += static_cast<double> (h[n]) * h[n];
        }

        for (auto& s : h)
            s = static_cast<float> (s / std::sqrt (e));

        return h;
    }

    ImpulseResponseSet noiseResponses (std::size_t length, unsigned seed, double decaySamples)
    {
        ImpulseResponseSet set;
        set.sampleRate = 48000.0;

        for (unsigned p = 0; p < 4; ++p)
            set.paths[p] = noiseResponse (length, seed + p, decaySamples);

        return set;
    }

    /** The reference: input, then silence, through DirectConvolver; `length` samples of it. */
    std::vector<float> directlyConvolved (const std::vector<float>& input, const std::vector<float>& response, std::size_t length)
    {
        std::vector<float> x (length, 0.0f), y (length, 0.0f);
        std::copy_n (input.begin(), std::min (input.size(), length), x.begin());

        DirectConvolver direct;
        direct.setImpulseResponse (response);
        direct.process (x, y);
        return y;
    }

    /** Settings at which the wet path is nothing but the convolution: fully
        wet, no pre-delay, the cuts at their ends (off), no brightness, EQ
        flat, width 100 % (mean + spread x 1 gives each speaker back), Cloud
        100 % (Cloud's neutral: lower gathers the reverb towards the sound),
        no mono bass, ducking, freeze or movement, every speaker at 0 dB and
        0 ms, and the living tail silent (a kernel's living level is 0 unless
        set).

        Two things remain that aren't exactly nothing, both far under the
        tolerance: the dry gain at full wet is cos (pi / 2) in float, -4.4e-8,
        and the width stage rounds mean + (x - mean) in the last bit. */
    ReverbControls neutral()
    {
        ReverbControls c;
        c.mix = 1.0f;
        c.predelayMs = 0.0f;
        c.brightnessDb = 0.0f;
        c.lowCutHz = 20.0f;
        c.highCutHz = 20000.0f;
        c.widthPercent = 100.0f;
        c.cloudPercent = 100.0f;
        c.monoBelowHz = 0.0f;
        c.ducking = 0.0f;
        c.outputGain = 1.0f;
        c.freeze = false;
        c.movement = 0.0f;
        c.eq = WetEqualiser::flat;
        c.speakerLevelDb.fill (0.0f);
        c.speakerDelayMs.fill (0.0f);
        return c;
    }

    /** Plays every channel through the reverb in blocks of random length, from
        a single sample to several chunks. */
    void playInRandomBlocks (ConvolutionReverb& reverb, std::vector<std::vector<float>>& channels,
                             const ReverbControls& c, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_int_distribution<std::size_t> sizes (1, 3 * ConvolutionReverb::chunkSize + 37);
        std::vector<float*> pointers (channels.size());
        const auto length = channels.front().size();

        for (std::size_t pos = 0; pos < length;)
        {
            const auto n = std::min (sizes (rng), length - pos);

            for (std::size_t ch = 0; ch < channels.size(); ++ch)
                pointers[ch] = channels[ch].data() + pos;

            reverb.process (pointers.data(), pointers.size(), n, c);
            pos += n;
        }
    }

    struct WorstError
    {
        float error = 0.0f;
        std::size_t at = 0;
    };

    /** The largest difference between out and the reference played `latency`
        samples later (and silent before and after it). */
    WorstError worstError (const std::vector<float>& out, const std::vector<float>& reference, std::size_t latency)
    {
        WorstError worst;

        for (std::size_t n = 0; n < out.size(); ++n)
        {
            const auto wanted = n >= latency && n - latency < reference.size() ? reference[n - latency] : 0.0f;
            const auto error = std::abs (out[n] - wanted);

            if (! (error <= worst.error)) // a NaN is the worst of all
            {
                worst.error = std::isfinite (error) ? error : std::numeric_limits<float>::infinity();
                worst.at = n;
            }
        }

        return worst;
    }

    float peakOf (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        float most = 0.0f;

        for (auto n = from; n < std::min (to, x.size()); ++n)
            most = std::max (most, std::abs (x[n]));

        return most;
    }
}

TEST_CASE ("At neutral settings each output is each input through exactly its own true-stereo path", "[reverb][null]")
{
    // A response longer than 8192 taps reaches every stage of the partitioned
    // convolver: the head, the 64 and 512 stages and the 4096 one from tap 8192.
    constexpr double sr = 48000.0;
    const auto set = noiseResponses (9000, 300, 2500.0);
    const auto input = testing::noise (4000, 31);
    const auto length = input.size() + set.length();

    using P = ImpulseResponseSet;
    const std::array<std::array<P::Path, 2>, 2> paths { { { P::leftToLeft, P::leftToRight }, { P::rightToLeft, P::rightToRight } } };
    std::array<std::array<std::vector<float>, 2>, 2> reference; // [input][output]
    float peak = 0.0f;

    for (std::size_t i = 0; i < 2; ++i)
    {
        for (std::size_t k = 0; k < 2; ++k)
        {
            reference[i][k] = directlyConvolved (input, set.paths[paths[i][k]], length);
            peak = std::max (peak, peakOf (reference[i][k]));
        }
    }

    // Low CPU leaves out the time-domain head and the small stages: the same
    // response, exactly its latency late.
    for (const auto latency : { std::size_t { 0 }, ConvolutionReverb::lowCpuLatency (sr) })
    {
        for (std::size_t i = 0; i < 2; ++i)
        {
            ConvolutionReverb reverb;
            reverb.prepare (sr);
            reverb.submit (std::make_unique<ReverbKernel> (set, LivingTailDesign {}, latency));

            std::vector<std::vector<float>> channels (2, std::vector<float> (length + latency, 0.0f));
            std::copy (input.begin(), input.end(), channels[i].begin());
            playInRandomBlocks (reverb, channels, neutral(), static_cast<unsigned> (latency + i));
            REQUIRE (reverb.latency() == latency);

            for (std::size_t k = 0; k < 2; ++k)
            {
                const auto worst = worstError (channels[k], reference[i][k], latency);
                INFO ("latency " << latency << ", input " << i << " to output " << k << ": worst error "
                                 << worst.error / peak << " of the peak, at sample " << worst.at);
                CHECK (worst.error <= 1.0e-4f * peak);
            }
        }
    }
}

TEST_CASE ("At neutral settings each surround output is each input speaker through exactly its own paths", "[reverb][null][surround]")
{
    constexpr double sr = 48000.0;
    const auto which = GENERATE (0, 1);
    const auto layout = which == 0 ? layouts::fivePointOne() : layouts::sevenPointOnePointFour();
    const auto numSpeakers = layout.size();

    // Each stage source to each speaker its own noise, the LFE's included (a
    // kernel has a path per output; the reverb never plays the LFE's).
    SpatialResponseSet set;
    set.sampleRate = sr;
    unsigned seed = 500;

    for (auto& source : set.paths)
        for (std::size_t k = 0; k < numSpeakers; ++k)
            source.push_back (noiseResponse (4600, seed++, 1500.0));

    const auto input = testing::noise (2048, 41);
    const auto length = input.size() + set.length();

    std::array<std::vector<std::vector<float>>, 2> reference; // [source][speaker]
    float peak = 0.0f;

    for (std::size_t s = 0; s < 2; ++s)
    {
        for (std::size_t k = 0; k < numSpeakers; ++k)
        {
            reference[s].push_back (directlyConvolved (input, set.paths[s][k], length));
            peak = std::max (peak, peakOf (reference[s].back()));
        }
    }

    for (const auto latency : { std::size_t { 0 }, ConvolutionReverb::lowCpuLatency (sr) })
    {
        for (std::size_t j = 0; j < numSpeakers; ++j)
        {
            // The documented routing: left-hand speakers feed the left stage source,
            // right-hand ones the right, centred ones both at -3 dB, the LFE neither.
            const auto& from = layout[j];
            const auto toLeft = from.isLeft() ? 1.0f : from.isCentred() ? std::sqrt (0.5f) : 0.0f;
            const auto toRight = from.isRight() ? 1.0f : from.isCentred() ? std::sqrt (0.5f) : 0.0f;

            ConvolutionReverb reverb;
            reverb.prepare (sr, layout, layout);
            reverb.submit (std::make_unique<ReverbKernel> (set, LivingTailDesign {}, latency));

            std::vector<std::vector<float>> channels (numSpeakers, std::vector<float> (length + latency, 0.0f));
            std::copy (input.begin(), input.end(), channels[j].begin());
            playInRandomBlocks (reverb, channels, neutral(), static_cast<unsigned> (100 * which + static_cast<int> (latency + j)));
            REQUIRE (reverb.latency() == latency);

            for (std::size_t k = 0; k < numSpeakers; ++k)
            {
                std::vector<float> wanted (length, 0.0f);

                if (! layout[k].lfe)
                    for (std::size_t n = 0; n < length; ++n)
                        wanted[n] = toLeft * reference[0][k][n] + toRight * reference[1][k][n];

                const auto worst = worstError (channels[k], wanted, latency);
                INFO ((which == 0 ? "5.1" : "7.1.4") << ", latency " << latency << ", input speaker " << j << " to speaker " << k
                                                     << ": worst error " << worst.error / peak << " of the peak, at sample " << worst.at);
                CHECK (worst.error <= 1.0e-4f * peak);
            }
        }
    }
}

namespace
{
    /** Steady noise, low-passed at 200 Hz (fourth order), so that what the reverb
        makes of it is smooth: its third difference is then tiny and a click (a
        step, anything sharp) stands far out of it. White noise is as rough as a
        click, and would hide one. */
    std::vector<float> smoothNoise (std::size_t length, unsigned seed)
    {
        auto x = testing::noise (length, seed);

        for (int pass = 0; pass < 2; ++pass)
        {
            Biquad lp;
            lp.setCoefficients (Biquad::lowPass (48000.0, 200.0));
            lp.process (x.data(), x.size());
        }

        return x;
    }

    /** The largest third difference over [from, to) of either channel, as
        tests/plugin/ClickTests.cpp measures a click. */
    float roughness (const std::vector<float>& l, const std::vector<float>& r, std::size_t from, std::size_t to)
    {
        float most = 0.0f;

        for (const auto* x : { &l, &r })
            for (auto i = std::max<std::size_t> (from, 3); i < std::min (to, x->size()); ++i)
                most = std::max (most, std::abs ((*x)[i] - 3.0f * (*x)[i - 1] + 3.0f * (*x)[i - 2] - (*x)[i - 3]));

        return most;
    }

    float decibels (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-10f)); }

    /** Plays l and r in blocks, submitting the next of `kernels` (moved from)
        at each of `at`, which fall on block boundaries. */
    void playSwapping (ConvolutionReverb& reverb, std::vector<float>& l, std::vector<float>& r,
                       std::vector<std::unique_ptr<ReverbKernel>>& kernels, const std::vector<std::size_t>& at,
                       std::size_t block = 240)
    {
        std::size_t next = 0;

        for (std::size_t pos = 0; pos < l.size(); pos += block)
        {
            while (next < at.size() && at[next] <= pos)
                reverb.submit (std::move (kernels[next++]));

            reverb.process (l.data() + pos, r.data() + pos, std::min (block, l.size() - pos), neutral());
        }
    }

    /** Every kernel takeRetired() has to give, still alive, so that no address
        is reused while they are compared. */
    std::vector<std::unique_ptr<ReverbKernel>> drainRetired (ConvolutionReverb& reverb)
    {
        std::vector<std::unique_ptr<ReverbKernel>> back;

        while (auto k = reverb.takeRetired())
            back.push_back (std::move (k));

        return back;
    }

    std::size_t timesBack (const std::vector<std::unique_ptr<ReverbKernel>>& back, const ReverbKernel* kernel)
    {
        return static_cast<std::size_t> (std::count_if (back.begin(), back.end(), [kernel] (const auto& k) { return k.get() == kernel; }));
    }
}

TEST_CASE ("Rooms changing faster than their tails ring out never click, and every kernel comes back once", "[reverb][swap]")
{
    // Dragging a decay band, automating scenes or clicking through spaces: kernels
    // A, B, C and D 30 ms apart, inside the 40 ms handover, into steady sound. As
    // with a decay being dragged, they are one room with four decays, alike enough
    // that a gain error in a handover adds up rather than averaging out.
    constexpr double sr = 48000.0;
    constexpr std::size_t settle = 24000, gap = 1440; // 0.5 s, then 30 ms apart
    const std::array<double, 4> decays { 2400.0, 3600.0, 1800.0, 3000.0 };
    std::vector<ImpulseResponseSet> rooms;

    for (const auto decay : decays)
        rooms.push_back (noiseResponses (9600, 700, decay));

    const auto length = settle + 3 * gap + 28800;
    const auto inputL = smoothNoise (length, 61), inputR = smoothNoise (length, 62);

    std::vector<std::unique_ptr<ReverbKernel>> kernels;
    std::vector<ReverbKernel*> submitted;

    for (const auto& room : rooms)
    {
        kernels.push_back (std::make_unique<ReverbKernel> (room));
        submitted.push_back (kernels.back().get());
    }

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    auto l = inputL, r = inputR;
    playSwapping (reverb, l, r, kernels, { 0, settle, settle + gap, settle + 2 * gap });

    // Each room alone, for the level it plays at.
    constexpr std::size_t window = 19200; // the swaps and 0.3 s after
    float loudestAlone = 0.0f;

    for (const auto& room : rooms)
    {
        ConvolutionReverb alone;
        alone.prepare (sr);
        alone.submit (std::make_unique<ReverbKernel> (room));
        auto al = inputL, ar = inputR;
        run (alone, al, ar, neutral(), 240);
        loudestAlone = std::max ({ loudestAlone, peakOf (al, settle, settle + window), peakOf (ar, settle, settle + window) });
    }

    bool finite = true;

    for (std::size_t n = 0; n < length; ++n)
        finite = finite && std::isfinite (l[n]) && std::isfinite (r[n]);

    REQUIRE (finite);

    const auto level = std::max ({ peakOf (l, settle - 12000, settle), peakOf (r, settle - 12000, settle),
                                   peakOf (l, length - 12000), peakOf (r, length - 12000) });
    const auto steady = std::max (roughness (l, r, settle - 12000, settle), roughness (l, r, length - 12000, length));
    const auto click = roughness (l, r, settle, settle + window);
    const auto swapPeak = std::max (peakOf (l, settle, settle + window), peakOf (r, settle, settle + window));

    INFO ("click " << decibels (click / level) << " dB, steady roughness " << decibels (steady / level) << " dB, peak while swapping "
                   << decibels (swapPeak / loudestAlone) << " dB over the loudest room alone");
    CHECK (decibels (click / level) < std::max (-40.0f, decibels (steady / level) + 6.0f));

    // No louder than the loudest of the rooms alone: a gain error in a handover (both
    // kernels at full input, say) goes over. Between identical rooms the next test
    // holds the handover to the exact sum.
    CHECK (decibels (swapPeak / loudestAlone) < 1.0f);

    // Once the tails have rung out, every kernel but D is back for freeing, once.
    const auto back = drainRetired (reverb);

    for (std::size_t k = 0; k < 3; ++k)
        CHECK (timesBack (back, submitted[k]) == 1);

    CHECK (back.size() == 3);
}

TEST_CASE ("Swapping to the same room while sound plays changes nothing", "[reverb][swap]")
{
    // The input moves from the outgoing kernel to the new one over 40 ms while the
    // outgoing one rings out what it heard. Between identical rooms the two shares
    // add up to the input exactly, so the handover is transparent. Twice here, the
    // second once the first one's tail has rung out.
    //
    // Two swaps closer together than a tail can't be: the reverb keeps two
    // outgoing kernels, so a third arrival fades the oldest out over 40 ms, cutting
    // its tail short by design. That case is held to no click and no jump in level
    // by the test above.
    constexpr double sr = 48000.0;
    const auto room = noiseResponses (9600, 900, 3000.0);
    constexpr std::size_t first = 12000, second = first + 9600 + 1920 + 4800;
    const auto length = second + 24000;
    const auto inputL = testing::noise (length, 71), inputR = testing::noise (length, 72);

    ConvolutionReverb unswapped;
    unswapped.prepare (sr);
    unswapped.submit (std::make_unique<ReverbKernel> (room));
    auto wantedL = inputL, wantedR = inputR;
    run (unswapped, wantedL, wantedR, neutral(), 240);

    std::vector<std::unique_ptr<ReverbKernel>> kernels;

    for (int k = 0; k < 3; ++k)
        kernels.push_back (std::make_unique<ReverbKernel> (room));

    ConvolutionReverb swapped;
    swapped.prepare (sr);
    auto l = inputL, r = inputR;
    playSwapping (swapped, l, r, kernels, { 0, first, second });

    const auto worstL = worstError (l, wantedL, 0), worstR = worstError (r, wantedR, 0);
    INFO ("worst error " << worstL.error << " at " << worstL.at << " (left), " << worstR.error << " at " << worstR.at << " (right)");
    CHECK (worstL.error < 1.0e-5f);
    CHECK (worstR.error < 1.0e-5f);
    CHECK (drainRetired (swapped).size() == 2);
}

TEST_CASE ("Forty swaps while nothing frees kernels neither allocate nor lose a kernel", "[reverb][swap][realtime]")
{
    // The builder frees retired kernels; here it doesn't get to. However many swaps
    // pile up, the audio thread must neither allocate nor free, nor drop a kernel.
    // Each swap waits for the last one's tail to ring out, so every kernel has
    // played and been finished with. A kernel is known by its length (one sample
    // more each): one freed by submit() leaves its address to the next.
    constexpr double sr = 48000.0;
    constexpr std::size_t block = 240, between = 7200; // 150 ms: the tail (under 1600 taps) and the 40 ms handover
    constexpr std::size_t shortest = 1500;
    const auto inputL = testing::noise (between, 81), inputR = testing::noise (between, 82);
    std::vector<float> l (block), r (block);

    ConvolutionReverb reverb;
    reverb.prepare (sr);

    std::size_t submitted = 0;
    std::vector<std::size_t> freedBySubmit; // lengths
    std::size_t allocations = 0, frees = 0;

    const auto submit = [&]
    {
        auto kernel = std::make_unique<ReverbKernel> (noiseResponses (shortest + submitted, 1100, 400.0));

        // submit() frees a kernel that was waiting and never picked up: the one before.
        testing::AllocationCounter counter;
        reverb.submit (std::move (kernel));

        if (counter.freed() > 0)
            freedBySubmit.push_back (shortest + submitted - 1);

        ++submitted;
    };

    const auto play = [&]
    {
        for (std::size_t pos = 0; pos < between; pos += block)
        {
            std::copy_n (inputL.begin() + static_cast<std::ptrdiff_t> (pos), block, l.begin());
            std::copy_n (inputR.begin() + static_cast<std::ptrdiff_t> (pos), block, r.begin());

            testing::AllocationCounter counter;
            reverb.process (l.data(), r.data(), block, neutral());
            allocations += counter.count();
            frees += counter.freed();
        }
    };

    submit();
    play();

    for (int swap = 0; swap < 40; ++swap)
    {
        submit();
        play();
    }

    CHECK (allocations == 0);
    CHECK (frees == 0);

    // The builder comes back: what was finished with is there to free, and the rest
    // once the reverb has played on (a kernel that had to wait goes in, and the one
    // it replaces rings out).
    auto back = drainRetired (reverb);
    play();
    play();

    for (auto& k : drainRetired (reverb))
        back.push_back (std::move (k));

    CHECK (allocations == 0);
    CHECK (frees == 0);

    // Each kernel but the one playing came back exactly once, or was freed by
    // submit() on the builder's thread without ever having played.
    for (std::size_t k = 0; k < submitted; ++k)
    {
        const auto length = shortest + k;
        const auto returned = std::count_if (back.begin(), back.end(), [length] (const auto& b) { return b->length() == length; });
        const auto replaced = std::count (freedBySubmit.begin(), freedBySubmit.end(), length);
        INFO ("kernel " << k << " of " << submitted << ": came back " << returned << " times, freed by submit " << replaced);
        CHECK (returned + replaced == (k + 1 < submitted ? 1 : 0));
    }
}

TEST_CASE ("A stereo room longer than the sample budget is shortened to fit, fading out", "[reverb][builder]")
{
    // Four paths of 12,000 samples: a quarter of a second at 48 kHz, less than the room.
    constexpr double sr = 48000.0;
    constexpr std::size_t length = 12000, budget = 4 * length;
    const auto request = hall (sr);

    ConvolutionReverb whole, shortened;
    whole.prepare (sr);
    shortened.prepare (sr);
    KernelBuilder unlimited (whole, std::chrono::milliseconds (30), 0);
    KernelBuilder limited (shortened, std::chrono::milliseconds (30), budget);
    unlimited.buildNow (request);
    limited.buildNow (request);

    const auto full = unlimited.lastResponses();
    const auto cut = limited.lastResponses();
    REQUIRE (full->length() > length);
    CHECK (unlimited.shortenedTo() == 0.0);
    CHECK (limited.shortenedTo() == Catch::Approx (static_cast<double> (length) / sr));

    // Every path is the room as it was up to its last quarter, which fades to nothing.
    const auto fade = length / 4;

    for (std::size_t p = 0; p < 4; ++p)
    {
        INFO ("path " << p);
        const auto& kept = cut->paths[p];
        const auto& room = full->paths[p];
        REQUIRE (kept.size() == length);
        CHECK (std::equal (kept.begin(), kept.end() - static_cast<std::ptrdiff_t> (fade), room.begin()));

        auto fading = true;
        auto gain = 1.0f;

        for (auto n = length - fade; n < length; ++n)
        {
            if (std::abs (room[n]) < 1.0e-6f)
                continue;

            const auto now = kept[n] / room[n];
            fading = fading && now >= 0.0f && now <= gain + 1.0e-5f;
            gain = now;
        }

        CHECK (fading);
        CHECK (gain < 1.0e-3f);
    }

    // The reverb plays the shortened room: nothing rings past its end.
    std::vector<float> l (length + 9600, 0.0f), r (l.size(), 0.0f);
    l[0] = 1.0f;
    ReverbControls c;
    c.mix = 1.0f;
    run (shortened, l, r, c);

    CHECK (energy (l, 0, length) > 1.0e-3);
    CHECK (energy (l, length + 64) < 1.0e-12);
    CHECK (energy (r, length + 64) < 1.0e-12);

    // A budget the room fits in leaves it whole.
    ConvolutionReverb roomy;
    roomy.prepare (sr);
    KernelBuilder ample (roomy, std::chrono::milliseconds (30), 4 * full->length());
    ample.buildNow (request);
    CHECK (ample.shortenedTo() == 0.0);
    CHECK (ample.lastResponses()->paths == full->paths);
}

TEST_CASE ("One block far larger than a chunk plays as small blocks do", "[reverb]")
{
    constexpr double sr = 48000.0;
    const auto set = synthesiseImpulseResponse (hall (sr));
    const auto stereo = GENERATE (true, false);
    INFO ((stereo ? "stereo" : "mono (left and right the same buffer)"));

    ReverbControls c;
    c.mix = 0.5f;

    const auto played = [&] (std::size_t block)
    {
        ConvolutionReverb reverb;
        reverb.prepare (sr);
        reverb.submit (std::make_unique<ReverbKernel> (set));

        // A quarter of a second of noise, then the room rings out.
        auto l = testing::noise (static_cast<std::size_t> (sr), 41), r = testing::noise (l.size(), 42);
        std::fill (l.begin() + static_cast<std::ptrdiff_t> (l.size() / 4), l.end(), 0.0f);
        std::fill (r.begin() + static_cast<std::ptrdiff_t> (r.size() / 4), r.end(), 0.0f);

        for (std::size_t pos = 0; pos < l.size(); pos += block)
        {
            const auto n = std::min (block, l.size() - pos);
            reverb.process (l.data() + pos, stereo ? r.data() + pos : l.data() + pos, n, c);
        }

        return std::make_pair (l, r);
    };

    const auto [smallL, smallR] = played (480);
    const auto [oneL, oneR] = played (48000);
    REQUIRE (energy (smallL, smallL.size() / 2) > 1.0e-3);

    float worst = 0.0f, loudest = 0.0f;

    for (std::size_t n = 0; n < smallL.size(); ++n)
    {
        worst = std::max (worst, std::abs (oneL[n] - smallL[n]));
        loudest = std::max (loudest, std::abs (smallL[n]));

        if (stereo)
            worst = std::max (worst, std::abs (oneR[n] - smallR[n]));
    }

    INFO ("loudest " << loudest << ", differs by up to " << worst);
    CHECK (worst <= 1.0e-5f * loudest);
}

namespace
{
    /** An impulse into the left input, then a burst of noise into the right, through the
        reverb in 480-sample blocks. */
    std::pair<std::vector<float>, std::vector<float>> impulseAndBurst (ConvolutionReverb& reverb, const ReverbControls& c)
    {
        std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
        l[0] = 1.0f;
        const auto burst = testing::noise (2400, 51);
        std::copy (burst.begin(), burst.end(), r.begin() + 4800);
        run (reverb, l, r, c);
        return { l, r };
    }

    float worstDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        float worst = 0.0f;

        for (std::size_t n = 0; n < std::min (a.size(), b.size()); ++n)
            worst = std::max (worst, std::abs (a[n] - b[n]));

        return worst;
    }
}

TEST_CASE ("Prepared again at the same rate, the reverb keeps its room and forgets what it heard", "[reverb]")
{
    constexpr double sr = 48000.0;
    const auto set = synthesiseImpulseResponse (hall (sr));
    ReverbControls c;
    c.mix = 0.7f;
    c.predelayMs = 12.0f;

    ConvolutionReverb fresh;
    fresh.prepare (sr);
    fresh.submit (std::make_unique<ReverbKernel> (set));
    const auto [wantedL, wantedR] = impulseAndBurst (fresh, c);
    const auto loudest = peakOf (wantedR);
    REQUIRE (loudest > 0.0f);

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    reverb.submit (std::make_unique<ReverbKernel> (set));

    // Played loud with the reverb ducked, and stopped with the room still ringing.
    auto ducked = c;
    ducked.ducking = 1.0f;
    auto l = testing::noise (12000, 52), r = testing::noise (12000, 53);
    run (reverb, l, r, ducked);
    REQUIRE (reverb.isReady());

    reverb.prepare (sr);
    CHECK (reverb.isReady());
    CHECK (reverb.tailSeconds() == Catch::Approx (static_cast<double> (set.length()) / sr));

    // No room is sent again: the one it had plays from silence, as in a reverb that never played.
    const auto [againL, againR] = impulseAndBurst (reverb, c);
    INFO ("loudest " << loudest << ", differs by up to " << std::max (worstDifference (againL, wantedL), worstDifference (againR, wantedR)));
    CHECK (worstDifference (againL, wantedL) <= 1.0e-6f * loudest);
    CHECK (worstDifference (againR, wantedR) <= 1.0e-6f * loudest);
    CHECK (reverb.takeRetired() == nullptr);
}

TEST_CASE ("Prepared at another rate, the reverb drops its room until one for the new rate comes", "[reverb]")
{
    ConvolutionReverb reverb;
    reverb.prepare (48000.0);
    reverb.submit (std::make_unique<ReverbKernel> (synthesiseImpulseResponse (hall (48000.0))));

    ReverbControls c;
    c.mix = 1.0f;
    auto l = testing::noise (9600, 54), r = testing::noise (9600, 55);
    run (reverb, l, r, c);
    REQUIRE (reverb.isReady());

    reverb.prepare (44100.0);
    CHECK_FALSE (reverb.isReady());
    CHECK (reverb.tailSeconds() == 0.0);
    CHECK (reverb.takeRetired() == nullptr); // freed in prepare(), with audio stopped

    // Nothing of the 48 kHz room plays at 44.1 kHz: fully wet is silent.
    std::vector<float> silentL (8820, 0.0f), silentR (8820, 0.0f);
    silentL[0] = silentR[0] = 1.0f;
    run (reverb, silentL, silentR, c);
    CHECK (energy (silentL) < 1.0e-12);
    CHECK (energy (silentR) < 1.0e-12);

    // A room for the new rate plays as in a reverb prepared for it from the start.
    const auto set = synthesiseImpulseResponse (hall (44100.0));
    reverb.submit (std::make_unique<ReverbKernel> (set));
    const auto [gotL, gotR] = impulseAndBurst (reverb, c);
    CHECK (reverb.isReady());

    ConvolutionReverb fresh;
    fresh.prepare (44100.0);
    fresh.submit (std::make_unique<ReverbKernel> (set));
    const auto [wantedL, wantedR] = impulseAndBurst (fresh, c);
    const auto loudest = peakOf (wantedR);
    REQUIRE (loudest > 0.0f);
    CHECK (worstDifference (gotL, wantedL) <= 1.0e-6f * loudest);
    CHECK (worstDifference (gotR, wantedR) <= 1.0e-6f * loudest);
}

TEST_CASE ("reset() keeps the room and forgets what it heard, even part way through a swap", "[reverb]")
{
    constexpr double sr = 48000.0;
    auto request = hall (sr);
    const auto before = synthesiseImpulseResponse (request);
    request.decaySeconds[4] = 0.3f; // a different room
    const auto after = synthesiseImpulseResponse (request);

    ReverbControls c;
    c.mix = 0.6f;

    ConvolutionReverb fresh;
    fresh.prepare (sr);
    fresh.submit (std::make_unique<ReverbKernel> (after));
    const auto [wantedL, wantedR] = impulseAndBurst (fresh, c);
    const auto loudest = peakOf (wantedR);
    REQUIRE (loudest > 0.0f);

    ConvolutionReverb reverb;
    reverb.prepare (sr);
    auto first = std::make_unique<ReverbKernel> (before);
    const auto* const firstKernel = first.get();
    reverb.submit (std::move (first));

    auto l = testing::noise (9600, 56), r = testing::noise (9600, 57);
    run (reverb, l, r, c);

    // The new room arrives and takes the input over from the old one for 40 ms; one block in, reset().
    reverb.submit (std::make_unique<ReverbKernel> (after));
    std::vector<float> blockL (480, 0.5f), blockR (480, 0.5f);
    reverb.process (blockL.data(), blockR.data(), blockL.size(), c);
    CHECK (reverb.takeRetired() == nullptr); // the old room still rings
    reverb.reset();

    // The old room is done with, and goes back to be freed.
    const auto retired = reverb.takeRetired();
    CHECK (retired.get() == firstKernel);
    CHECK (reverb.takeRetired() == nullptr);
    CHECK (reverb.isReady());

    // Silence in is silence out: nothing that was ringing carries on.
    std::vector<float> quietL (9600, 0.0f), quietR (9600, 0.0f);
    run (reverb, quietL, quietR, c);
    CHECK (energy (quietL) == 0.0);
    CHECK (energy (quietR) == 0.0);

    // And the new room plays as in a reverb that never played anything else.
    reverb.reset();
    const auto [gotL, gotR] = impulseAndBurst (reverb, c);
    INFO ("loudest " << loudest << ", differs by up to " << std::max (worstDifference (gotL, wantedL), worstDifference (gotR, wantedR)));
    CHECK (worstDifference (gotL, wantedL) <= 1.0e-6f * loudest);
    CHECK (worstDifference (gotR, wantedR) <= 1.0e-6f * loudest);
}

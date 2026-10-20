#include "TestHelpers.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/Spaces.h>

#include <catch2/catch_test_macros.hpp>

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
    reverb.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (hall (48000.0))));

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
    reverb.submit (std::make_unique<TrueStereoKernel> (set));

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
        unswapped.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (request)));
        std::vector<float> rr (r);
        run (unswapped, reference, rr, c);
    }

    ConvolutionReverb swapped;
    swapped.prepare (sr);
    swapped.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (request)));

    std::vector<float> sl (24000, 0.0f), sr2 (24000, 0.0f);
    for (std::size_t n = 0; n < 2400; ++n)
        sl[n] = sr2[n] = std::sin (static_cast<float> (n) * 0.05f);

    request.decaySeconds[4] = 0.2f; // a different response
    for (std::size_t pos = 0; pos < sl.size(); pos += 480)
    {
        if (pos == 4800)
            swapped.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (request)));
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
    reverb.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (hall (44100.0))));

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
    reverb.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (hall (48000.0))));

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
        reverb.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (hall (48000.0))));

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
    reverb.submit (std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (hall (48000.0))));

    auto request = hall (48000.0);
    request.decaySeconds[2] = 1.5f;
    auto second = std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (request));

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
            auto kernel = std::make_unique<TrueStereoKernel> (synthesiseImpulseResponse (request));
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

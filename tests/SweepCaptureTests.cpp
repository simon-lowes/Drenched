#include <drenched/ImpulseResponseSynth.h>
#include <drenched/PartitionedConvolver.h>
#include <drenched/RecordedResponse.h>
#include <drenched/Spaces.h>
#include <drenched/SweepCapture.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;

    SweepSettings shortSweep (double sweepSeconds = 3.0, double tailSeconds = 2.0)
    {
        SweepSettings s;
        s.sampleRate = rate;
        s.sweepSeconds = sweepSeconds;
        s.tailSeconds = tailSeconds;
        return s;
    }

    std::vector<float> convolve (const std::vector<float>& x, const std::vector<float>& h)
    {
        PartitionedConvolver convolver ({ std::span<const float> (h) });
        std::vector<float> in (x);
        in.resize (x.size() + h.size(), 0.0f);
        std::vector<float> out (in.size());
        float* outputs[] = { out.data() };
        convolver.process (in.data(), outputs, in.size());
        return out;
    }

    /** A bundled space's left-to-left path, cut short and band-limited well
        inside the sweep's range, so a perfect measurement equals it. */
    std::vector<float> referenceRoom (int space, double seconds)
    {
        ImpulseResponseRequest request;
        request.sampleRate = rate;
        request.space = space;
        request.decaySeconds = bundledSpaces[static_cast<std::size_t> (space)].decaySeconds;

        auto ir = synthesiseImpulseResponse (request).paths[ImpulseResponseSet::leftToLeft];
        ir.resize (static_cast<std::size_t> (seconds * rate));

        for (int pass = 0; pass < 2; ++pass)
        {
            Biquad hp, lp;
            hp.setCoefficients (Biquad::highPass (rate, 40.0));
            lp.setCoefficients (Biquad::lowPass (rate, 12000.0));
            hp.process (ir.data(), ir.size());
            lp.process (ir.data(), ir.size());
        }

        return ir;
    }

    double energyOf (const std::vector<float>& x)
    {
        double e = 0.0;

        for (auto v : x)
            e += static_cast<double> (v) * v;

        return e;
    }

    /** Error of a measurement against the reference, after an optional best
        fitting gain (a cubic nonlinearity changes the fundamental's level as
        well as adding harmonics, and that change is genuinely the speaker's). */
    double errorDb (const std::vector<float>& measured, std::size_t offset, const std::vector<float>& reference,
                    std::size_t length, bool fitGain = false)
    {
        double cross = 0.0, own = 0.0;

        for (std::size_t n = 0; n < length; ++n)
        {
            cross += static_cast<double> (measured[offset + n]) * reference[n];
            own += static_cast<double> (reference[n]) * reference[n];
        }

        const auto gain = fitGain ? cross / own : 1.0;
        double error = 0.0, signal = 0.0;

        for (std::size_t n = 0; n < length; ++n)
        {
            const double d = measured[offset + n] - gain * reference[n];
            error += d * d;
            signal += static_cast<double> (reference[n]) * reference[n];
        }

        return 10.0 * std::log10 (error / signal);
    }
}

TEST_CASE ("The sweep covers its range at the level asked for", "[capture]")
{
    const SineSweep sweep (shortSweep());
    const auto& x = sweep.signal();

    CHECK (x.size() == static_cast<std::size_t> (3.0 * rate));
    CHECK (sweep.segmentLength() == x.size() + static_cast<std::size_t> (2.0 * rate));

    const auto peak = *std::max_element (x.begin(), x.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    CHECK (std::abs (20.0 * std::log10 (std::abs (peak)) + 12.0) < 0.1);

    // Faded at both ends, so playing it never clicks.
    CHECK (std::abs (x.front()) < 1.0e-6f);
    CHECK (std::abs (x.back()) < 1.0e-6f);

    // Frequency rises exponentially: halfway through, the geometric mean.
    std::size_t crossings = 0;
    const auto middle = x.size() / 2, window = static_cast<std::size_t> (0.05 * rate);

    for (auto n = middle - window / 2; n < middle + window / 2; ++n)
        crossings += (x[n] < 0.0f) != (x[n + 1] < 0.0f) ? 1 : 0;

    const auto hz = static_cast<double> (crossings) / 2.0 / 0.05;
    CHECK (std::abs (hz / std::sqrt (20.0 * 20000.0) - 1.0) < 0.05);

    CHECK (std::abs (sweep.harmonicLeadSeconds (2) - 3.0 * std::log (2.0) / std::log (1000.0)) < 1.0e-9);

    SECTION ("the top is kept below Nyquist")
    {
        auto low = shortSweep();
        low.sampleRate = 32000.0;
        CHECK (SineSweep (low).settings().endHz <= 0.45 * 32000.0);
    }
}

TEST_CASE ("Deconvolving a recorded sweep gives back the room", "[capture]")
{
    const SineSweep sweep (shortSweep());
    const auto room = referenceRoom (3, 1.5);
    const std::size_t delay = 300; // the speaker-to-microphone round trip
    const auto compared = std::min (room.size(), sweep.tailLength() - delay);

    const auto recordingOf = [&] (const std::vector<float>& played)
    {
        auto heard = convolve (played, room);
        heard.insert (heard.begin(), delay, 0.0f);
        heard.resize (sweep.segmentLength());

        const auto hiss = testing::noise (heard.size(), 5);

        for (std::size_t n = 0; n < heard.size(); ++n)
            heard[n] += 1.0e-5f * hiss[n];

        return heard;
    };

    SECTION ("a clean speaker")
    {
        const auto measured = sweep.deconvolve (recordingOf (sweep.signal()));
        CHECK (measured.size() == sweep.leadLength() + sweep.tailLength());
        CHECK (errorDb (measured, sweep.leadLength() + delay, room, compared) < -40.0);
    }

    SECTION ("a distorting speaker: the harmonics are cut away")
    {
        auto driven = sweep.signal();

        for (auto& v : driven)
            v = v + 0.8f * v * v + 1.5f * v * v * v; // several per cent of second and third harmonic

        const auto measured = sweep.deconvolve (recordingOf (driven));
        CHECK (errorDb (measured, sweep.leadLength() + delay, room, compared, true) < -30.0);

        // A plain linear fit would have been ruined: the distortion is far
        // louder than the error left.
        std::vector<float> distortion (driven.size());

        for (std::size_t n = 0; n < driven.size(); ++n)
            distortion[n] = driven[n] - sweep.signal()[n];

        CHECK (10.0 * std::log10 (energyOf (distortion) / energyOf (sweep.signal())) > -25.0);
    }

    SECTION ("a recording that stops early is padded, not read past")
    {
        auto heard = recordingOf (sweep.signal());
        heard.resize (heard.size() / 2);
        CHECK (sweep.deconvolve (heard).size() == sweep.leadLength() + sweep.tailLength());
    }
}

TEST_CASE ("A true-stereo capture through the recorder measures the room", "[capture]")
{
    const auto space = GENERATE (2, 3);
    const auto& spec = bundledSpaces[static_cast<std::size_t> (space)];

    ImpulseResponseRequest request;
    request.sampleRate = rate;
    request.space = space;
    request.decaySeconds = spec.decaySeconds;
    const auto paths = synthesiseImpulseResponse (request).paths;

    const auto longest = *std::max_element (spec.decaySeconds.begin(), spec.decaySeconds.end());
    const auto sweep = std::make_shared<SineSweep> (shortSweep (4.0, longest * 1.5));

    CaptureRecorder recorder;
    recorder.prepare (sweep, 2, 2);
    recorder.start();
    REQUIRE (recorder.state() == CaptureRecorder::State::running);

    // Each speaker reaches both microphones through the room.
    PartitionedConvolver fromLeft ({ std::span<const float> (paths[ImpulseResponseSet::leftToLeft]),
                                     std::span<const float> (paths[ImpulseResponseSet::leftToRight]) });
    PartitionedConvolver fromRight ({ std::span<const float> (paths[ImpulseResponseSet::rightToLeft]),
                                      std::span<const float> (paths[ImpulseResponseSet::rightToRight]) });

    constexpr std::size_t block = 256;
    std::vector<float> micL (block, 0.0f), micR (block, 0.0f), a (block), b (block), c (block), d (block);

    while (recorder.state() == CaptureRecorder::State::running)
    {
        // In place, as a plugin host does: the microphones arrive in the
        // buffers the speakers leave in. The room answers a block later.
        std::vector<float> left (micL), right (micR);
        float* buffers[] = { left.data(), right.data() };
        REQUIRE (recorder.process (buffers, 2, buffers, 2, block));

        float* fromLeftOut[] = { a.data(), b.data() };
        float* fromRightOut[] = { c.data(), d.data() };
        fromLeft.process (left.data(), fromLeftOut, block);
        fromRight.process (right.data(), fromRightOut, block);

        for (std::size_t n = 0; n < block; ++n)
        {
            micL[n] = a[n] + c[n];
            micR[n] = b[n] + d[n];
        }
    }

    REQUIRE (recorder.state() == CaptureRecorder::State::finished);
    CHECK (recorder.progress() == 1.0f);

    const auto channels = deconvolveCapture (*sweep, recorder.recordings(), 2);
    REQUIRE (channels.size() == 4);

    const auto recorded = prepareRecordedResponse (channels, rate, "capture");

    for (std::size_t band = 0; band < 8; ++band)
    {
        INFO ("space " << space << " band " << band << " measured " << recorded->room.decaySeconds[band]);
        CHECK (std::abs (recorded->room.decaySeconds[band] / spec.decaySeconds[band] - 1.0f) < 0.15f);
    }

    // Left and right speakers were told apart: the cross paths differ from
    // the direct ones, as the room's do.
    CHECK (recorded->responses.paths[ImpulseResponseSet::leftToLeft]
           != recorded->responses.paths[ImpulseResponseSet::rightToLeft]);
}

TEST_CASE ("The recorder plays each speaker in turn without allocating", "[capture][realtime]")
{
    const auto sweep = std::make_shared<SineSweep> (shortSweep (0.5, 0.25));
    const auto segment = sweep->segmentLength();

    CaptureRecorder recorder;
    recorder.prepare (sweep, 2, 1);

    std::vector<float> left (segment * 2 + 100), right (left.size());
    const auto input = testing::noise (left.size(), 3);

    SECTION ("idle, it leaves the audio alone")
    {
        std::vector<float> buffer (input);
        float* channels[] = { buffer.data() };
        CHECK_FALSE (recorder.process (channels, 1, channels, 1, 64));
        CHECK (buffer == input);
    }

    SECTION ("running")
    {
        recorder.start();

        std::vector<float> mono (input);
        std::size_t done = 0;

        {
            testing::AllocationCounter counter;

            while (done < left.size())
            {
                const auto n = std::min<std::size_t> (512, left.size() - done);
                std::copy_n (mono.begin() + static_cast<std::ptrdiff_t> (done), n, left.begin() + static_cast<std::ptrdiff_t> (done));
                const float* ins[] = { left.data() + done };
                float* outs[] = { left.data() + done, right.data() + done };
                recorder.process (ins, 1, outs, 2, n);
                done += n;
            }

            CHECK (counter.count() == 0);
        }

        CHECK (recorder.state() == CaptureRecorder::State::finished);

        const auto& x = sweep->signal();

        for (std::size_t n = 0; n < x.size(); n += 97)
        {
            CHECK (left[n] == x[n]);
            CHECK (right[n] == 0.0f);
            CHECK (left[segment + n] == 0.0f);
            CHECK (right[segment + n] == x[n]);
        }

        // What came in was recorded before the sweep replaced it.
        const auto& take = recorder.recordings()[0];
        CHECK (std::equal (take.begin(), take.end(), input.begin()));
        CHECK (recorder.inputPeak() == std::abs (*std::max_element (input.begin(), input.begin() + static_cast<std::ptrdiff_t> (take.size()),
                                                                    [] (float p, float q) { return std::abs (p) < std::abs (q); })));
    }

    SECTION ("cancelled part-way")
    {
        recorder.start();
        float* outs[] = { left.data(), right.data() };
        const float* ins[] = { left.data() };

        recorder.process (ins, 1, outs, 2, 1000);
        recorder.cancel();
        CHECK_FALSE (recorder.process (ins, 1, outs, 2, 1000));
        CHECK (recorder.state() == CaptureRecorder::State::cancelled);

        // A cancelled take can be prepared again and rerun.
        recorder.prepare (sweep, 1, 1);
        recorder.start();
        CHECK (recorder.process (ins, 1, outs, 2, 1000));
        CHECK (recorder.progress() > 0.0f);
    }

    CHECK_THROWS (recorder.prepare (sweep, 3, 1));
    CHECK_THROWS (deconvolveCapture (*sweep, { std::vector<float> (10) }, 1));
}

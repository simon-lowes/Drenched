#include <drenched/ConvolutionReverb.h>
#include <drenched/ImpulseResponseSynth.h>
#include <drenched/LivingTail.h>
#include <drenched/ReverbCloud.h>
#include <drenched/Spaces.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace drenched;

namespace
{
    constexpr double rate = 48000.0;
    constexpr std::size_t block = 256;

    double db (double ratio) { return 20.0 * std::log10 (ratio); }

    /** Plays `seconds` of noise in the given input channels (the rest silent)
        through the cloud's listener. */
    void hear (ReverbCloud& cloud, std::size_t numInputs, std::initializer_list<std::size_t> playing, double seconds,
               bool hold = false)
    {
        const auto source = testing::noise (block, 3u);
        const std::vector<float> quiet (block, 0.0f);
        std::vector<const float*> inputs (numInputs, quiet.data());

        for (const auto i : playing)
            inputs[i] = source.data();

        for (std::size_t n = 0; n < static_cast<std::size_t> (seconds * rate); n += block)
            cloud.listen (inputs.data(), numInputs, block, hold);
    }

    /** Runs the cloud over steady signals for `seconds` and returns the gains. */
    std::vector<float> settle (ReverbCloud& cloud, std::size_t numOutputs, float size, double seconds = 0.5)
    {
        std::vector<std::vector<float>> wet (numOutputs, std::vector<float> (block, 1.0f));
        std::vector<float*> pointers (numOutputs);

        for (std::size_t n = 0; n < static_cast<std::size_t> (seconds * rate); n += block)
        {
            for (std::size_t k = 0; k < numOutputs; ++k)
            {
                std::fill (wet[k].begin(), wet[k].end(), 1.0f);
                pointers[k] = wet[k].data();
            }

            cloud.process (pointers.data(), numOutputs, block, size);
        }

        std::vector<float> gains;

        for (std::size_t k = 0; k < numOutputs; ++k)
            gains.push_back (cloud.gain (k));

        return gains;
    }

    double powerOf (const std::vector<float>& gains, const SpeakerLayout& layout)
    {
        double p = 0.0;

        for (std::size_t k = 0; k < gains.size(); ++k)
            p += layout[k].lfe ? 0.0 : static_cast<double> (gains[k]) * gains[k];

        return p;
    }
}

TEST_CASE ("At full size the reverb is left exactly as it was", "[cloud]")
{
    ReverbCloud cloud;
    cloud.prepare (rate, layouts::stereo(), layouts::stereo());
    hear (cloud, 2, { 0 }, 0.5);

    auto left = testing::noise (block, 5u), right = testing::noise (block, 6u);
    const auto wantLeft = left, wantRight = right;
    float* wet[] { left.data(), right.data() };
    cloud.process (wet, 2, block, 100.0f);

    CHECK (left == wantLeft);
    CHECK (right == wantRight);
    CHECK (cloud.isNeutral());
}

TEST_CASE ("A sound panned left gathers the reverb on the left, at the same level", "[cloud]")
{
    const auto layout = layouts::stereo();
    ReverbCloud cloud;
    cloud.prepare (rate, layout, layout);
    hear (cloud, 2, { 0 }, 0.5);

    const auto narrow = settle (cloud, 2, 0.0f);
    INFO ("left " << db (narrow[0]) << " dB, right " << db (narrow[1]) << " dB");
    CHECK (db (narrow[0] / narrow[1]) > 15.0);
    CHECK (std::abs (powerOf (narrow, layout) - 2.0) < 1.0e-3);

    // Halfway is in between.
    const auto half = settle (cloud, 2, 50.0f);
    CHECK (db (half[0] / half[1]) > 3.0);
    CHECK (db (half[0] / half[1]) < db (narrow[0] / narrow[1]));
    CHECK (std::abs (powerOf (half, layout) - 2.0) < 1.0e-3);

    // Panned right, it moves over.
    hear (cloud, 2, { 1 }, 1.0);
    const auto moved = settle (cloud, 2, 0.0f);
    CHECK (db (moved[1] / moved[0]) > 15.0);
}

TEST_CASE ("A sound spread evenly keeps the reverb spread, whatever the size", "[cloud]")
{
    ReverbCloud cloud;
    cloud.prepare (rate, layouts::stereo(), layouts::stereo());
    hear (cloud, 2, { 0, 1 }, 0.5);

    const auto gains = settle (cloud, 2, 0.0f);
    CHECK (std::abs (gains[0] - 1.0f) < 1.0e-4f);
    CHECK (std::abs (gains[1] - 1.0f) < 1.0e-4f);
}

TEST_CASE ("In 7.1.4 the reverb gathers around a sound panned to the left rear", "[cloud][surround]")
{
    const auto layout = layouts::sevenPointOnePointFour(); // L R C LFE Lss Rss Lsr Rsr Ltf Rtf Ltr Rtr
    ReverbCloud cloud;
    cloud.prepare (rate, layout, layout);
    hear (cloud, layout.size(), { 6 }, 0.5);

    const auto gains = settle (cloud, layout.size(), 0.0f);
    const auto loudest = std::max_element (gains.begin(), gains.end()) - gains.begin();
    CHECK (loudest == 6);
    CHECK (gains[1] < gains[0]);  // front right quieter than front left
    CHECK (gains[7] < gains[6]);  // right rear quieter than left rear
    CHECK (gains[10] > gains[9]); // the height above it louder than the one across the room
    CHECK (gains[3] == 1.0f);     // the LFE is left alone (it carries no reverb)
    CHECK (std::abs (powerOf (gains, layout) - 11.0) < 1.0e-3);
}

TEST_CASE ("The reverb holds its place when the sound stops or the room is frozen", "[cloud]")
{
    ReverbCloud cloud;
    cloud.prepare (rate, layouts::stereo(), layouts::stereo());
    hear (cloud, 2, { 0 }, 0.5);
    const auto before = cloud.direction();

    hear (cloud, 2, {}, 2.0); // silence
    CHECK (cloud.direction() == before);

    hear (cloud, 2, { 1 }, 1.0, true); // panned right while frozen
    CHECK (cloud.direction() == before);

    hear (cloud, 2, { 1 }, 1.0);
    CHECK (cloud.direction()[0] > 0.4f);
}

TEST_CASE ("Changing the size glides without a step", "[cloud]")
{
    ReverbCloud cloud;
    cloud.prepare (rate, layouts::stereo(), layouts::stereo());
    hear (cloud, 2, { 0 }, 0.5);

    std::vector<float> right;
    std::vector<float> chunk (block);

    for (int b = 0; b < 100; ++b)
    {
        std::vector<float> left (block, 1.0f);
        std::fill (chunk.begin(), chunk.end(), 1.0f);
        float* wet[] { left.data(), chunk.data() };
        cloud.process (wet, 2, block, b < 10 ? 100.0f : 0.0f);
        right.insert (right.end(), chunk.begin(), chunk.end());
    }

    float largest = 0.0f;

    for (std::size_t n = 1; n < right.size(); ++n)
        largest = std::max (largest, std::abs (right[n] - right[n - 1]));

    CHECK (largest < 1.0e-3f);
    CHECK (right.back() < 0.2f); // and it got there
}

TEST_CASE ("Cloud size in the reverb: a sound on the left keeps its reverb on the left", "[cloud]")
{
    ImpulseResponseRequest r;
    r.sampleRate = rate;
    r.space = 3;
    r.decaySeconds = bundledSpaces[3].decaySeconds;
    const auto stereo = synthesiseImpulseResponse (r);

    auto sideRatio = [&] (float size)
    {
        ConvolutionReverb reverb;
        reverb.prepare (rate);
        reverb.submit (std::make_unique<ReverbKernel> (stereo, designLivingTail (r, stereo)));

        ReverbControls c;
        c.mix = 1.0f;
        c.cloudPercent = size;

        const auto source = testing::noise (static_cast<std::size_t> (rate), 8u);
        auto left = source;
        std::vector<float> right (left.size(), 0.0f);

        for (std::size_t pos = 0; pos < left.size(); pos += 512)
            reverb.process (left.data() + pos, right.data() + pos, std::min<std::size_t> (512, left.size() - pos), c);

        double l = 0.0, rr = 0.0;

        for (auto n = left.size() / 2; n < left.size(); ++n)
        {
            l += static_cast<double> (left[n]) * left[n];
            rr += static_cast<double> (right[n]) * right[n];
        }

        return 10.0 * std::log10 (l / rr);
    };

    const auto full = sideRatio (100.0f), gathered = sideRatio (0.0f);
    INFO ("left over right: " << full << " dB at full size, " << gathered << " dB gathered");
    CHECK (gathered > full + 12.0);
}

TEST_CASE ("Cloud size never allocates on the audio thread", "[cloud][realtime]")
{
    const auto layout = layouts::sevenPointOnePointFour();
    ReverbCloud cloud;
    cloud.prepare (rate, layout, layout);

    std::vector<std::vector<float>> channels (layout.size(), testing::noise (block, 2u));
    std::vector<float*> pointers;

    for (auto& ch : channels)
        pointers.push_back (ch.data());

    testing::AllocationCounter counter;

    for (int b = 0; b < 50; ++b)
    {
        cloud.listen (pointers.data(), layout.size(), block, false);
        cloud.process (pointers.data(), layout.size(), block, 20.0f);
    }

    CHECK (counter.count() == 0);
}

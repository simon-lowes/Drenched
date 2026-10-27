#include "TestHelpers.h"

#include <drenched/DirectConvolver.h>
#include <drenched/PartitionedConvolver.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <cmath>
#include <random>
#include <span>
#include <vector>

using drenched::DirectConvolver;
using drenched::PartitionedConvolver;

namespace
{
    /** Runs the convolver over the input in randomly sized host blocks, the way
        hosts actually call it, and returns output k. */
    std::vector<std::vector<float>> runInRandomBlocks (PartitionedConvolver& conv, const std::vector<float>& input, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_int_distribution<std::size_t> blockSize (1, 700);

        std::vector<std::vector<float>> out (conv.numOutputs(), std::vector<float> (input.size(), 0.0f));
        std::vector<float*> ptrs (conv.numOutputs());

        for (std::size_t pos = 0; pos < input.size();)
        {
            const auto n = std::min (blockSize (rng), input.size() - pos);

            for (std::size_t k = 0; k < ptrs.size(); ++k)
                ptrs[k] = out[k].data() + pos;

            conv.process (input.data() + pos, ptrs.data(), n);
            pos += n;
        }

        return out;
    }

    float peak (const std::vector<float>& v)
    {
        float m = 0.0f;
        for (auto s : v)
            m = std::max (m, std::abs (s));
        return m;
    }

    float maxDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        float m = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i)
            m = std::max (m, std::abs (a[i] - b[i]));
        return m;
    }
}

TEST_CASE ("Partitioned convolution nulls against direct convolution", "[partitioned]")
{
    // Lengths straddle every stage boundary: head only, first stage, second stage.
    const auto length = GENERATE (std::size_t (1), std::size_t (63), std::size_t (64), std::size_t (65), std::size_t (500),
                                  std::size_t (512), std::size_t (513), std::size_t (4097), std::size_t (9000));

    const auto ir = testing::noise (length, static_cast<unsigned> (length));
    const auto input = testing::noise (length * 2 + 3000, 99);

    DirectConvolver reference;
    reference.setImpulseResponse (ir);
    std::vector<float> expected (input.size());
    reference.process (input, expected);

    PartitionedConvolver conv ({ std::span<const float> (ir) });
    const auto actual = runInRandomBlocks (conv, input, 7)[0];

    INFO ("IR length " << length);
    REQUIRE (maxDifference (actual, expected) <= 1.0e-4f * std::max (1.0f, peak (expected)));
}

TEST_CASE ("Long, sparse responses null across the large stages", "[partitioned]")
{
    // A sparse response has a cheap exact reference, which lets the test reach
    // the 4096 and 32768 sample stages. 40000 and 300000 end just past a stage
    // boundary, where the last stage takes more partitions instead of growing;
    // a million (a long cathedral at 48 kHz, or a shorter room at 96) is far
    // past where the stages stop growing.
    const auto length = GENERATE (std::size_t (40000), std::size_t (150000), std::size_t (300000), std::size_t (1000000));
    INFO ("length " << length);
    std::vector<float> ir (length, 0.0f);
    std::mt19937 rng (3);
    std::uniform_int_distribution<std::size_t> where (0, length - 1);
    std::uniform_real_distribution<float> value (-1.0f, 1.0f);

    for (int i = 0; i < 60; ++i)
        ir[where (rng)] = value (rng);

    ir[0] = 1.0f;
    ir[length - 1] = 0.5f;

    const auto input = testing::noise (length + 40000, 11);
    std::vector<float> expected (input.size(), 0.0f);

    for (std::size_t k = 0; k < length; ++k)
        if (ir[k] != 0.0f)
            for (std::size_t n = k; n < input.size(); ++n)
                expected[n] += ir[k] * input[n - k];

    PartitionedConvolver conv ({ std::span<const float> (ir) });
    const auto actual = runInRandomBlocks (conv, input, 5)[0];

    REQUIRE (maxDifference (actual, expected) <= 1.0e-4f * peak (expected));
}

TEST_CASE ("The large stages work out each block during the next, with or without a latency", "[partitioned][latency]")
{
    // From slackFrom up a stage starts at tap 2B and spreads its transforms over
    // the following block. Two outputs, odd block sizes and a latency check that
    // the work queue for every output lands on the right samples.
    const auto latency = GENERATE (std::size_t (0), std::size_t (1024));
    const auto length = GENERATE (std::size_t (20000), std::size_t (90000));
    INFO ("latency " << latency << ", length " << length);

    std::mt19937 rng (17);
    std::uniform_int_distribution<std::size_t> where (0, length - 1);
    std::uniform_real_distribution<float> value (-1.0f, 1.0f);
    std::vector<float> left (length, 0.0f), right (length, 0.0f);

    for (int i = 0; i < 40; ++i)
    {
        left[where (rng)] = value (rng);
        right[where (rng)] = value (rng);
    }

    left[length - 1] = right[0] = 0.5f;

    const auto input = testing::noise (length + latency + 30000, 23);
    const auto reference = [&] (const std::vector<float>& ir)
    {
        std::vector<float> out (input.size(), 0.0f);

        for (std::size_t k = 0; k < length; ++k)
            if (ir[k] != 0.0f)
                for (std::size_t n = k + latency; n < input.size(); ++n)
                    out[n] += ir[k] * input[n - k - latency];

        return out;
    };

    PartitionedConvolver conv ({ std::span<const float> (left), std::span<const float> (right) }, latency);
    const auto actual = runInRandomBlocks (conv, input, 13);
    const auto expectedLeft = reference (left), expectedRight = reference (right);

    REQUIRE (maxDifference (actual[0], expectedLeft) <= 1.0e-4f * peak (expectedLeft));
    REQUIRE (maxDifference (actual[1], expectedRight) <= 1.0e-4f * peak (expectedRight));
}

TEST_CASE ("There is no latency: an impulse returns the response from sample zero", "[partitioned]")
{
    const auto ir = testing::noise (5000, 21);
    std::vector<float> impulse (6000, 0.0f);
    impulse[0] = 1.0f;

    PartitionedConvolver conv ({ std::span<const float> (ir) });
    const auto out = runInRandomBlocks (conv, impulse, 1)[0];

    for (std::size_t n = 0; n < ir.size(); ++n)
        REQUIRE (std::abs (out[n] - ir[n]) < 1.0e-5f);

    for (std::size_t n = ir.size(); n < out.size(); ++n)
        REQUIRE (std::abs (out[n]) < 1.0e-5f);
}

TEST_CASE ("Several responses can share one input", "[partitioned]")
{
    const auto a = testing::noise (3000, 1);
    const auto b = testing::noise (1200, 2); // shorter: zero padded internally
    const auto input = testing::noise (8000, 3);

    PartitionedConvolver shared ({ std::span<const float> (a), std::span<const float> (b) });
    const auto out = runInRandomBlocks (shared, input, 4);

    for (const auto* ir : { &a, &b })
    {
        DirectConvolver reference;
        reference.setImpulseResponse (*ir);
        std::vector<float> expected (input.size());
        reference.process (input, expected);

        const auto& actual = out[ir == &a ? 0 : 1];
        REQUIRE (maxDifference (actual, expected) <= 1.0e-4f * peak (expected));
    }
}

TEST_CASE ("Reset clears the history", "[partitioned]")
{
    const auto ir = testing::noise (2000, 8);
    const auto input = testing::noise (4000, 9);

    PartitionedConvolver conv ({ std::span<const float> (ir) });
    const auto first = runInRandomBlocks (conv, input, 2)[0];
    conv.reset();
    const auto second = runInRandomBlocks (conv, input, 3)[0];

    REQUIRE (maxDifference (first, second) < 1.0e-5f);
}

TEST_CASE ("Processing never allocates", "[partitioned][realtime]")
{
    const auto ir = testing::noise (70000, 10);
    const auto input = testing::noise (100000, 12);
    std::vector<float> out (input.size());

    PartitionedConvolver conv ({ std::span<const float> (ir) });
    float* outputs[] { out.data() };

    testing::AllocationCounter counter;

    for (std::size_t pos = 0; pos + 333 <= input.size(); pos += 333)
    {
        outputs[0] = out.data() + pos;
        conv.process (input.data() + pos, outputs, 333);
    }

    REQUIRE (counter.count() == 0);
}

TEST_CASE ("With a latency the output is the same response, exactly that many samples late", "[partitioned][latency]")
{
    const auto latency = GENERATE (std::size_t (64), std::size_t (512), std::size_t (1024), std::size_t (2048));
    const auto length = GENERATE (std::size_t (1), std::size_t (700), std::size_t (2048), std::size_t (2049), std::size_t (30000));

    const auto ir = testing::noise (length, static_cast<unsigned> (length + latency));
    const auto input = testing::noise (length + 3 * latency + 5000, 31);

    // The reference is the response with latency silent taps in front.
    std::vector<float> delayed (latency, 0.0f);
    delayed.insert (delayed.end(), ir.begin(), ir.end());
    DirectConvolver reference;
    reference.setImpulseResponse (delayed);
    std::vector<float> expected (input.size());
    reference.process (input, expected);

    PartitionedConvolver conv ({ std::span<const float> (ir), std::span<const float> (ir) }, latency);
    REQUIRE (conv.latency() == latency);
    REQUIRE (conv.numOutputs() == 2);

    const auto actual = runInRandomBlocks (conv, input, 11);

    INFO ("latency " << latency << ", IR length " << length);
    REQUIRE (maxDifference (actual[0], expected) <= 1.0e-4f * std::max (1.0f, peak (expected)));
    REQUIRE (actual[1] == actual[0]);
}

TEST_CASE ("A latency that is not a power of two from 64 is refused", "[partitioned][latency]")
{
    const std::vector<float> ir (100, 0.5f);
    CHECK_THROWS_AS (PartitionedConvolver ({ std::span<const float> (ir) }, 32), std::invalid_argument);
    CHECK_THROWS_AS (PartitionedConvolver ({ std::span<const float> (ir) }, 1000), std::invalid_argument);
    CHECK_NOTHROW (PartitionedConvolver ({ std::span<const float> (ir) }, 0));
}

TEST_CASE ("A latency runs the same response on less CPU and never allocates", "[partitioned][latency][realtime]")
{
    // Five seconds of stereo room at 48 kHz, as one input feeding two outputs.
    const auto left = testing::noise (240000, 41), right = testing::noise (240000, 42);
    const auto input = testing::noise (48000 * 4, 43);
    std::vector<float> a (input.size()), b (input.size());

    const auto timeIt = [&] (std::size_t latency)
    {
        PartitionedConvolver conv ({ std::span<const float> (left), std::span<const float> (right) }, latency);
        float* outputs[] { a.data(), b.data() };
        const auto start = std::chrono::steady_clock::now();
        testing::AllocationCounter counter;

        for (std::size_t pos = 0; pos + 256 <= input.size(); pos += 256)
        {
            outputs[0] = a.data() + pos;
            outputs[1] = b.data() + pos;
            conv.process (input.data() + pos, outputs, 256);
        }

        REQUIRE (counter.count() == 0);
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    };

    // Best of three, so a busy test machine doesn't decide it.
    auto zero = 1.0e9, low = 1.0e9;

    for (int i = 0; i < 3; ++i)
    {
        zero = std::min (zero, timeIt (0));
        low = std::min (low, timeIt (1024));
    }

    INFO ("zero latency " << zero << " s, 1024 samples " << low << " s for 4 s of audio");
    CHECK (low < zero * 0.8);
}

#include <drenched/DirectConvolver.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{
    // Textbook full convolution, the ground truth for every engine test.
    std::vector<float> naiveConvolve (const std::vector<float>& x, const std::vector<float>& h)
    {
        std::vector<float> y (x.size(), 0.0f);
        for (std::size_t n = 0; n < x.size(); ++n)
            for (std::size_t k = 0; k < h.size() && k <= n; ++k)
                y[n] += h[k] * x[n - k];
        return y;
    }

    std::vector<float> noise (std::size_t length, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> v (length);
        for (auto& s : v)
            s = dist (rng);
        return v;
    }
}

TEST_CASE ("An impulse returns the impulse response", "[convolver]")
{
    const std::vector<float> ir { 0.5f, -0.25f, 0.125f, 1.0f };
    drenched::DirectConvolver conv;
    conv.setImpulseResponse (ir);

    std::vector<float> input (8, 0.0f), output (8);
    input[0] = 1.0f;
    conv.process (input, output);

    for (std::size_t i = 0; i < ir.size(); ++i)
        REQUIRE_THAT (output[i], WithinAbs (ir[i], 1.0e-7));
    for (std::size_t i = ir.size(); i < output.size(); ++i)
        REQUIRE (output[i] == 0.0f);
}

TEST_CASE ("Block-wise processing matches one-shot convolution", "[convolver]")
{
    const auto ir = noise (97, 1);
    const auto input = noise (1000, 2);
    const auto expected = naiveConvolve (input, ir);

    drenched::DirectConvolver conv;
    conv.setImpulseResponse (ir);

    // Uneven block sizes catch history wrap-around bugs.
    std::vector<float> output (input.size());
    std::size_t pos = 0;
    for (std::size_t block : { 1u, 13u, 64u, 7u, 256u, 659u })
    {
        conv.process (std::span (input).subspan (pos, block), std::span (output).subspan (pos, block));
        pos += block;
    }
    REQUIRE (pos == input.size());

    for (std::size_t i = 0; i < output.size(); ++i)
        REQUIRE_THAT (output[i], WithinAbs (expected[i], 1.0e-4));
}

TEST_CASE ("Processing in place is supported", "[convolver]")
{
    const auto ir = noise (16, 3);
    auto buffer = noise (128, 4);
    const auto expected = naiveConvolve (buffer, ir);

    drenched::DirectConvolver conv;
    conv.setImpulseResponse (ir);
    conv.process (buffer, buffer);

    for (std::size_t i = 0; i < buffer.size(); ++i)
        REQUIRE_THAT (buffer[i], WithinAbs (expected[i], 1.0e-5));
}

TEST_CASE ("Reset clears the history", "[convolver]")
{
    drenched::DirectConvolver conv;
    conv.setImpulseResponse (std::vector<float> { 1.0f, 1.0f, 1.0f });

    std::vector<float> ones (3, 1.0f), out (3);
    conv.process (ones, out);
    conv.reset();

    std::vector<float> silence (3, 0.0f);
    conv.process (silence, out);
    for (auto s : out)
        REQUIRE (s == 0.0f);
}

TEST_CASE ("An empty impulse response outputs silence", "[convolver]")
{
    drenched::DirectConvolver conv;
    std::vector<float> input (4, 1.0f), output (4, 9.0f);
    conv.process (input, output);
    for (auto s : output)
        REQUIRE (s == 0.0f);
}

#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <functional>
#include <random>
#include <vector>

using namespace drenched;

/*  Hosts call plugins in all sorts of ways: blocks of any size (including none, and larger
    than they promised), audio before the plugin was prepared, the sample rate changed
    mid-session. The sound must never depend on how the audio was cut into blocks. */

namespace
{
    std::vector<float> noiseBurst (int length)
    {
        std::mt19937 rng (11u);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
        std::vector<float> x (static_cast<size_t> (length), 0.0f);

        for (int i = 0; i < length / 8; ++i)
            x[static_cast<size_t> (i)] = noise (rng);

        return x;
    }

    /** Plays `input` (on both channels) cut into blocks of the sizes `nextBlock` gives. */
    std::vector<float> play (DrenchedProcessor& p, const std::vector<float>& input, const std::function<int (int)>& nextBlock)
    {
        std::vector<float> out (input.size(), 0.0f);
        juce::MidiBuffer midi;
        const auto total = static_cast<int> (input.size());

        for (int pos = 0, index = 0; pos < total; ++index)
        {
            const auto n = std::min (nextBlock (index), total - pos);
            juce::AudioBuffer<float> buffer (2, n);

            for (int c = 0; c < 2; ++c)
                buffer.copyFrom (c, 0, input.data() + pos, n);

            p.processBlock (buffer, midi);

            for (int i = 0; i < n; ++i)
                out[static_cast<size_t> (pos + i)] = buffer.getSample (0, i);

            pos += n;
        }

        return out;
    }

    void prepare (DrenchedProcessor& p, double rate, int blockSize, bool lowCpu = false)
    {
        testing::onMessageThread ([&]
        {
            for (auto* each : p.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each); ranged != nullptr && ranged->paramID == ParamIDs::lowCpu)
                    ranged->setValueNotifyingHost (lowCpu ? 1.0f : 0.0f);

            p.prepareToPlay (rate, blockSize);
        });
    }

    double energy (const std::vector<float>& x, size_t from)
    {
        double e = 0.0;

        for (auto i = from; i < x.size(); ++i)
            e += static_cast<double> (x[i]) * x[i];

        return e;
    }
}

TEST_CASE ("The sound doesn't depend on how the host cuts the audio into blocks", "[plugin][host]")
{
    constexpr double rate = 48000.0;
    const auto input = noiseBurst (static_cast<int> (rate));
    const auto lowCpu = GENERATE (false, true);
    INFO ("Low CPU " << lowCpu);

    testing::Plugin reference;
    prepare (*reference, rate, 512, lowCpu);
    const auto expected = play (*reference, input, [] (int) { return 512; });

    // The room rings on after the burst, so the comparison hears the reverb.
    REQUIRE (energy (expected, input.size() / 4) > 1.0e-3);

    const auto check = [&] (const char* name, int promised, const std::function<int (int)>& nextBlock)
    {
        INFO (name);
        testing::Plugin p;
        prepare (*p, rate, promised, lowCpu);
        const auto got = play (*p, input, nextBlock);
        double worst = 0.0;

        for (size_t i = 0; i < got.size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (got[i] - expected[i])));

        CHECK (worst < 1.0e-5);
    };

    check ("one sample at a time", 512, [] (int) { return 1; });
    check ("odd sizes", 512, [] (int i) { return 1 + (i * 37) % 300; });
    check ("empty blocks between", 512, [] (int i) { return i % 2 == 0 ? 0 : 129; });
    check ("larger than promised", 64, [] (int i) { return i % 3 == 0 ? 4096 : 64; });
    check ("one block for everything", 512, [] (int) { return 48000; });
}

TEST_CASE ("Audio before the plugin is prepared passes through unharmed", "[plugin][host]")
{
    testing::Plugin p;
    const auto input = noiseBurst (4096);
    const auto out = play (*p, input, [] (int) { return 256; });

    for (const auto sample : out)
        REQUIRE (std::isfinite (sample));
}

TEST_CASE ("A new sample rate mid-session plays the same room", "[plugin][host]")
{
    testing::Plugin p;

    for (const auto rate : { 44100.0, 96000.0, 22050.0, 192000.0, 48000.0 })
    {
        INFO (rate);
        prepare (*p, rate, 256);
        testing::onMessageThread ([&] { p->releaseResources(); });
        prepare (*p, rate, 256);

        std::vector<float> click (static_cast<size_t> (rate), 0.0f);
        click[0] = 1.0f;
        const auto out = play (*p, click, [] (int) { return 256; });

        bool finite = true;

        for (const auto sample : out)
            finite = finite && std::isfinite (sample);

        CHECK (finite);
        CHECK (energy (out, static_cast<size_t> (rate * 0.05)) > 1.0e-6);
    }
}

TEST_CASE ("Bypassed, the dry sound plays untouched and in time", "[plugin][host]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    const auto lowCpu = GENERATE (false, true);
    INFO ("Low CPU " << lowCpu);

    testing::Plugin p;
    prepare (*p, rate, block, lowCpu);
    const auto latency = p->getLatencySamples();
    CHECK ((latency > 0) == lowCpu);

    // Output gain turned down: bypass ignores it.
    testing::onMessageThread ([&]
    {
        for (auto* each : p->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each); ranged != nullptr && ranged->paramID == ParamIDs::output)
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (-12.0f));
    });

    const auto input = noiseBurst (static_cast<int> (rate));
    std::vector<float> out (input.size(), 0.0f);
    juce::MidiBuffer midi;

    for (int pos = 0; pos + block <= static_cast<int> (input.size()); pos += block)
    {
        juce::AudioBuffer<float> buffer (2, block);

        for (int c = 0; c < 2; ++c)
            buffer.copyFrom (c, 0, input.data() + pos, block);

        p->processBlockBypassed (buffer, midi);

        for (int i = 0; i < block; ++i)
            out[static_cast<size_t> (pos + i)] = buffer.getSample (0, i);
    }

    double worst = 0.0;

    for (size_t i = static_cast<size_t> (latency); i < out.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (out[i] - input[i - static_cast<size_t> (latency)])));

    CHECK (worst < 1.0e-5);
}

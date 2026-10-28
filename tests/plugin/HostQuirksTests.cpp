#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <utility>
#include <vector>

using namespace drenched;

/*  Hosts call plugins in all sorts of ways: blocks of any size (including none, and larger
    than they promised), audio before the plugin was prepared, the sample rate (8 kHz to
    384 kHz) or speaker layout changed mid-session. The sound must never depend on how the
    audio was cut into blocks. */

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

    for (const auto rate : { 44100.0, 96000.0, 22050.0, 192000.0, 8000.0, 384000.0, 48000.0 })
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

TEST_CASE ("Bouncing offline, a change to the room lands at the same place every time", "[plugin][host]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    // Silence, then the decay is cut short, then a click rings in the new room.
    const auto bounce = [&] (bool changeDecay)
    {
        testing::Plugin p;
        testing::onMessageThread ([&] { p->setNonRealtime (true); });
        prepare (*p, rate, block);

        std::vector<float> out;
        juce::MidiBuffer midi;

        for (int pos = 0; pos < static_cast<int> (rate * 3.0); pos += block)
        {
            if (changeDecay && pos == block * 20)
                for (auto* each : p->getParameters())
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each); ranged != nullptr)
                        for (const auto* id : ParamIDs::decay)
                            if (ranged->paramID == id)
                                ranged->setValueNotifyingHost (ranged->convertTo0to1 (0.3f));

            juce::AudioBuffer<float> buffer (2, block);
            buffer.clear();

            if (pos == block * 40)
                buffer.setSample (0, 0, 1.0f);

            p->processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        }

        return out;
    };

    const auto unchanged = bounce (false);
    const auto changed = bounce (true);
    const auto again = bounce (true);

    // The room heard is the new, short one: its tail is gone a second after the click.
    const auto late = static_cast<size_t> (block * 40 + rate);
    CHECK (energy (changed, late) < 0.01 * energy (unchanged, late));
    CHECK (changed == again);
}

TEST_CASE ("Every speaker layout a host can choose plays the room on every speaker", "[plugin][host][layouts]")
{
    // One instance, as a host that lets the person change the track's layout mid-session.
    using Set = juce::AudioChannelSet;
    testing::Plugin p;
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    const std::vector<std::pair<Set, Set>> layouts {
        { Set::mono(), Set::mono() },
        { Set::mono(), Set::stereo() },
        { Set::stereo(), Set::stereo() },
        { Set::stereo(), Set::createLCR() },
        { Set::stereo(), Set::quadraphonic() },
        { Set::mono(), Set::create5point1() },
        { Set::stereo(), Set::create5point1() },
        { Set::create5point1(), Set::create5point1() },
        { Set::stereo(), Set::create7point1() },
        { Set::create7point1point4(), Set::create7point1point4() },
        { Set::stereo(), Set::create9point1point6() },
        { Set::stereo(), Set::stereo() },
    };

    for (const auto& [in, out] : layouts)
    {
        INFO (in.getDescription() << " in, " << out.getDescription() << " out");

        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        REQUIRE (testing::onMessageThread ([&] { return p->setBusesLayout (layout); }));
        prepare (*p, rate, blockSize);

        const auto channels = std::max (in.size(), out.size());
        const auto input = noiseBurst (static_cast<int> (rate));
        std::vector<double> tail (static_cast<size_t> (out.size()), 0.0);
        auto finite = true;
        juce::MidiBuffer midi;

        for (int pos = 0; pos < static_cast<int> (input.size()); pos += blockSize)
        {
            const auto n = std::min (blockSize, static_cast<int> (input.size()) - pos);
            juce::AudioBuffer<float> buffer (channels, n);
            buffer.clear();

            for (int c = 0; c < in.size(); ++c)
                buffer.copyFrom (c, 0, input.data() + pos, n);

            p->processBlock (buffer, midi);

            for (int c = 0; c < out.size(); ++c)
            {
                for (int i = 0; i < n; ++i)
                {
                    const auto x = buffer.getSample (c, i);
                    finite = finite && std::isfinite (x);

                    if (pos >= static_cast<int> (rate / 4)) // after the burst: the room
                        tail[static_cast<size_t> (c)] += static_cast<double> (x) * x;
                }
            }
        }

        CHECK (finite);

        for (int c = 0; c < out.size(); ++c)
        {
            const auto type = out.getTypeOfChannel (c);

            if (type == Set::LFE || type == Set::LFE2)
                continue;

            INFO (Set::getAbbreviatedChannelTypeName (type));
            CHECK (tail[static_cast<size_t> (c)] > 1.0e-4);
        }
    }

    // Layouts it can't play are refused, so the host offers the next best.
    for (const auto& [in, out] : std::vector<std::pair<Set, Set>> { { Set::create5point1(), Set::stereo() }, { Set::disabled(), Set::stereo() } })
    {
        INFO (in.getDescription() << " in, " << out.getDescription() << " out");
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        CHECK_FALSE (testing::onMessageThread ([&] { return p->checkBusesLayoutSupported (layout); }));
    }
}

TEST_CASE ("A host tempo that can't be right leaves synced predelay playing", "[plugin][host]")
{
    // Synced predelay snaps to a 64th note at the host's tempo. A host reporting no tempo,
    // or one that can't be, plays the predelay as set rather than a broken one.
    struct Head final : juce::AudioPlayHead
    {
        double bpm = 120.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            return info;
        }
    };

    constexpr double rate = 48000.0;
    const auto input = noiseBurst (24000);
    const auto bpm = GENERATE (std::numeric_limits<double>::infinity(), 1.0e300, std::numeric_limits<double>::quiet_NaN(), -120.0, 0.0);
    INFO ("bpm " << bpm);

    Head head;
    testing::Plugin p;
    p->setPlayHead (&head);
    prepare (*p, rate, 512);

    testing::onMessageThread ([&]
    {
        for (const auto* id : { ParamIDs::predelaySync, ParamIDs::predelay })
        {
            auto* parameter = p->getState().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (id == ParamIDs::predelay ? 40.0f : 1.0f));
        }
    });

    head.bpm = bpm;
    const auto got = play (*p, input, [] (int) { return 512; });

    for (auto x : got)
        REQUIRE (std::isfinite (x));

    REQUIRE (energy (got, got.size() / 2) > 1.0e-6);
}

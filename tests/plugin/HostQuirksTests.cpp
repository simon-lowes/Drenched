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
    384 kHz, or one no audio runs at) or speaker layout changed mid-session. The sound must
    never depend on how the audio was cut into blocks. */

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

TEST_CASE ("A block with fewer or more channels than the buses plays without harm", "[plugin][host]")
{
    // Hosts hand over fewer channels than the buses have when a bus is switched off or a
    // call only passes on automation, and some hand over spare ones. Playing, bypassed and
    // capturing a space, only the channels that are there are touched.
    for (const auto channels : { 0, 1, 3, 13 })
    {
        INFO (channels << " channels for stereo buses");
        testing::Plugin p;
        prepare (*p, 48000.0, 512);
        juce::MidiBuffer midi;
        auto allFinite = true;

        const auto block = [&] (bool bypassed, bool impulses)
        {
            juce::AudioBuffer<float> buffer (channels, 512);
            buffer.clear();

            if (impulses)
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < 512; i += 50)
                        buffer.setSample (c, i, 0.5f);

            if (bypassed)
                p->processBlockBypassed (buffer, midi);
            else
                p->processBlock (buffer, midi);

            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < 512; ++i)
                    allFinite = allFinite && std::isfinite (buffer.getSample (c, i));
        };

        for (int b = 0; b < 40; ++b)
            block (b % 4 == 3, true);

        testing::onMessageThread ([&]
        {
            auto options = std::make_unique<juce::DynamicObject>();
            options->setProperty ("sweepSeconds", 2.0);
            options->setProperty ("tailSeconds", 1.0);
            p->startCapture (juce::var (options.release()));
        });

        for (int b = 0; b < 400; ++b)
            block (false, false);

        CHECK (allFinite);
    }
}

TEST_CASE ("A sample rate no audio runs at leaves the plugin playing", "[plugin][host]")
{
    // Hosts have prepared plugins at 0 Hz before their audio starts, and with a rate they
    // failed to read. Preparing at 0 Hz used to hang the host while the room was built.
    for (const auto rate : { 0.0, -48000.0, 1.0, 100.0, 1.0e7, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity() })
    {
        INFO (rate);
        testing::Plugin p;
        prepare (*p, rate, 0);

        std::vector<float> click (48000, 0.0f);
        click[0] = 1.0f;
        const auto out = play (*p, click, [] (int) { return 256; });

        bool finite = true;

        for (const auto sample : out)
            finite = finite && std::isfinite (sample);

        CHECK (finite);
        CHECK (energy (out, 2400) > 1.0e-6);
        CHECK (p->getLatencySamples() >= 0);

        // The host's next prepare, at a real rate, plays as usual.
        prepare (*p, 48000.0, 256);
        const auto after = play (*p, click, [] (int) { return 256; });
        CHECK (energy (after, 2400) > 1.0e-6);
    }
}

TEST_CASE ("Hosts are told a tail long enough for every room to die away", "[plugin][host]")
{
    // Hosts stop calling a plugin, or end a bounce, once its tail has passed after the
    // input stops. A tail shorter than the room's would cut the reverb off.
    struct Room { const char* space; std::vector<std::pair<const char*, float>> values; };
    std::vector<std::pair<const char*, float>> longest { { ParamIDs::living, 100.0f }, { ParamIDs::size, 150.0f },
                                                         { ParamIDs::predelay, 250.0f }, { ParamIDs::directDelay, 50.0f } };

    for (const auto* id : ParamIDs::decay)
        longest.push_back ({ id, 20.0f });

    const std::vector<Room> rooms { { nullptr, {} }, { nullptr, { { ParamIDs::living, 100.0f }, { ParamIDs::predelay, 250.0f } } },
                                    { "modelled-hall", {} }, { "modelled-hall", longest } };

    for (const auto& room : rooms)
    {
        testing::Plugin p;
        p->setNonRealtime (true);
        prepare (*p, 48000.0, 512);

        testing::onMessageThread ([&]
        {
            if (room.space != nullptr)
            {
                p->getUiState().setProperty (UiIDs::spaceId, room.space, nullptr);
                p->storeScene (8, room.space);
                p->recallScene (8, true);
            }

            for (const auto& [id, value] : room.values)
            {
                auto* param = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (id));
                param->setValueNotifyingHost (param->convertTo0to1 (value));
            }
        });

        play (*p, std::vector<float> (48000, 0.0f), [] (int) { return 512; });

        const auto told = p->getTailLengthSeconds();
        INFO ((room.space != nullptr ? room.space : "new instance") << " with " << room.values.size() << " changes, tail " << told << " s");
        REQUIRE (std::isfinite (told));

        std::vector<float> click (static_cast<size_t> ((told + 2.0) * 48000.0) + static_cast<size_t> (p->getLatencySamples()), 0.0f);
        click[0] = 1.0f;
        const auto out = play (*p, click, [] (int) { return 512; });

        float peak = 0.0f;
        size_t last = 0;

        for (const auto sample : out)
            peak = std::max (peak, std::abs (sample));

        // The last sample within 80 dB of the loudest.
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > peak * 1.0e-4f)
                last = i;

        CHECK (static_cast<double> (last) <= told * 48000.0 + p->getLatencySamples());
    }

    testing::Plugin p;
    prepare (*p, 48000.0, 512);
    dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::freeze))->setValueNotifyingHost (1.0f);
    play (*p, std::vector<float> (4096, 0.0f), [] (int) { return 512; });
    CHECK (std::isinf (p->getTailLengthSeconds()));
}

#include "PluginTestHelpers.h"

#include <TestHelpers.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <thread>
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

    void setParameter (DrenchedProcessor& p, const char* id, float value)
    {
        testing::onMessageThread ([&]
        {
            for (auto* each : p.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each); ranged != nullptr && ranged->paramID == id)
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
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
    // The room at each rate is heard as the 48 kHz one is: the same decay (a kernel built
    // for the wrong rate rings twice or half as long) and the same pre-delay (one counted
    // in samples rather than milliseconds moves). Fully wet, so the onset is the room's.
    testing::Plugin p;
    setParameter (*p, ParamIDs::mix, 100.0f);

    struct Room
    {
        double rt60 = 0.0, onsetMs = 0.0;
    };

    const auto hear = [&p] (double rate)
    {
        prepare (*p, rate, 256);
        testing::onMessageThread ([&] { p->releaseResources(); });
        prepare (*p, rate, 256);

        // Long enough for the default room's 1 kHz decay to fall well past the T20 range.
        std::vector<float> click (static_cast<size_t> (2.5 * rate), 0.0f);
        click[0] = 1.0f;
        const auto out = play (*p, click, [] (int) { return 256; });

        bool finite = true;
        float peak = 0.0f;

        for (const auto sample : out)
        {
            finite = finite && std::isfinite (sample);
            peak = std::max (peak, std::abs (sample));
        }

        REQUIRE (finite);
        REQUIRE (peak > 1.0e-3f);

        // The onset: where the room's first arrival comes within 12 dB of the peak. A lower
        // threshold catches the ringing of that band-limited arrival instead, which at 8 kHz
        // (a millisecond is 8 samples) starts a whole millisecond early at about -24 dB.
        size_t onset = 0;

        while (std::abs (out[onset]) < 0.25f * peak)
            ++onset;

        return Room { testing::measureRt60 (out, rate, 1000.0), 1000.0 * static_cast<double> (onset) / rate };
    };

    const auto reference = hear (48000.0);
    INFO ("at 48 kHz: T20 " << reference.rt60 << " s, onset " << reference.onsetMs << " ms");
    REQUIRE (reference.rt60 > 1.0);
    REQUIRE (reference.onsetMs > 10.0); // the default pre-delay, 18 ms

    for (const auto rate : { 44100.0, 96000.0, 22050.0, 192000.0, 8000.0, 384000.0, 48000.0 })
    {
        const auto room = hear (rate);
        INFO ("at " << rate << " Hz: T20 " << room.rt60 << " s, onset " << room.onsetMs << " ms");
        CHECK (std::abs (room.rt60 / reference.rt60 - 1.0) < 0.1);
        CHECK (std::abs (room.onsetMs - reference.onsetMs) < 0.5);
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

TEST_CASE ("A frozen tail reaches a CLAP host as endless, not as none", "[plugin][host]")
{
    // The CLAP wrapper turns the tail into samples with juce::roundToIntAccurate, which
    // gives 0 for infinity. CLAP's endless is INT32_MAX samples or more.
    testing::Plugin p;
    p->is_clap = true;

    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        p->setRateAndBufferSizeDetails (rate, 512); // as the CLAP wrapper does before preparing
        prepare (*p, rate, 512);
        dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::freeze))->setValueNotifyingHost (1.0f);
        play (*p, std::vector<float> (4096, 0.0f), [] (int) { return 512; });

        const auto samples = static_cast<std::uint32_t> (juce::roundToIntAccurate (rate * p->getTailLengthSeconds()));
        INFO (rate);
        CHECK (samples >= static_cast<std::uint32_t> (std::numeric_limits<std::int32_t>::max()));
    }
}

TEST_CASE ("A soloed EQ band is heard alone while the window shows it, and never in a bounce", "[plugin][host]")
{
    // Solo mutes the dry sound and keeps one band of the reverberation, for listening while
    // a band is set. It is a thing of the window, not the session: a bounce ignores it, so a
    // mix rendered after the window is closed never comes out with the dry sound missing.
    constexpr int block = 512;
    std::vector<float> click (static_cast<size_t> (block * 40), 0.0f);
    const auto at = static_cast<size_t> (block * 4);
    click[at] = 1.0f;

    const auto dryClick = [&] (const std::vector<float>& out) { return out[at] == 1.0f && energy (out, at + 1) < 1.0e-9; };

    testing::Plugin live;
    prepare (*live, 48000.0, block);
    setParameter (*live, ParamIDs::mix, 0.0f);
    REQUIRE (dryClick (play (*live, click, [] (int) { return block; })));

    live->setEqSolo (2);
    const auto soloed = play (*live, click, [] (int) { return block; });
    CHECK (soloed[at] < 0.5f);
    CHECK (energy (soloed, at + 1) > 1.0e-6);

    // Solo off: the dry is back (fresh prepared, so the soloed click's tail is gone).
    live->setEqSolo (WetEqualiser::noSolo);
    prepare (*live, 48000.0, block);
    CHECK (dryClick (play (*live, click, [] (int) { return block; })));

    testing::Plugin bounce;
    bounce->setNonRealtime (true);
    prepare (*bounce, 48000.0, block);
    setParameter (*bounce, ParamIDs::mix, 0.0f);
    bounce->setEqSolo (2);
    CHECK (dryClick (play (*bounce, click, [] (int) { return block; })));
}

TEST_CASE ("Bypassed, a soloed EQ band can't mute the dry sound", "[plugin][host]")
{
    // A solo mutes the dry and plays one band of the reverberation; bypass (and the dry-only
    // sound after the trial) must still be the input as it came in.
    constexpr int block = 512;
    std::vector<float> click (static_cast<size_t> (block * 40), 0.0f);
    const auto at = static_cast<size_t> (block * 4);
    click[at] = 1.0f;

    testing::Plugin p;
    prepare (*p, 48000.0, block);
    setParameter (*p, ParamIDs::mix, 50.0f);
    p->setEqSolo (0);

    std::vector<float> out (click.size(), 0.0f);
    juce::MidiBuffer midi;

    for (size_t pos = 0; pos + block <= click.size(); pos += block)
    {
        juce::AudioBuffer<float> buffer (2, block);

        for (int c = 0; c < 2; ++c)
            buffer.copyFrom (c, 0, click.data() + pos, block);

        p->processBlockBypassed (buffer, midi);

        for (int i = 0; i < block; ++i)
            out[pos + static_cast<size_t> (i)] = buffer.getSample (0, i);
    }

    CHECK (out[at] == 1.0f);
    CHECK (energy (out, at + 1) < 1.0e-9);
}

TEST_CASE ("A session the host loads on a thread of its own plays in a bounce started at once", "[plugin][host]")
{
    // A host can load a project or preset on a worker thread and bounce straight away,
    // holding the message thread meanwhile, so no timer tick comes between the two. The
    // bounce must play the session loaded, not the settings before it.
    constexpr int block = 512;
    testing::Plugin p;
    p->setNonRealtime (true);
    prepare (*p, 48000.0, block);

    auto* mix = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::mix));
    mix->setValueNotifyingHost (mix->convertTo0to1 (0.0f));
    juce::MemoryBlock dry;
    p->getStateInformation (dry);
    mix->setValueNotifyingHost (mix->convertTo0to1 (100.0f));

    std::thread host ([&] { p->setStateInformation (dry.getData(), static_cast<int> (dry.getSize())); });
    host.join();

    std::vector<float> click (static_cast<size_t> (block * 40), 0.0f);
    const auto at = static_cast<size_t> (block * 4);
    click[at] = 1.0f;
    const auto out = play (*p, click, [] (int) { return block; });

    CHECK (p->getState().getRawParameterValue (ParamIDs::mix)->load() == 0.0f);
    CHECK (out[at] == 1.0f);
    CHECK (energy (out, at + 1) < 1.0e-9);
}

TEST_CASE ("A bounce on a thread of its own waits only once for a session the host loaded", "[plugin][host]")
{
    // Bouncing on a thread of its own, the plugin waits for the message thread to apply a
    // session just loaded. A host may hold that thread for the whole bounce (as the tests
    // do: their message loop never runs), so the wait gives up, and only once.
    constexpr int block = 512;
    testing::Plugin p;
    p->setNonRealtime (true);
    prepare (*p, 48000.0, block);

    auto* mix = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::mix));
    mix->setValueNotifyingHost (mix->convertTo0to1 (0.0f));
    juce::MemoryBlock dry;
    p->getStateInformation (dry);
    mix->setValueNotifyingHost (mix->convertTo0to1 (100.0f));

    const auto start = std::chrono::steady_clock::now();

    std::thread host ([&]
    {
        p->setStateInformation (dry.getData(), static_cast<int> (dry.getSize()));
        play (*p, std::vector<float> (static_cast<size_t> (block * 10), 0.0f), [] (int) { return 512; });
    });

    host.join();
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    INFO (seconds << " s for 10 blocks");
    CHECK (seconds < 8.0);

    // The session still lands, at the timer's next tick.
    ((juce::Timer&) *p).timerCallback();
    CHECK (p->getState().getRawParameterValue (ParamIDs::mix)->load() == 0.0f);
}

TEST_CASE ("Changes outside the parameters mark the host's project as changed", "[plugin][host][session]")
{
    // Hosts learn of parameter changes by themselves, but not of scenes, speaker trims, the
    // space, locks, view or skin: unless told, they don't ask to save before closing. They
    // mustn't be told of what isn't the person's change: the window's size, a save, a
    // session loading, a scene the scene parameter's automation recalls.
    struct Host final : juce::AudioProcessorListener
    {
        int marks = 0;

        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}

        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            if (details.nonParameterStateChanged)
                ++marks;
        }
    };

    testing::Plugin p;
    prepare (*p, 48000.0, 512);
    Host host;
    p->addListener (&host);

    // How many times the host was told at the next timer tick.
    const auto marks = [&]
    {
        ((juce::Timer&) *p).timerCallback();
        return std::exchange (host.marks, 0);
    };

    auto ui = p->getUiState();
    marks();
    CHECK (marks() == 0);

    auto* size = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::size));
    size->setValueNotifyingHost (0.25f);
    CHECK (marks() == 0);

    CHECK (p->storeScene (1, "Verse") == 1);
    CHECK (marks() == 1);
    size->setValueNotifyingHost (0.75f);
    p->storeScene (1, {}); // over the same scene: a property or more, one mark
    CHECK (marks() == 1);
    p->renameScene (1, "Chorus");
    CHECK (marks() == 1);
    p->deleteScene (1);
    CHECK (marks() == 1);

    ui.setProperty (UiIDs::speakers, "-6 0;0 2.5", nullptr);
    CHECK (marks() == 1);
    ui.setProperty (UiIDs::spaceId, bundledSpaces.back().id, nullptr);
    CHECK (marks() == 1);
    ui.setProperty (UiIDs::locks, "decay", nullptr);
    ui.setProperty (UiIDs::view, "graph", nullptr);
    CHECK (marks() == 1);

    ui.setProperty (UiIDs::width, 900, nullptr);
    CHECK (marks() == 0);
    ui.setProperty (UiIDs::irEvent, 7, nullptr);
    CHECK (marks() == 0);

    juce::MemoryBlock saved;
    p->getStateInformation (saved);
    CHECK (marks() == 0);

    std::thread saver ([&] { juce::MemoryBlock copy; p->getStateInformation (copy); });
    saver.join();
    CHECK (marks() == 0);

    // A change the session loaded replaces isn't one either.
    ui.setProperty (UiIDs::skin, "night", nullptr);
    p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (marks() == 0);

    std::thread loader ([&] { p->setStateInformation (saved.getData(), static_cast<int> (saved.getSize())); });
    loader.join();
    CHECK (marks() == 0);

    // A scene recalled by automation plays back; one recalled by the person is a change.
    ui = p->getUiState();
    ui.setProperty (UiIDs::spaceId, bundledSpaces.front().id, nullptr);
    p->storeScene (2, "Bridge");
    ui.setProperty (UiIDs::spaceId, bundledSpaces.back().id, nullptr);
    CHECK (marks() == 1);

    auto* scene = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::scene));
    scene->setValueNotifyingHost (scene->convertTo0to1 (2.0f));
    CHECK (marks() == 0);
    CHECK (p->chosenSpaceId() == bundledSpaces.front().id);

    ui.setProperty (UiIDs::spaceId, bundledSpaces.back().id, nullptr);
    marks();
    p->recallScene (2);
    CHECK (marks() == 1);

    p->removeListener (&host);
}

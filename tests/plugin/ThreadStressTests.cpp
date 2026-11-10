#include "PluginTestHelpers.h"

#include "Library.h"

#include <drenched/Spaces.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

using namespace drenched;

/*  The host plays audio on its own thread while the interface and the host change
    everything else: spaces, settings, scenes, sessions. This runs both at once, as fast
    as they go, for a fixed number of changes, so every machine runs the same sequence.
    Run under ThreadSanitizer (DRENCHED_SANITIZE=thread, as CI does) it also finds data
    races between them. Afterwards each test checks the plugin came through whole: what
    it played stayed in bounds, the room it plays is the one asked for, and it plays a
    session as a new instance does. */

namespace
{
    void set (DrenchedProcessor& p, const juce::String& id, float normalised)
    {
        if (auto* parameter = p.getState().getParameter (id))
            parameter->setValueNotifyingHost (normalised);
    }

    /** What the processor's timer does: follows the scene parameter, rebuilds the response.
        Timer is a private base, which only a C-style cast may reach. */
    void tick (DrenchedProcessor& p)
    {
        ((juce::Timer&) p).timerCallback();
    }

    /** Runs step (0, 1, 2 ...) `count` times. The time limit only guards against a hang,
        well inside CTest's timeout even under sanitizers: a run it cuts short says so,
        and still checks what it did. */
    template <typename Step>
    void runSteps (int count, Step&& step)
    {
        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds (60);
        auto done = 0;

        while (done < count && std::chrono::steady_clock::now() < giveUp)
            step (done++);

        if (done < count)
            WARN ("Stopped after " << done << " of " << count << " steps, at the time limit");
    }

    /** What the audio threads played: whether every sample was a number, the loudest, and
        how many blocks. */
    struct Played
    {
        std::atomic<bool> finite { true };
        std::atomic<float> peak { 0.0f };
        std::atomic<int> blocks { 0 };

        void note (const juce::AudioBuffer<float>& buffer)
        {
            auto most = 0.0f;

            for (int c = 0; c < buffer.getNumChannels(); ++c)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    if (const auto x = buffer.getSample (c, i); std::isfinite (x))
                        most = std::max (most, std::abs (x));
                    else
                        finite.store (false);
                }
            }

            for (auto was = peak.load(); most > was && ! peak.compare_exchange_weak (was, most);)
            {}

            blocks.fetch_add (1);
        }
    };

    /** Noise at -6 dBFS peak, the input every audio thread here plays. With every control
        anywhere in its range the reverb can't make that more than about 46 dB louder; a
        sum gone wrong between threads (garbage, rather than NaN) is far louder still. */
    constexpr float loudestOutput = 100.0f;

    /** Prepares p at 48 kHz, as a host restarting audio does, then plays it an impulse and
        a second of silence: the room its session asks for, heard from scratch. */
    std::vector<float> impulseThrough (DrenchedProcessor& p)
    {
        constexpr int block = 512;
        testing::onMessageThread ([&] { p.prepareToPlay (48000.0, block); });
        REQUIRE (testing::builderIdle (p));
        CHECK (p.roomIsCurrent());

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        std::vector<float> out;

        for (int b = 0; b < 48000 / block; ++b)
        {
            buffer.clear();

            if (b == 0)
            {
                buffer.setSample (0, 0, 0.5f);
                buffer.setSample (1, 0, 0.5f);
            }

            p.processBlock (buffer, midi);

            for (int i = 0; i < block; ++i)
            {
                out.push_back (buffer.getSample (0, i));
                out.push_back (buffer.getSample (1, i));
            }
        }

        return out;
    }

    /** Whether p, given `session`, plays it as a new instance given the same session does. */
    void playsAsNew (DrenchedProcessor& p, const juce::MemoryBlock& session)
    {
        testing::onMessageThread ([&] { p.setStateInformation (session.getData(), static_cast<int> (session.getSize())); });
        const auto heard = impulseThrough (p);

        testing::Plugin fresh;
        testing::onMessageThread ([&] { fresh->setStateInformation (session.getData(), static_cast<int> (session.getSize())); });
        const auto expected = impulseThrough (*fresh);

        REQUIRE (heard.size() == expected.size());
        auto difference = 0.0f, loudest = 0.0f;

        for (size_t i = 0; i < heard.size(); ++i)
        {
            difference = std::max (difference, std::abs (heard[i] - expected[i]));
            loudest = std::max (loudest, std::abs (expected[i]));
        }

        INFO ("loudest " << loudest << ", differs by up to " << difference);
        CHECK (loudest > 0.0f);
        CHECK (difference <= 1.0e-5f * loudest);
    }

    std::vector<juce::String> allSpaces()
    {
        std::vector<juce::String> spaces;

        for (const auto& space : bundledSpaces)
            spaces.push_back (space.id);

        for (const auto& space : library::spaces())
            spaces.push_back (space.id);

        return spaces;
    }

    std::vector<juce::String> librarySpaces()
    {
        std::vector<juce::String> spaces;

        for (const auto& space : library::spaces())
            spaces.push_back (space.id);

        return spaces;
    }

    /** Every parameter but Low CPU, a latency change, which hosts restart for. */
    std::vector<juce::String> parameterIds (DrenchedProcessor& p)
    {
        std::vector<juce::String> ids;

        for (auto* parameter : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (ranged->paramID != ParamIDs::lowCpu)
                    ids.push_back (ranged->paramID);

        return ids;
    }
}

TEST_CASE ("Playing while everything else changes stays finite", "[plugin][stress]")
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 128;

    testing::Plugin plugin;
    auto& p = *plugin;
    juce::MemoryBlock initial;

    testing::onMessageThread ([&]
    {
        p.prepareToPlay (rate, blockSize);
        p.getStateInformation (initial);
    });

    std::atomic<bool> running { true };
    Played played;

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::mt19937 rng (1u);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        while (running.load())
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (c, i, noise (rng));

            p.processBlock (buffer, midi);
            played.note (buffer);
        }
    });

    testing::onMessageThread ([&]
    {
        const auto spaces = allSpaces();
        const auto ids = parameterIds (p);

        std::mt19937 rng (2u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        const auto pick = [&rng] (const auto& list) { return list[rng() % list.size()]; };

        juce::MemoryBlock session;
        p.getStateInformation (session);

        runSteps (4000, [&] (int step)
        {
            switch (step % 8)
            {
                case 0: p.getUiState().setProperty (UiIDs::spaceId, pick (spaces), nullptr); break;
                case 1: for (int k = 0; k < 8; ++k) set (p, pick (ids), unit (rng)); break;
                case 2: p.storeScene (1 + static_cast<int> (rng() % 4), {}); break;
                case 3: p.recallScene (1 + static_cast<int> (rng() % 4)); break;
                case 4: p.getStateInformation (session); break;
                case 5: if (step % 40 == 5) p.setStateInformation (session.getData(), static_cast<int> (session.getSize())); break;
                case 6: p.takePeaks(); p.takeSpeakerPeaks(); p.wetSpectrum(); p.impulseResponseInfo(); p.bugReportDetails(); break;
                default: break;
            }

            tick (p);
        });
    });

    running.store (false);
    audio.join();

    INFO (played.blocks.load() << " blocks played, the loudest sample " << played.peak.load());
    CHECK (played.blocks.load() > 0);
    CHECK (played.finite.load());
    CHECK (played.peak.load() < loudestOutput);

    // Nothing left behind: the session it started with plays as it would in a new instance.
    playsAsNew (p, initial);
}

TEST_CASE ("Many instances play at once while the host opens and closes more", "[plugin][stress]")
{
    // A session with reverb on several tracks: each instance has its own audio thread
    // (as hosts that spread tracks across cores do) and sample rate, they share the
    // space library, and the host opens and closes instances while they play.
    constexpr int blockSize = 128;
    const std::array<double, 4> rates { 44100.0, 48000.0, 88200.0, 96000.0 };

    std::vector<std::unique_ptr<testing::Plugin>> playing;

    for (const auto rate : rates)
    {
        auto& plugin = playing.emplace_back (std::make_unique<testing::Plugin>());
        testing::onMessageThread ([&] { (*plugin)->prepareToPlay (rate, blockSize); });
    }

    std::atomic<bool> running { true };
    Played played;
    std::vector<std::thread> audio;

    for (size_t n = 0; n < playing.size(); ++n)
    {
        audio.emplace_back ([&, n]
        {
            auto& p = **playing[n];
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            std::mt19937 rng (static_cast<unsigned> (n + 1));
            std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

            while (running.load())
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (c, i, noise (rng));

                p.processBlock (buffer, midi);
                played.note (buffer);
            }
        });
    }

    testing::onMessageThread ([&]
    {
        const auto spaces = allSpaces();
        std::mt19937 rng (3u);

        runSteps (400, [&] (int step)
        {
            auto& p = **playing[static_cast<size_t> (step) % playing.size()];
            p.getUiState().setProperty (UiIDs::spaceId, spaces[rng() % spaces.size()], nullptr);
            set (p, ParamIDs::size, static_cast<float> (rng() % 100) / 100.0f);
            tick (p);

            // Opening and closing other instances, which load and free their own spaces.
            if (step % 50 == 0)
            {
                testing::Plugin extra;
                extra->prepareToPlay (rates[rng() % rates.size()], blockSize);
                extra->getUiState().setProperty (UiIDs::spaceId, spaces[rng() % spaces.size()], nullptr);
                tick (*extra);
            }
        });
    });

    running.store (false);

    for (auto& thread : audio)
        thread.join();

    INFO (played.blocks.load() << " blocks played, the loudest sample " << played.peak.load());
    CHECK (played.blocks.load() > static_cast<int> (playing.size()));
    CHECK (played.finite.load());
    CHECK (played.peak.load() < loudestOutput);

    // Each instance still builds the room its own settings ask for, at its own rate.
    for (size_t n = 0; n < playing.size(); ++n)
    {
        INFO ("instance " << n);
        auto& p = **playing[n];
        testing::onMessageThread ([&] { p.prepareToPlay (rates[n], blockSize); });
        REQUIRE (testing::builderIdle (p));
        CHECK (p.roomIsCurrent());
    }
}

TEST_CASE ("The host can save the session from a thread of its own while things change", "[plugin][stress]")
{
    // Some hosts save (autosave, undo history, a project save while playing) from a
    // thread of their own, while the interface goes on changing the session on the
    // message thread.
    testing::Plugin plugin;
    auto& p = *plugin;
    testing::onMessageThread ([&] { p.prepareToPlay (48000.0, 128); });
    const auto sessionType = p.getState().state.getType();

    const auto hostSave = [&p]
    {
        juce::MemoryBlock saved;
        std::thread host ([&] { p.getStateInformation (saved); });
        host.join();
        return juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    };

    std::atomic<bool> running { true }, readable { true };
    std::atomic<int> saves { 0 };

    std::thread host ([&]
    {
        while (running.load())
        {
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));

            if (xml == nullptr || ! xml->hasTagName (sessionType.toString()))
                readable.store (false);

            saves.fetch_add (1);
        }
    });

    testing::onMessageThread ([&]
    {
        const auto spaces = allSpaces();
        const auto ids = parameterIds (p);

        std::mt19937 rng (4u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        const auto pick = [&rng] (const auto& list) { return list[rng() % list.size()]; };

        runSteps (6000, [&] (int step)
        {
            switch (step % 5)
            {
                case 0: p.getUiState().setProperty (UiIDs::spaceId, pick (spaces), nullptr); break;
                case 1: for (int k = 0; k < 8; ++k) set (p, pick (ids), unit (rng)); break;
                case 2: p.storeScene (1 + static_cast<int> (rng() % 4), {}); break;
                case 3: p.recallScene (1 + static_cast<int> (rng() % 4)); break;
                default: p.getUiState().setProperty (UiIDs::speakers, juce::String (unit (rng) * -6.0f) + " 0", nullptr); break;
            }

            tick (p);
        });
    });

    running.store (false);
    host.join();

    INFO (saves.load() << " sessions saved");
    CHECK (saves.load() > 0);
    CHECK (readable.load());

    // What a save from the host's thread holds: the settings as they are, and the
    // interface's choices once the processor has caught up with them.
    testing::onMessageThread ([&]
    {
        set (p, ParamIDs::size, 0.25f);
        p.getUiState().setProperty (UiIDs::spaceId, bundledSpaces.back().id, nullptr);
        tick (p);
    });

    const auto xml = hostSave();
    REQUIRE (xml != nullptr);
    const auto session = juce::ValueTree::fromXml (*xml);
    const auto* size = p.getState().getParameter (ParamIDs::size);
    CHECK (static_cast<float> (session.getChildWithProperty ("id", ParamIDs::size).getProperty ("value"))
           == size->convertFrom0to1 (0.25f));
    CHECK (session.getChildWithName (UiIDs::tree).getProperty (UiIDs::spaceId).toString() == bundledSpaces.back().id);

    // A setting changed a moment ago, before the processor's timer runs, is saved too.
    testing::onMessageThread ([&] { set (p, ParamIDs::size, 0.75f); });
    const auto later = hostSave();
    REQUIRE (later != nullptr);
    CHECK (static_cast<float> (juce::ValueTree::fromXml (*later).getChildWithProperty ("id", ParamIDs::size).getProperty ("value"))
           == size->convertFrom0to1 (0.75f));

    // And the session saved from the host's thread plays as it would in a new instance.
    juce::MemoryBlock saved;
    std::thread saver ([&] { p.getStateInformation (saved); });
    saver.join();
    playsAsNew (p, saved);
}

TEST_CASE ("The host can load a session from a thread of its own while things change", "[plugin][stress]")
{
    // Hosts that save from a thread of their own may load sessions and presets there too,
    // while the interface goes on changing things on the message thread.
    testing::Plugin plugin;
    auto& p = *plugin;
    testing::onMessageThread ([&] { p.prepareToPlay (48000.0, 128); });

    const auto sessionWith = [&p] (float size, const juce::String& space)
    {
        set (p, ParamIDs::size, size);
        p.getUiState().setProperty (UiIDs::spaceId, space, nullptr);
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        return saved;
    };

    const auto first = testing::onMessageThread ([&] { return sessionWith (0.2f, bundledSpaces.front().id); });
    const auto second = testing::onMessageThread ([&] { return sessionWith (0.8f, bundledSpaces.back().id); });
    const auto hostLoad = [&p] (const juce::MemoryBlock& session)
    {
        std::thread host ([&] { p.setStateInformation (session.getData(), static_cast<int> (session.getSize())); });
        host.join();
    };

    std::atomic<bool> running { true };
    std::atomic<int> loads { 0 };

    std::thread host ([&]
    {
        while (running.load())
        {
            const auto& session = loads.load() % 2 == 0 ? first : second;
            p.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
            loads.fetch_add (1);
        }
    });

    testing::onMessageThread ([&]
    {
        const auto spaces = librarySpaces();
        std::mt19937 rng (5u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        runSteps (4000, [&] (int step)
        {
            switch (step % 4)
            {
                case 0: if (! spaces.empty()) p.getUiState().setProperty (UiIDs::spaceId, spaces[rng() % spaces.size()], nullptr); break;
                case 1: set (p, ParamIDs::brightness, unit (rng)); break;
                case 2: p.storeScene (1 + static_cast<int> (rng() % 4), {}); break;
                default: { juce::MemoryBlock saved; p.getStateInformation (saved); } break;
            }

            tick (p);
        });
    });

    running.store (false);
    host.join();

    INFO (loads.load() << " sessions loaded");
    CHECK (loads.load() > 0);

    // The last session loaded is the one that plays, and the one saved, once the
    // processor has caught up with it.
    hostLoad (second);
    testing::onMessageThread ([&] { tick (p); });

    const auto* size = p.getState().getParameter (ParamIDs::size);
    CHECK (p.getState().getRawParameterValue (ParamIDs::size)->load() == size->convertFrom0to1 (0.8f));
    CHECK (testing::onMessageThread ([&] { return p.getUiState().getProperty (UiIDs::spaceId).toString(); }) == bundledSpaces.back().id);
    REQUIRE (testing::builderIdle (p));
    CHECK (testing::onMessageThread ([&] { return p.roomIsCurrent(); }));

    // Saved before the processor catches up, a session just loaded saves as loaded.
    hostLoad (first);
    juce::MemoryBlock saved;
    std::thread saver ([&] { p.getStateInformation (saved); });
    saver.join();
    const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (juce::ValueTree::fromXml (*xml).getChildWithName (UiIDs::tree).getProperty (UiIDs::spaceId).toString() == bundledSpaces.front().id);

    // And that session plays as it would in a new instance.
    playsAsNew (p, first);
}

TEST_CASE ("The host can prepare the plugin from a thread of its own while things change", "[plugin][stress]")
{
    // Some hosts prepare and release plugins away from the message thread (an AU can be
    // initialised from any thread), while the interface goes on changing things.
    testing::Plugin plugin;
    auto& p = *plugin;
    testing::onMessageThread ([&] { p.prepareToPlay (48000.0, 128); });
    std::atomic<bool> running { true };
    std::atomic<int> prepares { 0 };
    Played played;

    std::thread host ([&]
    {
        juce::MidiBuffer midi;

        while (running.load())
        {
            p.prepareToPlay (prepares.load() % 2 == 0 ? 44100.0 : 48000.0, 256);

            for (int b = 0; b < 20; ++b)
            {
                juce::AudioBuffer<float> buffer (2, 256);
                buffer.clear();
                buffer.setSample (0, 0, 0.5f);
                p.processBlock (buffer, midi);
                played.note (buffer);
            }

            p.releaseResources();
            prepares.fetch_add (1);
        }
    });

    testing::onMessageThread ([&]
    {
        const auto spaces = librarySpaces();
        std::mt19937 rng (5u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);

        runSteps (12000, [&] (int step)
        {
            switch (step % 5)
            {
                case 0: if (! spaces.empty()) p.getUiState().setProperty (UiIDs::spaceId, spaces[rng() % spaces.size()], nullptr); break;
                case 1: set (p, ParamIDs::brightness, unit (rng)); break;
                case 2: p.storeScene (1 + static_cast<int> (rng() % 4), {}); break;
                case 3: p.getUiState().setProperty (UiIDs::speakers, juce::String (step % 7) + " 0;0 0", nullptr); break;
                default: { juce::MemoryBlock saved; p.getStateInformation (saved); } break;
            }

            // Between redraws too: the interface writes its state many times a second.
            if (step % 3 == 0)
                tick (p);
        });
    });

    running.store (false);
    host.join();

    INFO (prepares.load() << " prepares, the loudest sample " << played.peak.load());
    CHECK (prepares.load() > 0);
    CHECK (played.finite.load());
    CHECK (played.peak.load() < loudestOutput);

    // Prepared once more, it builds the room it is asked for.
    testing::onMessageThread ([&] { p.prepareToPlay (48000.0, 256); });
    REQUIRE (testing::builderIdle (p));
    CHECK (p.roomIsCurrent());
}

TEST_CASE ("The host can prepare the plugin on its own thread while the interface writes its state", "[plugin][stress]")
{
    // The interface writes its state many times a second (the space, speaker trims); a host
    // that prepares on its own thread meanwhile must not read that state, or the heap can be
    // corrupted. Under ThreadSanitizer this reports the race at once if it comes back.
    testing::Plugin plugin;
    auto& p = *plugin;
    testing::onMessageThread ([&] { p.prepareToPlay (48000.0, 128); });
    std::atomic<bool> running { true };
    std::atomic<int> prepares { 0 };

    std::thread host ([&]
    {
        while (running.load())
        {
            p.prepareToPlay (48000.0, 256);
            prepares.fetch_add (1);
        }
    });

    testing::onMessageThread ([&]
    {
        const auto spaces = librarySpaces();

        runSteps (1000000, [&] (int step)
        {
            const auto n = static_cast<size_t> (step);

            if (! spaces.empty())
                p.getUiState().setProperty (UiIDs::spaceId, spaces[n % spaces.size()], nullptr);

            p.getUiState().setProperty (UiIDs::speakers, juce::String (static_cast<int> (n % 7)) + " 0;0 0", nullptr);
        });
    });

    running.store (false);
    host.join();
    INFO (prepares.load());
    CHECK (prepares.load() > 0);

    // The space last written is the one a host's prepare plays, once the timer has seen it.
    testing::onMessageThread ([&]
    {
        tick (p);
        p.prepareToPlay (48000.0, 256);
    });

    REQUIRE (testing::builderIdle (p));
    CHECK (p.roomIsCurrent());
}

TEST_CASE ("A save from another thread while a session loaded off it is applied saves that session", "[plugin][stress][sessions-thread]")
{
    // The host loads a session on a thread of its own, then saves from one (autosave, undo
    // history) while the message thread is applying it. The save must be that session, not
    // the interface state before it with the new session's parameters. Applying is slowest
    // when the message thread has to read the session's file itself: here it appears only
    // after the host's thread looked for it.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-save-while-applying");
    dir.deleteRecursively();
    REQUIRE (dir.createDirectory());
    const auto file = dir.getChildFile ("arrives late.wav");

    testing::Plugin plugin;
    auto& p = *plugin;

    juce::MemoryBlock loaded;

    testing::onMessageThread ([&]
    {
        p.prepareToPlay (48000.0, 512);
        p.getUiState().setProperty (UiIDs::locks, "before", nullptr);

        juce::MemoryBlock data;
        p.getStateInformation (data);
        auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
        REQUIRE (xml != nullptr);
        auto session = juce::ValueTree::fromXml (*xml);
        session.getChildWithProperty ("id", ParamIDs::mix).setProperty ("value", 11.0, nullptr);
        auto ui = session.getOrCreateChildWithName (UiIDs::tree, nullptr);
        ui.setProperty (UiIDs::locks, "loaded", nullptr);
        ui.setProperty (UiIDs::spaceId, "user", nullptr);
        ui.setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
        juce::AudioProcessor::copyXmlToBinary (*session.createXml(), loaded);
    });

    std::thread ([&] { p.setStateInformation (loaded.getData(), static_cast<int> (loaded.getSize())); }).join();

    {
        // Long enough to read that applying it takes a while.
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (file.createOutputStream().release(), 96000.0, 8, 16, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> noise (8, 96000);
        juce::Random random (5);

        for (int second = 0; second < 30; ++second)
        {
            for (int ch = 0; ch < 8; ++ch)
                for (int n = 0; n < noise.getNumSamples(); ++n)
                    noise.setSample (ch, n, (random.nextFloat() - 0.5f) * std::exp (-0.2f * static_cast<float> (second)));

            REQUIRE (writer->writeFromAudioSampleBuffer (noise, 0, noise.getNumSamples()));
        }
    }

    std::atomic<bool> done { false };
    std::atomic<int> saves { 0 }, mixedUp { 0 };

    std::thread host ([&]
    {
        while (! done.load())
        {
            juce::MemoryBlock data;
            p.getStateInformation (data);
            auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
            const auto session = juce::ValueTree::fromXml (*xml);
            const auto mix = static_cast<double> (session.getChildWithProperty ("id", ParamIDs::mix).getProperty ("value"));
            const auto locks = session.getChildWithName (UiIDs::tree).getProperty (UiIDs::locks).toString();
            ++saves;

            if (std::abs (mix - 11.0) < 0.01 && locks != "loaded")
                ++mixedUp;
        }
    });

    while (saves.load() < 10)
        std::this_thread::yield();

    // What the timer does at its next tick; a save on the message thread does it too.
    testing::onMessageThread ([&] { juce::MemoryBlock data; p.getStateInformation (data); });

    const auto after = saves.load();

    while (saves.load() < after + 10)
        std::this_thread::yield();

    done.store (true);
    host.join();

    CHECK (mixedUp.load() == 0);
    CHECK (testing::onMessageThread ([&] { return p.getUiState().getProperty (UiIDs::locks).toString(); }) == "loaded");

    testing::onMessageThread ([&] { p.releaseResources(); });
    dir.deleteRecursively();
}

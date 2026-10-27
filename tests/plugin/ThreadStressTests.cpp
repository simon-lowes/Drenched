#include "PluginTestHelpers.h"

#include "Library.h"

#include <drenched/Spaces.h>

#include <catch2/catch_test_macros.hpp>

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
    as they go, for a few seconds. Run under ThreadSanitizer (DRENCHED_SANITIZE=thread)
    it also finds data races between them. */

namespace
{
    void set (DrenchedProcessor& p, const juce::String& id, float normalised)
    {
        if (auto* parameter = p.getState().getParameter (id))
            parameter->setValueNotifyingHost (normalised);
    }
}

TEST_CASE ("Playing while everything else changes stays finite", "[plugin][stress]")
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 128;

    testing::Plugin plugin;
    auto& p = *plugin;
    testing::onMessageThread ([&] { p.prepareToPlay (rate, blockSize); });

    std::atomic<bool> running { true }, finite { true };
    std::atomic<int> blocks { 0 };

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

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                    if (! std::isfinite (buffer.getSample (c, i)))
                        finite.store (false);

            blocks.fetch_add (1);
        }
    });

    testing::onMessageThread ([&]
    {
        std::vector<juce::String> spaces;

        for (const auto& space : bundledSpaces)
            spaces.push_back (space.id);

        for (const auto& space : library::spaces())
            spaces.push_back (space.id);

        std::vector<juce::String> ids;

        for (auto* parameter : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (ranged->paramID != ParamIDs::lowCpu) // a latency change; hosts restart for it
                    ids.push_back (ranged->paramID);

        std::mt19937 rng (2u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        const auto pick = [&rng] (const auto& list) { return list[rng() % list.size()]; };

        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (4);
        juce::MemoryBlock session;
        p.getStateInformation (session);

        for (int step = 0; std::chrono::steady_clock::now() < end; ++step)
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

            // What the processor's timer does: follows the scene parameter, rebuilds the
            // response. Timer is a private base, which only a C-style cast may reach.
            ((juce::Timer&) p).timerCallback();
        }
    });

    running.store (false);
    audio.join();

    INFO (blocks.load() << " blocks played");
    CHECK (blocks.load() > 0);
    CHECK (finite.load());
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

    std::atomic<bool> running { true }, finite { true };
    std::atomic<int> blocks { 0 };
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

                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < blockSize; ++i)
                        if (! std::isfinite (buffer.getSample (c, i)))
                            finite.store (false);

                blocks.fetch_add (1);
            }
        });
    }

    testing::onMessageThread ([&]
    {
        std::vector<juce::String> spaces;

        for (const auto& space : bundledSpaces)
            spaces.push_back (space.id);

        for (const auto& space : library::spaces())
            spaces.push_back (space.id);

        std::mt19937 rng (3u);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (4);

        for (int step = 0; std::chrono::steady_clock::now() < end; ++step)
        {
            auto& p = **playing[static_cast<size_t> (step) % playing.size()];
            p.getUiState().setProperty (UiIDs::spaceId, spaces[rng() % spaces.size()], nullptr);
            set (p, ParamIDs::size, static_cast<float> (rng() % 100) / 100.0f);
            ((juce::Timer&) p).timerCallback();

            // Opening and closing other instances, which load and free their own spaces.
            if (step % 25 == 0)
            {
                testing::Plugin extra;
                extra->prepareToPlay (rates[rng() % rates.size()], blockSize);
                extra->getUiState().setProperty (UiIDs::spaceId, spaces[rng() % spaces.size()], nullptr);
                ((juce::Timer&) *extra).timerCallback();
            }
        }
    });

    running.store (false);

    for (auto& thread : audio)
        thread.join();

    INFO (blocks.load() << " blocks played");
    CHECK (blocks.load() > static_cast<int> (playing.size()));
    CHECK (finite.load());
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
        std::vector<juce::String> spaces;

        for (const auto& space : bundledSpaces)
            spaces.push_back (space.id);

        for (const auto& space : library::spaces())
            spaces.push_back (space.id);

        std::vector<juce::String> ids;

        for (auto* parameter : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (ranged->paramID != ParamIDs::lowCpu)
                    ids.push_back (ranged->paramID);

        std::mt19937 rng (4u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        const auto pick = [&rng] (const auto& list) { return list[rng() % list.size()]; };
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (4);

        for (int step = 0; std::chrono::steady_clock::now() < end; ++step)
        {
            switch (step % 5)
            {
                case 0: p.getUiState().setProperty (UiIDs::spaceId, pick (spaces), nullptr); break;
                case 1: for (int k = 0; k < 8; ++k) set (p, pick (ids), unit (rng)); break;
                case 2: p.storeScene (1 + static_cast<int> (rng() % 4), {}); break;
                case 3: p.recallScene (1 + static_cast<int> (rng() % 4)); break;
                default: p.getUiState().setProperty (UiIDs::speakers, juce::String (unit (rng) * -6.0f) + " 0", nullptr); break;
            }

            ((juce::Timer&) p).timerCallback();
        }
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
        ((juce::Timer&) p).timerCallback();
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
}

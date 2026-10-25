#include "PluginTestHelpers.h"

#include "Library.h"

#include <drenched/Spaces.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>

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

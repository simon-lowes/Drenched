#include "PluginTestHelpers.h"

#include <TestHelpers.h>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <random>
#include <utility>
#include <vector>

using namespace drenched;

/*  The audio thread must never wait, so the whole plugin's audio path, not just the
    engine's, never takes or gives back memory: either can take a lock inside the
    allocator, or a free of a large kernel can take milliseconds. Here every way a
    session changes while it plays (each control end to end, a new room, a scene, a
    capture, bypass, tempo, odd block sizes) runs with every allocation and free on
    the audio thread counted. The interface's timer runs between blocks, uncounted,
    and the builder on its own thread, as in a host. */

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    juce::RangedAudioParameter* find (DrenchedProcessor& p, const juce::String& id)
    {
        return dynamic_cast<juce::RangedAudioParameter*> (p.getState().getParameter (id));
    }

    struct Host
    {
        DrenchedProcessor& p;
        std::size_t allocations = 0, frees = 0;
        bool bypassed = false;
        int blocks = 0;
        std::mt19937 rng { 5u };

        void block (int size)
        {
            juce::AudioBuffer<float> buffer (2, size);
            juce::MidiBuffer midi;
            std::uniform_real_distribution<float> noise (-0.3f, 0.3f);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < size; ++i)
                    buffer.setSample (c, i, noise (rng));

            {
                testing::AllocationCounter counter;

                if (bypassed)
                    p.processBlockBypassed (buffer, midi);
                else
                    p.processBlock (buffer, midi);

                allocations += counter.count();
                frees += counter.freed();
            }

            // The interface's timer, about every 50 ms.
            if (++blocks % 9 == 0)
                tick();
        }

        /** What the processor's timer does. Timer is a private base, which only a C-style
            cast may reach. */
        void tick() { ((juce::Timer&) p).timerCallback(); }

        /** Plays in real time's order: the builder gets time to work between blocks. */
        void play (double seconds, int size = blockSize)
        {
            for (int b = 0; b < static_cast<int> (seconds * rate) / size; ++b)
            {
                block (size);

                if (b % 4 == 0)
                    juce::Thread::sleep (1);
            }
        }

        /** Makes a change and lets the timer hand it to the builder, then plays, with the
            builder working on its own thread meanwhile, until the builder has built
            everything asked of it, and on until the old room has faded out. So a new room
            always lands in a counted block, however slow the machine. Returns whether a
            new room was built. */
        bool playThrough (const std::function<void()>& change)
        {
            const auto before = p.responseVersion();
            testing::onMessageThread (change);
            tick();

            const auto giveUp = juce::Time::getMillisecondCounterHiRes() + static_cast<double> (testing::buildTimeout.count());

            while (! p.waitForBuilder (5))
            {
                REQUIRE (juce::Time::getMillisecondCounterHiRes() < giveUp); // a hang, not a slow machine
                play (0.01);
            }

            play (0.2);
            return p.responseVersion() != before;
        }

        /** playThrough, and the room built must be the one the change asks for: a change
            that needs a new room gets one, and one that doesn't, doesn't. */
        void changeRoom (const std::function<void()>& change)
        {
            REQUIRE (p.roomIsCurrent());
            auto needsRoom = false;

            const auto built = playThrough ([&]
            {
                change();
                needsRoom = ! p.roomIsCurrent();
            });

            CHECK (built == needsRoom);
            CHECK (p.roomIsCurrent());
        }

        void check (const char* what)
        {
            INFO (what);
            CHECK (allocations == 0);
            CHECK (frees == 0);
            allocations = frees = 0;
        }
    };

    struct Playing
    {
        testing::Plugin p;
        Host host { *p };

        Playing()
        {
            testing::onMessageThread ([this] { p->prepareToPlay (rate, blockSize); });

            // The first room arrives before counting starts.
            host.playThrough ([] {});
            host.allocations = host.frees = 0;
        }
    };

    /** Chooses a space as the browser does. The tests can't run the message loop that
        brings a recording read in the background, so it is read at once, as a scene
        recalled for a bounce does. */
    void chooseSpace (DrenchedProcessor& p, const juce::String& id)
    {
        p.getUiState().setProperty (UiIDs::spaceId, id, nullptr);
        p.storeScene (8, id);
        p.recallScene (8, true);
    }
}

TEST_CASE ("The counter sees every way memory is taken and given back", "[plugin][realtime]")
{
    // Seen where the compiler can't prove the memory unused, so it can't leave out the
    // allocations.
    static void* volatile seen = nullptr;
    std::size_t taken = 0, given = 0;

    {
        testing::AllocationCounter counter;
        auto* number = new int (1);
        seen = number;
        delete number;
        juce::HeapBlock<float> block (100);
        seen = block.get();
        block.free();
        taken = counter.count();
        given = counter.freed();
    }

    (void) seen;
    CHECK (taken == (testing::AllocationCounter::seesMalloc() ? 2 : 1));
    CHECK (given == taken);
}

// One test case per control (TestType::value is its place in ParamIDs::ordered), so each
// is its own CTest entry: they run in parallel, and each stays well inside the timeout
// under sanitizers, where all of them in one would not.
TEMPLATE_LIST_TEST_CASE ("A control moves end to end without memory taken or given back on the audio thread", "[plugin][realtime]",
                         testing::EachParameter)
{
    const auto* id = ParamIDs::ordered[TestType::value];
    INFO (id);

    Playing playing;
    auto& host = playing.host;
    auto& p = playing.p;

    host.play (0.25);
    host.check ("playing");

    auto* control = find (*p, id);
    REQUIRE (control != nullptr);
    const auto was = control->getValue();

    // To the far end of its range and back: a new room each way for the controls that
    // change the room, each landing in a counted block.
    host.changeRoom ([&] { control->setValueNotifyingHost (was < 0.5f ? 1.0f : 0.0f); });
    host.check ("to the far end");

    host.changeRoom ([&] { control->setValueNotifyingHost (was); });
    host.check ("and back");
}

TEST_CASE ("Every control moves at once without memory taken or given back on the audio thread", "[plugin][realtime]")
{
    Playing playing;
    auto& host = playing.host;
    auto& p = playing.p;

    host.play (0.25);
    host.check ("playing");

    // Every control to the far end of its range at once, then all back at once.
    std::vector<std::pair<juce::RangedAudioParameter*, float>> was;

    for (const auto* id : ParamIDs::ordered)
    {
        auto* control = find (*p, id);
        REQUIRE (control != nullptr);
        was.emplace_back (control, control->getValue());
    }

    host.changeRoom ([&]
    {
        for (const auto& [control, value] : was)
            control->setValueNotifyingHost (value < 0.5f ? 1.0f : 0.0f);
    });

    host.check ("every control at the far end");

    host.changeRoom ([&]
    {
        for (const auto& [control, value] : was)
            control->setValueNotifyingHost (value);
    });

    host.check ("every control back");
}

TEST_CASE ("Rooms, scenes, bypass, tempo and captures play without memory taken or given back on the audio thread", "[plugin][realtime]")
{
    Playing playing;
    auto& host = playing.host;
    auto& p = playing.p;

    // A new instance plays St Paul's (ORTF), so the first room chosen is another.
    for (const auto* id : { "modelled-chapel", "stpauls-hall", "stpauls-ortf", "modelled-hall" })
    {
        INFO (id);
        CHECK (host.playThrough ([&] { chooseSpace (*p, id); }));
        host.check (id);
    }

    host.playThrough ([&] { p->storeScene (2, "Chapel"); chooseSpace (*p, "modelled-room"); });
    CHECK (host.playThrough ([&] { p->recallScene (2); }));
    host.check ("a scene recalled");

    host.bypassed = true;
    host.play (0.5);
    host.bypassed = false;
    host.play (0.5);
    host.check ("bypass");

    struct Head final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (97.0);
            info.setIsPlaying (true);
            return info;
        }
    } head;

    p->setPlayHead (&head);
    host.playThrough ([&] { find (*p, ParamIDs::predelaySync)->setValueNotifyingHost (1.0f); });
    host.play (0.5);
    p->setPlayHead (nullptr);
    host.check ("tempo");

    for (const auto size : { 1, 7, 0, 1000, 4096 })
        for (int i = 0; i < 20; ++i)
            host.block (size);

    // Offline, hosts hand over blocks far larger than they prepared for.
    for (const auto size : { 65536, 1048576 })
        for (int i = 0; i < 2; ++i)
            host.block (size);

    host.check ("odd block sizes");

    testing::onMessageThread ([&]
    {
        auto options = std::make_unique<juce::DynamicObject>();
        options->setProperty ("sweepSeconds", 2.0);
        options->setProperty ("tailSeconds", 1.0);
        p->startCapture (juce::var (options.release()));
    });

    REQUIRE (p->captureStatus()["stage"].toString() == "playing");
    host.play (3.5);
    host.check ("capturing a space");

    // The take is analysed off the audio thread; meanwhile the room plays on.
    for (int i = 0; i < 300 && p->captureStatus()["stage"].toString() == "working"; ++i)
    {
        host.play (0.01);
        juce::Thread::sleep (10);
    }

    CHECK (p->captureStatus()["stage"].toString() != "playing");
    host.check ("after the capture");
}

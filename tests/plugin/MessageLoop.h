#pragma once

#include <juce_events/juce_events.h>

#include <catch2/catch_test_macros.hpp>

/*  Running the message loop from a test, so what the plugin hands to the message thread
    (MessageManager::callAsync, and its timer) runs as it does in a host.

    The tests run on the message thread (see Main.cpp) and nothing else dispatches its
    messages, so a test that waits for one has to dispatch them itself.
    MessageManager::runDispatchLoopUntil would do, but JUCE only builds it with
    JUCE_MODAL_LOOPS_PERMITTED, which the plugin leaves off (as plugins should) and the
    tests can't turn on for themselves: they link the plugin's own copy of JUCE. So this
    calls what runDispatchLoopUntil calls, juce::detail::dispatchNextMessageOnSystemQueue,
    which JUCE builds on Linux and Windows whatever that setting, and declares in the same
    way where it uses it itself (juce_LinuxMessageThread.h, juce_FileChooser_windows.cpp).

    On macOS the loop belongs to NSApplication, which a console program doesn't run, so
    tests that need the loop are skipped there: start them with
    DRENCHED_NEEDS_MESSAGE_LOOP() and tag them [messageloop]. */

#if ! JUCE_MAC
namespace juce::detail
{
    bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}
#endif

#if JUCE_MAC
 #define DRENCHED_NEEDS_MESSAGE_LOOP() SKIP ("The message loop needs a full application on macOS")
#else
 #define DRENCHED_NEEDS_MESSAGE_LOOP() static_cast<void> (0)
#endif

namespace testing
{
    /** Dispatches messages on the message thread for about `ms` milliseconds: what
        MessageManager::runDispatchLoopUntil (ms) does. Linux and Windows only. */
    inline void pumpFor (int ms)
    {
       #if JUCE_MAC
        juce::ignoreUnused (ms);
        FAIL ("No message loop to run on macOS: use DRENCHED_NEEDS_MESSAGE_LOOP()");
       #else
        REQUIRE (juce::MessageManager::existsAndIsCurrentThread());
        const auto end = juce::Time::getMillisecondCounterHiRes() + ms;

        do
        {
            if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
                juce::Thread::sleep (1);
        }
        while (juce::Time::getMillisecondCounterHiRes() < end);
       #endif
    }

    /** Dispatches every message already waiting for the message thread, and any those
        post in turn, without waiting for more: whether there were any. Linux and Windows
        only. */
    inline bool dispatchWaiting()
    {
       #if JUCE_MAC
        FAIL ("No message loop to run on macOS: use DRENCHED_NEEDS_MESSAGE_LOOP()");
        return false;
       #else
        REQUIRE (juce::MessageManager::existsAndIsCurrentThread());
        auto any = false;

        while (juce::detail::dispatchNextMessageOnSystemQueue (true))
            any = true;

        return any;
       #endif
    }

    /** Runs the message loop, 10 ms at a time, until `done` returns true (checked between
        runs, on the message thread). False if it still hasn't after timeoutMs. */
    template <typename Predicate>
    bool pumpUntil (Predicate done, int timeoutMs = 10000)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + timeoutMs;

        while (! done())
        {
            if (juce::Time::getMillisecondCounterHiRes() >= end)
                return false;

            pumpFor (10);
        }

        return true;
    }
}

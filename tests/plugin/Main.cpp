#include "PluginTestHelpers.h"

#include <catch2/catch_session.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>

namespace
{
    /** JUCE's assertions, and the leaks its leak detector finds, reach the logger in a
        Debug build (JUCE_LOG_ASSERTIONS, set in the top-level CMakeLists.txt), and are
        only printed: JUCE carries on. Here one a test didn't expect (see
        testing::ExpectJuceAssertions) fails the run. The tests carry on to the end, which
        then fails; one after the tests, as a leak found when the program closes is,
        ends it at once with an error. CTest runs each test case on its own, so it knows
        which one it was. Release builds have no assertions, so nothing changes there. */
    class AssertionLog final : public juce::Logger
    {
    public:
        void logMessage (const juce::String& message) override
        {
            std::cerr << message << std::endl;

            if (! message.startsWith ("JUCE Assertion failure"))
                return;

            if (testing::ExpectJuceAssertions::active())
                return;

            unexpected.fetch_add (1);

            if (finished.load())
            {
                std::cerr << "An unexpected JUCE assertion after the tests finished (a leak?)" << std::endl;
                std::_Exit (EXIT_FAILURE);
            }
        }

        std::atomic<int> unexpected { 0 };
        std::atomic<bool> finished { false };
    };
}

/** The tests run on the message thread, as a host calls a plugin's state and setup
    functions. Waiting on a dispatch loop from another thread hangs on macOS, where the
    loop needs a full application. Nothing runs the loop by itself: tests that need what
    the plugin hands to the message thread run it with testing::pumpUntil (MessageLoop.h),
    on Linux and Windows. */
int main (int argc, char* argv[])
{
    // Plugins made by the tests leave crash reports alone (CrashReportTests.cpp sets the
    // handler up in child processes of its own). DRENCHED_CRASH_REPORTS=1 turns them on.
   #if JUCE_WINDOWS
    if (std::getenv ("DRENCHED_CRASH_REPORTS") == nullptr)
        _putenv_s ("DRENCHED_CRASH_REPORTS", "0");
   #else
    setenv ("DRENCHED_CRASH_REPORTS", "0", 0);
   #endif

    // Never deleted: the leak detector reports once main has returned.
    static auto* const log = new AssertionLog();
    juce::Logger::setCurrentLogger (log);

    auto result = 0;

    {
        juce::ScopedJuceInitialiser_GUI juce;
        result = Catch::Session().run (argc, argv);
    }

    if (const auto count = log->unexpected.load(); count > 0)
    {
        std::cerr << count << " unexpected JUCE assertion" << (count == 1 ? "" : "s") << " (see above)" << std::endl;

        if (result == 0)
            result = EXIT_FAILURE;
    }

    log->finished.store (true);
    return result;
}

#include <CrashReports.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <csignal>
#include <cstdlib>

#if ! JUCE_WINDOWS
 #include <pthread.h>
 #include <sys/wait.h>
 #include <unistd.h>
#endif

using namespace drenched;
using Catch::Matchers::ContainsSubstring;

/*  Crash reports (CrashReports.h): the steps kept for "What Drenched last did", the reports
    waiting in the folder, and, on macOS and Linux, a real crash in a child process, which
    leaves a report when Drenched is on the crashing stack and none when it isn't.
    The plugin tests run with DRENCHED_CRASH_REPORTS=0 (Main.cpp), so no plugin made here
    sets the handler up in this process; the child processes set it up themselves. */

namespace
{
    juce::File emptyFolder (const char* name)
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile (name, {}, false);
        REQUIRE (folder.createDirectory());
        return folder;
    }

   #if defined (__SANITIZE_ADDRESS__) || defined (__SANITIZE_THREAD__)
    [[maybe_unused]] constexpr bool sanitized = true;
   #elif defined (__has_feature)
    #if __has_feature (address_sanitizer) || __has_feature (thread_sanitizer) || __has_feature (realtime_sanitizer) || __has_feature (undefined_behavior_sanitizer)
    [[maybe_unused]] constexpr bool sanitized = true;
    #else
    [[maybe_unused]] constexpr bool sanitized = false;
    #endif
   #else
    [[maybe_unused]] constexpr bool sanitized = false;
   #endif

   #if ! JUCE_WINDOWS
    [[gnu::noinline]] void crashInDrenched()
    {
        crashReports::step ("about to crash");
        std::raise (SIGSEGV);
    }

    [[gnu::noinline]] void badWriteInDrenched()
    {
        *static_cast<volatile int*> (nullptr) = 1;
    }

    /** Runs fn in a child process that sets the handler up first; its wait status. */
    template <typename Fn>
    int inChild (const juce::File& folder, Fn&& fn)
    {
        const auto child = fork();
        REQUIRE (child >= 0);

        if (child == 0)
        {
            // Without the test framework's own crash handler, which would report a failure.
            for (const auto sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT })
                std::signal (sig, SIG_DFL);

            crashReports::install (folder, "Drenched test build\nHost: the tests");
            fn();
            _exit (0);
        }

        int status = 0;
        waitpid (child, &status, 0);
        return status;
    }
   #endif
}

TEST_CASE ("The last sixteen steps are kept, oldest first, each with its time", "[crash]")
{
    for (int i = 0; i < 20; ++i)
        crashReports::step ("step " + juce::String (i) + "\nwith a new line");

    const auto lines = juce::StringArray::fromLines (crashReports::recentSteps());
    REQUIRE (lines.size() == 16);
    CHECK (lines[0].endsWith (" step 4 with a new line"));
    CHECK (lines[15].endsWith (" step 19 with a new line"));
    CHECK (lines[15].substring (2, 3) == ":"); // 12:34:56 step 19

    crashReports::step (juce::String::repeatedString ("x", 1000));
    CHECK (juce::StringArray::fromLines (crashReports::recentSteps())[15].length() < 200);
}

TEST_CASE ("The newest waiting crash report is offered, and gone once handled", "[crash]")
{
    const auto folder = emptyFolder ("drenched-crashes");

    CHECK_FALSE (crashReports::pending (folder).has_value());
    CHECK_FALSE (crashReports::pending (folder.getChildFile ("missing")).has_value());

    REQUIRE (folder.getChildFile ("crash-1700000000.txt").replaceWithText ("older"));
    REQUIRE (folder.getChildFile ("crash-1800000000.txt").replaceWithText ("newer"));
    REQUIRE (folder.getChildFile ("crash-1900000000.txt").replaceWithText ("")); // a crash that wrote nothing
    REQUIRE (folder.getChildFile ("notes.txt").replaceWithText ("not a report"));

    auto crash = crashReports::pending (folder);
    REQUIRE (crash.has_value());
    CHECK (crash->text == "newer");
    CHECK (crash->when == juce::Time (1800000000000));

    crashReports::handled (crash->file);
    crash = crashReports::pending (folder);
    REQUIRE (crash.has_value());
    CHECK (crash->text == "older");

    folder.deleteRecursively();
}

#if ! JUCE_WINDOWS

TEST_CASE ("A crash in Drenched leaves a report saying where, and what it last did", "[crash]")
{
    if (sanitized)
        SKIP ("A sanitizer handles crashes itself");

    for (auto* crash : { &crashInDrenched, &badWriteInDrenched })
    {
        const auto folder = emptyFolder ("drenched-crash");
        const auto status = inChild (folder, crash);

        // The crash still ends the process, as it would have without Drenched's handler.
        CHECK (WIFSIGNALED (status));

        const auto report = crashReports::pending (folder);
        REQUIRE (report.has_value());
        INFO (report->text);
        CHECK_THAT (report->text.toStdString(), ContainsSubstring ("Drenched crashed: signal 11 (SIGSEGV"));
        CHECK_THAT (report->text.toStdString(), ContainsSubstring ("Drenched test build\nHost: the tests"));
        CHECK_THAT (report->text.toStdString(), ContainsSubstring ("Stack (module+offset):\n 0 "));
        CHECK_THAT (report->text.toStdString(), ContainsSubstring ("Drenched+0x"));
        CHECK_THAT (report->text.toStdString(), ContainsSubstring ("What Drenched last did"));

        if (crash == &crashInDrenched)
            CHECK_THAT (report->text.toStdString(), ContainsSubstring (" about to crash\n"));
        else
            CHECK_THAT (report->text.toStdString(), ContainsSubstring (", address 0x0\n"));

        folder.deleteRecursively();
    }
}

TEST_CASE ("A crash with Drenched nowhere on the stack is left to the host", "[crash]")
{
    if (sanitized)
        SKIP ("A sanitizer handles crashes itself");

    const auto folder = emptyFolder ("drenched-crash");

    // A thread that starts straight in the C library's abort(): only system code runs on it.
    const auto status = inChild (folder, []
    {
        pthread_t thread;
        pthread_create (&thread, nullptr, reinterpret_cast<void* (*) (void*)> (reinterpret_cast<void*> (&std::abort)), nullptr);
        pthread_join (thread, nullptr);
    });

    CHECK (WIFSIGNALED (status));
    CHECK (WTERMSIG (status) == SIGABRT);
    CHECK_FALSE (crashReports::pending (folder).has_value());
    folder.deleteRecursively();
}

#endif

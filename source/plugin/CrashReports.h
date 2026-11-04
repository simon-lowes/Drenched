#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace drenched
{

/** Crash reports: when Drenched crashes, a short text file saying where, and what it last
    did, so the next time it opens it can offer to send it (Report a bug, ReportSender).

    install() sets up a handler once per process, for this module only:
    - macOS and Linux: SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT. The handler writes the
      file only when this module is on the crashing stack (a host's own crash is left
      alone), then puts back whatever handler was there before and returns, so the host's
      crash reporter still sees the crash.
    - Windows: a vectored exception handler for fatal exceptions raised in this module's
      code (the C runtime is linked in statically, so that covers it too), and abort().
    Unloading the module takes the handlers away again. Nothing is ever sent without the
    person choosing to: the file only sits in the settings folder until then.

    The handler only writes: no allocation or locks, everything it needs prepared here. */
namespace crashReports
{
    /** Sets the handler up (the first call in a process; later calls do nothing).
        about: a few lines on the build and setup, written at the top of every report. */
    void install (const juce::File& folder, const juce::String& about);

    /** Notes something Drenched did, for "What Drenched last did" in reports (the last
        sixteen are kept). Any thread but the audio thread. */
    void step (const juce::String& what);

    /** The steps noted so far, oldest first, one per line with the time. */
    juce::String recentSteps();

    struct Crash
    {
        juce::File file;
        juce::String text;
        juce::Time when;
    };

    /** The newest crash report waiting in folder, if any. */
    std::optional<Crash> pending (const juce::File& folder);

    /** Done with: sent, or the person chose not to. Deletes it. */
    void handled (const juce::File& crashFile);

    /** For tests: true once install() has run in this process. */
    bool isInstalled();
}

} // namespace drenched

#include "PluginTestHelpers.h"

#include <catch2/catch_session.hpp>

/** The tests run on the message thread, as a host calls a plugin's state and setup
    functions. Waiting on a dispatch loop from another thread hangs on macOS, where the
    loop needs a full application. Nothing runs the loop by itself: tests that need what
    the plugin hands to the message thread run it with testing::pumpUntil (MessageLoop.h),
    on Linux and Windows. */
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}

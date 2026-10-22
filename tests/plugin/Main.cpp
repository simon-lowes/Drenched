#include "PluginTestHelpers.h"

#include <catch2/catch_session.hpp>

/** The tests run on the message thread, as a host calls a plugin's state and setup
    functions. Waiting on a dispatch loop from another thread hangs on macOS, where the
    loop needs a full application, and nothing here needs one. */
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}

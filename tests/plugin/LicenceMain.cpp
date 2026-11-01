#include "LicenceTestHelpers.h"

#include <catch2/catch_session.hpp>

#include <cstdlib>
#include <thread>

namespace
{
    juce::File home;
}

juce::File testing::isolatedHome()
{
    return home;
}

/** The licence tests (tests/plugin/CMakeLists.txt). Unlike the other plugin tests these run
    on their own thread while the message thread runs its loop for real, because the
    licence manager answers from the activation server through the message thread
    (MessageManager::callAsync). Tests reach the message thread with onMessageThread().

    HOME points at an empty folder for the whole run, so the licence file the plugin
    itself uses (LicenceManager::Environment::system()) is a new one there and never the
    developer's own. Windows doesn't look at HOME, so LicenceMuteTests.cpp checks where
    the file is before touching it. */
int main (int argc, char* argv[])
{
    // Found before HOME changes: on macOS the temporary folder is inside the home folder.
    home = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("drenched-licence-home", {}, false);
    home.createDirectory();

   #if ! JUCE_WINDOWS
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
   #endif

    int result = 0;

    {
        juce::ScopedJuceInitialiser_GUI juce;

        std::thread tests ([&]
        {
            result = Catch::Session().run (argc, argv);
            juce::MessageManager::getInstance()->stopDispatchLoop();
        });

        juce::MessageManager::getInstance()->runDispatchLoop();
        tests.join();
    }

    home.deleteRecursively();
    return result;
}

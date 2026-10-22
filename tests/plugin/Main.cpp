#include "PluginTestHelpers.h"

#include <catch2/catch_session.hpp>

#include <thread>

/** The plugin expects a message thread, so the tests run on a second thread while this
    one dispatches messages, the way a host runs. */
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto result = 0;

    std::thread tests ([&]
    {
        result = Catch::Session().run (argc, argv);
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    tests.join();
    return result;
}

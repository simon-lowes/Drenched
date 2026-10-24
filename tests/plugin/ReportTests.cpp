#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace drenched;

/*  Report a bug copies what a tester's report needs about their setup, so it has to name
    every fact the user guide asks for. */

TEST_CASE ("Bug report details name the version, format, host, computer, audio, space and look", "[report]")
{
    testing::Plugin plugin;

    const auto details = testing::onMessageThread ([&]
    {
        plugin->setRateAndBufferSizeDetails (44100.0, 128); // as a host does before preparing
        plugin->prepareToPlay (44100.0, 128);
        plugin->getUiState().setProperty (UiIDs::spaceId, "modelled-chapel", nullptr);
        plugin->getUiState().setProperty (UiIDs::skin, "folio", nullptr);
        return plugin->bugReportDetails();
    });

    INFO (details);
    const auto lines = juce::StringArray::fromLines (details);
    REQUIRE (lines.size() == 8);
    CHECK (lines[0].startsWith ("Drenched "));
    CHECK ((lines[0].endsWith ("32-bit") || lines[0].endsWith ("64-bit")));
    CHECK (lines[1].startsWith ("Format: "));
    CHECK (lines[2].startsWith ("Host: "));
    CHECK (lines[3].startsWith ("System: "));
    CHECK (lines[4].startsWith ("Audio: 44100 Hz, buffer 128, latency "));
    CHECK (lines[5].startsWith ("Channels: "));
    CHECK (lines[6] == "Space: modelled-chapel");
    CHECK (lines[7] == "Look: folio, view studio");
}

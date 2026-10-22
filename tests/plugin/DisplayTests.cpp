#include "PluginTestHelpers.h"

#include <drenched/ImpulseResponseSynth.h>

#include <catch2/catch_test_macros.hpp>

using namespace drenched;

/*  The decay displays: the playing response's analysis, sent to the interface whenever a
    new response plays, and the live spectrum of the reverb. */

TEST_CASE ("The playing response's analysis is sent in the form the interface reads", "[plugin][displays]")
{
    testing::Plugin p;
    testing::onMessageThread ([&] { p->prepareToPlay (48000.0, 512); });

    REQUIRE (p->responseVersion() > 0);

    ImpulseResponseRequest request;
    request.decaySeconds = defaultDecay;
    const auto result = DrenchedProcessor::analysisToVar (analyseResponse (synthesiseImpulseResponse (request)));

    REQUIRE (result["bands"].size() == static_cast<int> (analysisBands.size()));
    REQUIRE (result["waterfall"].size() == static_cast<int> (ResponseAnalysis::numSlices));
    REQUIRE (result["waterfall"][0].size() == static_cast<int> (analysisBands.size()));
    REQUIRE (result["decay"].size() == 200);
    REQUIRE (result["rt60"].size() == 8);
    REQUIRE (static_cast<double> (result["seconds"]) > 0.0);
    REQUIRE (static_cast<double> (result["sliceSeconds"]) > 0.0);
}

TEST_CASE ("The live spectrum is silent before anything plays", "[plugin][displays]")
{
    testing::Plugin p;
    testing::onMessageThread ([&] { p->prepareToPlay (48000.0, 512); });

    for (const auto level : testing::onMessageThread ([&] { return p->wetSpectrum(); }))
        REQUIRE (level <= SpectrumTap::floorDb);
}

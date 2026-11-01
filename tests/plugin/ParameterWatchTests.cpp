#include "PluginTestHelpers.h"

#include <ParameterWatch.h>
#include <TestHelpers.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <map>
#include <thread>

using namespace drenched;

/*  The window shows the page every parameter as it changes: the host's automation, a
    preset, a scene. It looks for changes on its timer rather than listening, because a
    listener runs on the thread that sets the value, which for automation is the audio
    thread, where posting to the message thread can lock and allocate. */

namespace
{
    std::map<juce::String, float> changes (ParameterWatch& watch, bool onlyChanged = true)
    {
        std::map<juce::String, float> seen;
        watch.takeChanges (onlyChanged, [&seen] (const juce::String& id, float value) { seen[id] = value; });
        return seen;
    }

    void set (DrenchedProcessor& p, const juce::String& id, float value)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p.getState().getParameter (id));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
}

TEST_CASE ("The window sees host automation at its next look, without work on the audio thread", "[plugin][window]")
{
    testing::Plugin p;
    ParameterWatch watch (p->getState());

    CHECK (watch.anyChanged()); // nothing shown yet
    CHECK (changes (watch).size() == ParamIDs::ordered.size());
    CHECK_FALSE (watch.anyChanged());

    // Automation, set on the audio thread with the window open, costs the audio thread no
    // allocation: nothing listens there for the window.
    auto* mix = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::mix));
    auto* size = dynamic_cast<juce::RangedAudioParameter*> (p->getState().getParameter (ParamIDs::size));
    const auto mixValue = mix->convertTo0to1 (25.0f), sizeValue = size->convertTo0to1 (140.0f);
    std::size_t allocations = 0;

    std::thread audio ([&]
    {
        testing::AllocationCounter counter;
        mix->setValueNotifyingHost (mixValue);
        size->setValueNotifyingHost (sizeValue);
        allocations = counter.count();
    });

    audio.join();
    CHECK (allocations == 0);

    REQUIRE (watch.anyChanged());
    const auto seen = changes (watch);
    CHECK (seen.size() == 2);
    CHECK (seen.at (ParamIDs::mix) == p->getState().getRawParameterValue (ParamIDs::mix)->load());
    CHECK (seen.at (ParamIDs::size) == p->getState().getRawParameterValue (ParamIDs::size)->load());
    CHECK_FALSE (watch.anyChanged());
}

TEST_CASE ("The window doesn't send the page back what the page set", "[plugin][window]")
{
    testing::Plugin p;
    ParameterWatch watch (p->getState());
    changes (watch);

    set (*p, ParamIDs::brightness, 3.0f);
    watch.noteShown (ParamIDs::brightness);
    CHECK_FALSE (watch.anyChanged());

    // But a later change to the same control, from the host, is sent.
    set (*p, ParamIDs::brightness, -3.0f);
    CHECK (changes (watch).count (ParamIDs::brightness) == 1);
}

TEST_CASE ("A new session sends the page every value, and a value that isn't a number once", "[plugin][window]")
{
    testing::Plugin p;
    ParameterWatch watch (p->getState());
    changes (watch);

    watch.resendAll();
    CHECK (watch.anyChanged());
    CHECK (changes (watch).size() == ParamIDs::ordered.size());

    // Damaged automation: the page is sent it (and shows the default) once, not every tick.
    p->getState().getRawParameterValue (ParamIDs::output)->store (std::numeric_limits<float>::quiet_NaN());
    CHECK (changes (watch).size() == 1);
    CHECK_FALSE (watch.anyChanged());
}

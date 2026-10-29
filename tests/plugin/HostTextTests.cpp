#include "PluginTestHelpers.h"

#include <TypedValues.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace drenched;

/*  Hosts show each control by its name and its value as text (automation lanes, generic
    editors, control surfaces) and let people type a value in. The names must tell controls
    apart even where a host has room for only a few characters, the text must read back as
    the value it shows, and a typed value must mean what it would in the plugin's own fields. */

namespace
{
    juce::RangedAudioParameter& find (DrenchedProcessor& p, const juce::String& id)
    {
        auto* found = dynamic_cast<juce::RangedAudioParameter*> (p.getState().getParameter (id));
        REQUIRE (found != nullptr);
        return *found;
    }
}

TEST_CASE ("Typed numbers read as the interface reads them", "[plugin][hosttext]")
{
    const auto numbers = [] (const juce::String& text, bool thousands)
    {
        std::vector<float> found;
        for (auto v : typed::numbers (text, thousands))
            found.push_back (v);
        return found;
    };

    CHECK (numbers ("1,5", false) == std::vector<float> { 1.5f });
    CHECK (numbers ("1,234,567", true) == std::vector<float> { 1234567.0f });
    CHECK (numbers ("12 000 Hz", true) == std::vector<float> { 12000.0f });
    CHECK (numbers ("12 000 Hz", false) == std::vector<float> { 12.0f, 0.0f });
    CHECK (numbers (juce::CharPointer_UTF8 ("\xe2\x88\x92" "3,5 dB"), false) == std::vector<float> { -3.5f });
    CHECK (numbers (juce::CharPointer_UTF8 ("\xe2\x80\x93" "2"), false) == std::vector<float> { -2.0f });
    CHECK (numbers ("-.5", false) == std::vector<float> { -0.5f });
    CHECK (numbers ("5.", false) == std::vector<float> { 5.0f });
    CHECK (numbers ("30 / 70", false) == std::vector<float> { 30.0f, 70.0f });
    CHECK (numbers ("1.5, 2", false) == std::vector<float> { 1.5f, 2.0f });
    CHECK (numbers ("-", false).empty());
    CHECK (numbers ("Off", false).empty());
    CHECK (numbers ("", true).empty());
}

TEST_CASE ("Every control's text in a host reads back as the value it shows", "[plugin][hosttext]")
{
    testing::Plugin p;

    for (auto* param : p->getParameters())
    {
        auto& ranged = *dynamic_cast<juce::RangedAudioParameter*> (param);
        const auto& range = ranged.getNormalisableRange();
        const auto across = ranged.getParameterID() == ParamIDs::listenerX || ranged.getParameterID() == ParamIDs::sourceX;

        // Shown to the nearest step, or the nearest percent off centre.
        const auto shownTo = std::max (range.interval, across ? 0.5f : 0.0f) + 1.0e-4f * (range.end - range.start);

        for (int i = 0; i <= 200; ++i)
        {
            const auto shown = ranged.convertFrom0to1 (static_cast<float> (i) / 200.0f);

            for (const auto& text : { param->getText (ranged.convertTo0to1 (shown), 0),
                                      param->getText (ranged.convertTo0to1 (shown), 0) + " " + param->getLabel(),
                                      param->getText (ranged.convertTo0to1 (shown), 0) + param->getLabel(),
                                      param->getText (ranged.convertTo0to1 (shown), 8) })
            {
                const auto read = ranged.convertFrom0to1 (param->getValueForText (text));
                INFO (param->getName (100) << ": " << shown << " shown as '" << text << "' read as " << read);
                CHECK (std::abs (read - shown) <= shownTo);
            }

            CHECK (param->getText (ranged.convertTo0to1 (shown), 8).length() <= 8);
        }
    }
}

TEST_CASE ("A value typed into a host means what it would in the plugin", "[plugin][hosttext]")
{
    testing::Plugin p;
    const auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));

    struct Case { const char* id; juce::String typed; float wanted; };

    const Case cases[] {
        { ParamIDs::wetHighCut, "14 kHz", 14000.0f }, { ParamIDs::wetHighCut, "14k", 14000.0f },
        { ParamIDs::wetHighCut, "14", 14000.0f }, { ParamIDs::wetHighCut, "8,5 kHz", 8500.0f },
        { ParamIDs::wetHighCut, "12 000", 12000.0f }, { ParamIDs::wetHighCut, "12,000 Hz", 12000.0f },
        { ParamIDs::wetHighCut, "800 Hz", 1000.0f }, { ParamIDs::wetHighCut, "800", 1000.0f },
        { ParamIDs::crossover, "0,5 kHz", 500.0f }, { ParamIDs::crossover, "1k", 1000.0f },
        { ParamIDs::wetLowCut, "120", 120.0f }, { ParamIDs::eqFrequency[2], "2.5 kHz", 2500.0f },
        { ParamIDs::predelay, "0,1 s", 100.0f }, { ParamIDs::predelay, ".05 s", 50.0f },
        { ParamIDs::predelay, "12 ms", 12.0f }, { ParamIDs::predelay, "2 seconds", 250.0f },
        { ParamIDs::gate, "1,5 s", 1500.0f }, { ParamIDs::gate, "1,200 ms", 1200.0f }, { ParamIDs::gate, "1 200", 1200.0f },
        { ParamIDs::directDelay, "2,5", 2.5f },
        { ParamIDs::decay[4], "1,5", 1.5f }, { ParamIDs::decay[4], "800 ms", 0.8f }, { ParamIDs::decay[4], ".5", 0.5f },
        { ParamIDs::decayRight[0], "2.5 s", 2.5f }, { ParamIDs::movementRate, "0,7 Hz", 0.7f },
        { ParamIDs::brightness, minus + "3,5 dB", -3.5f }, { ParamIDs::brightness, juce::CharPointer_UTF8 ("\xe2\x80\x93" "2"), -2.0f },
        { ParamIDs::output, "-6", -6.0f }, { ParamIDs::earlyLate, "30 / 70", 70.0f },
        { ParamIDs::mix, "off", 0.0f }, { ParamIDs::gate, "Off", 0.0f }, { ParamIDs::directLevel, "off", -60.0f },
        { ParamIDs::output, "off", 0.0f }, { ParamIDs::mix, "lots", 30.0f }, { ParamIDs::decay[0], "", defaultDecay[0] },
        { ParamIDs::listenerX, "30% left", 35.0f }, { ParamIDs::listenerX, "30% L", 35.0f }, { ParamIDs::sourceX, "Centre", 50.0f },
        { ParamIDs::sourceX, "center", 50.0f }, { ParamIDs::sourceX, "100% right", 100.0f }, { ParamIDs::sourceX, "54 R", 77.0f },
        { ParamIDs::sourceX, "25", 25.0f },
        { ParamIDs::scene, "Scene 4", 4.0f }, { ParamIDs::scene, "4", 4.0f }, { ParamIDs::scene, "Off", 0.0f }, { ParamIDs::scene, "40", 32.0f },
        { ParamIDs::freeze, "on", 1.0f }, { ParamIDs::freeze, " On ", 1.0f }, { ParamIDs::freeze, "yes", 1.0f }, { ParamIDs::freeze, "1", 1.0f },
        { ParamIDs::freeze, "off", 0.0f }, { ParamIDs::freeze, "0", 0.0f }, { ParamIDs::freeze, "maybe", 0.0f },
    };

    for (const auto& c : cases)
    {
        auto& param = find (*p, c.id);
        const auto got = param.convertFrom0to1 (param.getValueForText (c.typed));
        INFO (c.id << " typed as '" << c.typed << "' gave " << got << ", not " << c.wanted);
        CHECK (std::abs (got - c.wanted) < 1.0e-3f);
    }
}

TEST_CASE ("Hosts with little room still tell every control apart", "[plugin][hosttext]")
{
    testing::Plugin p;

    // A VST3 host's short title holds 8 characters; Pro Tools and control surfaces ask for
    // as few as 4 or as many as 31.
    for (const auto length : { 8, 12, 16, 31 })
    {
        std::set<juce::String> names;

        for (auto* param : p->getParameters())
        {
            const auto name = param->getName (length);
            INFO (param->getName (100) << " at " << length << ": '" << name << "'");
            CHECK (name.length() <= length);
            CHECK (name.isNotEmpty());
            CHECK (names.insert (name).second);
        }
    }

    // With room, every name is whole.
    CHECK (find (*p, ParamIDs::decayRight[3]).getName (1024) == "Right decay 500 Hz");
    CHECK (find (*p, ParamIDs::eqGain[5]).getName (1024) == "EQ high gain");
    CHECK (find (*p, ParamIDs::mix).getName (8) == "Mix");
}

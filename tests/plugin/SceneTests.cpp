#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace drenched;

/*  Scenes keep every setting and the space, for post: a scene per shot, recalled from the
    interface or by automating the scene parameter. */

namespace
{
    float valueOf (DrenchedProcessor& p, const char* id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }

    bool near (float a, float b)
    {
        return std::abs (a - b) < 0.01f;
    }

    void set (DrenchedProcessor& p, const char* id, float value)
    {
        auto* parameter = p.getState().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void chooseSpace (DrenchedProcessor& p, const juce::String& id)
    {
        p.getUiState().setProperty (UiIDs::spaceId, id, nullptr);
    }

    juce::MemoryBlock saved (DrenchedProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return block;
    }
}

TEST_CASE ("A scene brings back every setting and the space", "[scenes]")
{
    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        chooseSpace (p, bundledSpaces[2].id);
        set (p, ParamIDs::mix, 42.0f);
        set (p, ParamIDs::predelay, 60.0f);
        set (p, ParamIDs::decay[4], 3.5f);
        set (p, ParamIDs::sourceX, 20.0f);
        p.getUiState().setProperty (UiIDs::speakers, "-3 10;-3 10", nullptr);
        REQUIRE (p.storeScene (0, "Chapel, wide") == 1);

        chooseSpace (p, bundledSpaces[0].id);
        set (p, ParamIDs::mix, 10.0f);
        set (p, ParamIDs::predelay, 5.0f);
        set (p, ParamIDs::decay[4], 1.0f);
        set (p, ParamIDs::sourceX, 80.0f);
        set (p, ParamIDs::freeze, 1.0f);
        p.getUiState().setProperty (UiIDs::speakers, "", nullptr);
        REQUIRE (p.storeScene (0, "") == 2);

        p.recallScene (1);
    });

    CHECK (p.chosenSpaceId() == juce::String (bundledSpaces[2].id));
    CHECK (near (valueOf (p, ParamIDs::mix), 42.0f));
    CHECK (near (valueOf (p, ParamIDs::predelay), 60.0f));
    CHECK (near (valueOf (p, ParamIDs::decay[4]), 3.5f));
    CHECK (near (valueOf (p, ParamIDs::sourceX), 20.0f));
    CHECK (p.getUiState().getProperty (UiIDs::speakers).toString() == "-3 10;-3 10");
    CHECK (near (valueOf (p, ParamIDs::scene), 1.0f));

    // Freeze is played, not stored: it stays where it is.
    CHECK (near (valueOf (p, ParamIDs::freeze), 1.0f));

    const auto list = testing::onMessageThread ([&] { return p.sceneList(); });
    REQUIRE (list.size() == 2);
    CHECK (list[0]["name"].toString() == "Chapel, wide");
    CHECK (list[0]["space"].toString() == juce::String (bundledSpaces[2].name));
    CHECK (list[1]["name"].toString() == "Scene 2");
}

TEST_CASE ("Automating the scene parameter recalls the scene", "[scenes]")
{
    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        set (p, ParamIDs::mix, 70.0f);
        chooseSpace (p, bundledSpaces[3].id);
        p.storeScene (5, "Kitchen");
        set (p, ParamIDs::mix, 20.0f);
        chooseSpace (p, bundledSpaces[1].id);

        // The host moves the parameter (from the audio thread, in a real session).
        set (p, ParamIDs::scene, 5.0f);
        p.followSceneParameter();
    });

    CHECK (near (valueOf (p, ParamIDs::mix), 70.0f));
    CHECK (p.chosenSpaceId() == juce::String (bundledSpaces[3].id));

    // A move to Off, or to a number with no scene, leaves the settings alone.
    testing::onMessageThread ([&]
    {
        set (p, ParamIDs::mix, 33.0f);
        set (p, ParamIDs::scene, 0.0f);
        p.followSceneParameter();
        set (p, ParamIDs::scene, 9.0f);
        p.followSceneParameter();
    });

    CHECK (near (valueOf (p, ParamIDs::mix), 33.0f));
}

TEST_CASE ("Scenes keep their numbers and are saved with the session", "[scenes]")
{
    testing::Plugin first;
    juce::MemoryBlock session;

    testing::onMessageThread ([&]
    {
        auto& p = *first;
        p.storeScene (0, "One");
        p.storeScene (0, "Two");
        p.storeScene (0, "Three");
        p.deleteScene (2);
        CHECK (p.storeScene (0, "New") == 2); // the lowest free number
        p.renameScene (3, "Hall");
        CHECK (p.storeScene (SceneIDs::maxScenes + 1, "Too far") == 0);

        set (p, ParamIDs::mix, 55.0f);
        p.storeScene (1, ""); // update: keeps the name
        p.recallScene (1);

        // A change after the recall is what the session saves, and plays when it loads.
        set (p, ParamIDs::mix, 12.0f);
        session = saved (p);
    });

    testing::Plugin second;
    testing::onMessageThread ([&]
    {
        auto& p = *second;
        p.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        p.followSceneParameter();
    });

    CHECK (near (valueOf (*second, ParamIDs::scene), 1.0f));
    CHECK (near (valueOf (*second, ParamIDs::mix), 12.0f));

    const auto list = testing::onMessageThread ([&] { return second->sceneList(); });
    REQUIRE (list.size() == 3);
    CHECK (list[0]["name"].toString() == "One");
    CHECK (list[1]["name"].toString() == "New");
    CHECK (list[2]["name"].toString() == "Hall");
    CHECK (static_cast<int> (list[2]["number"]) == 3);

    testing::onMessageThread ([&] { second->recallScene (1); });
    CHECK (near (valueOf (*second, ParamIDs::mix), 55.0f));
}

TEST_CASE ("A scene stored before a parameter existed recalls it at its default", "[scenes]")
{
    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        p.storeScene (1, "Old");

        // As if the scene came from a version without cloud size.
        auto scene = p.getState().state.getChildWithName (SceneIDs::tree).getChildWithProperty (SceneIDs::number, 1);
        REQUIRE (scene.isValid());
        scene.removeProperty (ParamIDs::cloudSize, nullptr);

        set (p, ParamIDs::cloudSize, 30.0f);
        p.recallScene (1);
    });

    CHECK (near (valueOf (p, ParamIDs::cloudSize), 100.0f));
}

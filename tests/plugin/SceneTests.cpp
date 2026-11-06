#include "PluginTestHelpers.h"

#include <Library.h>
#include <RecordingCache.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <cmath>
#include <map>
#include <thread>

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

    /** Half a second of decaying noise as a stereo WAV file, plate.wav in dir. */
    juce::File writePlate (const juce::File& dir)
    {
        const auto file = dir.getChildFile ("plate.wav");
        REQUIRE (dir.createDirectory());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (file.createOutputStream().release(), 48000.0, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> ir (2, 24000);
        juce::Random random (9);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < ir.getNumSamples(); ++n)
                ir.setSample (ch, n, (random.nextFloat() - 0.5f) * std::exp (-0.0003f * static_cast<float> (n)));

        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
        return file;
    }

    using Values = std::map<juce::String, float>;

    /** Every parameter's value, by id. */
    Values valuesOf (DrenchedProcessor& p)
    {
        Values values;

        for (const auto& id : ParamIDs::all())
            values[id] = p.getState().getRawParameterValue (id)->load();

        return values;
    }

    /** Whether two values of a parameter are the same, as it stores them. */
    bool same (float a, float b)
    {
        return std::abs (a - b) <= 1.0e-4f * std::max (1.0f, std::abs (b));
    }

    /** Sets every parameter to a value of its own (which picks one of two sets that differ
        in every parameter, the first off every default) and returns what each then holds. */
    Values setEveryParameter (DrenchedProcessor& p, int which)
    {
        const auto& ids = ParamIDs::all();

        for (int i = 0; i < ids.size(); ++i)
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getState().getParameter (ids[i]));
            REQUIRE (param != nullptr);
            const auto onByDefault = std::round (param->getDefaultValue());
            auto distinct = static_cast<float> (i + 1) / static_cast<float> (ids.size() + 2);

            // A value that snaps to the default would not show whether it came back.
            if (same (param->convertFrom0to1 (distinct), param->convertFrom0to1 (param->getDefaultValue())))
                distinct = std::fmod (distinct + 0.25f, 1.0f);

            if (param->getNumSteps() == 2)
                param->setValueNotifyingHost (which == 0 ? 1.0f - onByDefault : onByDefault);
            else
                param->setValueNotifyingHost (which == 0 ? distinct : std::fmod (distinct + 0.5f, 1.0f));
        }

        return valuesOf (p);
    }

    /** What a scene leaves alone: the scene itself, Freeze (played, not stored) and Low CPU
        (a setting for the computer, not the room). Everything else it brings back. */
    bool leftAlone (const juce::String& id)
    {
        return id == ParamIDs::scene || id == ParamIDs::freeze || id == ParamIDs::lowCpu;
    }

    /** After scene 1, stored with first, is recalled over second: everything a scene stores
        is first again, the scene parameter names scene 1, and the rest is left alone. */
    void checkRecalled (DrenchedProcessor& p, const Values& first, const Values& second)
    {
        const auto now = valuesOf (p);

        for (const auto& id : ParamIDs::all())
        {
            INFO (id << ": " << now.at (id) << " (stored " << first.at (id) << ", then " << second.at (id) << ")");

            if (id == ParamIDs::scene)
                CHECK (same (now.at (id), 1.0f));
            else if (leftAlone (id))
                CHECK (same (now.at (id), second.at (id)));
            else
                CHECK (same (now.at (id), first.at (id)));
        }
    }
}

TEST_CASE ("A scene stores every parameter but those it leaves alone", "[scenes]")
{
    testing::Plugin plugin;
    auto& p = *plugin;

    Values first, second;

    testing::onMessageThread ([&]
    {
        first = setEveryParameter (p, 0);
        REQUIRE (p.storeScene (1, "Everything") == 1);
        second = setEveryParameter (p, 1);
    });

    // So a parameter brought back, or left alone, shows.
    for (const auto& id : ParamIDs::all())
    {
        INFO (id << ": " << first.at (id) << " then " << second.at (id));
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getState().getParameter (id));
        CHECK_FALSE (same (first.at (id), second.at (id)));
        CHECK_FALSE (same (first.at (id), param->convertFrom0to1 (param->getDefaultValue())));
    }

    // The scene holds every parameter it brings back, by id, and nothing it leaves alone.
    const auto scene = testing::onMessageThread ([&]
    {
        return p.getState().state.getChildWithName (SceneIDs::tree).getChildWithProperty (SceneIDs::number, 1);
    });
    REQUIRE (scene.isValid());

    for (const auto& id : ParamIDs::all())
    {
        INFO (id);
        CHECK (SceneIDs::isStored (id) == ! leftAlone (id));
        CHECK (scene.hasProperty (id) == ! leftAlone (id));
    }

    SECTION ("recalled at once")
    {
        testing::onMessageThread ([&] { p.recallScene (1); });
        checkRecalled (p, first, second);
    }

    SECTION ("recalled after the session was saved and loaded into another instance")
    {
        const auto session = testing::onMessageThread ([&] { return saved (p); });

        testing::Plugin other;
        testing::onMessageThread ([&] { other->setStateInformation (session.getData(), static_cast<int> (session.getSize())); });

        // The session plays as saved, not as the scene it holds.
        const auto loaded = valuesOf (*other);

        for (const auto& id : ParamIDs::all())
        {
            INFO (id << ": " << loaded.at (id) << ", saved " << second.at (id));
            CHECK (same (loaded.at (id), second.at (id)));
        }

        testing::onMessageThread ([&] { other->recallScene (1); });
        checkRecalled (*other, first, second);
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

TEST_CASE ("A scene of a described room brings the room back as it was described", "[scenes][describe]")
{
    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        const auto first = p.describeRoom ("small tiled bathroom", "Loo", true);
        REQUIRE (first["place"].toString().contains ("bathroom"));
        REQUIRE (p.storeScene (0, "") == 1);

        const auto second = p.describeRoom ("stone cathedral", "", true);
        REQUIRE (second["place"].toString().contains ("cathedral"));
        REQUIRE (p.storeScene (0, "") == 2);

        chooseSpace (p, bundledSpaces[0].id);
        REQUIRE (p.storeScene (0, "") == 3);

        p.recallScene (1);
    });

    CHECK (p.chosenSpaceId() == "described");
    auto room = testing::onMessageThread ([&] { return p.describedRoomInfo(); });
    CHECK (room["name"].toString() == "Loo");
    CHECK (room["kind"].toString() == "room");
    CHECK (room["place"].toString().contains ("bathroom"));
    CHECK (room["words"].toString() == "small tiled bathroom");
    CHECK (static_cast<double> (room["H"]) < 3.0);

    testing::onMessageThread ([&] { p.recallScene (2); });
    room = testing::onMessageThread ([&] { return p.describedRoomInfo(); });
    CHECK (room["kind"].toString() == "chapel");
    CHECK (room["place"].toString().contains ("cathedral"));
    CHECK (static_cast<double> (room["H"]) > 10.0);

    // The scene list names each by the room's name.
    const auto list = testing::onMessageThread ([&] { return p.sceneList(); });
    REQUIRE (list.size() == 3);
    CHECK (list[0]["space"].toString() == "Loo");
    CHECK (list[1]["space"].toString().isNotEmpty());
    CHECK (list[1]["space"].toString() != "Loo");
    CHECK (list[2]["space"].toString() == juce::String (bundledSpaces[0].name));

    // Recalling the modelled space leaves the described room where it was, to come back to.
    testing::onMessageThread ([&] { p.recallScene (3); });
    CHECK (p.chosenSpaceId() == juce::String (bundledSpaces[0].id));
    room = testing::onMessageThread ([&] { return p.describedRoomInfo(); });
    CHECK (room["place"].toString().contains ("cathedral"));
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

TEST_CASE ("Automating the scene in a bounce changes the room at that block", "[scenes][offline]")
{
    // Bouncing runs faster than real time, so a room that changes when the interface's
    // timer next notices, or once a file has loaded in the background, lands somewhere
    // different in every bounce. The block the scene changes at plays the new room.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-scene-bounce");
    dir.deleteRecursively();
    const auto irFile = writePlate (dir);

    REQUIRE_FALSE (library::spaces().empty());
    const auto libraryId = library::spaces().back().id;

    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        chooseSpace (p, bundledSpaces[0].id);
        REQUIRE (p.storeScene (0, "Studio") == 1);
        chooseSpace (p, libraryId);
        REQUIRE (p.storeScene (0, "Recorded") == 2);
        p.getUiState().setProperty (UiIDs::irPath, irFile.getFullPathName(), nullptr);
        p.getUiState().setProperty (UiIDs::irName, "plate", nullptr);
        chooseSpace (p, "user");
        REQUIRE (p.storeScene (0, "Own file") == 3);
        p.recallScene (1);

        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 512);
    });

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    const auto playBlockAt = [&] (float scene)
    {
        return testing::onMessageThread ([&]
        {
            set (p, ParamIDs::scene, scene);
            const auto before = p.responseVersion();
            buffer.clear();
            p.processBlock (buffer, midi);
            return p.responseVersion() != before;
        });
    };

    CHECK_FALSE (playBlockAt (1.0f));

    CHECK (playBlockAt (2.0f));
    CHECK (p.chosenSpaceId() == libraryId);

    CHECK (playBlockAt (3.0f));
    CHECK (p.chosenSpaceId() == "user");
    const auto info = testing::onMessageThread ([&] { return p.impulseResponseInfo(); });
    CHECK_FALSE (static_cast<bool> (info["missing"]));
    CHECK (p.getUiState().getProperty (UiIDs::irPath).toString() == irFile.getFullPathName());

    CHECK (playBlockAt (1.0f));
    CHECK (p.chosenSpaceId() == juce::String (bundledSpaces[0].id));

    // An empty scene number changes nothing, and isn't tried again every block.
    CHECK_FALSE (playBlockAt (9.0f));
    CHECK_FALSE (playBlockAt (9.0f));

    testing::onMessageThread ([&] { p.releaseResources(); });
    dir.deleteRecursively();
}

TEST_CASE ("A bounce whose host holds the message thread doesn't hang on a scene change", "[scenes][offline]")
{
    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        chooseSpace (p, bundledSpaces[0].id);
        REQUIRE (p.storeScene (0, "Studio") == 1);
        chooseSpace (p, bundledSpaces[2].id);
        REQUIRE (p.storeScene (0, "Chapel") == 2);
        p.recallScene (1);
        p.setNonRealtime (true);
        p.prepareToPlay (48000.0, 512);
        set (p, ParamIDs::scene, 2.0f);
    });

    // The test holds the message thread, as a host may while it bounces on another.
    std::array<double, 2> seconds {};

    std::thread bounce ([&]
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (auto& s : seconds)
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            buffer.clear();
            p.processBlock (buffer, midi);
            s = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        }
    });

    bounce.join();

    // The first block waits for the message thread for its full 2 s, as it is held. Waiting
    // again would take the next block as long; playing it takes milliseconds, even under
    // sanitizers, so half the wait tells the two apart with room to spare.
    CHECK (seconds[0] < 5.0);
    CHECK (seconds[1] < 1.0); // not waited for again

    testing::onMessageThread ([&] { p.releaseResources(); });
}

TEST_CASE ("Going back to a scene's room plays it at once", "[scenes]")
{
    // The plugin holds one recorded space and one file of your own at a time. Recordings
    // played recently also stay prepared (see RecordingCache), so a scene recalled again
    // changes rooms without waiting for its recording to be read.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-scene-again");
    dir.deleteRecursively();
    const auto plate = writePlate (dir);
    const auto otherPlate = dir.getChildFile ("other plate.wav");
    REQUIRE (plate.copyFileTo (otherPlate));
    REQUIRE (library::spaces().size() >= 2);
    const auto firstId = library::spaces()[0].id, secondId = library::spaces()[1].id;

    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        chooseSpace (p, firstId);
        REQUIRE (p.storeScene (0, "First") == 1);
        chooseSpace (p, secondId);
        REQUIRE (p.storeScene (0, "Second") == 2);

        for (const auto& file : { plate, otherPlate })
        {
            p.getUiState().setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
            p.getUiState().setProperty (UiIDs::irName, file.getFileNameWithoutExtension(), nullptr);
            chooseSpace (p, "user");
            p.storeScene (0, file.getFileNameWithoutExtension());
        }

        // Each played once.
        p.prepareToPlay (48000.0, 512);

        for (int scene = 1; scene <= 4; ++scene)
            p.recallScene (scene, true);
    });

    // The tests run no message loop, so anything still to be read in the background never
    // would be: the room changes only if nothing needs reading.
    const auto builtAfter = [&p] (int scene)
    {
        const auto before = p.responseVersion();
        testing::onMessageThread ([&] { p.recallScene (scene); });
        REQUIRE (testing::builderIdle (p));
        return p.responseVersion() != before;
    };

    CHECK (builtAfter (3));
    CHECK (p.getUiState().getProperty (UiIDs::irPath).toString() == plate.getFullPathName());
    CHECK (testing::onMessageThread ([&] { return p.impulseResponseInfo(); })["name"].toString() == "plate");

    CHECK (builtAfter (1));
    CHECK (p.chosenSpaceId() == firstId);

    testing::onMessageThread ([&] { p.releaseResources(); });
    dir.deleteRecursively();
}

TEST_CASE ("A scene whose file is still being read leaves the room as it was", "[scenes]")
{
    // A file not played recently is read in the background. Until it has been, the room
    // stays as it was, rather than playing the last file with the new scene's settings.
    // Each section runs this again, writing the same files again within the second, so
    // they would still be prepared from the run before (RecordingCache knows a file by its
    // path, size and time to the second): none has been played yet in this run.
    RecordingCache::shared().clear();
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-scene-reading");
    dir.deleteRecursively();
    const auto plate = writePlate (dir);
    const auto unplayed = dir.getChildFile ("unplayed plate.wav"), later = dir.getChildFile ("later plate.wav");
    REQUIRE (plate.copyFileTo (unplayed));
    REQUIRE (plate.copyFileTo (later));
    REQUIRE (! library::spaces().empty());
    const auto spaceId = library::spaces()[0].id;

    testing::Plugin plugin;
    auto& p = *plugin;

    testing::onMessageThread ([&]
    {
        for (const auto& file : { plate, unplayed, later })
        {
            p.getUiState().setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
            p.getUiState().setProperty (UiIDs::irName, file.getFileNameWithoutExtension(), nullptr);
            chooseSpace (p, "user");
            set (p, ParamIDs::size, file == plate ? 100.0f : 150.0f);
            p.storeScene (0, file.getFileNameWithoutExtension()); // 1, 2, 3
        }

        chooseSpace (p, spaceId);
        REQUIRE (p.storeScene (0, "Recorded") == 4);

        p.prepareToPlay (48000.0, 512);
        p.recallScene (1, true);
    });

    // The tests run no message loop, so the background read never comes back by itself:
    // sceneImpulseResponseRead below stands in for it.
    // Whether a change built a new room, once the builder has finished with it (so a
    // change that builds nothing is known not to without waiting to see).
    const auto builtAfter = [&p] (const std::function<void()>& change)
    {
        const auto before = p.responseVersion();
        testing::onMessageThread (change);
        REQUIRE (testing::builderIdle (p));
        return p.responseVersion() != before;
    };

    const auto read = [&p] (const juce::File& file)
    {
        juce::String error;
        auto loaded = DrenchedProcessor::readImpulseResponse (file, error);
        REQUIRE (loaded != nullptr);
        p.sceneImpulseResponseRead (file, std::move (loaded), error);
    };

    // Until the builder has finished with everything asked of it so far.
    const auto settle = [&p] { REQUIRE (testing::builderIdle (p)); };

    REQUIRE (builtAfter ([&] { p.recallScene (1); }));
    settle();

    CHECK_FALSE (builtAfter ([&] { p.recallScene (2); }));
    CHECK (near (valueOf (p, ParamIDs::size), 150.0f));
    CHECK (p.getUiState().getProperty (UiIDs::irName).toString() == "unplayed plate");

    CHECK (builtAfter ([&] { read (unplayed); }));
    CHECK (testing::onMessageThread ([&] { return p.impulseResponseInfo(); })["name"].toString() == "unplayed plate");

    SECTION ("another scene chosen meanwhile stays chosen")
    {
        testing::onMessageThread ([&]
        {
            p.recallScene (3);
            p.recallScene (4);
            read (later);
        });

        CHECK (p.chosenSpaceId() == spaceId);
    }

    SECTION ("another file chosen meanwhile stays chosen")
    {
        testing::onMessageThread ([&]
        {
            p.recallScene (3);
            p.recallScene (1); // played, so at once
            read (later);
        });

        CHECK (p.getUiState().getProperty (UiIDs::irName).toString() == "plate");
        CHECK (testing::onMessageThread ([&] { return p.impulseResponseInfo(); })["name"].toString() == "plate");
    }

    SECTION ("a file that can't be read is missing, as in a session")
    {
        const auto gone = dir.getChildFile ("gone.wav");

        testing::onMessageThread ([&]
        {
            p.getUiState().setProperty (UiIDs::irPath, gone.getFullPathName(), nullptr);
            p.getUiState().setProperty (UiIDs::irName, "gone", nullptr);
            chooseSpace (p, "user");
            REQUIRE (p.storeScene (0, "Gone") == 5);
            p.recallScene (1);
            p.recallScene (5);
        });

        settle();
        CHECK (builtAfter ([&] { p.sceneImpulseResponseRead (gone, nullptr, "Drenched can't find gone.wav."); }));

        const auto info = testing::onMessageThread ([&] { return p.impulseResponseInfo(); });
        CHECK (static_cast<bool> (info["missing"]));
        CHECK (info["name"].toString() == "gone");
    }

    testing::onMessageThread ([&] { p.releaseResources(); });
    dir.deleteRecursively();
}

#include "PluginTestHelpers.h"

#include <Library.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <cmath>
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

    CHECK (seconds[0] < 5.0);
    CHECK (seconds[1] < 0.5); // not waited for again

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

        for (int i = 0; i < 300 && p.responseVersion() == before; ++i)
            juce::Thread::sleep (10);

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
    const auto builtAfter = [&p] (const std::function<void()>& change)
    {
        const auto before = p.responseVersion();
        testing::onMessageThread (change);

        for (int i = 0; i < 300 && p.responseVersion() == before; ++i)
            juce::Thread::sleep (10);

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
    const auto settle = [&p]
    {
        for (auto seen = p.responseVersion();;)
        {
            juce::Thread::sleep (300);

            if (const auto now = p.responseVersion(); now != seen)
                seen = now;
            else
                return;
        }
    };

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

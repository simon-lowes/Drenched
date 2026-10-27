#include "PluginTestHelpers.h"

#include <MovedFiles.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace drenched;

/*  A session that plays the person's own impulse response names its file by path. Sessions
    travel between computers and accounts, and files get moved, so a file that isn't where
    the session says is looked for where it most likely is now, and the session plays as
    it was saved. */

namespace
{
    juce::File scratch (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    juce::File touch (const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText ("x");
        return file;
    }

    /** A second of decaying noise as a 32-bit float WAV file. */
    juce::File writeImpulseResponse (const juce::File& file)
    {
        constexpr double rate = 48000.0;
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 1, 32, {}, 0));
        REQUIRE (writer != nullptr);

        std::mt19937 rng (3u);
        std::normal_distribution<float> noise;
        juce::AudioBuffer<float> buffer (1, static_cast<int> (rate));

        for (int n = 0; n < buffer.getNumSamples(); ++n)
            buffer.setSample (0, n, noise (rng) * std::exp (-6.9f * static_cast<float> (n) / static_cast<float> (rate)));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_CASE ("A file that moved is found where it most likely is now", "[moved]")
{
    const auto dir = scratch ("drenched-moved-files");
    const auto home = dir.getChildFile ("home");
    const auto folder = dir.getChildFile ("last-folder");
    folder.createDirectory();

    SECTION ("where the session says, when it's there")
    {
        const auto file = touch (dir.getChildFile ("there.wav"));
        CHECK (movedFiles::find (file.getFullPathName(), home, { folder }) == file);
    }

    SECTION ("under this account's home folder, from any computer")
    {
        const auto file = touch (home.getChildFile ("Music").getChildFile ("IRs").getChildFile ("hall.wav"));

        for (const auto* saved : { "/Users/alice/Music/IRs/hall.wav",
                                   "C:\\Users\\alice\\Music\\IRs\\hall.wav",
                                   "D:/Users/Bob Smith/Music/IRs/hall.wav",
                                   "/home/alice/Music/IRs/hall.wav" })
        {
            INFO (saved);
            CHECK (movedFiles::find (saved, home, { folder }) == file);
        }
    }

    SECTION ("in a folder given, by name")
    {
        const auto file = touch (folder.getChildFile ("plate.wav"));
        CHECK (movedFiles::find ("/Volumes/Old drive/IRs/plate.wav", home, { dir.getChildFile ("nowhere"), folder }) == file);
        CHECK (movedFiles::find ("E:\\IRs\\plate.wav", home, { folder }) == file);
    }

    SECTION ("nowhere")
    {
        for (const auto* saved : { "", "/", "\\", "/Users/alice/Music/IRs/gone.wav", "relative.wav", "/a/../plate.wav" })
        {
            INFO (saved);
            CHECK (movedFiles::find (saved, home, { folder }) == juce::File());
        }
    }

    dir.deleteRecursively();
}

TEST_CASE ("A session whose impulse response moved plays it with the session's settings", "[moved][plugin]")
{
    // The same place under this account's home folder, as if the session came from another
    // computer where the account had another name.
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto folder = home.getChildFile (".drenched-moved-test");
    folder.deleteRecursively();
    const auto file = writeImpulseResponse (folder.getChildFile ("hall.wav"));
    const juce::String saved ("/Users/someone-else/.drenched-moved-test/hall.wav");

    juce::MemoryBlock session;

    {
        testing::Plugin fresh;
        auto ui = fresh->getUiState();
        ui.setProperty (UiIDs::spaceId, "user", nullptr);
        ui.setProperty (UiIDs::irPath, saved, nullptr);
        ui.setProperty (UiIDs::irName, "hall", nullptr);

        auto* decay = fresh->getState().getParameter (ParamIDs::decay[0]);
        REQUIRE (decay != nullptr);
        decay->setValueNotifyingHost (decay->convertTo0to1 (7.5f));

        testing::onMessageThread ([&] { fresh->getStateInformation (session); });
    }

    testing::Plugin p;
    testing::onMessageThread ([&] { p->setStateInformation (session.getData(), static_cast<int> (session.getSize())); });

    const auto info = testing::onMessageThread ([&] { return p->impulseResponseInfo(); });
    CHECK_FALSE (static_cast<bool> (info["missing"]));
    CHECK (p->getUiState().getProperty (UiIDs::irPath).toString() == file.getFullPathName());

    // Found, not loaded afresh: the decay the session was saved with stays.
    CHECK (p->getState().getRawParameterValue (ParamIDs::decay[0])->load() == Catch::Approx (7.5f).epsilon (1.0e-4));

    folder.deleteRecursively();
}

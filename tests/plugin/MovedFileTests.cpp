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

    SECTION ("on a network share only if this person has used it")
    {
       #if JUCE_WINDOWS
        // A computer that doesn't exist: nothing here may try to reach it.
        const juce::String asShare ("\\\\drenched-test-nas\\IRs\\hall.wav");
        const auto share = movedFiles::shareOf (asShare);
        CHECK (share == "\\\\drenched-test-nas\\irs");
        CHECK (movedFiles::find (asShare, home, { folder }) == juce::File());
       #else
        // On Linux and macOS //tmp/... (or //var/folders/...) is a local path, so the file is
        // really there; it is still taken for a share (\\tmp\<dir>) and left alone until that
        // share is trusted.
        const auto file = touch (dir.getChildFile ("share").getChildFile ("hall.wav"));
        const auto asShare = "/" + file.getFullPathName();
        const auto share = movedFiles::shareOf (asShare);
        auto parts = juce::StringArray::fromTokens (file.getFullPathName(), "/", "");
        parts.removeEmptyStrings();
        REQUIRE (parts.size() > 2);
        CHECK (share == ("\\\\" + parts[0] + "\\" + parts[1]).toLowerCase());
        CHECK (movedFiles::find (asShare, home, { folder }) == juce::File());
        CHECK (movedFiles::find (asShare, home, { folder }, { share }) == juce::File (asShare));
       #endif
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

TEST_CASE ("Paths that name another computer are told apart from local ones", "[moved]")
{
    for (const auto* path : { "\\\\nas\\IRs\\hall.wav", "//nas/IRs/hall.wav", "\\\\?\\UNC\\nas\\IRs\\hall.wav",
                              "\\\\nas@SSL\\DavWWWRoot\\hall.wav", "\\\\.\\pipe\\x", "/\\nas\\IRs\\hall.wav" })
    {
        INFO (path);
        CHECK (movedFiles::isNetworkPath (path));
        CHECK_FALSE (movedFiles::mayOpen (path, {}));
    }

    for (const auto* path : { "C:\\IRs\\hall.wav", "\\\\?\\C:\\IRs\\hall.wav", "/Volumes/NAS/hall.wav", "/home/a/hall.wav", "hall.wav", "" })
    {
        INFO (path);
        CHECK_FALSE (movedFiles::isNetworkPath (path));
        CHECK (movedFiles::mayOpen (path, {}));
    }

    CHECK (movedFiles::shareOf ("\\\\NAS\\IRs\\Halls\\hall.wav") == "\\\\nas\\irs");
    CHECK (movedFiles::shareOf ("\\\\?\\UNC\\NAS\\IRs\\hall.wav") == "\\\\nas\\irs");
    CHECK (movedFiles::shareOf ("\\\\.\\pipe\\x").isEmpty());
    CHECK (movedFiles::shareOf ("\\\\nas").isEmpty());
    CHECK (movedFiles::mayOpen ("//NAS/irs/hall.wav", { "\\\\nas\\irs" }));
    CHECK_FALSE (movedFiles::mayOpen ("//NAS/other/hall.wav", { "\\\\nas\\irs" }));
    CHECK_FALSE (movedFiles::mayOpen ("\\\\.\\pipe\\x", { "\\\\.\\pipe" }));
}

TEST_CASE ("A session can't make the plugin open a file on a share this person hasn't used", "[moved][plugin]")
{
    // The plugin reads a session's file while the session loads, with no one asked: on
    // Windows that would sign in to whatever computer the session names.
    juce::String error;
   #if JUCE_WINDOWS
    // A computer that doesn't exist: refused before any attempt to reach it.
    CHECK (DrenchedProcessor::readImpulseResponse (juce::File ("\\\\drenched-test-nas\\IRs\\hall.wav"), error) == nullptr);
    CHECK (error.contains ("network share"));
   #else
    // //tmp/... is local here, so the file is really there and only the check refuses it.
    const auto dir = scratch ("drenched-untrusted-share");
    const auto file = touch (dir.getChildFile ("hall.wav"));
    CHECK (DrenchedProcessor::readImpulseResponse (juce::File ("/" + file.getFullPathName()), error) == nullptr);
    CHECK (error.contains ("network share"));
    dir.deleteRecursively();
   #endif
}

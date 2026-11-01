#include "MessageLoop.h"
#include "PluginTestHelpers.h"

#include <DroppedFile.h>
#include <Library.h>
#include <RecordingCache.h>
#include <Settings.h>

#include <drenched/ImpulseResponseSynth.h>
#include <drenched/PartitionedConvolver.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>

using namespace drenched;

/*  What the plugin finishes on the message thread: a library space read in the background
    once it is chosen in the browser, a room matched, the decay displays' analysis, a
    capture saved and a dropped file kept, each handed back with MessageManager::callAsync.
    These tests run the message loop (see MessageLoop.h) and the plugin's timer with it, as
    a host does, so those completions run as they do there. */

namespace
{
    void chooseSpace (DrenchedProcessor& p, const juce::String& id)
    {
        p.getUiState().setProperty (UiIDs::spaceId, id, nullptr);
    }

    juce::String nameOf (const LibrarySpace& space)
    {
        return space.info["name"].toString();
    }

    /** The name of the recording the playing room was built from, or "" for none. */
    juce::String playingName (const DrenchedProcessor& p)
    {
        const auto recording = p.playingRecording();
        return recording != nullptr ? juce::String (recording->name) : juce::String();
    }

    /** Lets the plugin's timer see the interface state as it is now, without dispatching
        anything else: like a person clicking faster than recordings load, whose loads
        are still waiting for the message thread when the next click comes. A tick missed
        only means that choice wasn't seen, as with a click between two ticks. */
    void letTimerTick()
    {
        juce::Thread::sleep (60); // the timer runs at 20 Hz
        juce::Timer::callPendingTimersSynchronously();
    }

    /** Runs the message loop until the plugin has finished what it was doing when this was
        called: no new room built for 300 ms. */
    void settle (DrenchedProcessor& p)
    {
        for (auto seen = p.responseVersion();;)
        {
            testing::pumpFor (300);

            if (const auto now = p.responseVersion(); now != seen)
                seen = now;
            else
                return;
        }
    }

    juce::File scratch (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    /** Bursts of noise with pauses, played in a modelled space: a recording to match. */
    juce::File recordingIn (int space, const juce::File& file)
    {
        constexpr double rate = 44100.0;
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = space;
        r.decaySeconds = bundledSpaces[static_cast<size_t> (space)].decaySeconds;
        const auto room = synthesiseImpulseResponse (r);

        const auto total = static_cast<size_t> (12.0 * rate);
        auto dry = testing::noise (total + room.length(), 3u);

        for (size_t n = 0; n < dry.size(); ++n)
            if (n >= total || (n % static_cast<size_t> (rate)) > static_cast<size_t> (0.3 * rate))
                dry[n] = 0.0f;

        juce::AudioBuffer<float> buffer (2, static_cast<int> (dry.size()));

        for (int ch = 0; ch < 2; ++ch)
        {
            PartitionedConvolver convolver ({ std::span<const float> (room.paths[ch == 0 ? ImpulseResponseSet::leftToLeft : ImpulseResponseSet::leftToRight]) });
            float* out[] { buffer.getWritePointer (ch) };
            convolver.process (dry.data(), out, dry.size());
        }

        buffer.applyGain (0.25f);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (file.createOutputStream().release(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    /** Half a second of decaying noise as a 24-bit stereo WAV file, in memory. */
    juce::MemoryBlock impulseResponseFile()
    {
        constexpr double rate = 48000.0;
        juce::MemoryBlock block;
        juce::WavAudioFormat wav;

        {
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::MemoryOutputStream (block, false), rate, 2, 24, {}, 0));
            REQUIRE (writer != nullptr);

            juce::AudioBuffer<float> buffer (2, static_cast<int> (rate / 2));
            juce::Random random (11);

            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < buffer.getNumSamples(); ++n)
                    buffer.setSample (ch, n, (random.nextFloat() * 2.0f - 1.0f) * std::exp (-9.0f * static_cast<float> (n) / static_cast<float> (rate)));

            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        return block;
    }

    float decayOf (DrenchedProcessor& p, size_t band)
    {
        return p.getState().getRawParameterValue (ParamIDs::decay[band])->load();
    }

    /** On Linux, HOME is an empty folder while this lives, so what the plugin saves in the
        person's folders goes there. Elsewhere it changes nothing. */
    struct ScopedHome
    {
        ScopedHome()
        {
           #if JUCE_LINUX || JUCE_BSD
            if (const auto* old = std::getenv ("HOME"))
                previous = juce::String (old);

            setenv ("HOME", folder.getFullPathName().toRawUTF8(), 1);
           #endif
        }

        ~ScopedHome()
        {
           #if JUCE_LINUX || JUCE_BSD
            if (previous.has_value())
                setenv ("HOME", previous->toRawUTF8(), 1);
            else
                unsetenv ("HOME");
           #endif

            folder.deleteRecursively();
        }

        const juce::File folder = scratch ("drenched-test-home");
        std::optional<juce::String> previous;
    };
}

TEST_CASE ("A library space chosen in the browser is read in the background, then plays", "[plugin][messageloop][library]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    const auto& spaces = library::spaces();
    REQUIRE (spaces.size() >= 2);
    RecordingCache::shared().clear(); // or an earlier test's read would play at once

    testing::Plugin plugin;
    auto& p = *plugin;
    p.prepareToPlay (48000.0, 512);
    settle (p);
    REQUIRE (playingName (p) == nameOf (spaces.front()));

    const auto& chosen = spaces.back();

    SECTION ("playing") {}

    // Offline the timer builds nothing, so the room changes only if the load asks for it.
    SECTION ("bouncing, the load asks for the room to be built itself")
    {
        p.setNonRealtime (true);
    }

    const auto before = p.responseVersion();
    chooseSpace (p, chosen.id);

    // Nothing is read on the message thread: until the recording is ready the room stays.
    CHECK (p.responseVersion() == before);
    CHECK (playingName (p) == nameOf (spaces.front()));

    REQUIRE (testing::pumpUntil ([&] { return p.responseVersion() != before; }));
    CHECK (playingName (p) == nameOf (chosen));
    CHECK (p.chosenSpaceId() == chosen.id);

    p.releaseResources();
}

TEST_CASE ("A new plugin prepared at once reads its first space once and builds its room once", "[plugin][messageloop][library]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    RecordingCache::shared().clear();

    for (int run = 0; run < 3; ++run)
    {
        INFO ("run " << run);
        RecordingCache::shared().clear();

        // Opening starts reading the first space in the background; a host then prepares
        // the plugin at once, which reads it there and then so playback starts with it.
        // The background read lands later, and must not put a second copy in its place
        // and build the same room again.
        testing::Plugin plugin;
        auto& p = *plugin;
        p.prepareToPlay (48000.0, 512);

        const auto version = p.responseVersion();
        const auto recording = p.playingRecording();
        REQUIRE (recording != nullptr);

        testing::pumpFor (1000);
        CHECK (p.responseVersion() == version);
        CHECK (p.playingRecording() == recording);
        p.releaseResources();
    }
}

TEST_CASE ("Clicking through library spaces faster than they load plays only the last one", "[plugin][messageloop][library]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    const auto& spaces = library::spaces();
    REQUIRE (spaces.size() >= 21);
    RecordingCache::shared().clear();

    testing::Plugin plugin;
    auto& p = *plugin;
    p.prepareToPlay (48000.0, 512);
    settle (p);
    REQUIRE (playingName (p) == nameOf (spaces.front()));

    // Twenty clicks, each seen by the timer, none of whose loads has landed yet.
    const auto before = p.responseVersion();

    for (size_t i = 1; i <= 20; ++i)
    {
        chooseSpace (p, spaces[i].id);
        letTimerTick();
    }

    // The last click must have been seen before anything lands; seeing it again does nothing.
    for (int i = 0; i < 3; ++i)
        letTimerTick();

    const auto& last = spaces[20];
    std::set<juce::String> played;

    const auto lastPlays = testing::pumpUntil ([&]
    {
        played.insert (playingName (p));
        return playingName (p) == nameOf (last);
    }, 20000);

    REQUIRE (lastPlays);

    // Anything still on its way lands now, and changes nothing.
    settle (p);
    played.insert (playingName (p));

    CHECK (playingName (p) == nameOf (last));
    CHECK (p.chosenSpaceId() == last.id);
    CHECK (played == std::set<juce::String> { nameOf (spaces.front()), nameOf (last) });
    CHECK (p.responseVersion() - before >= 1);
    CHECK (p.responseVersion() - before <= 2); // the skipped ones weren't built

    p.releaseResources();
}

TEST_CASE ("A plugin closed while a library space loads is left alone by the load", "[plugin][messageloop][library]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    const auto& spaces = library::spaces();
    REQUIRE (spaces.size() >= 2);
    RecordingCache::shared().clear();

    // The longest recording, so its read is still going when the plugin closes.
    const auto& longest = *std::max_element (spaces.begin() + 1, spaces.end(),
                                             [] (const auto& a, const auto& b) { return a.size < b.size; });
    const auto key = RecordingCache::keyOf (longest.id);

    auto plugin = std::make_unique<testing::Plugin>();
    auto& p = **plugin;
    p.prepareToPlay (48000.0, 512);
    settle (p);

    // Recalling a scene starts the read at once, where the browser's choice waits for the
    // timer, so the read is sure to have started.
    chooseSpace (p, longest.id);
    REQUIRE (p.storeScene (0, "Long") == 1);
    chooseSpace (p, spaces.front().id);
    p.recallScene (1);
    REQUIRE (p.chosenSpaceId() == longest.id);

    SECTION ("closed while the recording is read") {}

    SECTION ("closed with the read recording waiting for the message thread")
    {
        // It is kept for later before it is handed over; the plugin's destructor then
        // waits for the hand-over to be posted.
        for (int i = 0; i < 1000 && RecordingCache::shared().find (key) == nullptr; ++i)
            juce::Thread::sleep (5);

        REQUIRE (RecordingCache::shared().find (key) != nullptr);
    }

    plugin.reset();
    testing::pumpFor (1000); // the hand-over runs, finds the plugin gone and does nothing

    // A plugin opened afterwards plays as usual.
    testing::Plugin next;
    next->prepareToPlay (48000.0, 512);
    CHECK (next->responseVersion() > 0);
    next->releaseResources();
}

TEST_CASE ("Match a room answers on the message thread with what the recording says", "[plugin][messageloop][match]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    const auto dir = scratch ("drenched-match-async");
    const auto file = recordingIn (2, dir.getChildFile ("chapel recording.wav"));

    auto plugin = std::make_unique<testing::Plugin>();
    juce::var answer;
    int answers = 0;
    bool onMessageThread = false;

    const auto match = [&] (const juce::File& f)
    {
        (*plugin)->matchRoomAsync (f, [&] (const juce::var& result)
        {
            answer = result;
            ++answers;
            onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
        });
    };

    SECTION ("a recording")
    {
        match (file);
        CHECK (answers == 0); // listened to in the background

        REQUIRE (testing::pumpUntil ([&] { return answers > 0; }, 60000));
        testing::pumpFor (100);
        CHECK (answers == 1);
        CHECK (onMessageThread);

        REQUIRE (answer["error"].isVoid());
        CHECK (answer["file"].toString() == file.getFileName());
        CHECK (answer["bands"].size() == 8);
        CHECK (answer["spaceId"].toString().isNotEmpty());
        CHECK (answer["spaceName"].toString().isNotEmpty());
        CHECK (static_cast<double> (answer["seconds"]) > 10.0);

        // Exactly what matching at once finds.
        CHECK (juce::JSON::toString (answer) == juce::JSON::toString (DrenchedProcessor::matchRecording (file)));
    }

    SECTION ("a file that isn't there")
    {
        match (dir.getChildFile ("gone.wav"));
        REQUIRE (testing::pumpUntil ([&] { return answers > 0; }));
        CHECK (answer["error"].toString().isNotEmpty());
        CHECK (answer["bands"].isVoid());
    }

    SECTION ("the plugin closed before the answer")
    {
        // Once the read the plugin starts on opening is done, so the listening starts at
        // once rather than waiting behind it (a job not started is simply dropped).
        settle (**plugin);
        match (file);
        juce::Thread::sleep (100);
        plugin.reset(); // waits for the listening to finish
        testing::pumpFor (500);
        CHECK (answers == 0);
    }

    plugin.reset();
    dir.deleteRecursively();
}

TEST_CASE ("The decay displays get the playing room's analysis on the message thread", "[plugin][messageloop][displays]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    const auto& spaces = library::spaces();
    REQUIRE (spaces.size() >= 2);

    auto plugin = std::make_unique<testing::Plugin>();
    auto& p = **plugin;
    std::vector<juce::var> answers;

    const auto analyse = [&]
    {
        p.analyseResponseAsync ([&] (const juce::var& result) { answers.push_back (result); });
    };

    const auto analysed = [&]
    {
        const auto count = answers.size();
        analyse();
        CHECK (answers.size() == count); // in the background
        REQUIRE (testing::pumpUntil ([&] { return answers.size() > count; }));
        return answers.back();
    };

    SECTION ("nothing built yet: nothing to show")
    {
        CHECK (analysed().isVoid());
    }

    SECTION ("the room playing, and the next one")
    {
        p.prepareToPlay (48000.0, 512);
        settle (p);

        const auto first = analysed();
        REQUIRE (first.isObject());
        CHECK (first["bands"].size() == static_cast<int> (analysisBands.size()));
        CHECK (first["waterfall"].size() == static_cast<int> (ResponseAnalysis::numSlices));
        CHECK (first["decay"].size() == 200);
        CHECK (first["rt60"].size() == 8);
        CHECK (static_cast<double> (first["seconds"]) > 0.0);

        CHECK (juce::JSON::toString (analysed()) == juce::JSON::toString (first));

        const auto before = p.responseVersion();
        chooseSpace (p, spaces.back().id);
        REQUIRE (testing::pumpUntil ([&] { return p.responseVersion() != before; }));
        CHECK (juce::JSON::toString (analysed()) != juce::JSON::toString (first));

        p.releaseResources();
    }

    SECTION ("the plugin closed before the answer")
    {
        p.prepareToPlay (48000.0, 512);
        analyse();
        juce::Thread::sleep (100); // started, so the plugin waits for it as it closes
        plugin.reset();
        testing::pumpFor (500);
        CHECK (answers.empty());
    }
}

TEST_CASE ("A dropped impulse response is kept, read in the background, then plays", "[plugin][messageloop][drop]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    const auto dir = scratch ("drenched-drop-async");
    const auto folder = dir.getChildFile ("Imported");
    const auto data = impulseResponseFile();

    testing::Plugin plugin;
    auto& p = *plugin;
    p.prepareToPlay (48000.0, 512);
    settle (p);

    auto ui = p.getUiState();
    const auto events = static_cast<int> (ui.getProperty (UiIDs::irEvent, 0));
    const auto spaceBefore = p.chosenSpaceId();
    const auto decayBefore = decayOf (p, 4);
    const auto answered = [&] { return static_cast<int> (ui.getProperty (UiIDs::irEvent, 0)) != events; };

    // As the editor hands it over: the pieces the page sends, then the whole file.
    const auto drop = [&] (const juce::MemoryBlock& bytes, bool keepSettings)
    {
        DroppedFile dropped;
        REQUIRE (dropped.start (1, "garage.wav", static_cast<juce::int64> (bytes.getSize()), keepSettings));

        for (size_t at = 0; at < bytes.getSize(); at += 30000)
        {
            const juce::MemoryBlock piece (static_cast<const char*> (bytes.getData()) + at, std::min<size_t> (30000, bytes.getSize() - at));
            REQUIRE (dropped.add (1, juce::Base64::toBase64 (piece.getData(), piece.getSize())));
        }

        REQUIRE (dropped.complete (1));
        const auto name = dropped.fileName();
        p.importImpulseResponseAsync (name, dropped.take(), dropped.keepsSettings(), folder);
        CHECK_FALSE (answered()); // kept and read in the background
    };

    SECTION ("a file to play")
    {
        const auto keepSettings = GENERATE (false, true);
        INFO ("keeping the settings: " << (keepSettings ? "yes" : "no"));
        drop (data, keepSettings);
        REQUIRE (testing::pumpUntil (answered));

        const auto kept = folder.getChildFile ("garage.wav");
        REQUIRE (kept.existsAsFile());
        juce::MemoryBlock read;
        REQUIRE (kept.loadFileAsData (read));
        CHECK (read == data);

        CHECK_FALSE (ui.hasProperty (UiIDs::irError));
        CHECK (ui.getProperty (UiIDs::irPath).toString() == kept.getFullPathName());
        CHECK (ui.getProperty (UiIDs::irName).toString() == "garage");
        CHECK (p.chosenSpaceId() == "user");
        CHECK (p.impulseResponseInfo()["name"].toString() == "garage");

        REQUIRE (testing::pumpUntil ([&] { return playingName (p) == "garage"; }));

        // The graph opens on the file's own decay, unless the settings are kept.
        const auto measured = p.playingRecording()->room.decaySeconds[4];

        CHECK (std::abs (decayOf (p, 4) - (keepSettings ? decayBefore : measured)) < 0.05f);
    }

    SECTION ("a file that can't be kept")
    {
        p.importImpulseResponseAsync ("garage.wav", {}, false, folder);
        CHECK_FALSE (answered());
        REQUIRE (testing::pumpUntil (answered));

        CHECK (ui.getProperty (UiIDs::irError).toString().contains ("empty"));
        CHECK (p.chosenSpaceId() == spaceBefore);
        settle (p);
        CHECK (playingName (p) != "garage");
    }

    p.releaseResources();
    dir.deleteRecursively();
}

TEST_CASE ("A captured space is saved, then plays", "[plugin][messageloop][capture]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();

    // Captures are saved in Documents/Drenched/Captures. On Linux that is under HOME, so
    // the test gives itself an empty one; on Windows the saved file is deleted afterwards.
    ScopedHome home;
    const auto folder = settings::capturesFolder();
    const auto folderExisted = folder.isDirectory();
   #if JUCE_LINUX || JUCE_BSD
    REQUIRE (folder.isAChildOf (home.folder));
   #endif

    testing::Plugin plugin;
    auto& p = *plugin;
    p.prepareToPlay (48000.0, 512);
    settle (p);

    auto options = std::make_unique<juce::DynamicObject>();
    options->setProperty ("sweepSeconds", 2.0);
    options->setProperty ("tailSeconds", 1.0);
    options->setProperty ("levelDb", -12.0);
    p.startCapture (juce::var (options.release()));
    REQUIRE (p.captureStatus()["stage"].toString() == "playing");

    // The microphone hears the speaker a block later: three seconds of sweep and tail,
    // and a little more.
    juce::AudioBuffer<float> buffer (2, 512), played (2, 512);
    juce::MidiBuffer midi;
    played.clear();

    for (int b = 0; b < 48000 * 4 / 512; ++b)
    {
        buffer.makeCopyOf (played);
        p.processBlock (buffer, midi);
        played.makeCopyOf (buffer);
    }

    // The timer notices the take is done; the loader works it out and saves it.
    const auto stage = [&] { return p.captureStatus()["stage"].toString(); };
    REQUIRE (testing::pumpUntil ([&] { return stage() != "playing" && stage() != "working"; }, 60000));
    INFO (p.captureStatus()["message"].toString());
    REQUIRE (stage() == "idle");

    const auto saved = folder.findChildFiles (juce::File::findFiles, false, "*.wav");
    REQUIRE (! saved.isEmpty());

    if (! folderExisted)
        CHECK (saved.size() == 1);

    const auto file = *std::max_element (saved.begin(), saved.end(), [] (const auto& a, const auto& b)
    {
        return a.getCreationTime() < b.getCreationTime();
    });

    CHECK (p.captureStatus()["message"].toString().contains (file.getFileName()));

    // Then it is loaded like any other file.
    REQUIRE (testing::pumpUntil ([&] { return p.getUiState().getProperty (UiIDs::irPath).toString() == file.getFullPathName(); }));
    CHECK (p.chosenSpaceId() == "user");
    CHECK (p.impulseResponseInfo()["name"].toString() == file.getFileNameWithoutExtension());
    REQUIRE (testing::pumpUntil ([&] { return playingName (p) == file.getFileNameWithoutExtension(); }));

    p.releaseResources();
    file.deleteFile();

    if (! folderExisted)
        folder.deleteRecursively();
}

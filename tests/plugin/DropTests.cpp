#include "PluginTestHelpers.h"

#include <DroppedFile.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace drenched;

/*  A file dropped on the interface reaches the plugin as its name and its bytes, in base64
    pieces, since the web view can't say where the file is. The plugin keeps a copy, which
    the session names, and loads that. */

namespace
{
    juce::File scratch (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    juce::MemoryBlock bytes (std::initializer_list<int> values)
    {
        juce::MemoryBlock block;

        for (auto v : values)
        {
            const auto byte = static_cast<char> (v);
            block.append (&byte, 1);
        }

        return block;
    }

    juce::String base64 (const juce::MemoryBlock& block)
    {
        return juce::Base64::toBase64 (block.getData(), block.getSize());
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
            juce::Random random (5);

            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < buffer.getNumSamples(); ++n)
                    buffer.setSample (ch, n, (random.nextFloat() * 2.0f - 1.0f) * std::exp (-9.0f * static_cast<float> (n) / static_cast<float> (rate)));

            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        return block;
    }
}

TEST_CASE ("A dropped file is put together from its pieces", "[drop]")
{
    const auto whole = bytes ({ 0, 1, 2, 250, 251, 252, 253, 7, 8 });
    juce::MemoryBlock first (whole.begin(), 4), second (whole.begin() + 4, 5);
    DroppedFile dropped;

    SECTION ("in order")
    {
        REQUIRE (dropped.start (3, "hall.wav", 9, true));
        CHECK_FALSE (dropped.complete (3));
        REQUIRE (dropped.add (3, base64 (first)));
        CHECK_FALSE (dropped.complete (3));
        REQUIRE (dropped.add (3, base64 (second)));
        REQUIRE (dropped.complete (3));
        CHECK (dropped.fileName() == "hall.wav");
        CHECK (dropped.keepsSettings());
        CHECK (dropped.take() == whole);
        CHECK_FALSE (dropped.isCurrent (3));
    }

    SECTION ("ignoring pieces of another drop")
    {
        REQUIRE (dropped.start (3, "hall.wav", 9, false));
        CHECK (dropped.add (2, base64 (second)));
        REQUIRE (dropped.add (3, base64 (first)));
        REQUIRE (dropped.add (3, base64 (second)));
        CHECK_FALSE (dropped.complete (2));
        CHECK (dropped.take() == whole);
    }

    SECTION ("starting over when another drop starts")
    {
        REQUIRE (dropped.start (3, "hall.wav", 9, false));
        REQUIRE (dropped.add (3, base64 (first)));
        REQUIRE (dropped.start (4, "plate.wav", 5, false));
        REQUIRE (dropped.add (4, base64 (second)));
        CHECK (dropped.complete (4));
        CHECK (dropped.fileName() == "plate.wav");
        CHECK (dropped.take() == second);
    }

    SECTION ("an empty file")
    {
        REQUIRE (dropped.start (1, "empty.wav", 0, false));
        CHECK (dropped.complete (1));
        CHECK (dropped.take().isEmpty());
    }

    SECTION ("refusing what doesn't fit")
    {
        CHECK_FALSE (dropped.start (1, "huge.wav", DroppedFile::largest + 1, false));
        CHECK_FALSE (dropped.start (1, "negative.wav", -1, false));
        CHECK_FALSE (dropped.isCurrent (1));

        REQUIRE (dropped.start (1, "short.wav", 8, false));
        CHECK_FALSE (dropped.add (1, base64 (whole)));
        CHECK_FALSE (dropped.isCurrent (1));

        REQUIRE (dropped.start (1, "short.wav", 8, false));
        CHECK_FALSE (dropped.add (1, "not base64!"));
        CHECK_FALSE (dropped.complete (1));
    }

    SECTION ("cancelled")
    {
        REQUIRE (dropped.start (1, "hall.wav", 9, false));
        dropped.cancel (2);
        CHECK (dropped.isCurrent (1));
        dropped.cancel (1);
        CHECK_FALSE (dropped.isCurrent (1));
    }
}

TEST_CASE ("A dropped file is kept once, under its own name", "[drop]")
{
    const auto dir = scratch ("drenched-dropped-files");
    const auto folder = dir.getChildFile ("Imported");
    const auto a = bytes ({ 1, 2, 3 }), b = bytes ({ 4, 5, 6, 7 });
    juce::String error;

    const auto kept = DrenchedProcessor::keepImportedFile (folder, "hall.wav", a, error);
    REQUIRE (kept == folder.getChildFile ("hall.wav"));
    CHECK (error.isEmpty());

    juce::MemoryBlock read;
    REQUIRE (kept.loadFileAsData (read));
    CHECK (read == a);

    SECTION ("dropped again, the same file")
    {
        CHECK (DrenchedProcessor::keepImportedFile (folder, "hall.wav", a, error) == kept);
        CHECK (folder.getNumberOfChildFiles (juce::File::findFiles) == 1);
    }

    SECTION ("another file of the same name, numbered")
    {
        const auto second = DrenchedProcessor::keepImportedFile (folder, "hall.wav", b, error);
        CHECK (second == folder.getChildFile ("hall 2.wav"));
        CHECK (DrenchedProcessor::keepImportedFile (folder, "hall.wav", bytes ({ 9 }), error) == folder.getChildFile ("hall 3.wav"));
        CHECK (DrenchedProcessor::keepImportedFile (folder, "hall.wav", b, error) == second);
        juce::MemoryBlock again; // loadFileAsData adds to what a block holds
        CHECK (kept.loadFileAsData (again));
        CHECK (again == a);
    }

    SECTION ("names that aren't plain file names")
    {
        for (const auto* name : { "../../escape.wav", "C:\\Users\\alice\\plate.wav", "a:b*c?.wav", ".wav", "" })
        {
            INFO (name);
            const auto file = DrenchedProcessor::keepImportedFile (folder, name, b, error);
            REQUIRE (file != juce::File());
            CHECK (file.getParentDirectory() == folder);
            CHECK (file.getFileName().isNotEmpty());
            CHECK_FALSE (file.getFileName().startsWithChar ('.'));
        }
    }

    SECTION ("nothing to keep, or nowhere to keep it")
    {
        CHECK (DrenchedProcessor::keepImportedFile (folder, "empty.wav", {}, error) == juce::File());
        CHECK (error.contains ("empty"));

        error = {};
        const auto blocked = dir.getChildFile ("a file");
        blocked.replaceWithText ("x");
        CHECK (DrenchedProcessor::keepImportedFile (blocked.getChildFile ("Imported"), "hall.wav", a, error) == juce::File());
        CHECK (error.isNotEmpty());
    }

    dir.deleteRecursively();
}

TEST_CASE ("A dropped impulse response is kept byte for byte", "[drop]")
{
    const auto dir = scratch ("drenched-dropped-ir");
    const auto data = impulseResponseFile();

    // Through the pieces the page sends.
    DroppedFile dropped;
    REQUIRE (dropped.start (1, "garage.wav", static_cast<juce::int64> (data.getSize()), false));

    for (size_t at = 0; at < data.getSize(); at += 10000)
    {
        const juce::MemoryBlock piece (static_cast<const char*> (data.getData()) + at, std::min<size_t> (10000, data.getSize() - at));
        REQUIRE (dropped.add (1, base64 (piece)));
    }

    REQUIRE (dropped.complete (1));

    juce::String error;
    const auto kept = DrenchedProcessor::keepImportedFile (dir.getChildFile ("Imported"), dropped.fileName(), dropped.take(), error);
    REQUIRE (kept.existsAsFile());

    juce::MemoryBlock read;
    REQUIRE (kept.loadFileAsData (read));
    CHECK (read == data);

    dir.deleteRecursively();
}

#if defined (__linux__) && ! defined (__SANITIZE_ADDRESS__) && ! defined (__SANITIZE_THREAD__)
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>

TEST_CASE ("A drop there isn't the memory for is refused, not a crash", "[drop]")
{
    // A 32-bit host has little room to spare, so the largest drop may not fit. Here the
    // process is limited to a little more memory than it already has.
    rlimit before {};
    REQUIRE (getrlimit (RLIMIT_AS, &before) == 0);

    long pages = 0;
    {
        std::ifstream statm ("/proc/self/statm");
        statm >> pages;
    }
    REQUIRE (pages > 0);

    const auto used = static_cast<rlim_t> (pages) * static_cast<rlim_t> (sysconf (_SC_PAGESIZE));
    rlimit tight = before;
    tight.rlim_cur = used + (64u << 20);
    DroppedFile dropped;
    bool refused = false, stillWorks = false;

    {
        // Back to the old limit however the block is left.
        struct Restore { rlimit limit; ~Restore() { setrlimit (RLIMIT_AS, &limit); } } restore { before };
        REQUIRE (setrlimit (RLIMIT_AS, &tight) == 0);

        refused = ! dropped.start (1, "huge.wav", DroppedFile::largest, false);
        stillWorks = refused && dropped.start (2, "small.wav", 9, false);
    }

    CHECK (refused);
    CHECK_FALSE (dropped.isCurrent (1));
    CHECK (stillWorks);
}
#endif

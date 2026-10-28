#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace drenched;

/*  A capture is saved as a WAV file and then loaded as the person's impulse response. A
    save that fails, such as on a full disk, must leave no half-written file to be loaded
    then or later. */

namespace
{
    juce::File scratch (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }
}

TEST_CASE ("A capture is saved whole, or not at all", "[capture]")
{
    const auto dir = scratch ("drenched-capture-save");
    const auto folder = dir.getChildFile ("Captures");

    std::vector<std::vector<float>> channels (4, std::vector<float> (12345));

    for (size_t ch = 0; ch < channels.size(); ++ch)
        for (size_t n = 0; n < channels[ch].size(); ++n)
            channels[ch][n] = static_cast<float> (ch + 1) * 1.0e-3f * static_cast<float> (n % 997) - 0.5f;

    SECTION ("saved, sample for sample")
    {
        const auto file = DrenchedProcessor::writeCapture (channels, 96000.0, folder);
        REQUIRE (file.existsAsFile());
        CHECK (file.getParentDirectory() == folder);
        CHECK (file.hasFileExtension ("wav"));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
        REQUIRE (reader != nullptr);
        CHECK (reader->sampleRate == 96000.0);
        CHECK (reader->numChannels == 4);
        REQUIRE (reader->lengthInSamples == 12345);

        juce::AudioBuffer<float> read (4, 12345);
        REQUIRE (reader->read (&read, 0, 12345, 0, true, true));

        auto same = true;

        for (int ch = 0; ch < 4; ++ch)
            for (int n = 0; n < 12345; ++n)
                same = same && read.getSample (ch, n) == channels[static_cast<size_t> (ch)][static_cast<size_t> (n)];

        CHECK (same);

        // Another in the same second gets a name of its own.
        const auto second = DrenchedProcessor::writeCapture (channels, 96000.0, folder);
        CHECK (second.existsAsFile());
        CHECK (second != file);
    }

    SECTION ("nowhere to save it")
    {
        const auto blocked = dir.getChildFile ("a file");
        blocked.replaceWithText ("x");
        CHECK (DrenchedProcessor::writeCapture (channels, 48000.0, blocked.getChildFile ("Captures")) == juce::File());
        CHECK (blocked.loadFileAsString() == "x");
    }

    SECTION ("nothing to save")
    {
        CHECK (DrenchedProcessor::writeCapture ({}, 48000.0, folder) == juce::File());
        CHECK (DrenchedProcessor::writeCapture ({ {}, {} }, 48000.0, folder) == juce::File());
        CHECK (folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0);
    }

    dir.deleteRecursively();
}

#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace drenched;

/*  Testers will load long, high-rate and many-channel recordings. They must load, play
    and be reported like any other file. */

namespace
{
    /** Decaying noise written as 24-bit PCM, so its tail ends in digital silence, as a
        real recording's does. Written a block at a time, as it may be long. */
    juce::File writeLong (const juce::File& file, double seconds, double sampleRate, int channels)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file),
                                                                              sampleRate, static_cast<unsigned> (channels),
                                                                              24, {}, 0));
        REQUIRE (writer != nullptr);

        std::mt19937 rng (3u);
        std::normal_distribution<float> noise;
        constexpr int block = 65536;
        juce::AudioBuffer<float> buffer (channels, block);
        const auto total = static_cast<juce::int64> (seconds * sampleRate);

        for (juce::int64 pos = 0; pos < total; pos += block)
        {
            const auto n = static_cast<int> (std::min<juce::int64> (block, total - pos));

            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (c, i, 0.3f * noise (rng)
                                                * std::exp (-2.3f * static_cast<float> (pos + i) / static_cast<float> (sampleRate)));

            writer->writeFromAudioSampleBuffer (buffer, 0, n);
        }

        return file;
    }

    juce::var loadAsSession (const juce::File& file)
    {
        juce::MemoryBlock session;
        {
            testing::Plugin fresh;
            auto ui = fresh->getUiState();
            ui.setProperty (UiIDs::spaceId, "user", nullptr);
            ui.setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
            ui.setProperty (UiIDs::irName, file.getFileNameWithoutExtension(), nullptr);
            testing::onMessageThread ([&] { fresh->getStateInformation (session); });
        }

        testing::Plugin p;
        testing::onMessageThread ([&] { p->setStateInformation (session.getData(), static_cast<int> (session.getSize())); });
        return testing::onMessageThread ([&] { return p->impulseResponseInfo(); });
    }
}

TEST_CASE ("Long, high-rate impulse responses load", "[plugin][large]")
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-large-files");
    dir.createDirectory();

    SECTION ("30 seconds at 96 kHz")
    {
        const auto info = loadAsSession (writeLong (dir.getChildFile ("long.wav"), 30.0, 96000.0, 2));
        CHECK_FALSE (static_cast<bool> (info["missing"]));
    }

    SECTION ("four channels at 192 kHz")
    {
        const auto info = loadAsSession (writeLong (dir.getChildFile ("fast.wav"), 10.0, 192000.0, 4));
        CHECK_FALSE (static_cast<bool> (info["missing"]));
    }

    SECTION ("but not above 384 kHz")
    {
        const auto info = loadAsSession (writeLong (dir.getChildFile ("too-fast.wav"), 1.0, 768000.0, 2));
        CHECK (static_cast<bool> (info["missing"]));
    }

    dir.deleteRecursively();
}

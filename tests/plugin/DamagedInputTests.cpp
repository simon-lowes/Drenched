#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace drenched;

/*  Testers will load all sorts of files and hosts hand back all sorts of sessions. None of
    it may crash the plugin or make it play anything but finite audio: a file that can't be
    used is reported, and a damaged session falls back to what it can read. */

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    juce::File scratch()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-damaged-input");
        dir.createDirectory();
        return dir;
    }

    juce::MemoryBlock randomBytes (size_t count, unsigned seed)
    {
        std::mt19937 rng (seed);
        juce::MemoryBlock block (count);

        for (size_t i = 0; i < count; ++i)
            static_cast<std::uint8_t*> (block.getData())[i] = static_cast<std::uint8_t> (rng());

        return block;
    }

    /** A WAV file of the given samples, as 32-bit float. */
    juce::File writeWav (const juce::File& file, const std::vector<float>& samples, double sampleRate, int channels = 1)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file),
                                                                              sampleRate, static_cast<unsigned> (channels),
                                                                              32, {}, 0));
        REQUIRE (writer != nullptr);

        juce::AudioBuffer<float> buffer (channels, static_cast<int> (samples.size()));

        for (int c = 0; c < channels; ++c)
            buffer.copyFrom (c, 0, samples.data(), static_cast<int> (samples.size()));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    std::vector<float> decayingNoise (double seconds, double sampleRate)
    {
        std::mt19937 rng (3u);
        std::normal_distribution<float> noise;
        std::vector<float> samples (static_cast<size_t> (seconds * sampleRate));

        for (size_t n = 0; n < samples.size(); ++n)
            samples[n] = noise (rng) * std::exp (-6.9f * static_cast<float> (n) / static_cast<float> (sampleRate));

        return samples;
    }

    /** Plays a second of noise and checks every output sample is finite. */
    void playsFinite (DrenchedProcessor& p)
    {
        testing::onMessageThread ([&] { p.prepareToPlay (rate, blockSize); });

        std::mt19937 rng (5u);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        bool finite = true;

        for (int pos = 0; pos < static_cast<int> (rate); pos += blockSize)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (c, i, noise (rng));

            p.processBlock (buffer, midi);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                    finite = finite && std::isfinite (buffer.getSample (c, i));
        }

        CHECK (finite);
    }

    juce::MemoryBlock stateOf (DrenchedProcessor& p)
    {
        juce::MemoryBlock block;
        testing::onMessageThread ([&] { p.getStateInformation (block); });
        return block;
    }

    void setState (DrenchedProcessor& p, const juce::MemoryBlock& block)
    {
        testing::onMessageThread ([&] { p.setStateInformation (block.getData(), static_cast<int> (block.getSize())); });
    }

    /** A session that plays the file at `path` as the user's own impulse response. */
    juce::MemoryBlock sessionPlaying (const juce::String& path)
    {
        testing::Plugin fresh;
        auto ui = fresh->getUiState();
        ui.setProperty (UiIDs::spaceId, "user", nullptr);
        ui.setProperty (UiIDs::irPath, path, nullptr);
        ui.setProperty (UiIDs::irName, "damaged", nullptr);
        return stateOf (*fresh);
    }
}

TEST_CASE ("Damaged and unusable impulse response files are refused without harm", "[plugin][damaged]")
{
    const auto dir = scratch();
    const auto good = writeWav (dir.getChildFile ("good.wav"), decayingNoise (1.0, rate), rate, 2);

    juce::MemoryBlock goodBytes;
    good.loadFileAsData (goodBytes);

    const auto withBytes = [&] (const juce::String& name, const void* data, size_t size)
    {
        auto file = dir.getChildFile (name);
        file.replaceWithData (data, size);
        return file;
    };

    std::vector<juce::File> files;
    files.push_back (withBytes ("empty.wav", "", 0));
    files.push_back (withBytes ("header-only.wav", goodBytes.getData(), 44));
    files.push_back (withBytes ("cut-short.wav", goodBytes.getData(), 50));
    files.push_back (withBytes ("half.wav", goodBytes.getData(), goodBytes.getSize() / 2));

    for (const auto* name : { "noise.wav", "noise.aif", "noise.flac", "noise.amb", "noise.sofa" })
    {
        const auto bytes = randomBytes (20000, static_cast<unsigned> (juce::String (name).hashCode()));
        files.push_back (withBytes (name, bytes.getData(), bytes.getSize()));
    }

    // A SOFA file is HDF5: its signature followed by nonsense, and a real-looking
    // superblock cut off.
    {
        auto bytes = randomBytes (4096, 11u);
        std::memcpy (bytes.getData(), "\x89HDF\r\n\x1a\n", 8);
        files.push_back (withBytes ("hdf-noise.sofa", bytes.getData(), bytes.getSize()));
        files.push_back (withBytes ("hdf-cut.sofa", bytes.getData(), 12));
    }

    {
        auto damaged = goodBytes;
        // Flip bytes all through the data chunk's header and samples.
        auto* data = static_cast<std::uint8_t*> (damaged.getData());

        for (size_t i = 36; i < damaged.getSize(); i += 97)
            data[i] = static_cast<std::uint8_t> (data[i] ^ 0x5a);

        files.push_back (withBytes ("flipped.wav", damaged.getData(), damaged.getSize()));
    }

    auto withNan = decayingNoise (0.5, rate);
    withNan[1000] = std::nanf ("");
    files.push_back (writeWav (dir.getChildFile ("nan.wav"), withNan, rate));

    auto withInf = decayingNoise (0.5, rate);
    withInf[2000] = INFINITY;
    files.push_back (writeWav (dir.getChildFile ("inf.wav"), withInf, rate));

    files.push_back (writeWav (dir.getChildFile ("silent.wav"), std::vector<float> (24000, 0.0f), rate));
    files.push_back (writeWav (dir.getChildFile ("one-sample.wav"), { 1.0f }, rate));
    files.push_back (writeWav (dir.getChildFile ("slow-rate.wav"), decayingNoise (1.0, 1000.0), 1000.0));
    files.push_back (writeWav (dir.getChildFile ("too-many-channels.wav"), decayingNoise (0.2, rate), rate, 24));
    files.push_back (dir.getChildFile ("missing.wav"));
    files.push_back (dir); // a folder

    for (const auto& file : files)
    {
        INFO (file.getFileName());

        testing::Plugin p;
        setState (*p, sessionPlaying (file.getFullPathName()));

        const auto info = testing::onMessageThread ([&] { return p->impulseResponseInfo(); });
        CHECK (static_cast<bool> (info["missing"]));

        playsFinite (*p);
    }

    // A loud but usable file still plays, at the library's level.
    auto loud = decayingNoise (1.0, rate);

    for (auto& s : loud)
        s *= 1.0e6f;

    const auto loudFile = writeWav (dir.getChildFile ("loud.wav"), loud, rate);

    testing::Plugin p;
    setState (*p, sessionPlaying (loudFile.getFullPathName()));
    CHECK_FALSE (static_cast<bool> (testing::onMessageThread ([&] { return p->impulseResponseInfo(); })["missing"]));
    playsFinite (*p);

    dir.deleteRecursively();
}

TEST_CASE ("Damaged sessions load what they can and play finite audio", "[plugin][damaged]")
{
    testing::Plugin source;
    const auto saved = stateOf (*source);

    SECTION ("nonsense")
    {
        for (unsigned seed = 1; seed <= 20; ++seed)
        {
            testing::Plugin p;
            setState (*p, randomBytes (static_cast<size_t> (seed) * 97, seed));
            playsFinite (*p);
        }
    }

    SECTION ("cut short")
    {
        for (const auto size : { size_t (0), size_t (1), size_t (8), size_t (9), saved.getSize() / 3, saved.getSize() - 1 })
        {
            testing::Plugin p;
            setState (*p, juce::MemoryBlock (saved.getData(), size));
            playsFinite (*p);
        }
    }

    SECTION ("nonsense values")
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
        REQUIRE (xml != nullptr);

        for (const auto* value : { "nan", "inf", "-inf", "1e30", "-1e30", "", "banana" })
        {
            INFO (value);
            auto damaged = std::make_unique<juce::XmlElement> (*xml);

            for (auto* param : damaged->getChildWithTagNameIterator ("PARAM"))
                param->setAttribute ("value", value);

            if (auto* ui = damaged->getChildByName ("ui"))
                for (int i = 0; i < ui->getNumAttributes(); ++i)
                    ui->setAttribute (ui->getAttributeName (i), value);

            damaged->setAttribute ("version", value);

            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*damaged, block);

            testing::Plugin p;
            setState (*p, block);
            playsFinite (*p);

            for (auto* param : p->getParameters())
                CHECK (std::isfinite (param->getValue()));
        }
    }
}

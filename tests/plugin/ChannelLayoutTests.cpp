#include "PluginTestHelpers.h"

#include <ChannelLayouts.h>
#include <Library.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

using namespace drenched;

namespace
{
    /** A surround impulse response as a 24-bit WAV file: decaying noise on every channel. */
    juce::File surroundWav (const juce::File& file, int channels, const juce::AudioChannelSet* layout)
    {
        constexpr double rate = 48000.0;
        juce::AudioBuffer<float> buffer (channels, static_cast<int> (0.6 * rate));
        juce::Random random (5);

        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.setSample (ch, n, 0.3f * (random.nextFloat() * 2.0f - 1.0f) * std::pow (10.0f, -3.0f * static_cast<float> (n) / (0.4f * static_cast<float> (rate))));

        file.deleteFile();
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (layout != nullptr
            ? wav.createWriterFor (stream.release(), rate, *layout, 24, {}, 0)
            : wav.createWriterFor (stream.release(), rate, static_cast<unsigned> (channels), 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    std::shared_ptr<const RecordedResponse> load (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::String error;
        auto loaded = prepareFromReader (*reader, file.getFileNameWithoutExtension(), file.getFileName(), error);
        INFO (error);
        REQUIRE (loaded != nullptr);
        return loaded;
    }
}

TEST_CASE ("Surround files without a channel layout take the WAV order for their size", "[layouts]")
{
    using T = juce::AudioChannelSet;

    const auto fivePointOne = channelLayouts::fileLayout (T::create5point1(), 6);
    CHECK (fivePointOne == layouts::fivePointOne());

    // Twelve channels with no layout: 7.1.4, backs behind the sides.
    const auto twelve = channelLayouts::fileLayout (T::discreteChannels (12), 12);
    REQUIRE (twelve.size() == 12);
    CHECK (twelve[3].lfe);
    CHECK (twelve[4].azimuth == -150.0f);
    CHECK (twelve[6].azimuth == -90.0f);
    CHECK (twelve[8].elevation > 0.0f);
    CHECK (channelLayouts::nameOf (twelve) == "7.1.4");

    // JUCE's guess of SDDS for a maskless eight-channel WAV is replaced by 7.1.
    const auto eight = channelLayouts::fileLayout (T::create7point1SDDS(), 8);
    REQUIRE (eight.size() == 8);
    CHECK (eight[6].azimuth == -90.0f);
    CHECK (channelLayouts::nameOf (eight) == "7.1");

    CHECK (channelLayouts::fileLayout (T::discreteChannels (9), 9).empty());
}

TEST_CASE ("A 7.1 WAV file with a channel mask loads as surround", "[layouts]")
{
    // 7.1 as a WAV channel mask has it: back left and right, then the sides.
    using T = juce::AudioChannelSet;
    T layout;

    for (auto type : { T::left, T::right, T::centre, T::LFE, T::leftSurround, T::rightSurround, T::leftSurroundSide, T::rightSurroundSide })
        layout.addChannel (type);

    const auto file = surroundWav (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-7.1-ir.wav"), 8, &layout);
    const auto loaded = load (file);
    file.deleteFile();

    CHECK (loaded->isSurround());
    CHECK (loaded->speakers.size() == 8);
    CHECK (channelLayouts::nameOf (loaded->layout) == "7.1");
    CHECK (loaded->layout[3].lfe);
    CHECK (loaded->layout[4].azimuth == -150.0f);
    CHECK (loaded->layout[6].azimuth == -90.0f);
}

TEST_CASE ("A six-channel WAV file without a mask loads as 5.1", "[layouts]")
{
    const auto file = surroundWav (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-6ch-ir.wav"), 6, nullptr);
    const auto loaded = load (file);
    file.deleteFile();

    CHECK (loaded->isSurround());
    CHECK (loaded->layout == layouts::fivePointOne());
}

TEST_CASE ("B-format files are known by their metadata, type or name", "[layouts][ambisonic]")
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto plain = surroundWav (temp.getChildFile ("drenched-hall.wav"), 4, nullptr);
    const juce::StringPairArray none;

    // Four channels with nothing to say otherwise are true stereo.
    CHECK (! channelLayouts::bFormatOf (plain, none, 4).has_value());

    CHECK (channelLayouts::bFormatOf (temp.getChildFile ("Hall B-format.wav"), none, 4) == AmbisonicFormat::fuMa);
    CHECK (channelLayouts::bFormatOf (temp.getChildFile ("hall_bformat_seat3.wav"), none, 4) == AmbisonicFormat::fuMa);
    CHECK (channelLayouts::bFormatOf (temp.getChildFile ("Hall AmbiX.wav"), none, 4) == AmbisonicFormat::ambiX);
    CHECK (channelLayouts::bFormatOf (temp.getChildFile ("Hall AmbiX 2nd order.wav"), none, 9) == AmbisonicFormat::ambiX);
    CHECK (! channelLayouts::bFormatOf (temp.getChildFile ("Hall B-format.wav"), none, 6).has_value());

    juce::StringPairArray acn, n3d, fuMa;
    acn.set ("ambisonicChnOrder", "ACN");
    acn.set ("ambisonicNorm", "SN3D");
    n3d.set ("ambisonicChnOrder", "ACN");
    n3d.set ("ambisonicNorm", "N3D");
    fuMa.set ("ambisonicFormat", "FuMa");
    CHECK (channelLayouts::bFormatOf (plain, acn, 4) == AmbisonicFormat::ambiX);
    CHECK (channelLayouts::bFormatOf (plain, n3d, 4) == AmbisonicFormat::ambiXN3D);
    CHECK (channelLayouts::bFormatOf (plain, fuMa, 4) == AmbisonicFormat::fuMa);

    // An extensible WAV of the B-format type, as .amb files are.
    juce::MemoryBlock bytes;
    REQUIRE (plain.loadFileAsData (bytes));
    auto* data = static_cast<juce::uint8*> (bytes.getData());
    const char tag[] = "fmt ";
    const auto fmt = std::search (data, data + bytes.getSize(), tag, tag + 4) - data;
    REQUIRE (fmt + 48 < static_cast<std::ptrdiff_t> (bytes.getSize()));
    REQUIRE (juce::ByteOrder::littleEndianShort (data + fmt + 8) == 0xfffe);
    const juce::uint8 bFormatType[] { 0x01, 0x00, 0x00, 0x00, 0x21, 0x07, 0xd3, 0x11, 0x86, 0x44, 0xC8, 0xC1, 0xCA, 0x00, 0x00, 0x00 };
    std::memcpy (data + fmt + 8 + 24, bFormatType, sizeof (bFormatType));
    const auto amb = temp.getChildFile ("drenched-hall.amb");
    REQUIRE (amb.replaceWithData (bytes.getData(), bytes.getSize()));

    CHECK (channelLayouts::hasBFormatType (amb));
    CHECK (! channelLayouts::hasBFormatType (plain));
    CHECK (channelLayouts::bFormatOf (amb, none, 4) == AmbisonicFormat::fuMa);

    // JUCE reads it once handed the stream, as the plugin does for .amb files.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (amb.createInputStream()));
    REQUIRE (reader != nullptr);
    juce::String error;
    const auto loaded = prepareFromReader (*reader, "Hall", amb.getFileName(), error, channelLayouts::bFormatOf (amb, reader->metadataValues, 4));
    INFO (error);
    REQUIRE (loaded != nullptr);
    CHECK (loaded->ambisonic);
    CHECK (loaded->speakers.size() == 4);
    CHECK (loaded->channels == 4);

    plain.deleteFile();
    amb.deleteFile();
}

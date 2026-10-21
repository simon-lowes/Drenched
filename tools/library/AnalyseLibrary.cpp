// Measures each bundled recording the way the plugin will (decay per band, room
// size) and prints the results as JSON, keyed by space id. build_library.py runs
// it and merges the numbers into library/library.json, so the interface can show
// a space before its recording has loaded.

#include <drenched/RecordedResponse.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: drenched_analyse_library LIBRARY_DIR\n";
        return 2;
    }

    const juce::File folder (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const auto manifestFile = folder.getChildFile ("library.json");
    const auto manifest = juce::JSON::parse (manifestFile);

    if (manifest.getArray() == nullptr)
    {
        std::cerr << manifestFile.getFullPathName() << " isn't a list of spaces\n";
        return 1;
    }

    juce::FlacAudioFormat flac;
    auto results = std::make_unique<juce::DynamicObject>();

    for (const auto& entry : *manifest.getArray())
    {
        const auto id = entry["id"].toString();
        const auto file = folder.getChildFile ("ir").getChildFile (id + ".flac");
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (file.createInputStream().release(), true));

        if (reader == nullptr)
        {
            std::cerr << file.getFullPathName() << " is missing or not FLAC\n";
            return 1;
        }

        const auto channels = static_cast<int> (reader->numChannels);
        const auto length = static_cast<int> (reader->lengthInSamples);
        juce::AudioBuffer<float> buffer (channels, length);
        reader->read (&buffer, 0, length, 0, true, true);

        std::vector<std::vector<float>> audio (static_cast<size_t> (channels));

        for (int c = 0; c < channels; ++c)
            audio[static_cast<size_t> (c)].assign (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

        const auto response = drenched::prepareRecordedResponse (audio, reader->sampleRate, id.toStdString());
        const auto& room = response->room;

        auto measured = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> bands;

        for (const auto seconds : room.decaySeconds)
            bands.add (static_cast<double> (seconds));

        measured->setProperty ("bands", bands);
        measured->setProperty ("W", static_cast<double> (room.width));
        measured->setProperty ("D", static_cast<double> (room.depth));
        measured->setProperty ("H", static_cast<double> (room.height));
        results->setProperty (id, juce::var (measured.release()));
    }

    std::cout << juce::JSON::toString (juce::var (results.release()), true) << "\n";
    return 0;
}

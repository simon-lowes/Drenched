#include "Library.h"

#include <DrenchedLibrary.h>

#include <stdexcept>

namespace drenched
{

std::shared_ptr<const RecordedResponse> prepareFromReader (juce::AudioFormatReader& reader, const juce::String& name,
                                                           const juce::String& displayName, juce::String& error)
{
    constexpr double maxSeconds = 31.0; // the engine keeps 30 s once the silence is trimmed

    const auto channels = static_cast<int> (reader.numChannels);
    const auto length = static_cast<int> (std::min<juce::int64> (reader.lengthInSamples,
                                                                   static_cast<juce::int64> (maxSeconds * reader.sampleRate)));

    if (channels < 1 || channels > 16 || length <= 0)
    {
        error = displayName + " has no audio in it.";
        return nullptr;
    }

    juce::AudioBuffer<float> buffer (channels, length);

    if (! reader.read (&buffer, 0, length, 0, true, true))
    {
        error = "Drenched couldn't read " + displayName + ".";
        return nullptr;
    }

    std::vector<std::vector<float>> audio (static_cast<size_t> (channels));

    for (int c = 0; c < channels; ++c)
        audio[static_cast<size_t> (c)].assign (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

    try
    {
        return prepareRecordedResponse (audio, reader.sampleRate, name.toStdString());
    }
    catch (const std::invalid_argument& e)
    {
        error = juce::String (e.what());
    }
    catch (const std::exception&)
    {
        error = displayName + " is too large to load.";
    }

    return nullptr;
}

namespace library
{

namespace
{
    std::vector<LibrarySpace> build()
    {
        std::vector<LibrarySpace> result;
        int manifestSize = 0;
        const auto* manifest = DrenchedLibrary::getNamedResource ("library_json", manifestSize);

        if (manifest == nullptr)
            return result;

        const auto entries = juce::JSON::parse (juce::String::fromUTF8 (manifest, manifestSize));

        for (const auto& entry : *entries.getArray())
        {
            const auto id = entry["id"].toString();
            const auto fileName = id + ".flac";

            for (int i = 0; i < DrenchedLibrary::namedResourceListSize; ++i)
            {
                if (fileName != DrenchedLibrary::originalFilenames[i])
                    continue;

                LibrarySpace space;
                space.id = id;
                space.info = entry;
                space.data = DrenchedLibrary::getNamedResource (DrenchedLibrary::namedResourceList[i], space.size);
                result.push_back (space);
                break;
            }
        }

        return result;
    }
}

const std::vector<LibrarySpace>& spaces()
{
    static const auto all = build();
    return all;
}

const LibrarySpace* find (const juce::String& id)
{
    for (const auto& space : spaces())
        if (space.id == id)
            return &space;

    return nullptr;
}

juce::var forInterface()
{
    juce::Array<juce::var> list;

    for (const auto& space : spaces())
        list.add (space.info);

    return list;
}

std::shared_ptr<const RecordedResponse> load (const LibrarySpace& space, juce::String& error)
{
    const auto name = space.info["name"].toString();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (space.data, static_cast<size_t> (space.size), false), true));

    if (reader == nullptr)
    {
        error = "The recording of " + name + " is damaged. Reinstall Drenched.";
        return nullptr;
    }

    return prepareFromReader (*reader, name, name, error);
}

} // namespace library
} // namespace drenched

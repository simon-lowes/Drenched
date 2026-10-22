#pragma once

#include <optional>

#include <drenched/RecordedResponse.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

namespace drenched
{

/** A recorded space bundled with the plugin: an impulse response from library/ir and
    its entry in library/library.json (name, where and how it was recorded, credit and
    licence, and its measured decay and size for the interface). */
struct LibrarySpace
{
    juce::String id;
    juce::var info;            // the manifest entry, passed to the interface as it is
    const char* data = nullptr; // the FLAC file, embedded in the binary
    int size = 0;
};

namespace library
{
    /** Every bundled recording, in the manifest's order. Built once, on first use. */
    const std::vector<LibrarySpace>& spaces();

    /** The space with this id, or null. */
    const LibrarySpace* find (const juce::String& id);

    /** The manifest entries, for the interface. */
    juce::var forInterface();

    /** Decodes and prepares a bundled recording. Allocates; never on the audio thread.
        Returns null (with the reason in `error`) only if the build embedded a bad file. */
    std::shared_ptr<const RecordedResponse> load (const LibrarySpace& space, juce::String& error);
}

/** Reads an impulse response from an audio reader and prepares it, up to 31 seconds of
    it, as B-format when `bFormat` says so. `displayName` names it in error messages. On
    failure returns null and says why. */
std::shared_ptr<const RecordedResponse> prepareFromReader (juce::AudioFormatReader& reader, const juce::String& name,
                                                           const juce::String& displayName, juce::String& error,
                                                           std::optional<AmbisonicFormat> bFormat = std::nullopt);

} // namespace drenched

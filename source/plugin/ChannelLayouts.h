#pragma once

#include <drenched/SpeakerLayout.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>

namespace drenched
{

/** Where each JUCE speaker sits, for the engine (ITU-R BS.775 and the Dolby
    Atmos bed and height positions). */
namespace channelLayouts
{
    inline std::optional<Speaker> speakerFor (juce::AudioChannelSet::ChannelType type)
    {
        using T = juce::AudioChannelSet;

        struct Entry { T::ChannelType type; Speaker speaker; };

        static const Entry table[] {
            { T::left,              { -30.0f, 0.0f } },   { T::right,             { 30.0f, 0.0f } },
            { T::centre,            { 0.0f, 0.0f } },     { T::LFE,               { 0.0f, 0.0f, true } },
            { T::LFE2,              { 0.0f, 0.0f, true } },
            { T::leftSurround,      { -110.0f, 0.0f } },  { T::rightSurround,     { 110.0f, 0.0f } },
            { T::leftCentre,        { -15.0f, 0.0f } },   { T::rightCentre,       { 15.0f, 0.0f } },
            { T::centreSurround,    { 180.0f, 0.0f } },
            { T::leftSurroundSide,  { -90.0f, 0.0f } },   { T::rightSurroundSide, { 90.0f, 0.0f } },
            { T::leftSurroundRear,  { -150.0f, 0.0f } },  { T::rightSurroundRear, { 150.0f, 0.0f } },
            { T::wideLeft,          { -60.0f, 0.0f } },   { T::wideRight,         { 60.0f, 0.0f } },
            { T::topMiddle,         { 0.0f, 90.0f } },
            { T::topFrontLeft,      { -45.0f, 45.0f } },  { T::topFrontCentre,    { 0.0f, 45.0f } },
            { T::topFrontRight,     { 45.0f, 45.0f } },
            { T::topRearLeft,       { -135.0f, 45.0f } }, { T::topRearCentre,     { 180.0f, 45.0f } },
            { T::topRearRight,      { 135.0f, 45.0f } },
            { T::topSideLeft,       { -90.0f, 45.0f } },  { T::topSideRight,      { 90.0f, 45.0f } },
        };

        for (const auto& entry : table)
            if (entry.type == type)
                return entry.speaker;

        return std::nullopt;
    }

    /** The engine's layout for a channel set, in channel order; empty if any
        channel has no place (ambisonics, discrete channels) or it is too big. */
    inline SpeakerLayout speakersOf (const juce::AudioChannelSet& set)
    {
        if (set.size() == 1)
            return layouts::mono();

        SpeakerLayout speakers;

        if (set.isDisabled() || set.isDiscreteLayout() || static_cast<std::size_t> (set.size()) > maxChannels)
            return speakers;

        for (const auto type : set.getChannelTypes())
        {
            const auto speaker = speakerFor (type);

            if (! speaker.has_value())
                return {};

            speakers.push_back (*speaker);
        }

        return speakers;
    }

    inline bool isSupported (const juce::AudioChannelSet& set)
    {
        return ! speakersOf (set).empty();
    }
}

} // namespace drenched

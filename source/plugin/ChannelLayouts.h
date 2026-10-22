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

    /** Which speaker each channel of an impulse response file is for: the
        file's own channel layout when it has one (a WAV channel mask), or else
        the WAV order for its channel count (L R C LFE, back left and right,
        side left and right, then the heights). Empty when there is no telling.
        Only used for counts other than 1, 2 and 4. */
    inline SpeakerLayout fileLayout (const juce::AudioChannelSet& fromFile, int numChannels)
    {
        using T = juce::AudioChannelSet;

        const auto with = [] (std::initializer_list<T::ChannelType> types)
        {
            T set;

            for (auto t : types)
                set.addChannel (t);

            return set;
        };

        // JUCE reads a maskless 7 or 8 channel WAV as SDDS, which reverb files never are.
        const auto sdds = fromFile == T::create7point0SDDS() || fromFile == T::create7point1SDDS();
        auto set = fromFile;

        if (set.size() != numChannels || sdds || set.isDiscreteLayout())
        {
            const auto back = { T::left, T::right, T::centre, T::LFE, T::leftSurround, T::rightSurround,
                                T::leftSurroundSide, T::rightSurroundSide, T::topFrontLeft, T::topFrontRight,
                                T::topRearLeft, T::topRearRight };

            switch (numChannels)
            {
                case 3:  set = T::createLCR(); break;
                case 5:  set = T::create5point0(); break;
                case 6:  set = T::create5point1(); break;
                case 7:  set = with ({ T::left, T::right, T::centre, T::leftSurround, T::rightSurround, T::leftSurroundSide, T::rightSurroundSide }); break;
                case 8:  set = with ({ T::left, T::right, T::centre, T::LFE, T::leftSurround, T::rightSurround, T::leftSurroundSide, T::rightSurroundSide }); break;
                case 10: set = with ({ T::left, T::right, T::centre, T::LFE, T::leftSurround, T::rightSurround, T::leftSurroundSide, T::rightSurroundSide,
                                       T::topFrontLeft, T::topFrontRight }); break;
                case 12: set = with (back); break;
                default: return {};
            }
        }

        auto speakers = speakersOf (set);

        // In WAV terms "surround" is the back pair once there are side speakers too.
        if (! speakers.empty() && set.getChannelIndexForType (T::leftSurroundSide) >= 0)
        {
            for (const auto [type, azimuth] : { std::pair { T::leftSurround, -150.0f }, std::pair { T::rightSurround, 150.0f } })
                if (const auto index = set.getChannelIndexForType (type); index >= 0)
                    speakers[static_cast<size_t> (index)].azimuth = azimuth;
        }

        return speakers;
    }

    /** A layout's usual name: 5.1, 7.1.4, 5.0 and so on (ear-level speakers,
        LFE, heights). */
    inline juce::String nameOf (const SpeakerLayout& layout)
    {
        int ear = 0, lfe = 0, height = 0;

        for (const auto& s : layout)
            (s.lfe ? lfe : s.elevation >= 10.0f ? height : ear) += 1;

        return juce::String (ear) + "." + juce::String (lfe) + (height > 0 ? "." + juce::String (height) : juce::String());
    }

    inline bool isSupported (const juce::AudioChannelSet& set)
    {
        return ! speakersOf (set).empty();
    }
}

} // namespace drenched

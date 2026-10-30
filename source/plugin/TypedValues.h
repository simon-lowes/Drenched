#pragma once

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <string>

namespace drenched
{

/** Reading a value as people type it into a host's parameter field: the same rules as the
    interface's own fields (typedNumbers and typedScale in source/ui/js/app.js), so a value
    typed in an automation lane or a generic editor means what it would in the plugin. */
namespace typed
{
    inline bool isDigit (char32_t c) noexcept { return c >= '0' && c <= '9'; }

    /** The numbers in text, in order: a minus or en dash is a minus sign, a comma between
        digits is a decimal point unless the text already has one (so "1,5" is 1.5), and when
        thousands is set (fields that reach 1000) a comma or space before three digits groups
        them (so "1,000" and "12 000" are whole numbers). */
    inline juce::Array<float> numbers (const juce::String& text, bool thousands)
    {
        std::u32string s;

        for (auto c : text)
            s.push_back (c == 0x2212 || c == 0x2013 ? U'-' : static_cast<char32_t> (c));

        const auto digitAt = [&s] (size_t i) { return i < s.size() && isDigit (s[i]); };

        if (thousands)
        {
            for (size_t i = 1; i < s.size();)
            {
                const auto c = s[i];
                const auto separator = c == ',' || c == ' ' || c == 0x00a0 || c == 0x202f;

                if (separator && digitAt (i - 1) && digitAt (i + 1) && digitAt (i + 2) && digitAt (i + 3) && ! digitAt (i + 4))
                    s.erase (i, 1);
                else
                    ++i;
            }
        }

        auto hasPoint = false;

        for (size_t i = 1; i + 1 < s.size(); ++i)
            hasPoint = hasPoint || (s[i] == '.' && digitAt (i - 1) && digitAt (i + 1));

        if (! hasPoint)
            for (size_t i = 1; i + 1 < s.size(); ++i)
                if (s[i] == ',' && digitAt (i - 1) && digitAt (i + 1))
                    s[i] = '.';

        juce::Array<float> found;

        for (size_t i = 0; i < s.size();)
        {
            auto end = i + (s[i] == '-' ? 1 : 0);
            const auto digitsStart = end;

            while (digitAt (end))
                ++end;

            const auto whole = end > digitsStart;

            if (whole && end < s.size() && s[end] == '.')
                for (++end; digitAt (end); ++end) {}
            else if (! whole && end < s.size() && s[end] == '.' && digitAt (end + 1))
                for (++end; digitAt (end); ++end) {}

            if (end == digitsStart)
            {
                ++i;
                continue;
            }

            const std::string ascii (s.begin() + static_cast<std::ptrdiff_t> (i), s.begin() + static_cast<std::ptrdiff_t> (end));
            found.add (std::strtof (ascii.c_str(), nullptr));
            i = end;
        }

        return found;
    }

    /** Whether text names seconds after a number ("2 s", "1.5sec", "3 seconds"), not milliseconds. */
    inline bool saysSeconds (const juce::String& text)
    {
        const auto lower = text.toLowerCase();

        if (lower.contains ("ms") || lower.contains ("millisec"))
            return false;

        for (int i = 0; i < lower.length(); ++i)
        {
            if (! isDigit (static_cast<char32_t> (lower[i])))
                continue;

            auto j = i + 1;

            while (j < lower.length() && lower[j] == ' ')
                ++j;

            if (j >= lower.length() || lower[j] != 's')
                continue;

            auto word = j;

            while (word < lower.length() && juce::CharacterFunctions::isLetter (lower[word]))
                ++word;

            const auto unit = lower.substring (j, word);

            if (unit == "s" || unit == "sec" || unit == "secs" || unit == "second" || unit == "seconds")
                return true;
        }

        return false;
    }

    /** What a number typed into a field shown in `label` is multiplied by for the unit it was
        typed in, if not the field's own: seconds in a millisecond field, milliseconds in a
        seconds field, kHz in a hertz field. A field whose lowest value is 1 kHz or more
        (the high cut) reads a bare number too small to be hertz, but not too big to be kHz,
        as kHz. */
    inline float scaleFor (const juce::String& label, const juce::String& text, float value, float minimum, float maximum)
    {
        if (label == "ms")
            return saysSeconds (text) ? 1000.0f : 1.0f;

        if (label == "s")
            return text.containsIgnoreCase ("ms") || text.containsIgnoreCase ("millisec") ? 0.001f : 1.0f;

        if (label == "Hz")
        {
            if (text.containsIgnoreCase ("k"))
                return 1000.0f;

            const auto small = minimum >= 1000.0f && value < minimum && value * 1000.0f <= maximum;
            return small && ! text.containsIgnoreCase ("hz") ? 1000.0f : 1.0f;
        }

        return 1.0f;
    }

    /** Which side of the room text names: -1 left, 1 right, 0 neither. "L" and "R" on
        their own count, as hosts with little room show them. */
    inline int side (const juce::String& text)
    {
        const auto lower = text.toLowerCase();

        if (lower.contains ("left"))
            return -1;

        if (lower.contains ("right"))
            return 1;

        for (int i = 0; i < lower.length(); ++i)
        {
            const auto alone = (i == 0 || ! juce::CharacterFunctions::isLetter (lower[i - 1]))
                            && (i + 1 == lower.length() || ! juce::CharacterFunctions::isLetter (lower[i + 1]));

            if (alone && (lower[i] == 'l' || lower[i] == 'r'))
                return lower[i] == 'l' ? -1 : 1;
        }

        return 0;
    }

    /** On or off as typed: "on", "yes", "true" or a number other than 0 is on. */
    inline bool isOn (const juce::String& text)
    {
        const auto word = text.trim().toLowerCase();

        if (word == "on" || word == "yes" || word == "true")
            return true;

        if (word == "off" || word == "no" || word == "false")
            return false;

        const auto values = numbers (word, false);
        return ! values.isEmpty() && (values.getLast() < 0.0f || values.getLast() > 0.0f);
    }
}

} // namespace drenched

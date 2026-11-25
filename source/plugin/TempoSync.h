#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace drenched
{

/** Pre-delay locked to the host tempo, as a note value. The interface keeps the same table
    (PRE_NOTES in source/ui/js/engine.js); keep the two in step. */
namespace tempoSync
{
    /** A note value: its name as hosts and the interface show it, and its length in whole notes. */
    struct Note
    {
        const char* name;
        double wholeNotes;
    };

    /** Choice 0 is how sync played before note values existed (and so its default): the
        pre-delay in milliseconds, rounded to the nearest 64th note. The rest are note values,
        shortest first, so the control turns one way through them: T is a triplet (two thirds
        of the note), D dotted (one and a half times it). */
    inline constexpr std::array<Note, 16> notes { {
        { "Nearest 1/64", 0.0 },
        { "1/64", 1.0 / 64.0 },
        { "1/32T", 1.0 / 48.0 },
        { "1/32", 1.0 / 32.0 },
        { "1/16T", 1.0 / 24.0 },
        { "1/32D", 3.0 / 64.0 },
        { "1/16", 1.0 / 16.0 },
        { "1/8T", 1.0 / 12.0 },
        { "1/16D", 3.0 / 32.0 },
        { "1/8", 1.0 / 8.0 },
        { "1/4T", 1.0 / 6.0 },
        { "1/8D", 3.0 / 16.0 },
        { "1/4", 1.0 / 4.0 },
        { "1/2T", 1.0 / 3.0 },
        { "1/4D", 3.0 / 8.0 },
        { "1/2", 1.0 / 2.0 },
    } };

    inline constexpr int nearest64th = 0;
    inline constexpr int numNotes = static_cast<int> (notes.size());

    /** The longest pre-delay sync can give, in ms: what the engine's delay line holds. */
    inline constexpr float longestMs = 2000.0f;

    /** A tempo a host could really be playing at; anything else (none, zero, infinite) leaves
        the pre-delay as set in milliseconds. */
    inline std::optional<double> usableTempo (std::optional<double> bpm) noexcept
    {
        if (bpm.has_value() && std::isfinite (*bpm) && *bpm > 1.0 && *bpm < 10000.0)
            return bpm;

        return std::nullopt;
    }

    /** The pre-delay to play, in ms. Unsynced, or with no usable tempo, it is the one set.
        Synced to "Nearest 1/64" it is that rounded to the nearest 64th note (up to 1 s, as
        before note values); synced to a note, that note at the tempo (up to longestMs). */
    inline float predelayMs (float requestedMs, bool synced, int note, std::optional<double> bpm) noexcept
    {
        const auto tempo = usableTempo (bpm);

        if (! synced || ! tempo.has_value())
            return requestedMs;

        const auto index = std::clamp (note, 0, numNotes - 1);

        if (index == nearest64th)
        {
            // Worked out exactly as before note values, so old sessions play the same.
            const auto step = static_cast<float> (60000.0 / *tempo / 16.0);
            return std::clamp (std::round (requestedMs / step) * step, 0.0f, 1000.0f);
        }

        const auto wholeMs = 4.0 * 60000.0 / *tempo;
        return std::min (static_cast<float> (wholeMs * notes[static_cast<size_t> (index)].wholeNotes), longestMs);
    }
}

}

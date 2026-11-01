#pragma once

#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cmath>

namespace drenched
{

/** What the interface was last shown of each parameter, so the window can send the page
    only what has changed. The window looks on its timer rather than listening: a parameter
    listener runs on whichever thread sets the value, which for host automation is the audio
    thread, and anything it did from there (posting to the message thread) could take a lock
    or allocate. Looking costs a compare per parameter. Message thread only. */
class ParameterWatch
{
public:
    static constexpr size_t numParams = ParamIDs::ordered.size();

    explicit ParameterWatch (juce::AudioProcessorValueTreeState& state)
    {
        for (size_t i = 0; i < numParams; ++i)
            values[i] = state.getRawParameterValue (ParamIDs::ordered[i]);
    }

    /** Whether any parameter differs from what was last shown (or, after resendAll, any). */
    bool anyChanged() const noexcept
    {
        if (everything)
            return true;

        for (size_t i = 0; i < numParams; ++i)
            if (values[i] != nullptr && ! same (values[i]->load(), shown[i]))
                return true;

        return false;
    }

    /** Calls fn (id, value) for each parameter that changed since it was last shown, or for
        every one (and for every one after resendAll), and counts them as shown. */
    template <typename Fn>
    void takeChanges (bool onlyChanged, Fn&& fn)
    {
        const auto all = everything || ! onlyChanged;
        everything = false;

        for (size_t i = 0; i < numParams; ++i)
        {
            if (values[i] == nullptr)
                continue;

            const auto value = values[i]->load();

            if (! all && same (value, shown[i]))
                continue;

            shown[i] = value;
            fn (juce::String (ParamIDs::ordered[i]), value);
        }
    }

    /** The page needs every value again: a whole new session, say. */
    void resendAll() noexcept { everything = true; }

    /** The page shows this parameter as it is now, because the page set it. */
    void noteShown (const juce::String& id) noexcept
    {
        for (size_t i = 0; i < numParams; ++i)
            if (values[i] != nullptr && id == ParamIDs::ordered[i])
                shown[i] = values[i]->load();
    }

private:
    // A value that isn't a number (damaged automation) is shown once, not every look.
    static bool same (float a, float b) noexcept { return juce::exactlyEqual (a, b) || (std::isnan (a) && std::isnan (b)); }

    std::array<std::atomic<float>*, numParams> values {};
    std::array<float, numParams> shown {};
    bool everything = true;
};

} // namespace drenched

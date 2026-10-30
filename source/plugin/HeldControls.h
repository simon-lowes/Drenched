#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <set>

namespace drenched
{

/** The controls the interface holds in a drag. The host is told when each is taken and let
    go (a change gesture), once however often the page says so, and every held control is
    let go when the page reloads or the window closes mid-drag: the page can't send the end
    of that drag, and the host would go on thinking the control is held, writing automation
    in touch or latch mode or ignoring its own. */
class HeldControls
{
public:
    explicit HeldControls (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse) {}

    ~HeldControls() { releaseAll(); }

    void hold (const juce::String& id)
    {
        if (auto* parameter = state.getParameter (id); parameter != nullptr && held.insert (id).second)
            parameter->beginChangeGesture();
    }

    void release (const juce::String& id)
    {
        if (held.erase (id) > 0)
            if (auto* parameter = state.getParameter (id))
                parameter->endChangeGesture();
    }

    void releaseAll()
    {
        for (const auto& id : held)
            if (auto* parameter = state.getParameter (id))
                parameter->endChangeGesture();

        held.clear();
    }

    bool isHeld (const juce::String& id) const { return held.count (id) > 0; }

private:
    juce::AudioProcessorValueTreeState& state;
    std::set<juce::String> held;

    JUCE_DECLARE_NON_COPYABLE (HeldControls)
};

} // namespace drenched

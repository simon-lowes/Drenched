#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace drenched
{

/** Parameter ids. The interface (source/ui/js/app.js) uses the same strings, so a change here
    is a change there too. The eight band decays are the decay-by-frequency graph; the rest are
    the macros and the deep parameters behind "All parameters". */
namespace ParamIDs
{
    inline constexpr auto mix = "mix";
    inline constexpr auto output = "output";
    inline constexpr auto predelay = "predelay";
    inline constexpr auto size = "size";
    inline constexpr auto brightness = "brightness";
    inline constexpr auto earlyLate = "earlyLate";
    inline constexpr auto distance = "distance";
    inline constexpr auto freeze = "freeze";
    inline constexpr auto predelaySync = "predelaySync";
    inline constexpr auto crossover = "crossover";
    inline constexpr auto wetLowCut = "wetLowCut";
    inline constexpr auto wetHighCut = "wetHighCut";
    inline constexpr auto width = "width";
    inline constexpr auto ducking = "ducking";
    inline constexpr auto monoBelow = "monoBelow";
    inline constexpr auto living = "living";
    inline constexpr auto movement = "movement";
    inline constexpr auto movementRate = "movementRate";
    inline constexpr auto reverse = "reverse";
    inline constexpr auto gate = "gate";
    inline constexpr auto attack = "attack";
    inline constexpr auto directLevel = "directLevel";
    inline constexpr auto directDelay = "directDelay";
    inline constexpr auto directColour = "directColour";
    inline constexpr auto crossFeed = "crossFeed";
    inline constexpr auto lowCpu = "lowCpu";
    inline constexpr auto cloudSize = "cloudSize";
    inline constexpr auto listenerX = "listenerX";
    inline constexpr auto sourceX = "sourceX";

    inline constexpr std::array<const char*, 8> decay { "decay63",  "decay125", "decay250", "decay500",
                                                        "decay1k",  "decay2k",  "decay4k",  "decay8k" };
    inline constexpr std::array<const char*, 8> decayNames { "Decay 63 Hz",  "Decay 125 Hz", "Decay 250 Hz", "Decay 500 Hz",
                                                             "Decay 1 kHz",  "Decay 2 kHz",  "Decay 4 kHz",  "Decay 8 kHz" };

    /** Every id, in a fixed order, for code that keeps per-parameter state. */
    inline constexpr std::array<const char*, 37> ordered { mix, output, predelay, size, brightness, earlyLate, distance, freeze,
                                                           predelaySync, crossover, wetLowCut, wetHighCut, width, ducking, monoBelow,
                                                           decay[0], decay[1], decay[2], decay[3], decay[4], decay[5], decay[6], decay[7],
                                                           living, movement, movementRate, reverse, gate, attack, directLevel, directDelay,
                                                           directColour, crossFeed, lowCpu, cloudSize, listenerX, sourceX };

    /** The same, as strings. */
    inline const juce::StringArray& all()
    {
        static const juce::StringArray ids (ordered.data(), static_cast<int> (ordered.size()));
        return ids;
    }
}

/** Interface state that is not a parameter but still belongs to the session: the chosen
    space (or a loaded impulse response), the listener position, the locks, the view and the skin. It lives in a child of the
    plugin's state tree so presets and sessions carry it. */
namespace UiIDs
{
    inline const juce::Identifier tree { "ui" };
    inline const juce::Identifier space { "space" };     // sessions before space ids: an index
    inline const juce::Identifier spaceId { "spaceId" }; // a library id, a modelled id, or "user"
    inline const juce::Identifier listenerX { "lx" };    // sessions before version 2: now the listenerX parameter
    inline const juce::Identifier locks { "locks" };
    inline const juce::Identifier mode { "mode" };
    inline const juce::Identifier view { "view" };
    inline const juce::Identifier skin { "skin" };
    inline const juce::Identifier width { "width" };

    // Each output speaker's reverb level (dB) and delay (ms), in the output
    // bus's channel order: "level delay" pairs separated by ";".
    inline const juce::Identifier speakers { "speakers" };

    // A loaded impulse response: the file it came from, its name, and the
    // outcome of the last load (an error to show, and a counter that changes
    // on every attempt so the page can tell a new outcome from an old one).
    inline const juce::Identifier irPath { "irPath" };
    inline const juce::Identifier irName { "irName" };
    inline const juce::Identifier irError { "irError" };
    inline const juce::Identifier irEvent { "irEvent" };
}

/** The default decay-by-frequency curve of the first bundled space (Grand Hall), in seconds. */
inline constexpr std::array<float, 8> defaultDecay { 2.9f, 2.8f, 2.6f, 2.4f, 2.3f, 2.0f, 1.6f, 1.1f };

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using Float = AudioParameterFloat;
    using Bool = AudioParameterBool;

    const auto percent = AudioParameterFloatAttributes().withLabel ("%");
    const auto decibels = AudioParameterFloatAttributes().withLabel ("dB");
    const auto millis = AudioParameterFloatAttributes().withLabel ("ms");
    const auto hertz = AudioParameterFloatAttributes().withLabel ("Hz");
    const auto seconds = AudioParameterFloatAttributes().withLabel ("s");

    // Across the room, 0 at the left wall and 100 at the right, shown from the centre.
    const auto across = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int)
        {
            const auto off = juce::roundToInt ((v - 50.0f) * 2.0f);
            return off == 0 ? String ("Centre") : String (std::abs (off)) + "% " + (off < 0 ? "left" : "right");
        })
        .withValueFromStringFunction ([] (const String& text)
        {
            const auto amount = text.retainCharacters ("0123456789.").getFloatValue();
            if (text.containsIgnoreCase ("left"))
                return 50.0f - amount / 2.0f;
            if (text.containsIgnoreCase ("right"))
                return 50.0f + amount / 2.0f;
            return text.containsIgnoreCase ("centre") || text.containsIgnoreCase ("center") ? 50.0f : amount;
        });

    const auto logRange = [] (float lo, float hi, float step)
    {
        NormalisableRange<float> range (lo, hi,
            [lo, hi] (float, float, float v) { return lo * std::pow (hi / lo, v); },
            [lo, hi] (float, float, float v) { return std::log (v / lo) / std::log (hi / lo); });
        range.interval = step;
        return range;
    };

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::output, 1 }, "Output",
                                         NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, decibels));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::predelay, 1 }, "Pre-delay",
                                         NormalisableRange<float> (0.0f, 250.0f, 1.0f), 18.0f, millis));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::size, 1 }, "Size",
                                         NormalisableRange<float> (50.0f, 150.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::brightness, 1 }, "Brightness",
                                         NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f, decibels));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::earlyLate, 1 }, "Early/Late",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 55.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::distance, 1 }, "Distance",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f, percent));
    layout.add (std::make_unique<Bool> (ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));
    layout.add (std::make_unique<Bool> (ParameterID { ParamIDs::predelaySync, 1 }, "Pre-delay sync", false));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::crossover, 1 }, "Crossover",
                                         logRange (100.0f, 1000.0f, 1.0f), 320.0f, hertz));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::wetLowCut, 1 }, "Wet low cut",
                                         logRange (20.0f, 1000.0f, 1.0f), 80.0f, hertz));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::wetHighCut, 1 }, "Wet high cut",
                                         logRange (1000.0f, 20000.0f, 10.0f), 14000.0f, hertz));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::width, 1 }, "Width",
                                         NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::ducking, 1 }, "Ducking",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::monoBelow, 1 }, "Mono below",
                                         NormalisableRange<float> (0.0f, 500.0f, 1.0f), 0.0f, hertz));

    for (size_t band = 0; band < ParamIDs::decay.size(); ++band)
        layout.add (std::make_unique<Float> (ParameterID { ParamIDs::decay[band], 1 }, ParamIDs::decayNames[band],
                                             logRange (0.1f, 20.0f, 0.01f), defaultDecay[band], seconds));

    // Added after the decays so earlier sessions and automation keep their order.
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::living, 1 }, "Living tail",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::movement, 1 }, "Movement",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 20.0f, percent));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::movementRate, 1 }, "Movement rate",
                                         logRange (0.05f, 5.0f, 0.01f), 0.7f, hertz));

    // How the response plays: backwards, cut short, or fading in. 0 ms is off.
    const auto skewed = [] (float hi, float centre)
    {
        NormalisableRange<float> range (0.0f, hi, 1.0f);
        range.setSkewForCentre (centre);
        return range;
    };

    layout.add (std::make_unique<Bool> (ParameterID { ParamIDs::reverse, 1 }, "Reverse", false));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::gate, 1 }, "Gate",
                                         skewed (2000.0f, 250.0f), 0.0f, millis));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::attack, 1 }, "Attack",
                                         skewed (500.0f, 60.0f), 0.0f, millis));

    // The direct sound in the room: its own level (-60 dB removes it), delay and colour.
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::directLevel, 1 }, "Direct level",
                                         NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, decibels));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::directDelay, 1 }, "Direct delay",
                                         NormalisableRange<float> (0.0f, 50.0f, 0.1f), 0.0f, millis));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::directColour, 1 }, "Direct colour",
                                         NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f, decibels));

    // How much each input feeds the opposite side: 0 makes two separate reverbs.
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::crossFeed, 1 }, "Cross-feed",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, percent));

    // Where the listener sits and the sound plays from, across the room (the map
    // drags them). Both default to the centre, where earlier versions put them.
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::listenerX, 1 }, "Listener position",
                                         NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, across));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::sourceX, 1 }, "Source position",
                                         NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, across));

    // Low-CPU mode trades about 21 ms of latency, which the host makes up, for a
    // third of the convolution's cost. Changing latency mid-song is no use to
    // automate, so hosts don't offer it.
    layout.add (std::make_unique<Bool> (ParameterID { ParamIDs::lowCpu, 1 }, "Low CPU", false,
                                        AudioParameterBoolAttributes().withAutomatable (false)));

    // How far the reverb spreads from where the sound is panned: 100 fills the
    // room as captured (how every earlier version played).
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::cloudSize, 1 }, "Cloud size",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, percent));

    return layout;
}

} // namespace drenched

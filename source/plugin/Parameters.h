#pragma once

#include "TypedValues.h"

#include <drenched/WetEqualiser.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <type_traits>
#include <utility>

namespace drenched
{

/** Parameter ids. The interface (source/ui/js/app.js) uses the same strings, so a change here
    is a change there too. The eight band decays are the decay-by-frequency graph (the left side's
    while the sides are unlinked, with decayRight the right's); the rest are the macros and the
    deep parameters behind "All parameters". */
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
    inline constexpr auto scene = "scene";

    inline constexpr std::array<const char*, 8> decay { "decay63",  "decay125", "decay250", "decay500",
                                                        "decay1k",  "decay2k",  "decay4k",  "decay8k" };
    inline constexpr std::array<const char*, 8> decayNames { "Decay 63 Hz",  "Decay 125 Hz", "Decay 250 Hz", "Decay 500 Hz",
                                                             "Decay 1 kHz",  "Decay 2 kHz",  "Decay 4 kHz",  "Decay 8 kHz" };
    inline constexpr std::array<const char*, 8> decayShortNames { "Dcy 63", "Dcy 125", "Dcy 250", "Dcy 500",
                                                                  "Dcy 1k", "Dcy 2k",  "Dcy 4k",  "Dcy 8k" };

    // The right-hand side's own decay curve, played while the two sides are unlinked.
    inline constexpr auto decayLink = "decayLink";
    inline constexpr std::array<const char*, 8> decayRight { "decayR63", "decayR125", "decayR250", "decayR500",
                                                             "decayR1k", "decayR2k",  "decayR4k",  "decayR8k" };
    inline constexpr std::array<const char*, 8> decayRightNames { "Right decay 63 Hz", "Right decay 125 Hz", "Right decay 250 Hz",
                                                                  "Right decay 500 Hz", "Right decay 1 kHz", "Right decay 2 kHz",
                                                                  "Right decay 4 kHz", "Right decay 8 kHz" };
    inline constexpr std::array<const char*, 8> decayRightShortNames { "RDcy 63", "RDcy 125", "RDcy 250", "RDcy 500",
                                                                       "RDcy 1k", "RDcy 2k",  "RDcy 4k",  "RDcy 8k" };

    // The wet EQ: twelve bands, each a low shelf, a bell or a high shelf, with a frequency,
    // gain and Q. The first six started as a low shelf, four bells and a high shelf (the
    // shelves got a Q, and every band a shape and an on switch, when the display became
    // editable); the other six are off until the person adds them on the graph.
    inline constexpr std::array<const char*, 12> eqFrequency { "eq1Freq", "eq2Freq", "eq3Freq", "eq4Freq", "eq5Freq", "eq6Freq",
                                                               "eq7Freq", "eq8Freq", "eq9Freq", "eq10Freq", "eq11Freq", "eq12Freq" };
    inline constexpr std::array<const char*, 12> eqGain { "eq1Gain", "eq2Gain", "eq3Gain", "eq4Gain", "eq5Gain", "eq6Gain",
                                                          "eq7Gain", "eq8Gain", "eq9Gain", "eq10Gain", "eq11Gain", "eq12Gain" };
    inline constexpr std::array<const char*, 12> eqQ { "eq1Q", "eq2Q", "eq3Q", "eq4Q", "eq5Q", "eq6Q",
                                                       "eq7Q", "eq8Q", "eq9Q", "eq10Q", "eq11Q", "eq12Q" };
    inline constexpr std::array<const char*, 12> eqShape { "eq1Shape", "eq2Shape", "eq3Shape", "eq4Shape", "eq5Shape", "eq6Shape",
                                                           "eq7Shape", "eq8Shape", "eq9Shape", "eq10Shape", "eq11Shape", "eq12Shape" };
    inline constexpr std::array<const char*, 12> eqOn { "eq1On", "eq2On", "eq3On", "eq4On", "eq5On", "eq6On",
                                                        "eq7On", "eq8On", "eq9On", "eq10On", "eq11On", "eq12On" };
    inline constexpr std::array<const char*, 12> eqNames { "EQ low", "EQ 2", "EQ 3", "EQ 4", "EQ 5", "EQ high",
                                                           "EQ 7", "EQ 8", "EQ 9", "EQ 10", "EQ 11", "EQ 12" };
    inline constexpr std::array<const char*, 12> eqShortNames { "EQ L", "EQ 2", "EQ 3", "EQ 4", "EQ 5", "EQ H",
                                                                "EQ 7", "EQ 8", "EQ 9", "EQ 10", "EQ 11", "EQ 12" };
    inline constexpr std::array<const char*, 3> eqShapeNames { "Low shelf", "Bell", "High shelf" };

    /** The bands that start out in use: the first six. */
    inline constexpr bool eqOnByDefault (std::size_t band) noexcept { return band < 6; }

    /** Every id, in a fixed order, for code that keeps per-parameter state. */
    inline constexpr std::array<const char*, 107> ordered { mix, output, predelay, size, brightness, earlyLate, distance, freeze,
                                                           predelaySync, crossover, wetLowCut, wetHighCut, width, ducking, monoBelow,
                                                           decay[0], decay[1], decay[2], decay[3], decay[4], decay[5], decay[6], decay[7],
                                                           living, movement, movementRate, reverse, gate, attack, directLevel, directDelay,
                                                           directColour, crossFeed, lowCpu, cloudSize, listenerX, sourceX, scene,
                                                           eqFrequency[0], eqGain[0],
                                                           eqFrequency[1], eqGain[1], eqQ[1], eqFrequency[2], eqGain[2], eqQ[2],
                                                           eqFrequency[3], eqGain[3], eqQ[3], eqFrequency[4], eqGain[4], eqQ[4],
                                                           eqFrequency[5], eqGain[5], decayLink,
                                                           decayRight[0], decayRight[1], decayRight[2], decayRight[3],
                                                           decayRight[4], decayRight[5], decayRight[6], decayRight[7],
                                                           eqQ[0], eqQ[5],
                                                           eqFrequency[6], eqGain[6], eqQ[6], eqFrequency[7], eqGain[7], eqQ[7],
                                                           eqFrequency[8], eqGain[8], eqQ[8], eqFrequency[9], eqGain[9], eqQ[9],
                                                           eqFrequency[10], eqGain[10], eqQ[10], eqFrequency[11], eqGain[11], eqQ[11],
                                                           eqShape[0], eqShape[1], eqShape[2], eqShape[3], eqShape[4], eqShape[5],
                                                           eqShape[6], eqShape[7], eqShape[8], eqShape[9], eqShape[10], eqShape[11],
                                                           eqOn[0], eqOn[1], eqOn[2], eqOn[3], eqOn[4], eqOn[5],
                                                           eqOn[6], eqOn[7], eqOn[8], eqOn[9], eqOn[10], eqOn[11] };

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
    inline const juce::Identifier graph { "graph" };   // what the main display draws: "decay" or "eq"
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

/** Scenes: named sets of every setting and the space, recalled from the interface or by
    automating the scene parameter (see DrenchedProcessor::recallScene). They live in a
    child of the plugin's state tree, one child per scene, each holding its parameter
    values as properties named by parameter id. */
namespace SceneIDs
{
    inline constexpr int maxScenes = 32;

    inline const juce::Identifier tree { "scenes" };
    inline const juce::Identifier scene { "scene" };
    inline const juce::Identifier number { "number" };     // 1 to maxScenes, the scene parameter's value
    inline const juce::Identifier name { "name" };
    inline const juce::Identifier spaceId { "spaceId" };
    inline const juce::Identifier irPath { "irPath" };     // when spaceId is "user"
    inline const juce::Identifier irName { "irName" };
    inline const juce::Identifier speakers { "speakers" };

    /** Parameters a scene leaves alone: the scene itself, Freeze (a performance control,
        like the A/B compare leaves it) and Low CPU (a setting for the computer, not the room). */
    inline bool isStored (const juce::String& id)
    {
        return id != ParamIDs::scene && id != ParamIDs::freeze && id != ParamIDs::lowCpu;
    }
}

/** A parameter as hosts show it. Hosts and control surfaces with room for only a few
    characters get a short name (a VST3 host's short title holds 8), so no two read the same,
    and a value typed into the host reads as the interface reads it (see TypedValues.h):
    "14 kHz" in the high cut, "1.5 s" in the pre-delay, "1,5" for 1.5, "off" for the lowest
    of a control that turns off there. Text with no number in it gives the default. */
template <typename Base>
class Hosted final : public Base
{
public:
    template <typename... Args>
    explicit Hosted (juce::String shortNameToUse, Args&&... args)
        : Base (std::forward<Args> (args)...), shortName (std::move (shortNameToUse))
    {
    }

    juce::String getName (int maximumStringLength) const override
    {
        return this->name.length() <= maximumStringLength ? this->name : shortName.substring (0, maximumStringLength);
    }

    float getValueForText (const juce::String& text) const override
    {
        if constexpr (std::is_same_v<Base, juce::AudioParameterBool>)
        {
            return typed::isOn (text) ? 1.0f : 0.0f;
        }
        else if constexpr (std::is_same_v<Base, juce::AudioParameterChoice>)
        {
            // A choice by name ("bell", "high shelf", or a part of one), or by its number.
            const auto typedText = text.trim();

            for (int i = 0; i < this->choices.size(); ++i)
                if (typedText.isNotEmpty() && (typedText.containsIgnoreCase (this->choices[i]) || this->choices[i].containsIgnoreCase (typedText)))
                    return this->convertTo0to1 (static_cast<float> (i));

            const auto values = typed::numbers (text, false);
            return values.isEmpty() ? static_cast<const juce::AudioProcessorParameter*> (this)->getDefaultValue()
                                    : this->convertTo0to1 (std::round (values.getLast()));
        }
        else
        {
            const auto& bounds = this->getNormalisableRange();
            const auto values = typed::numbers (text, bounds.end >= 1000.0f);
            const auto id = this->getParameterID();
            const auto across = id == ParamIDs::listenerX || id == ParamIDs::sourceX;

            if (across && text.containsIgnoreCase ("cent"))
                return this->convertTo0to1 (50.0f);

            if (values.isEmpty())
            {
                const auto turnsOff = std::abs (bounds.start) < 1.0e-6f || id == ParamIDs::directLevel;
                return text.containsIgnoreCase ("off") && turnsOff ? 0.0f : static_cast<const juce::AudioProcessorParameter*> (this)->getDefaultValue();
            }

            auto typedValue = values.getLast();

            if (across && typed::side (text) != 0)
                typedValue = 50.0f + static_cast<float> (typed::side (text)) * typedValue / 2.0f;
            else
                typedValue *= typed::scaleFor (this->getLabel(), text, typedValue, bounds.start, bounds.end);

            return this->convertTo0to1 (typedValue);
        }
    }

    const juce::String shortName;
};

/** The default decay-by-frequency curve of the first bundled space (Grand Hall), in seconds. */
inline constexpr std::array<float, 8> defaultDecay { 2.9f, 2.8f, 2.6f, 2.4f, 2.3f, 2.0f, 1.6f, 1.1f };

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using Float = Hosted<AudioParameterFloat>;
    using Bool = Hosted<AudioParameterBool>;
    using Int = Hosted<AudioParameterInt>;
    using Choice = Hosted<AudioParameterChoice>;

    const auto percent = AudioParameterFloatAttributes().withLabel ("%");
    const auto decibels = AudioParameterFloatAttributes().withLabel ("dB");
    const auto millis = AudioParameterFloatAttributes().withLabel ("ms");
    const auto hertz = AudioParameterFloatAttributes().withLabel ("Hz");
    const auto seconds = AudioParameterFloatAttributes().withLabel ("s");

    // Across the room, 0 at the left wall and 100 at the right, shown from the centre.
    const auto across = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int length)
        {
            const auto off = juce::roundToInt ((v - 50.0f) * 2.0f);
            const auto text = off == 0 ? String ("Centre") : String (std::abs (off)) + "% " + (off < 0 ? "left" : "right");

            if (length <= 0 || text.length() <= length)
                return text;

            return (off == 0 ? String ("C") : String (std::abs (off)) + "% " + (off < 0 ? "L" : "R")).substring (0, length);
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

    layout.add (std::make_unique<Float> ("Mix", ParameterID { ParamIDs::mix, 1 }, "Mix",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, percent));
    layout.add (std::make_unique<Float> ("Output", ParameterID { ParamIDs::output, 1 }, "Output",
                                         NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, decibels));
    layout.add (std::make_unique<Float> ("Predelay", ParameterID { ParamIDs::predelay, 1 }, "Pre-delay",
                                         NormalisableRange<float> (0.0f, 250.0f, 1.0f), 18.0f, millis));
    layout.add (std::make_unique<Float> ("Size", ParameterID { ParamIDs::size, 1 }, "Size",
                                         NormalisableRange<float> (50.0f, 150.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<Float> ("Bright", ParameterID { ParamIDs::brightness, 1 }, "Brightness",
                                         NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f, decibels));
    layout.add (std::make_unique<Float> ("Erly/Lte", ParameterID { ParamIDs::earlyLate, 1 }, "Early/Late",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 55.0f, percent));
    layout.add (std::make_unique<Float> ("Distance", ParameterID { ParamIDs::distance, 1 }, "Distance",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f, percent));
    layout.add (std::make_unique<Bool> ("Freeze", ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));
    layout.add (std::make_unique<Bool> ("PD sync", ParameterID { ParamIDs::predelaySync, 1 }, "Pre-delay sync", false));
    layout.add (std::make_unique<Float> ("X-over", ParameterID { ParamIDs::crossover, 1 }, "Crossover",
                                         logRange (100.0f, 1000.0f, 1.0f), 320.0f, hertz));
    layout.add (std::make_unique<Float> ("Low cut", ParameterID { ParamIDs::wetLowCut, 1 }, "Wet low cut",
                                         logRange (20.0f, 1000.0f, 1.0f), 80.0f, hertz));
    layout.add (std::make_unique<Float> ("High cut", ParameterID { ParamIDs::wetHighCut, 1 }, "Wet high cut",
                                         logRange (1000.0f, 20000.0f, 10.0f), 14000.0f, hertz));
    layout.add (std::make_unique<Float> ("Width", ParameterID { ParamIDs::width, 1 }, "Width",
                                         NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<Float> ("Ducking", ParameterID { ParamIDs::ducking, 1 }, "Ducking",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<Float> ("Mono lo", ParameterID { ParamIDs::monoBelow, 1 }, "Mono below",
                                         NormalisableRange<float> (0.0f, 500.0f, 1.0f), 0.0f, hertz));

    for (size_t band = 0; band < ParamIDs::decay.size(); ++band)
        layout.add (std::make_unique<Float> (ParamIDs::decayShortNames[band], ParameterID { ParamIDs::decay[band], 1 }, ParamIDs::decayNames[band],
                                             logRange (0.1f, 20.0f, 0.01f), defaultDecay[band], seconds));

    // Added after the decays so earlier sessions and automation keep their order.
    layout.add (std::make_unique<Float> ("Living", ParameterID { ParamIDs::living, 1 }, "Living tail",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<Float> ("Movement", ParameterID { ParamIDs::movement, 1 }, "Movement",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 20.0f, percent));
    layout.add (std::make_unique<Float> ("Mvt rate", ParameterID { ParamIDs::movementRate, 1 }, "Movement rate",
                                         logRange (0.05f, 5.0f, 0.01f), 0.7f, hertz));

    // How the response plays: backwards, cut short, or fading in. 0 ms is off.
    const auto skewed = [] (float hi, float centre)
    {
        NormalisableRange<float> range (0.0f, hi, 1.0f);
        range.setSkewForCentre (centre);
        return range;
    };

    layout.add (std::make_unique<Bool> ("Reverse", ParameterID { ParamIDs::reverse, 1 }, "Reverse", false));
    layout.add (std::make_unique<Float> ("Gate", ParameterID { ParamIDs::gate, 1 }, "Gate",
                                         skewed (2000.0f, 250.0f), 0.0f, millis));
    layout.add (std::make_unique<Float> ("Attack", ParameterID { ParamIDs::attack, 1 }, "Attack",
                                         skewed (500.0f, 60.0f), 0.0f, millis));

    // The direct sound in the room: its own level (-60 dB removes it), delay and colour.
    layout.add (std::make_unique<Float> ("Dir lvl", ParameterID { ParamIDs::directLevel, 1 }, "Direct level",
                                         NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, decibels));
    layout.add (std::make_unique<Float> ("Dir dly", ParameterID { ParamIDs::directDelay, 1 }, "Direct delay",
                                         NormalisableRange<float> (0.0f, 50.0f, 0.1f), 0.0f, millis));
    layout.add (std::make_unique<Float> ("Dir col", ParameterID { ParamIDs::directColour, 1 }, "Direct colour",
                                         NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f, decibels));

    // How much each input feeds the opposite side: 0 makes two separate reverbs.
    layout.add (std::make_unique<Float> ("X-feed", ParameterID { ParamIDs::crossFeed, 1 }, "Cross-feed",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, percent));

    // Where the listener sits and the sound plays from, across the room (the map
    // drags them). Both default to the centre, where earlier versions put them.
    layout.add (std::make_unique<Float> ("Listener", ParameterID { ParamIDs::listenerX, 1 }, "Listener position",
                                         NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, across));
    layout.add (std::make_unique<Float> ("Source", ParameterID { ParamIDs::sourceX, 1 }, "Source position",
                                         NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, across));

    // Low-CPU mode trades about 21 ms of latency, which the host makes up, for a
    // third of the convolution's cost. Changing latency mid-song is no use to
    // automate, so hosts don't offer it.
    layout.add (std::make_unique<Bool> ("Low CPU", ParameterID { ParamIDs::lowCpu, 1 }, "Low CPU", false,
                                        AudioParameterBoolAttributes().withAutomatable (false)));

    // How far the reverb spreads from where the sound is panned: 100 fills the
    // room as captured (how every earlier version played).
    layout.add (std::make_unique<Float> ("Cloud", ParameterID { ParamIDs::cloudSize, 1 }, "Cloud size",
                                         NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, percent));

    // The scene to recall: automating it changes every setting and the space at once.
    // Off (0, how every earlier version played) leaves the settings alone.
    layout.add (std::make_unique<Int> ("Scene", ParameterID { ParamIDs::scene, 1 }, "Scene", 0, SceneIDs::maxScenes, 0,
        AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("Off") : "Scene " + String (v); })));

    // The wet EQ, flat by default (how every earlier version played). The first six bands'
    // frequency, gain and (bells') Q came first; the rest were added later, so they follow
    // every earlier parameter.
    for (size_t band = 0; band < 6; ++band)
    {
        const auto& flat = WetEqualiser::flat[band];
        const String name (ParamIDs::eqNames[band]), shortName (ParamIDs::eqShortNames[band]);

        layout.add (std::make_unique<Float> (shortName + " Hz", ParameterID { ParamIDs::eqFrequency[band], 1 }, name + " frequency",
                                             logRange (20.0f, 20000.0f, 1.0f), flat.frequencyHz, hertz));
        layout.add (std::make_unique<Float> (shortName + " dB", ParameterID { ParamIDs::eqGain[band], 1 }, name + " gain",
                                             NormalisableRange<float> (WetEqualiser::minGainDb, WetEqualiser::maxGainDb, 0.1f),
                                             flat.gainDb, decibels));

        if (WetEqualiser::shapeOf (band) == WetEqualiser::Shape::bell)
            layout.add (std::make_unique<Float> (shortName + " Q", ParameterID { ParamIDs::eqQ[band], 1 }, name + " Q",
                                                 logRange (WetEqualiser::minQ, WetEqualiser::maxQ, 0.01f), flat.q));
    }

    // One decay curve for both sides unless unlinked (linked is how every earlier
    // version played); unlinked, the decays above are the left side's and these the right's.
    layout.add (std::make_unique<Bool> ("Dcy link", ParameterID { ParamIDs::decayLink, 1 }, "Decay linked", true));

    for (size_t band = 0; band < ParamIDs::decayRight.size(); ++band)
        layout.add (std::make_unique<Float> (ParamIDs::decayRightShortNames[band], ParameterID { ParamIDs::decayRight[band], 1 }, ParamIDs::decayRightNames[band],
                                             logRange (0.1f, 20.0f, 0.01f), defaultDecay[band], seconds));

    // The rest of the EQ: the shelves' Q (heard once a shelf is made a bell), six more bands,
    // and every band's shape and on switch. A band off is flat, like a band at 0 dB, so the
    // defaults (the first six on, the rest off, every shape as it was) play as before.
    const auto eqQParameter = [&] (size_t band)
    {
        const String name (ParamIDs::eqNames[band]), shortName (ParamIDs::eqShortNames[band]);
        return std::make_unique<Float> (shortName + " Q", ParameterID { ParamIDs::eqQ[band], 1 }, name + " Q",
                                        logRange (WetEqualiser::minQ, WetEqualiser::maxQ, 0.01f), WetEqualiser::flat[band].q);
    };

    layout.add (eqQParameter (0));
    layout.add (eqQParameter (5));

    for (size_t band = 6; band < WetEqualiser::numBands; ++band)
    {
        const auto& flat = WetEqualiser::flat[band];
        const String name (ParamIDs::eqNames[band]), shortName (ParamIDs::eqShortNames[band]);

        layout.add (std::make_unique<Float> (shortName + " Hz", ParameterID { ParamIDs::eqFrequency[band], 1 }, name + " frequency",
                                             logRange (20.0f, 20000.0f, 1.0f), flat.frequencyHz, hertz));
        layout.add (std::make_unique<Float> (shortName + " dB", ParameterID { ParamIDs::eqGain[band], 1 }, name + " gain",
                                             NormalisableRange<float> (WetEqualiser::minGainDb, WetEqualiser::maxGainDb, 0.1f),
                                             flat.gainDb, decibels));
        layout.add (eqQParameter (band));
    }

    // A shape's name is cut to the room the host has ("High she"), which getValueForText reads back.
    const StringArray shapeNames (ParamIDs::eqShapeNames.data(), static_cast<int> (ParamIDs::eqShapeNames.size()));
    const auto shapeText = AudioParameterChoiceAttributes().withStringFromValueFunction ([shapeNames] (int index, int maximumStringLength)
    {
        const auto text = shapeNames[jlimit (0, shapeNames.size() - 1, index)];
        return maximumStringLength > 0 ? text.substring (0, maximumStringLength) : text;
    });

    for (size_t band = 0; band < WetEqualiser::numBands; ++band)
    {
        const String name (ParamIDs::eqNames[band]), shortName (ParamIDs::eqShortNames[band]);
        layout.add (std::make_unique<Choice> (shortName + " Sh", ParameterID { ParamIDs::eqShape[band], 1 }, name + " shape",
                                              shapeNames, static_cast<int> (WetEqualiser::shapeOf (band)), shapeText));
    }

    for (size_t band = 0; band < WetEqualiser::numBands; ++band)
    {
        const String name (ParamIDs::eqNames[band]), shortName (ParamIDs::eqShortNames[band]);
        layout.add (std::make_unique<Bool> (shortName + " On", ParameterID { ParamIDs::eqOn[band], 1 }, name + " on",
                                            ParamIDs::eqOnByDefault (band)));
    }

    return layout;
}

} // namespace drenched

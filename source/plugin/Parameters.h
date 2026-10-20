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

    inline constexpr std::array<const char*, 8> decay { "decay63",  "decay125", "decay250", "decay500",
                                                        "decay1k",  "decay2k",  "decay4k",  "decay8k" };
    inline constexpr std::array<const char*, 8> decayNames { "Decay 63 Hz",  "Decay 125 Hz", "Decay 250 Hz", "Decay 500 Hz",
                                                             "Decay 1 kHz",  "Decay 2 kHz",  "Decay 4 kHz",  "Decay 8 kHz" };

    /** Every id, in a fixed order, for code that keeps per-parameter state. */
    inline const juce::StringArray& all()
    {
        static const juce::StringArray ids = []
        {
            juce::StringArray list { mix, output, predelay, size, brightness, earlyLate, distance, freeze,
                                     predelaySync, crossover, wetLowCut, wetHighCut, width, ducking, monoBelow };

            for (auto* id : decay)
                list.add (id);

            return list;
        }();

        return ids;
    }
}

/** Interface state that is not a parameter but still belongs to the session: the chosen
    space, the listener position, the locks, the view and the skin. It lives in a child of the
    plugin's state tree so presets and sessions carry it. */
namespace UiIDs
{
    inline const juce::Identifier tree { "ui" };
    inline const juce::Identifier space { "space" };
    inline const juce::Identifier listenerX { "lx" };
    inline const juce::Identifier locks { "locks" };
    inline const juce::Identifier mode { "mode" };
    inline const juce::Identifier view { "view" };
    inline const juce::Identifier skin { "skin" };
    inline const juce::Identifier width { "width" };
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

    return layout;
}

} // namespace drenched

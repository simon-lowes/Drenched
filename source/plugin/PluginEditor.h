#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace drenched
{

/** Placeholder editor: two native sliders until the web view interface lands.
    It exists so the plugin can be loaded and automated in a host today. */
class DrenchedEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DrenchedEditor (DrenchedProcessor&);
    ~DrenchedEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Slider mixSlider, outputSlider;
    juce::Label mixLabel, outputLabel;
    std::unique_ptr<SliderAttachment> mixAttachment, outputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedEditor)
};

} // namespace drenched

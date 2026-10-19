#include "PluginEditor.h"

namespace drenched
{

namespace
{
    const auto background = juce::Colour (0xff0e0f11);
    const auto ink = juce::Colour (0xffe8e6e1);
    const auto quiet = juce::Colour (0xff9a9da3);
    const auto accent = juce::Colour (0xfff2a93b);

    void styleSlider (juce::Slider& slider, juce::Label& label, const juce::String& name)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::textBoxTextColourId, ink);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setTitle (name);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, quiet);
        label.attachToComponent (&slider, false);
    }
}

DrenchedEditor::DrenchedEditor (DrenchedProcessor& processor)
    : AudioProcessorEditor (processor)
{
    styleSlider (mixSlider, mixLabel, "Space");
    styleSlider (outputSlider, outputLabel, "Output");

    addAndMakeVisible (mixSlider);
    addAndMakeVisible (outputSlider);

    mixAttachment = std::make_unique<SliderAttachment> (processor.getState(), ParamIDs::mix, mixSlider);
    outputAttachment = std::make_unique<SliderAttachment> (processor.getState(), ParamIDs::output, outputSlider);

    setResizable (true, true);
    setResizeLimits (480, 300, 1920, 1200);
    setSize (600, 360);
}

void DrenchedEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (ink);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("DRENCHED", getLocalBounds().removeFromTop (60), juce::Justification::centred);
}

void DrenchedEditor::resized()
{
    auto area = getLocalBounds().reduced (40);
    area.removeFromTop (60);

    const auto half = area.getWidth() / 2;
    mixSlider.setBounds (area.removeFromLeft (half).reduced (20));
    outputSlider.setBounds (area.reduced (20));
}

} // namespace drenched

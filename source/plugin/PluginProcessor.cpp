#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <drenched/Mix.h>

namespace drenched
{

DrenchedProcessor::DrenchedProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "DrenchedState", createParameterLayout())
{
    mixParam = state.getRawParameterValue (ParamIDs::mix);
    outputParam = state.getRawParameterValue (ParamIDs::output);
}

juce::AudioProcessorValueTreeState::ParameterLayout DrenchedProcessor::createParameterLayout()
{
    using namespace juce;

    auto percent = AudioParameterFloatAttributes().withLabel ("%");
    auto decibels = AudioParameterFloatAttributes().withLabel ("dB");

    return {
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Space",
                                               NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, percent),
        std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
                                               NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, decibels)
    };
}

bool DrenchedProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const auto monoOrStereo = [] (const juce::AudioChannelSet& set)
    { return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo(); };

    // Surround layouts arrive with the multichannel engine (phase 4).
    return monoOrStereo (in) && monoOrStereo (out);
}

void DrenchedProcessor::prepareToPlay (double sampleRate, int)
{
    constexpr double smoothingSeconds = 0.05;

    for (auto* smoother : { &dryGain, &wetGain, &outputGain })
        smoother->reset (sampleRate, smoothingSeconds);

    const auto gains = equalPowerMix (mixParam->load() / 100.0f);
    dryGain.setCurrentAndTargetValue (gains.dry);
    wetGain.setCurrentAndTargetValue (gains.wet);
    outputGain.setCurrentAndTargetValue (decibelsToGain (outputParam->load()));
}

void DrenchedProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputs = getTotalNumInputChannels();
    const auto numOutputs = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    // Mono in, stereo out: copy the input to the second channel.
    for (auto channel = numInputs; channel < numOutputs; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples);

    const auto gains = equalPowerMix (mixParam->load() / 100.0f);
    dryGain.setTargetValue (gains.dry);
    wetGain.setTargetValue (gains.wet);
    outputGain.setTargetValue (decibelsToGain (outputParam->load()));

    // The convolution engine plugs in here (phase 2). Until then the wet path
    // is silent, so the plugin applies the dry share of the mix and the output gain.
    for (int n = 0; n < numSamples; ++n)
    {
        const auto gain = dryGain.getNextValue() * outputGain.getNextValue();
        wetGain.skip (1);

        for (int channel = 0; channel < numOutputs; ++channel)
            buffer.getWritePointer (channel)[n] *= gain;
    }
}

juce::AudioProcessorEditor* DrenchedProcessor::createEditor()
{
    return new DrenchedEditor (*this);
}

void DrenchedProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DrenchedProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace drenched

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drenched::DrenchedProcessor();
}

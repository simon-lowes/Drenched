#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <drenched/Mix.h>

#include <algorithm>
#include <cmath>

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

juce::ValueTree DrenchedProcessor::getUiState()
{
    return state.state.getOrCreateChildWithName (UiIDs::tree, nullptr);
}

DrenchedProcessor::Peaks DrenchedProcessor::takePeaks() noexcept
{
    return { peaks[0].exchange (0.0f), peaks[1].exchange (0.0f),
             peaks[2].exchange (0.0f), peaks[3].exchange (0.0f) };
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

    for (auto& peak : peaks)
        peak.store (0.0f);
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

    const auto notePeak = [this, &buffer, numSamples, numOutputs] (size_t slot)
    {
        for (int channel = 0; channel < std::min (numOutputs, 2); ++channel)
        {
            auto& peak = peaks[slot + static_cast<size_t> (channel)];
            const auto level = buffer.getMagnitude (channel, 0, numSamples);

            // Hold the highest value until the editor collects it.
            auto current = peak.load (std::memory_order_relaxed);

            while (level > current && ! peak.compare_exchange_weak (current, level, std::memory_order_relaxed))
            {}
        }
    };

    notePeak (0);

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

    notePeak (2);
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

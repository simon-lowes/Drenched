#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <drenched/Mix.h>
#include <drenched/Spaces.h>

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
    const auto raw = [this] (const char* id) { return state.getRawParameterValue (id); };

    params.mix = raw (ParamIDs::mix);
    params.output = raw (ParamIDs::output);
    params.predelay = raw (ParamIDs::predelay);
    params.predelaySync = raw (ParamIDs::predelaySync);
    params.size = raw (ParamIDs::size);
    params.brightness = raw (ParamIDs::brightness);
    params.earlyLate = raw (ParamIDs::earlyLate);
    params.distance = raw (ParamIDs::distance);
    params.wetLowCut = raw (ParamIDs::wetLowCut);
    params.wetHighCut = raw (ParamIDs::wetHighCut);
    params.width = raw (ParamIDs::width);
    params.ducking = raw (ParamIDs::ducking);
    params.monoBelow = raw (ParamIDs::monoBelow);
    params.freeze = raw (ParamIDs::freeze);
    params.crossover = raw (ParamIDs::crossover);

    for (size_t band = 0; band < params.decay.size(); ++band)
        params.decay[band] = raw (ParamIDs::decay[band]);

    builder = std::make_unique<KernelBuilder> (reverb);
    cacheUiState();
    startTimerHz (20);
}

DrenchedProcessor::~DrenchedProcessor()
{
    stopTimer();
    builder.reset();
}

void DrenchedProcessor::cacheUiState()
{
    auto ui = getUiState();
    spaceIndex.store (juce::jlimit (0, static_cast<int> (bundledSpaces.size()) - 1, static_cast<int> (ui.getProperty (UiIDs::space, 0))));
    listenerX.store (juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (ui.getProperty (UiIDs::listenerX, 0.5)))));
}

ImpulseResponseRequest DrenchedProcessor::currentRequest (double sampleRate) const
{
    ImpulseResponseRequest request;
    request.sampleRate = sampleRate;
    request.space = spaceIndex.load();
    request.listenerX = listenerX.load();
    request.sizePercent = params.size->load();
    request.distancePercent = params.distance->load();
    request.earlyLatePercent = params.earlyLate->load();

    for (size_t band = 0; band < params.decay.size(); ++band)
        request.decaySeconds[band] = params.decay[band]->load();

    return request;
}

void DrenchedProcessor::timerCallback()
{
    cacheUiState();

    if (const auto rate = preparedRate.load(); rate > 0.0)
        builder->request (currentRequest (rate));
}

float DrenchedProcessor::predelayMs (float requested)
{
    if (params.predelaySync->load() < 0.5f)
        return requested;

    // Synced: snap to the nearest 64th note at the host tempo.
    if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
            {
                const auto step = static_cast<float> (60000.0 / *bpm / 16.0);
                return juce::jlimit (0.0f, 1000.0f, std::round (requested / step) * step);
            }
        }
    }

    return requested;
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
    reverb.prepare (sampleRate);
    preparedRate.store (sampleRate);
    cacheUiState();

    // Build the first response now, so playback (and offline bounces) start with the right sound.
    try
    {
        builder->buildNow (currentRequest (sampleRate));
    }
    catch (const std::exception&)
    {
        // Out of memory: the builder thread retries on the next change.
    }

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

    ReverbControls controls;
    controls.mix = params.mix->load() / 100.0f;
    controls.outputGain = decibelsToGain (params.output->load());
    controls.predelayMs = predelayMs (params.predelay->load());
    controls.brightnessDb = params.brightness->load();
    controls.lowCutHz = params.wetLowCut->load();
    controls.highCutHz = params.wetHighCut->load();
    controls.widthPercent = params.width->load();
    controls.ducking = params.ducking->load() / 100.0f;
    controls.monoBelowHz = params.monoBelow->load();
    controls.freeze = params.freeze->load() >= 0.5f;
    controls.crossoverHz = params.crossover->load();

    auto* left = buffer.getWritePointer (0);
    auto* right = numOutputs > 1 ? buffer.getWritePointer (1) : left;
    reverb.process (left, right, static_cast<size_t> (numSamples), controls);

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

    cacheUiState();

    if (const auto rate = preparedRate.load(); rate > 0.0)
        builder->request (currentRequest (rate));
}

} // namespace drenched

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drenched::DrenchedProcessor();
}

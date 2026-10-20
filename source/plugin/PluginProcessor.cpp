#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <drenched/Mix.h>
#include <drenched/Spaces.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    lifetime.reset();
    loader.removeAllJobs (true, 10000);
    builder.reset();
}

void DrenchedProcessor::cacheUiState()
{
    auto ui = getUiState();
    spaceIndex.store (juce::jlimit (0, userSpace, static_cast<int> (ui.getProperty (UiIDs::space, 0))));
    listenerX.store (juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (ui.getProperty (UiIDs::listenerX, 0.5)))));
}

ImpulseResponseRequest DrenchedProcessor::currentRequest (double sampleRate) const
{
    ImpulseResponseRequest request;
    request.sampleRate = sampleRate;
    request.space = spaceIndex.load();

    if (request.space == userSpace)
    {
        // A loaded response, or the first bundled space while its file is missing.
        request.space = 0;
        const std::lock_guard lock (recordedLock);
        request.recorded = recorded;
    }
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

//==============================================================================
std::shared_ptr<const RecordedResponse> DrenchedProcessor::readImpulseResponse (const juce::File& file, juce::String& error)
{
    constexpr double maxSeconds = 31.0; // the engine keeps 30 s once the silence is trimmed

    if (! file.existsAsFile())
    {
        error = "Drenched can't find " + file.getFileName() + ".";
        return nullptr;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = file.getFileName() + " isn't an audio file Drenched can read. Use WAV, AIFF or FLAC.";
        return nullptr;
    }

    const auto channels = static_cast<int> (reader->numChannels);
    const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples,
                                                                   static_cast<juce::int64> (maxSeconds * reader->sampleRate)));

    if (channels < 1 || channels > 16 || length <= 0)
    {
        error = file.getFileName() + " has no audio in it.";
        return nullptr;
    }

    juce::AudioBuffer<float> buffer (channels, length);

    if (! reader->read (&buffer, 0, length, 0, true, true))
    {
        error = "Drenched couldn't read " + file.getFileName() + ".";
        return nullptr;
    }

    std::vector<std::vector<float>> audio (static_cast<size_t> (channels));

    for (int c = 0; c < channels; ++c)
        audio[static_cast<size_t> (c)].assign (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

    try
    {
        return prepareRecordedResponse (audio, reader->sampleRate, file.getFileNameWithoutExtension().toStdString());
    }
    catch (const std::invalid_argument& e)
    {
        error = juce::String (e.what());
    }
    catch (const std::exception&)
    {
        error = file.getFileName() + " is too large to load.";
    }

    return nullptr;
}

void DrenchedProcessor::loadImpulseResponseAsync (const juce::File& file)
{
    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([this, file, alive]
    {
        juce::String error;
        auto loaded = readImpulseResponse (file, error);

        juce::MessageManager::callAsync ([this, file, alive, loaded = std::move (loaded), error]() mutable
        {
            if (! alive.expired())
                applyLoadedImpulseResponse (file, std::move (loaded), error);
        });
    });
}

void DrenchedProcessor::applyLoadedImpulseResponse (const juce::File& file, std::shared_ptr<const RecordedResponse> loaded,
                                                    const juce::String& error)
{
    auto ui = getUiState();
    ui.setProperty (UiIDs::irEvent, static_cast<int> (ui.getProperty (UiIDs::irEvent, 0)) + 1, nullptr);

    if (loaded == nullptr)
    {
        ui.setProperty (UiIDs::irError, error, nullptr);
        return;
    }

    const auto measured = loaded->room.decaySeconds;

    {
        const std::lock_guard lock (recordedLock);
        recorded = std::move (loaded);
        recordedPath = file.getFullPathName();
        recordedMissing = false;
    }

    ui.removeProperty (UiIDs::irError, nullptr);
    ui.setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
    ui.setProperty (UiIDs::irName, file.getFileNameWithoutExtension(), nullptr);

    // The graph opens on the room's own decay, so editing it reshapes from there.
    for (size_t band = 0; band < measured.size(); ++band)
    {
        if (auto* parameter = state.getParameter (ParamIDs::decay[band]))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (measured[band]));
            parameter->endChangeGesture();
        }
    }

    ui.setProperty (UiIDs::space, userSpace, nullptr);
    cacheUiState();
}

void DrenchedProcessor::restoreImpulseResponse()
{
    const auto path = getUiState().getProperty (UiIDs::irPath, "").toString();

    {
        const std::lock_guard lock (recordedLock);

        if (path.isEmpty())
        {
            recorded.reset();
            recordedPath = {};
            recordedMissing = false;
            return;
        }

        if (path == recordedPath && recorded != nullptr)
            return;
    }

    // Sessions load this way before playback starts, so read it now: the first
    // bounce must already use it.
    juce::String error;
    auto loaded = juce::File::isAbsolutePath (path) ? readImpulseResponse (juce::File (path), error) : nullptr;

    const std::lock_guard lock (recordedLock);
    recorded = std::move (loaded);
    recordedPath = path;
    recordedMissing = recorded == nullptr;
}

juce::var DrenchedProcessor::impulseResponseInfo()
{
    auto ui = getUiState();
    const auto name = ui.getProperty (UiIDs::irName, "").toString();

    std::shared_ptr<const RecordedResponse> current;
    bool missing = false;

    {
        const std::lock_guard lock (recordedLock);
        current = recorded;
        missing = recordedMissing;
    }

    if (current == nullptr && ! missing)
        return {};

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("name", current != nullptr ? juce::String (current->name) : name);
    object->setProperty ("missing", missing);

    juce::Array<juce::var> decay;

    for (size_t band = 0; band < params.decay.size(); ++band)
        decay.add (static_cast<double> (current != nullptr ? current->room.decaySeconds[band] : params.decay[band]->load()));

    object->setProperty ("bands", decay);

    if (current != nullptr)
    {
        object->setProperty ("ch", current->channels);
        object->setProperty ("rate", current->responses.sampleRate);
        object->setProperty ("W", static_cast<double> (current->room.width));
        object->setProperty ("D", static_cast<double> (current->room.depth));
        object->setProperty ("H", static_cast<double> (current->room.height));
    }

    return juce::var (object.release());
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

    restoreImpulseResponse();
    cacheUiState();

    if (const auto rate = preparedRate.load(); rate > 0.0)
        builder->request (currentRequest (rate));
}

} // namespace drenched

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drenched::DrenchedProcessor();
}

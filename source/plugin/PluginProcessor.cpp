#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Settings.h"
#include "ChannelLayouts.h"

#include <drenched/ClapCapture.h>
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
    params.living = raw (ParamIDs::living);
    params.movement = raw (ParamIDs::movement);
    params.movementRate = raw (ParamIDs::movementRate);
    params.reverse = raw (ParamIDs::reverse);
    params.gate = raw (ParamIDs::gate);
    params.attack = raw (ParamIDs::attack);
    params.directLevel = raw (ParamIDs::directLevel);
    params.directDelay = raw (ParamIDs::directDelay);
    params.directColour = raw (ParamIDs::directColour);
    params.crossFeed = raw (ParamIDs::crossFeed);
    params.cloudSize = raw (ParamIDs::cloudSize);
    params.lowCpu = raw (ParamIDs::lowCpu);

    // A new instance opens on the first recorded space, with the graph on its measured decay.
    if (const auto& recordings = library::spaces(); ! recordings.empty())
    {
        getUiState().setProperty (UiIDs::spaceId, recordings.front().id, nullptr);

        if (const auto* bands = recordings.front().info["bands"].getArray())
            for (size_t band = 0; band < params.decay.size() && static_cast<int> (band) < bands->size(); ++band)
                if (auto* parameter = state.getParameter (ParamIDs::decay[band]))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (static_cast<double> ((*bands)[static_cast<int> (band)]))));
    }

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

juce::String DrenchedProcessor::chosenSpaceId()
{
    auto ui = getUiState();
    const auto id = ui.getProperty (UiIDs::spaceId, "").toString();

    if (id.isNotEmpty())
        return id;

    // Sessions from before space ids saved the index into the modelled spaces, or the
    // slot after them for a loaded file.
    const auto index = juce::jlimit (0, userSpace, static_cast<int> (ui.getProperty (UiIDs::space, 0)));
    return index == userSpace ? juce::String ("user") : juce::String (bundledSpaces[static_cast<size_t> (index)].id);
}

void DrenchedProcessor::cacheUiState()
{
    auto ui = getUiState();
    const auto id = chosenSpaceId();
    auto index = 0;

    if (id == "user")
        index = userSpace;
    else if (library::find (id) != nullptr)
        index = librarySpace;
    else
        for (size_t i = 0; i < bundledSpaces.size(); ++i)
            if (id == bundledSpaces[i].id)
                index = static_cast<int> (i);

    if (index == librarySpace)
        loadLibrarySpace (id, false);

    spaceIndex.store (index);
    listenerX.store (juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (ui.getProperty (UiIDs::listenerX, 0.5)))));

    const auto speakers = juce::StringArray::fromTokens (ui.getProperty (UiIDs::speakers, "").toString(), ";", "");

    for (size_t k = 0; k < maxChannels; ++k)
    {
        const auto pair = k < static_cast<size_t> (speakers.size()) ? juce::StringArray::fromTokens (speakers[static_cast<int> (k)], " ", "")
                                                                     : juce::StringArray();
        const auto level = pair.size() > 0 ? pair[0].getFloatValue() : 0.0f;
        const auto delay = pair.size() > 1 ? pair[1].getFloatValue() : 0.0f;
        speakerLevelDb[k].store (juce::jlimit (SpeakerTrim::minLevelDb, SpeakerTrim::maxLevelDb, std::isfinite (level) ? level : 0.0f));
        speakerDelayMs[k].store (juce::jlimit (0.0f, SpeakerTrim::maxDelayMs, std::isfinite (delay) ? delay : 0.0f));
    }
}

std::optional<ImpulseResponseRequest> DrenchedProcessor::currentRequest (double sampleRate) const
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
    else if (request.space == librarySpace)
    {
        // The chosen recording; until it has loaded, the previous sound carries on.
        request.space = 0;
        const std::lock_guard lock (recordedLock);
        request.recorded = libraryResponse;

        if (libraryResponse == nullptr)
            return std::nullopt;
    }
    {
        const std::lock_guard lock (layoutLock);
        request.outputs = outputSpeakers;
    }

    request.listenerX = listenerX.load();
    request.sizePercent = params.size->load();
    request.distancePercent = params.distance->load();
    request.earlyLatePercent = params.earlyLate->load();
    request.livingPercent = params.living->load();
    request.reverse = params.reverse->load() >= 0.5f;
    request.gateMs = params.gate->load();
    request.attackMs = params.attack->load();
    request.directLevelDb = params.directLevel->load();
    request.directDelayMs = params.directDelay->load();
    request.directColourDb = params.directColour->load();
    request.crossFeedPercent = params.crossFeed->load();
    request.latencySamples = wantedLatency (sampleRate);

    for (size_t band = 0; band < params.decay.size(); ++band)
        request.decaySeconds[band] = params.decay[band]->load();

    return request;
}

int DrenchedProcessor::wantedLatency (double sampleRate) const
{
    return params.lowCpu->load() >= 0.5f ? static_cast<int> (ConvolutionReverb::lowCpuLatency (sampleRate)) : 0;
}

void DrenchedProcessor::updateLatency (double sampleRate)
{
    // The reverb delays its dry sound to match the running kernel, so the host
    // only needs telling; a new kernel with the new latency follows.
    if (const auto wanted = wantedLatency (sampleRate); wanted != getLatencySamples())
        setLatencySamples (wanted);
}

void DrenchedProcessor::timerCallback()
{
    cacheUiState();
    pollCapture();

    if (const auto rate = preparedRate.load(); rate > 0.0)
        updateLatency (rate);

    if (const auto rate = preparedRate.load(); rate > 0.0)
        if (auto request = currentRequest (rate))
            builder->request (*request);
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

    return prepareFromReader (*reader, file.getFileNameWithoutExtension(), file.getFileName(), error);
}

void DrenchedProcessor::loadLibrarySpace (const juce::String& id, bool now)
{
    const auto* space = library::find (id);

    if (space == nullptr)
        return;

    {
        const std::lock_guard lock (recordedLock);

        if ((libraryLoadedId == id && libraryResponse != nullptr) || (! now && libraryWanted == id))
            return;

        libraryWanted = id;
    }

    if (now)
    {
        juce::String error;
        auto loaded = library::load (*space, error);
        const std::lock_guard lock (recordedLock);
        libraryResponse = std::move (loaded);
        libraryLoadedId = id;
        return;
    }

    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([this, space, id, alive]
    {
        juce::String error;
        auto loaded = library::load (*space, error);

        juce::MessageManager::callAsync ([this, id, alive, loaded = std::move (loaded)]() mutable
        {
            if (alive.expired())
                return;

            {
                const std::lock_guard lock (recordedLock);

                if (libraryWanted != id)
                    return;

                libraryResponse = std::move (loaded);
                libraryLoadedId = id;
            }

            if (const auto rate = preparedRate.load(); rate > 0.0)
                if (auto request = currentRequest (rate))
                    builder->request (*request);
        });
    });
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

    ui.setProperty (UiIDs::spaceId, "user", nullptr);
    cacheUiState();
}

//==============================================================================
void DrenchedProcessor::startCapture (const juce::var& options)
{
    if (captureStage == CaptureStage::playing || captureStage == CaptureStage::working)
        return;

    const auto rate = preparedRate.load();
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();

    if (rate <= 0.0 || inputs < 1 || outputs < 1)
    {
        captureStage = CaptureStage::failed;
        captureMessage = "This track has no audio input. Put Drenched on a track that records the room's microphone.";
        return;
    }

    if (options.getProperty ("method", "sweep").toString() == "clap")
    {
        const auto seconds = juce::jlimit (3.0, 30.0, static_cast<double> (options.getProperty ("listenSeconds", 10.0)));

        try
        {
            capture.prepareListening (seconds, rate, std::min (inputs, 2));
        }
        catch (const std::exception&)
        {
            captureStage = CaptureStage::failed;
            captureMessage = "There isn't enough memory to listen this long. Try a shorter time.";
            return;
        }

        captureInterrupted.store (false);
        captureStage = CaptureStage::playing;
        captureMessage = "Listening. Clap once, sharply, where the sound would come from, then stay quiet while it dies away.";
        capture.start();
        return;
    }

    SweepSettings settings;
    settings.sampleRate = rate;
    settings.sweepSeconds = juce::jlimit (2.0, 30.0, static_cast<double> (options.getProperty ("sweepSeconds", 10.0)));
    settings.tailSeconds = juce::jlimit (1.0, 30.0, static_cast<double> (options.getProperty ("tailSeconds", 6.0)));
    settings.levelDb = juce::jlimit (-40.0f, -3.0f, static_cast<float> (options.getProperty ("levelDb", -12.0)));

    const auto speakers = juce::jlimit (1, std::min (outputs, 2), static_cast<int> (options.getProperty ("speakers", 1)));

    try
    {
        capture.prepare (std::make_shared<const SineSweep> (settings), speakers, std::min (inputs, 2));
    }
    catch (const std::exception&)
    {
        captureStage = CaptureStage::failed;
        captureMessage = "There isn't enough memory for a capture this long. Try a shorter sweep.";
        return;
    }

    captureInterrupted.store (false);
    captureStage = CaptureStage::playing;
    captureMessage = speakers == 2 ? "Playing the sweep from the left speaker, then the right." : "Playing the sweep from the left speaker.";
    capture.start();
}

void DrenchedProcessor::cancelCapture()
{
    if (captureStage != CaptureStage::playing)
        return;

    capture.cancel();
}

juce::var DrenchedProcessor::captureStatus() const
{
    static constexpr const char* stages[] { "idle", "playing", "working", "failed" };

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("stage", stages[static_cast<int> (captureStage)]);
    object->setProperty ("progress", static_cast<double> (captureStage == CaptureStage::playing ? capture.progress() : 0.0f));
    object->setProperty ("peakDb", static_cast<double> (juce::Decibels::gainToDecibels (capture.inputPeak(), -100.0f)));
    object->setProperty ("message", captureMessage);
    return juce::var (object.release());
}

void DrenchedProcessor::pollCapture()
{
    if (captureStage != CaptureStage::playing)
        return;

    const auto recorderState = capture.state();

    if (recorderState == CaptureRecorder::State::cancelled)
    {
        captureStage = CaptureStage::idle;
        captureMessage = "Capture stopped.";
        return;
    }

    if (recorderState != CaptureRecorder::State::finished)
    {
        if (recorderState == CaptureRecorder::State::idle && captureInterrupted.exchange (false))
        {
            captureStage = CaptureStage::failed;
            captureMessage = "The audio settings changed during the capture. Please capture again.";
        }

        return;
    }

    captureStage = CaptureStage::working;
    captureMessage = "Working out the impulse response.";

    std::weak_ptr<int> alive = lifetime;
    const auto peak = capture.inputPeak();

    loader.addJob ([this, alive, peak]
    {
        juce::String error;
        juce::File file;

        const auto clap = capture.sweep() == nullptr;

        // A clap capture picks its own clean clap, so a clipped one is only a problem if
        // every clap clipped; extractClap says so.
        if (peak < 1.0e-4f)
            error = "The microphone heard almost nothing. Check this track's input, then turn the speaker or mic gain up.";
        else if (peak >= 0.999f && ! clap)
            error = "The microphone clipped. Turn the speaker or the mic gain down and capture again.";

        if (error.isEmpty())
        {
            try
            {
                if (clap)
                {
                    file = writeCapture (extractClap (capture.recordings(), capture.sampleRate()), capture.sampleRate());
                }
                else
                {
                    const auto& sweep = *capture.sweep();
                    file = writeCapture (deconvolveCapture (sweep, capture.recordings(), capture.speakers()), sweep.settings().sampleRate);
                }

                if (file == juce::File())
                    error = "The capture couldn't be saved in " + settings::capturesFolder().getFullPathName() + ".";
            }
            catch (const ClapProblem& problem)
            {
                error = problem.what();
            }
            catch (const std::exception&)
            {
                error = "There isn't enough memory to work out this capture. Try a shorter sweep.";
            }
        }

        juce::MessageManager::callAsync ([this, alive, file, error]
        {
            if (alive.expired())
                return;

            if (error.isNotEmpty())
            {
                captureStage = CaptureStage::failed;
                captureMessage = error;
                return;
            }

            captureStage = CaptureStage::idle;
            captureMessage = "Saved as " + file.getFileName() + " in " + file.getParentDirectory().getFullPathName() + ".";
            loadImpulseResponseAsync (file);
        });
    });
}

juce::File DrenchedProcessor::writeCapture (const std::vector<std::vector<float>>& channels, double sampleRate)
{
    const auto folder = settings::capturesFolder();

    if (! folder.createDirectory())
        return {};

    const auto file = folder.getNonexistentChildFile ("Capture " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"),
                                                      ".wav", false);

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return {};

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (static_cast<int> (channels.size()))
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return {};

    std::vector<const float*> pointers;

    for (const auto& c : channels)
        pointers.push_back (c.data());

    if (! writer->writeFromFloatArrays (pointers.data(), static_cast<int> (pointers.size()), static_cast<int> (channels.front().size())))
        return {};

    writer.reset();
    return file;
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

std::array<float, maxChannels> DrenchedProcessor::takeSpeakerPeaks() noexcept
{
    std::array<float, maxChannels> out {};

    for (size_t k = 0; k < maxChannels; ++k)
        out[k] = speakerPeaks[k].exchange (0.0f);

    return out;
}

juce::var DrenchedProcessor::outputSpeakerNames() const
{
    const auto set = getChannelLayoutOfBus (false, 0);
    juce::Array<juce::var> list;

    for (const auto type : set.getChannelTypes())
    {
        auto speaker = std::make_unique<juce::DynamicObject>();
        const auto name = set.size() == 1 ? juce::String ("M") : juce::AudioChannelSet::getAbbreviatedChannelTypeName (type);
        speaker->setProperty ("name", name);
        speaker->setProperty ("wet", type != juce::AudioChannelSet::LFE && type != juce::AudioChannelSet::LFE2);
        list.add (juce::var (speaker.release()));
    }

    return list;
}

bool DrenchedProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Any output from mono to 9.1.6 whose speakers have a place; the input
    // mono, stereo or the same as the output.
    if (! channelLayouts::isSupported (out))
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo() || in == out;
}

void DrenchedProcessor::prepareToPlay (double sampleRate, int)
{
    // A capture can't carry on across new audio settings: the audio thread is stopped now,
    // so the take is dropped and the message thread says why.
    if (capture.state() == CaptureRecorder::State::running)
    {
        capture.reset();
        captureInterrupted.store (true);
    }

    auto inputs = channelLayouts::speakersOf (getChannelLayoutOfBus (true, 0));
    auto outputs = channelLayouts::speakersOf (getChannelLayoutOfBus (false, 0));

    if (inputs.empty())
        inputs = layouts::stereo();

    if (outputs.empty())
        outputs = layouts::stereo();

    {
        const std::lock_guard lock (layoutLock);
        outputSpeakers = outputs;
    }

    reverb.prepare (sampleRate, inputs, outputs);
    preparedRate.store (sampleRate);
    updateLatency (sampleRate);
    loadLibrarySpace (chosenSpaceId(), true);
    cacheUiState();

    // Build the first response now, so playback (and offline bounces) start with the right sound.
    try
    {
        if (auto request = currentRequest (sampleRate))
            builder->buildNow (*request);
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

    const auto notePeak = [this, &buffer, numSamples] (size_t slot, int numChannels)
    {
        for (int meter = 0; meter < 2; ++meter)
        {
            // A mono bus shows on both meters.
            const auto channel = std::min (meter, numChannels - 1);

            if (channel < 0)
                return;

            auto& peak = peaks[slot + static_cast<size_t> (meter)];
            const auto level = buffer.getMagnitude (channel, 0, numSamples);

            // Hold the highest value until the editor collects it.
            auto current = peak.load (std::memory_order_relaxed);

            while (level > current && ! peak.compare_exchange_weak (current, level, std::memory_order_relaxed))
            {}
        }
    };

    // While capturing a space, the outputs play only the sweep.
    if (capture.process (buffer.getArrayOfReadPointers(), numInputs, buffer.getArrayOfWritePointers(), numOutputs,
                         static_cast<size_t> (numSamples)))
    {
        notePeak (2, numOutputs);
        return;
    }

    notePeak (0, numInputs);

    ReverbControls controls;
    controls.mix = params.mix->load() / 100.0f;
    controls.outputGain = decibelsToGain (params.output->load());
    controls.predelayMs = predelayMs (params.predelay->load());
    controls.brightnessDb = params.brightness->load();
    controls.lowCutHz = params.wetLowCut->load();
    controls.highCutHz = params.wetHighCut->load();
    controls.widthPercent = params.width->load();
    controls.cloudPercent = params.cloudSize->load();
    controls.ducking = params.ducking->load() / 100.0f;
    controls.monoBelowHz = params.monoBelow->load();
    controls.freeze = params.freeze->load() >= 0.5f;
    controls.crossoverHz = params.crossover->load();
    controls.movement = params.movement->load() / 100.0f;
    controls.movementRateHz = params.movementRate->load();

    for (size_t k = 0; k < maxChannels; ++k)
    {
        controls.speakerLevelDb[k] = speakerLevelDb[k].load (std::memory_order_relaxed);
        controls.speakerDelayMs[k] = speakerDelayMs[k].load (std::memory_order_relaxed);
    }

    // After the trial, without a licence, only the dry signal plays.
    if (! licence->isAudioAllowed())
        controls.mix = 0.0f;

    reverb.process (buffer.getArrayOfWritePointers(), static_cast<size_t> (buffer.getNumChannels()),
                    static_cast<size_t> (numSamples), controls);

    notePeak (2, numOutputs);

    for (int channel = 0; channel < std::min (numOutputs, static_cast<int> (maxChannels)); ++channel)
    {
        auto& peak = speakerPeaks[static_cast<size_t> (channel)];
        const auto level = buffer.getMagnitude (channel, 0, numSamples);
        auto current = peak.load (std::memory_order_relaxed);

        while (level > current && ! peak.compare_exchange_weak (current, level, std::memory_order_relaxed))
        {}
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

    restoreImpulseResponse();
    loadLibrarySpace (chosenSpaceId(), true);
    cacheUiState();

    if (const auto rate = preparedRate.load(); rate > 0.0)
    {
        updateLatency (rate);

        if (auto request = currentRequest (rate))
            builder->request (*request);
    }
}

} // namespace drenched

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drenched::DrenchedProcessor();
}

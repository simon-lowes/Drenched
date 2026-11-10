#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "CrashReports.h"
#include "Settings.h"
#include "ChannelLayouts.h"
#include "MovedFiles.h"
#include "RecordingCache.h"

#include <drenched/ClapCapture.h>
#include <drenched/Mix.h>
#include <drenched/RoomDescription.h>
#include <drenched/RoomMatch.h>
#include <drenched/SceneNet.h>
#include <drenched/SofaResponse.h>
#include <drenched/Spaces.h>

#include <DrenchedScene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace drenched
{

namespace
{
    const juce::Identifier stateVersionID { "version" };
    constexpr const char* stateType = "DrenchedState"; // the session's outermost tag

    // How loud a loaded response plays (see LoadedLevel). New instances write "fileRate"
    // and keep it in every session they save. A session saved before it was written
    // doesn't have it, and plays at the session rate's level, as it always did, however
    // often it is loaded and saved again.
    const juce::Identifier loadedLevelID { "loadedLevel" };
    constexpr const char* fileRateLevel = "fileRate";

    /** Where a session's own impulse response file is now: where it was saved, or where
        it most likely moved to (see MovedFiles.h). {} when it can't be found. */
    juce::File findImpulseResponse (const juce::String& savedPath)
    {
        return movedFiles::find (savedPath, juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                 { settings::loadIrFolder(), settings::capturesFolder(), settings::importedFolder() },
                                 settings::loadTrustedShares());
    }
}

DrenchedProcessor::DrenchedProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, stateType, createParameterLayout())
{
    const auto raw = [this] (const char* id)
    {
        auto* parameter = state.getParameter (id);
        return Setting (state.getRawParameterValue (id), parameter->convertFrom0to1 (parameter->getDefaultValue()));
    };

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
    params.listenerX = raw (ParamIDs::listenerX);
    params.sourceX = raw (ParamIDs::sourceX);
    params.lowCpu = raw (ParamIDs::lowCpu);
    params.scene = raw (ParamIDs::scene);

    for (size_t band = 0; band < params.eqFrequency.size(); ++band)
    {
        params.eqFrequency[band] = raw (ParamIDs::eqFrequency[band]);
        params.eqGain[band] = raw (ParamIDs::eqGain[band]);
        params.eqQ[band] = raw (ParamIDs::eqQ[band]);
        params.eqShape[band] = raw (ParamIDs::eqShape[band]);
        params.eqOn[band] = raw (ParamIDs::eqOn[band]);
    }

    // A new instance opens on the first recorded space, with the graph on its measured decay.
    if (const auto& recordings = library::spaces(); ! recordings.empty())
    {
        getUiState().setProperty (UiIDs::spaceId, recordings.front().id, nullptr);

        if (const auto* bands = recordings.front().info["bands"].getArray())
            for (size_t band = 0; band < params.decay.size() && static_cast<int> (band) < bands->size(); ++band)
                for (const auto* id : { ParamIDs::decay[band], ParamIDs::decayRight[band] })
                    if (auto* parameter = state.getParameter (id))
                        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (static_cast<double> ((*bands)[static_cast<int> (band)]))));
    }

    for (size_t band = 0; band < params.decay.size(); ++band)
    {
        params.decay[band] = raw (ParamIDs::decay[band]);
        params.decayRight[band] = raw (ParamIDs::decayRight[band]);
    }

    params.decayLink = raw (ParamIDs::decayLink);

    state.state.setProperty (loadedLevelID, fileRateLevel, nullptr);

    builder = std::make_unique<KernelBuilder> (reverb);
    cacheUiState();
    state.state.addListener (this);
    keepSessionForSaving();
    startTimerHz (20);

    // A crash in Drenched leaves a report to offer next time (CrashReports.h). Tests that
    // crash on purpose turn it off with DRENCHED_CRASH_REPORTS=0.
    if (juce::SystemStats::getEnvironmentVariable ("DRENCHED_CRASH_REPORTS", "1") != "0")
        crashReports::install (settings::crashFolder(), aboutThisBuild());

    crashReports::step ("opened in " + juce::String (juce::PluginHostType().getHostDescription()));
}

DrenchedProcessor::~DrenchedProcessor()
{
    stopTimer();
    state.state.removeListener (this);
    lifetime.reset();

    // A capture still being worked out reads the recorder through this, so it has to end
    // before the recorder does: waiting for ever is better than freeing it under the job.
    loader.removeAllJobs (true, -1);
    builder.reset();
}

double DrenchedProcessor::getTailLengthSeconds() const
{
    const auto seconds = reverb.tailSeconds();

    // CLAP counts the tail in samples, and its wrapper converts without a check, so a frozen
    // (endless) tail would reach the host as none at all and the host could stop the plugin
    // as soon as its input went quiet. INT32_MAX samples is how CLAP says endless.
    if (is_clap && std::isinf (seconds))
        if (const auto rate = getSampleRate() > 0.0 ? getSampleRate() : preparedRate.load(); rate > 0.0)
            return static_cast<double> (std::numeric_limits<std::int32_t>::max()) / rate;

    return seconds;
}

juce::String DrenchedProcessor::chosenSpaceId()
{
    return chosenSpaceId (getUiState());
}

juce::String DrenchedProcessor::chosenSpaceId (const juce::ValueTree& ui)
{
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
    else if (id == "described")
        index = describedBase (ui.getProperty (UiIDs::roomKind, "").toString());
    else if (library::find (id) != nullptr)
        index = librarySpace;
    else
        for (size_t i = 0; i < bundledSpaces.size(); ++i)
            if (id == bundledSpaces[i].id)
                index = static_cast<int> (i);

    if (index == librarySpace)
        loadLibrarySpace (id, false);

    const auto metres = describedSize (ui, id);
    describedWidth.store (metres[0]);
    describedDepth.store (metres[1]);
    describedHeight.store (metres[2]);

    {
        const std::lock_guard lock (recordedLock);
        keptSpaceId = id;
    }

    spaceIndex.store (index);
    loadedLevel.store (state.state.hasProperty (loadedLevelID) ? LoadedLevel::fileRate : LoadedLevel::sessionRate);
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
    request.roomMetres = { describedWidth.load(), describedDepth.load(), describedHeight.load() }; // zeros but for a described room

    if (request.space == userSpace)
    {
        // A loaded response, or the first bundled space while its file is missing.
        request.space = 0;
        const std::lock_guard lock (recordedLock);
        request.recorded = recorded;

        if (recordedWanted.isNotEmpty())
            return std::nullopt;
    }
    else if (request.space == librarySpace)
    {
        // The chosen recording. Until it has loaded the room stays as it was, rather than
        // playing the last recording with the new space's settings (a scene's, say).
        request.space = 0;
        const std::lock_guard lock (recordedLock);
        request.recorded = libraryResponse;

        if (libraryResponse == nullptr || libraryLoadedId != libraryWanted)
            return std::nullopt;
    }

    // A modelled space plays the same in any session, so its requests don't differ by it.
    if (request.recorded != nullptr)
        request.loadedLevel = loadedLevel.load();

    {
        const std::lock_guard lock (layoutLock);
        request.outputs = outputSpeakers;
    }

    request.listenerX = params.listenerX.load() / 100.0f;
    request.sourceX = params.sourceX.load() / 100.0f;
    request.sizePercent = params.size.load();
    request.distancePercent = params.distance.load();
    request.earlyLatePercent = params.earlyLate.load();
    request.livingPercent = params.living.load();
    request.reverse = params.reverse.load() >= 0.5f;
    request.gateMs = params.gate.load();
    request.attackMs = params.attack.load();
    request.directLevelDb = params.directLevel.load();
    request.directDelayMs = params.directDelay.load();
    request.directColourDb = params.directColour.load();
    request.crossFeedPercent = params.crossFeed.load();
    request.latencySamples = wantedLatency (sampleRate);

    for (size_t band = 0; band < params.decay.size(); ++band)
        request.decaySeconds[band] = params.decay[band].load();

    if (params.decayLink.load() < 0.5f)
        for (size_t band = 0; band < params.decayRight.size(); ++band)
            request.rightDecaySeconds[band] = params.decayRight[band].load();

    return request;
}

int DrenchedProcessor::wantedLatency (double sampleRate) const
{
    return params.lowCpu.load() >= 0.5f ? static_cast<int> (ConvolutionReverb::lowCpuLatency (sampleRate)) : 0;
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
    applyPendingSession();

    // Bouncing, the audio thread follows the scene parameter itself, block by block.
    if (! isNonRealtime())
        followSceneParameter();

    cacheUiState();
    keepSessionForSaving();
    pollCapture();

    if (const auto rate = preparedRate.load(); rate > 0.0)
        updateLatency (rate);

    // Bouncing offline, render() builds changes itself, at the block they happen.
    if (const auto rate = preparedRate.load(); rate > 0.0 && ! isNonRealtime())
        if (auto request = currentRequest (rate))
            builder->request (*request);

    // Once a tick, however many changes there were.
    if (std::exchange (hostShouldMarkChanged, false))
        updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true));
}

float DrenchedProcessor::predelayMs (float requested)
{
    if (params.predelaySync.load() < 0.5f)
        return requested;

    // Synced: snap to the nearest 64th note at the host tempo, if it is one a host could
    // play at. A tempo that can't be (infinite, say) leaves the predelay as set.
    if (auto* head = getPlayHead())
    {
        if (const auto position = head->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0 && *bpm < 10000.0)
            {
                const auto step = static_cast<float> (60000.0 / *bpm / 16.0);
                return juce::jlimit (0.0f, 1000.0f, std::round (requested / step) * step);
            }
        }
    }

    return requested;
}

//==============================================================================
std::shared_ptr<const RecordedResponse> DrenchedProcessor::readSofaFile (const juce::File& file, juce::String& error)
{
    // SOFA files of many room measurements can be large; this keeps the
    // whole file and its decoded responses within reason.
    constexpr juce::int64 maxBytes = 512 * 1024 * 1024;

    if (file.getSize() > maxBytes)
    {
        error = file.getFileName() + " is too large to load.";
        return nullptr;
    }

    try
    {
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data) || data.getSize() == 0)
        {
            error = "Drenched couldn't read " + file.getFileName() + ".";
            return nullptr;
        }

        return prepareSofaResponse (data.getData(), data.getSize(), file.getFileNameWithoutExtension().toStdString());
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

std::shared_ptr<const RecordedResponse> DrenchedProcessor::readImpulseResponse (const juce::File& file, juce::String& error)
{
    // A session or scene from someone else may name a file on their computer; looking at it
    // would sign in to that computer as this person. Shares chosen from here are fine.
    if (! movedFiles::mayOpen (file.getFullPathName(), settings::loadTrustedShares()))
    {
        error = file.getFileName() + " is on a network share Drenched hasn't been used with on this computer. Load it once yourself to use it.";
        return nullptr;
    }

    if (! file.existsAsFile())
    {
        error = "Drenched can't find " + file.getFileName() + ".";
        return nullptr;
    }

    const auto key = RecordingCache::keyOf (file);

    if (auto kept = RecordingCache::shared().find (key))
        return kept;

    // Reading or preparing a large file can run out of memory, in a 32-bit host above all.
    // Nothing above this expects an exception: on a loader thread it would end the host.
    try
    {
        auto response = readImpulseResponseFile (file, error);
        RecordingCache::shared().add (key, response);
        return response;
    }
    catch (const std::exception&)
    {
        error = file.getFileName() + " is too large to load.";
        return nullptr;
    }
}

std::shared_ptr<const RecordedResponse> DrenchedProcessor::readImpulseResponseFile (const juce::File& file, juce::String& error)
{
    if (file.hasFileExtension ("sofa"))
        return readSofaFile (file, error);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    // .amb files are WAV files under another name.
    if (reader == nullptr)
        reader.reset (formats.createReaderFor (file.createInputStream()));

    if (reader == nullptr)
    {
        error = file.getFileName() + " isn't an audio file Drenched can read. Use WAV, AIFF, FLAC or SOFA.";
        return nullptr;
    }

    return prepareFromReader (*reader, file.getFileNameWithoutExtension(), file.getFileName(), error,
                              channelLayouts::bFormatOf (file, reader->metadataValues, static_cast<int> (reader->numChannels)));
}

void DrenchedProcessor::loadLibrarySpace (const juce::String& id, bool now)
{
    const auto* space = library::find (id);

    if (space == nullptr)
        return;

    {
        const std::lock_guard lock (recordedLock);

        // Chosen again while another space loads: that load is no longer wanted, and the
        // room this one plays is current again.
        if (libraryLoadedId == id && libraryResponse != nullptr)
        {
            libraryWanted = id;
            return;
        }

        if (! now && libraryWanted == id)
            return;

        libraryWanted = id;
    }

    // A space played recently is still prepared.
    if (auto kept = RecordingCache::shared().find (RecordingCache::keyOf (id)))
    {
        const std::lock_guard lock (recordedLock);
        libraryResponse = std::move (kept);
        libraryLoadedId = id;
        return;
    }

    if (now)
    {
        juce::String error;
        auto loaded = library::load (*space, error);
        RecordingCache::shared().add (RecordingCache::keyOf (id), loaded);
        const std::lock_guard lock (recordedLock);
        libraryResponse = std::move (loaded);
        libraryLoadedId = id;
        return;
    }

    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([this, space, id, alive]
    {
        // Clicking quickly through the browser queues a load per space; only the last
        // one chosen is worth reading.
        {
            const std::lock_guard lock (recordedLock);

            // Or it was read at once meanwhile, for a bounce.
            if (libraryWanted != id || (libraryLoadedId == id && libraryResponse != nullptr))
                return;
        }

        juce::String error;
        auto loaded = library::load (*space, error);
        RecordingCache::shared().add (RecordingCache::keyOf (id), loaded);

        juce::MessageManager::callAsync ([this, id, alive, loaded = std::move (loaded)]() mutable
        {
            if (alive.expired())
                return;

            {
                const std::lock_guard lock (recordedLock);

                // Another space was chosen meanwhile, or this one was read at once while
                // this read was going (prepareToPlay, as a new plugin is): it plays already,
                // so a second copy would only build the same room again.
                if (libraryWanted != id || (libraryLoadedId == id && libraryResponse != nullptr))
                    return;

                libraryResponse = std::move (loaded);
                libraryLoadedId = id;
            }

            if (const auto rate = preparedRate.load(); rate > 0.0)
                if (auto request = currentRequest (rate))
                    builder->request (*request, false); // a choice, not a drag
        });
    });
}

void DrenchedProcessor::loadImpulseResponseAsync (const juce::File& file, bool keepSettings)
{
    crashReports::step ("loading an impulse response (" + file.getFileExtension() + ", "
                        + juce::String (file.getSize() / 1024) + " KB)");
    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([this, file, alive, keepSettings]
    {
        juce::String error;
        auto loaded = readImpulseResponse (file, error);

        juce::MessageManager::callAsync ([this, file, alive, loaded = std::move (loaded), error, keepSettings]() mutable
        {
            if (! alive.expired())
                applyLoadedImpulseResponse (file, std::move (loaded), error, ! keepSettings);
        });
    });
}

void DrenchedProcessor::importImpulseResponseAsync (const juce::String& fileName, juce::MemoryBlock data,
                                                    bool keepSettings, const juce::File& folder)
{
    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([this, fileName, data = std::move (data), folder, alive, keepSettings]
    {
        juce::String error;
        juce::File file;
        std::shared_ptr<const RecordedResponse> loaded;

        try
        {
            file = keepImportedFile (folder, fileName, data, error);
            loaded = file != juce::File() ? readImpulseResponse (file, error) : nullptr;
        }
        catch (const std::exception&) // std::bad_alloc, comparing with a large file kept before
        {
            error = fileName + " is too large to load.";
        }

        juce::MessageManager::callAsync ([this, file, alive, loaded = std::move (loaded), error, keepSettings]() mutable
        {
            if (! alive.expired())
                applyLoadedImpulseResponse (file, std::move (loaded), error, ! keepSettings);
        });
    });
}

juce::File DrenchedProcessor::keepImportedFile (const juce::File& folder, const juce::String& fileName,
                                                const juce::MemoryBlock& data, juce::String& error)
{
    auto name = juce::File::createLegalFileName (fileName.fromLastOccurrenceOf ("/", false, false)
                                                         .fromLastOccurrenceOf ("\\", false, false)).trim();

    if (name.isEmpty() || name.startsWithChar ('.'))
        name = "Dropped" + name;

    if (data.isEmpty())
    {
        error = name + " is empty.";
        return {};
    }

    if (const auto made = folder.createDirectory(); made.failed())
    {
        error = "Drenched can't keep a copy of " + name + " in " + folder.getFullPathName() + ": " + made.getErrorMessage();
        return {};
    }

    const auto plain = folder.getChildFile (name);
    auto file = plain;

    for (int n = 2; file.exists(); ++n)
    {
        juce::MemoryBlock existing;

        if (file.existsAsFile() && file.getSize() == static_cast<juce::int64> (data.getSize())
            && file.loadFileAsData (existing) && existing == data)
            return file;

        file = folder.getChildFile (plain.getFileNameWithoutExtension() + " " + juce::String (n) + plain.getFileExtension());
    }

    if (! file.replaceWithData (data.getData(), data.getSize()))
    {
        error = "Drenched can't keep a copy of " + name + " in " + folder.getFullPathName() + ". Is the disk full?";
        return {};
    }

    return file;
}

void DrenchedProcessor::loadSceneImpulseResponseAsync (const juce::File& file)
{
    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([this, file, alive]
    {
        juce::String error;
        auto loaded = readImpulseResponse (file, error);

        juce::MessageManager::callAsync ([this, file, alive, loaded = std::move (loaded), error]() mutable
        {
            if (! alive.expired())
                sceneImpulseResponseRead (file, std::move (loaded), error);
        });
    });
}

void DrenchedProcessor::sceneImpulseResponseRead (const juce::File& file, std::shared_ptr<const RecordedResponse> loaded,
                                                  const juce::String& error)
{
    // The recall that asked for the file was the change, if it was the person's.
    const juce::ScopedValueSetter<bool> quiet (quietChanges, true);

    {
        const std::lock_guard lock (recordedLock);

        if (recordedWanted == file.getFullPathName())
            recordedWanted = {};
    }

    // Another scene, or the person, may have chosen something else meanwhile: another file,
    // or a space that isn't a file at all.
    if (getUiState().getProperty (UiIDs::irPath, "").toString() != file.getFullPathName() || chosenSpaceId() != "user")
        return;

    // A file that can't be read is missing, as it is when a session names one, rather than
    // leaving the last file playing under the scene's name.
    if (loaded == nullptr)
    {
        const std::lock_guard lock (recordedLock);
        recorded.reset();
        recordedPath = file.getFullPathName();
        recordedMissing = true;
    }

    applyLoadedImpulseResponse (file, std::move (loaded), error, false);

    // The scene changed rooms on the cut, so there is nothing to wait to settle.
    if (const auto rate = preparedRate.load(); rate > 0.0)
        if (auto request = currentRequest (rate))
            builder->request (*request, false);
}

//==============================================================================
juce::var DrenchedProcessor::matchRecording (const juce::File& file)
{
    constexpr double longestSeconds = 600.0; // enough to find the pauses in; more only takes longer
    constexpr double shortestSeconds = 5.0;
    constexpr int block = 65536;

    auto result = std::make_unique<juce::DynamicObject>();
    const auto failed = [&result] (const juce::String& why)
    {
        result->setProperty ("error", why);
        return juce::var (result.release());
    };

    if (! file.existsAsFile())
        return failed ("Drenched can't find " + file.getFileName() + ".");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels < 1)
        return failed (file.getFileName() + " isn't an audio file Drenched can read. Use WAV, AIFF or FLAC.");

    const auto channels = static_cast<int> (std::min (reader->numChannels, 64u));
    const auto length = std::min (reader->lengthInSamples, static_cast<juce::int64> (longestSeconds * reader->sampleRate));

    if (static_cast<double> (length) < shortestSeconds * reader->sampleRate)
        return failed (file.getFileName() + " is too short. Choose a recording at least five seconds long.");

    RoomListener listener (reader->sampleRate);
    juce::AudioBuffer<float> buffer (channels, block);
    std::vector<float> mono (static_cast<size_t> (block));

    for (juce::int64 pos = 0; pos < length; pos += block)
    {
        const auto n = static_cast<int> (std::min<juce::int64> (block, length - pos));

        if (! reader->read (&buffer, 0, n, pos, true, true))
            return failed (file.getFileName() + " couldn't be read to the end.");

        std::fill (mono.begin(), mono.end(), 0.0f);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                mono[static_cast<size_t> (i)] += buffer.getSample (ch, i) / static_cast<float> (channels);

        listener.listen (mono.data(), static_cast<size_t> (n));
    }

    RoomEstimate heard;

    try
    {
        heard = listener.estimate();
    }
    catch (const RoomMatchProblem& problem)
    {
        return failed (problem.what());
    }

    // The closest space: a recorded one, or a modelled one.
    juce::String closestId, closestName;
    auto closest = std::numeric_limits<float>::max();

    const auto consider = [&] (const juce::String& id, const juce::String& name, const std::array<float, 8>& decay)
    {
        if (const auto distance = decayDistance (heard.decaySeconds, decay); distance < closest)
        {
            closest = distance;
            closestId = id;
            closestName = name;
        }
    };

    for (const auto& space : library::spaces())
    {
        if (const auto* bands = space.info["bands"].getArray(); bands != nullptr && bands->size() == 8)
        {
            std::array<float, 8> decay {};

            for (size_t b = 0; b < 8; ++b)
                decay[b] = static_cast<float> (static_cast<double> ((*bands)[static_cast<int> (b)]));

            consider (space.id, space.info["name"].toString(), decay);
        }
    }

    for (const auto& space : bundledSpaces)
        consider (space.id, space.name, space.decaySeconds);

    juce::Array<juce::var> bands, counts;

    for (size_t b = 0; b < 8; ++b)
    {
        bands.add (static_cast<double> (heard.decaySeconds[b]));
        counts.add (heard.decaysHeard[b]);
    }

    result->setProperty ("bands", bands);
    result->setProperty ("heard", counts);
    result->setProperty ("spaceId", closestId);
    result->setProperty ("spaceName", closestName);
    result->setProperty ("file", file.getFileName());
    result->setProperty ("seconds", listener.secondsHeard());
    return juce::var (result.release());
}

void DrenchedProcessor::matchRoomAsync (const juce::File& file, std::function<void (const juce::var&)> done)
{
    std::weak_ptr<int> alive = lifetime;

    loader.addJob ([file, alive, done = std::move (done)]() mutable
    {
        auto result = matchRecording (file);

        juce::MessageManager::callAsync ([alive, done = std::move (done), result = std::move (result)]
        {
            if (! alive.expired())
                done (result);
        });
    });
}

//==============================================================================
// Describe a room

namespace
{
    juce::String numbers (const float* values, std::size_t count)
    {
        juce::StringArray out;

        for (std::size_t i = 0; i < count; ++i)
            out.add (juce::String (static_cast<double> (values[i]), 3));

        return out.joinIntoString (" ");
    }

    juce::Array<juce::var> numbersOf (const juce::String& text)
    {
        juce::Array<juce::var> out;

        for (const auto& item : juce::StringArray::fromTokens (text, " ", ""))
            if (const auto value = item.getDoubleValue(); std::isfinite (value))
                out.add (value);

        return out;
    }
}

int DrenchedProcessor::describedBase (const juce::String& kind) noexcept
{
    // The modelled space whose early pattern and tail the described room borrows: the
    // open air borrows the hall's sparse early reflections, the underground the car park's.
    for (size_t i = 0; i < bundledSpaces.size(); ++i)
        if (kind == juce::String (bundledSpaces[i].id).fromLastOccurrenceOf ("-", false, false))
            return static_cast<int> (i);

    if (kind == "outdoor")
        return describedBase ("hall");

    if (kind == "underground")
        return describedBase ("garage");

    return describedBase ("room");
}

std::array<float, 3> DrenchedProcessor::describedSize (const juce::ValueTree& ui, const juce::String& chosenId)
{
    std::array<float, 3> metres {};

    if (chosenId != "described")
        return metres;

    const auto sides = juce::StringArray::fromTokens (ui.getProperty (UiIDs::roomSize, "").toString(), " ", "");

    if (sides.size() != 3)
        return metres;

    for (int i = 0; i < 3; ++i)
    {
        const auto side = sides[i].getFloatValue();

        if (! std::isfinite (side) || side <= 0.0f)
            return {};

        metres[static_cast<size_t> (i)] = side;
    }

    return metres;
}

juce::var DrenchedProcessor::describeRoom (const juce::String& words, const juce::String& name, bool apply, const juce::String& from)
{
    const auto text = words.trim().substring (0, 400);
    const auto room = drenched::describeRoom (text.toStdString());
    const auto shown = name.trim().isNotEmpty() ? name.trim().substring (0, 64) : juce::String (room.name);
    const auto source = from == "photo" ? juce::String ("photo") : juce::String ("words");

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("name", shown);
    object->setProperty ("kind", juce::String (room.kind));
    object->setProperty ("place", juce::String (room.place));
    object->setProperty ("W", static_cast<double> (room.width));
    object->setProperty ("D", static_cast<double> (room.depth));
    object->setProperty ("H", static_cast<double> (room.height));
    object->setProperty ("mat", juce::String (room.materials));

    juce::Array<juce::var> bands;

    for (const auto t : room.decaySeconds)
        bands.add (static_cast<double> (t));

    object->setProperty ("bands", bands);
    object->setProperty ("pre", static_cast<double> (room.predelayMs));
    object->setProperty ("dist", static_cast<double> (room.distancePercent));
    object->setProperty ("el", static_cast<double> (room.earlyLatePercent));
    object->setProperty ("words", text);

    juce::Array<juce::var> understood;

    for (const auto& word : room.understood)
        understood.add (juce::String (word));

    object->setProperty ("understood", understood);
    object->setProperty ("known", room.knownRoom);
    object->setProperty ("from", source);
    object->setProperty ("applied", apply);

    if (apply)
    {
        auto ui = getUiState();
        const std::array<float, 3> size { room.width, room.depth, room.height };
        ui.setProperty (UiIDs::roomWords, text, nullptr);
        ui.setProperty (UiIDs::roomName, shown, nullptr);
        ui.setProperty (UiIDs::roomFrom, source, nullptr);
        ui.setProperty (UiIDs::roomKind, juce::String (room.kind), nullptr);
        ui.setProperty (UiIDs::roomSize, numbers (size.data(), size.size()), nullptr);
        ui.setProperty (UiIDs::roomDecay, numbers (room.decaySeconds.data(), room.decaySeconds.size()), nullptr);
        ui.setProperty (UiIDs::roomPredelay, static_cast<double> (room.predelayMs), nullptr);
        ui.setProperty (UiIDs::roomPlace, juce::String (room.place), nullptr);
        ui.setProperty (UiIDs::roomMaterials, juce::String (room.materials), nullptr);
        ui.setProperty (UiIDs::spaceId, "described", nullptr);
        cacheUiState();
    }

    return juce::var (object.release());
}

juce::var DrenchedProcessor::describedRoomInfo()
{
    auto ui = getUiState();

    if (! ui.hasProperty (UiIDs::roomSize))
        return {};

    const auto size = numbersOf (ui.getProperty (UiIDs::roomSize, "").toString());
    const auto bands = numbersOf (ui.getProperty (UiIDs::roomDecay, "").toString());

    if (size.size() != 3 || bands.size() != 8)
        return {};

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("name", ui.getProperty (UiIDs::roomName, "").toString());
    object->setProperty ("kind", ui.getProperty (UiIDs::roomKind, "").toString());
    object->setProperty ("place", ui.getProperty (UiIDs::roomPlace, "").toString());
    object->setProperty ("W", size[0]);
    object->setProperty ("D", size[1]);
    object->setProperty ("H", size[2]);
    object->setProperty ("mat", ui.getProperty (UiIDs::roomMaterials, "").toString());
    object->setProperty ("bands", bands);
    object->setProperty ("pre", static_cast<double> (ui.getProperty (UiIDs::roomPredelay, 0.0)));
    object->setProperty ("words", ui.getProperty (UiIDs::roomWords, "").toString());
    object->setProperty ("from", ui.getProperty (UiIDs::roomFrom, "words").toString());
    return juce::var (object.release());
}

void DrenchedProcessor::lookAtPhotoAsync (const juce::String& name, std::function<std::vector<std::uint8_t>()> read,
                                          std::function<void (const juce::var&)> done)
{
    std::weak_ptr<int> alive = lifetime;

    analyser.addJob ([this, name, alive, read = std::move (read), done = std::move (done)]() mutable
    {
        juce::String words, error;
        juce::Array<juce::var> seen;

        try
        {
            const auto rgb = read();

            if (rgb.size() != static_cast<std::size_t> (SceneNet::side * SceneNet::side * 3))
                throw std::runtime_error ("the picture is not the size the model takes");

            int size = 0;
            const auto* data = DrenchedScene::getNamedResource ("places365_drsn", size);

            if (data == nullptr)
                throw std::runtime_error ("this build has no scene model");

            // Read for each look: the model is only wanted now and then, and large.
            const SceneNet model (reinterpret_cast<const std::uint8_t*> (data), static_cast<std::size_t> (size));
            const auto reading = model.look (rgb.data());
            words = sceneWords (reading);

            for (const auto& category : reading.categories)
            {
                auto* object = new juce::DynamicObject();
                object->setProperty ("name", juce::String (category.name).replaceCharacter ('_', ' '));
                object->setProperty ("probability", static_cast<double> (category.probability));
                object->setProperty ("indoors", category.indoors);
                seen.add (juce::var (object));
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }

        juce::MessageManager::callAsync ([this, alive, name, words, error, seen, done = std::move (done)]
        {
            if (alive.expired())
                return;

            if (error.isNotEmpty())
            {
                auto object = std::make_unique<juce::DynamicObject>();
                object->setProperty ("name", name);
                object->setProperty ("error", error);
                done (juce::var (object.release()));
                return;
            }

            auto result = describeRoom (words, name, true, "photo");
            result.getDynamicObject()->setProperty ("seen", seen);
            done (result);
        });
    });
}

void DrenchedProcessor::describePhotoAsync (const juce::File& file, std::function<void (const juce::var&)> done)
{
    lookAtPhotoAsync (file.getFileNameWithoutExtension(), [file]
    {
        if (! file.existsAsFile())
            throw std::runtime_error ("the file isn't there");

        auto image = juce::ImageFileFormat::loadFrom (file);

        if (! image.isValid())
            throw std::runtime_error ("Drenched reads JPEG, PNG and GIF pictures, and this isn't one it can read");

        // Halved (each new pixel the mean of four) until it is a sensible size, then every
        // pixel counted into the model's 224 x 224.
        while (image.getWidth() > 2048 || image.getHeight() > 2048)
            image = image.rescaled (std::max (1, image.getWidth() / 2), std::max (1, image.getHeight() / 2), juce::Graphics::highResamplingQuality);

        const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::readOnly);
        std::vector<std::uint8_t> rgb (static_cast<std::size_t> (bitmap.width) * static_cast<std::size_t> (bitmap.height) * 3);

        for (int y = 0; y < bitmap.height; ++y)
        {
            auto* out = rgb.data() + static_cast<std::size_t> (y) * static_cast<std::size_t> (bitmap.width) * 3;

            for (int x = 0; x < bitmap.width; ++x)
            {
                const auto colour = bitmap.getPixelColour (x, y);
                out[3 * x] = colour.getRed();
                out[3 * x + 1] = colour.getGreen();
                out[3 * x + 2] = colour.getBlue();
            }
        }

        return scaleForSceneNet (rgb.data(), bitmap.width, bitmap.height, 3, bitmap.width * 3);
    }, std::move (done));
}

void DrenchedProcessor::describePhotoAsync (const juce::String& name, std::vector<std::uint8_t> rgb, std::function<void (const juce::var&)> done)
{
    lookAtPhotoAsync (name, [rgb = std::move (rgb)] { return rgb; }, std::move (done));
}

std::shared_ptr<const RecordedResponse> DrenchedProcessor::playingRecording() const
{
    const auto built = builder->lastBuilt();
    return built ? built->recorded : nullptr;
}

bool DrenchedProcessor::roomIsCurrent() const
{
    const auto rate = preparedRate.load();

    if (rate <= 0.0)
        return false;

    const auto wanted = currentRequest (rate);
    return wanted.has_value() && wanted == builder->lastBuilt();
}

bool DrenchedProcessor::hasBackgroundWork() const
{
    // Each job posts what it hands back before it finishes, so once the pools are empty
    // it is already in the message queue.
    return loader.getNumJobs() > 0 || analyser.getNumJobs() > 0 || ! builder->waitUntilIdle (std::chrono::milliseconds (0));
}

void DrenchedProcessor::analyseResponseAsync (std::function<void (const juce::var&)> done)
{
    std::weak_ptr<int> alive = lifetime;
    auto responses = builder->lastResponses();

    analyser.addJob ([responses = std::move (responses), alive, done = std::move (done)]() mutable
    {
        juce::var result;

        try
        {
            if (responses != nullptr)
                result = analysisToVar (analyseResponse (*responses));
        }
        catch (const std::exception&) {} // std::bad_alloc: the displays stay as they were

        juce::MessageManager::callAsync ([alive, done = std::move (done), result = std::move (result)]
        {
            if (! alive.expired())
                done (result);
        });
    });
}

juce::var DrenchedProcessor::analysisToVar (const ResponseAnalysis& analysis)
{
    const auto tenths = [] (double v) { return std::round (v * 10.0) / 10.0; };
    const auto list = [&tenths] (const auto& values)
    {
        juce::Array<juce::var> out;

        for (const auto v : values)
            out.add (tenths (static_cast<double> (v)));

        return juce::var (out);
    };

    juce::Array<juce::var> waterfall;

    for (const auto& slice : analysis.waterfall)
        waterfall.add (list (slice));

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("seconds", std::round (analysis.seconds * 1000.0) / 1000.0);
    object->setProperty ("bands", list (analysisBands));
    object->setProperty ("waterfall", waterfall);
    object->setProperty ("sliceSeconds", analysis.sliceSeconds);
    object->setProperty ("decay", list (analysis.decayDb));
    object->setProperty ("decayStep", analysis.decayStepSeconds);

    juce::Array<juce::var> rt60;

    for (const auto v : analysis.rt60)
        rt60.add (std::round (static_cast<double> (v) * 1000.0) / 1000.0);

    object->setProperty ("rt60", rt60);
    return juce::var (object.release());
}

SpectrumTap::Levels DrenchedProcessor::wetSpectrum()
{
    const auto rate = preparedRate.load();
    return reverb.wetSpectrum().read (rate > 0.0 ? rate : 48000.0);
}

void DrenchedProcessor::applyLoadedImpulseResponse (const juce::File& file, std::shared_ptr<const RecordedResponse> loaded,
                                                    const juce::String& error, bool measuredDecay)
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
        recordedWanted = {}; // a file chosen meanwhile replaces a scene's still being read
    }

    ui.removeProperty (UiIDs::irError, nullptr);
    ui.setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
    ui.setProperty (UiIDs::irName, file.getFileNameWithoutExtension(), nullptr);

    // The graph opens on the room's own decay, so editing it reshapes from there; a
    // scene brings its own.
    for (size_t band = 0; band < (measuredDecay ? measured.size() : 0); ++band)
    {
        for (const auto* id : { ParamIDs::decay[band], ParamIDs::decayRight[band] })
        {
            if (auto* parameter = state.getParameter (id))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (measured[band]));
                parameter->endChangeGesture();
            }
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
                    file = writeCapture (extractClap (capture.recordings(), capture.sampleRate()), capture.sampleRate(), settings::capturesFolder());
                }
                else
                {
                    const auto& sweep = *capture.sweep();
                    file = writeCapture (deconvolveCapture (sweep, capture.recordings(), capture.speakers()), sweep.settings().sampleRate,
                                         settings::capturesFolder());
                }

                if (file == juce::File())
                    error = "The capture couldn't be saved in " + settings::capturesFolder().getFullPathName() + ". Is the disk full?";
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

juce::File DrenchedProcessor::writeCapture (const std::vector<std::vector<float>>& channels, double sampleRate,
                                           const juce::File& folder)
{
    if (channels.empty() || channels.front().empty() || folder.createDirectory().failed())
        return {};

    // Made in memory and written in one go, so a full disk can't leave half a capture
    // behind looking like a whole one: the WAV header is only finished as the writer closes,
    // and a file stream's write errors don't reach the writer.
    juce::MemoryBlock data;

    {
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (static_cast<int> (channels.size()))
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (data, false);
        auto writer = wav.createWriterFor (stream, options);

        if (writer == nullptr)
            return {};

        std::vector<const float*> pointers;

        for (const auto& c : channels)
            pointers.push_back (c.data());

        if (! writer->writeFromFloatArrays (pointers.data(), static_cast<int> (pointers.size()), static_cast<int> (channels.front().size())))
            return {};
    }

    const auto file = folder.getNonexistentChildFile ("Capture " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"),
                                                      ".wav", false);

    if (! file.replaceWithData (data.getData(), data.getSize()))
    {
        file.deleteFile();
        return {};
    }

    return file;
}

void DrenchedProcessor::restoreImpulseResponse (juce::ValueTree ui)
{
    const auto path = ui.getProperty (UiIDs::irPath, "").toString();

    {
        const std::lock_guard lock (recordedLock);
        recordedWanted = {}; // the session's file, not a scene's

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
    // bounce must already use it. A file that moved, or a session from another
    // computer, is looked for where it most likely is now.
    const auto file = findImpulseResponse (path);
    juce::String error;
    auto loaded = file != juce::File() ? readImpulseResponse (file, error) : nullptr;
    const auto found = loaded != nullptr ? file.getFullPathName() : path;

    {
        const std::lock_guard lock (recordedLock);
        recorded = std::move (loaded);
        recordedPath = found;
        recordedMissing = recorded == nullptr;
    }

    // The session saves where it was found.
    if (found != path)
        ui.setProperty (UiIDs::irPath, found, nullptr);
}

juce::String DrenchedProcessor::aboutThisBuild() const
{
   #ifdef JucePlugin_VersionString
    const juce::String version (JucePlugin_VersionString);
   #else
    const juce::String version ("unknown");
   #endif

   #ifdef DRENCHED_BUILD_NUMBER
    const juce::String buildNumber (DRENCHED_BUILD_NUMBER), commit (DRENCHED_COMMIT);
   #else
    const juce::String buildNumber, commit ("unknown");
   #endif
    const auto build = (buildNumber.isEmpty() ? juce::String ("local build") : "build " + buildNumber)
                       + " (" + commit + ")";

    juce::StringArray lines;
    lines.add ("Drenched " + version + (LicenceManager::isBeta() ? " beta" : "") + ", " + build + ", "
               + juce::String (static_cast<int> (sizeof (void*) * 8)) + "-bit");
    lines.add ("Format: " + juce::String (getWrapperTypeDescription (wrapperType)));
    lines.add ("Host: " + juce::String (juce::PluginHostType().getHostDescription()));
    lines.add ("System: " + juce::SystemStats::getOperatingSystemName() + ", "
               + juce::SystemStats::getCpuModel().trim() + ", "
               + juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0, 1) + " GB");
    return lines.joinIntoString ("\n");
}

juce::String DrenchedProcessor::bugReportDetails()
{
    const auto layout = getBusesLayout();
    const auto ui = getUiState();
    const auto irName = ui.getProperty (UiIDs::irName, "").toString();
    const auto space = chosenSpaceId();

    juce::StringArray lines;
    lines.add (aboutThisBuild());
    lines.add ("Audio: " + juce::String (getSampleRate(), 0) + " Hz, buffer " + juce::String (getBlockSize())
               + ", latency " + juce::String (getLatencySamples()) + " samples"
               + (params.lowCpu.load() > 0.5f ? " (Low CPU)" : ""));
    lines.add ("Channels: " + layout.getMainInputChannelSet().getDescription() + " in, "
               + layout.getMainOutputChannelSet().getDescription() + " out");
    lines.add ("Space: " + (space == "user" ? "your IR " + irName
                            : space == "described" ? "described room \"" + ui.getProperty (UiIDs::roomName, "").toString() + "\" ("
                                                         + ui.getProperty (UiIDs::roomWords, "").toString() + "; "
                                                         + ui.getProperty (UiIDs::roomSize, "").toString() + " m)"
                            : space));
    lines.add ("Look: " + ui.getProperty (UiIDs::skin, settings::loadDefaultSkin()).toString()
               + ", view " + ui.getProperty (UiIDs::mode, "studio").toString());

    if (const auto restarts = reverb.recoveries(); restarts > 0)
        lines.add ("Reverb restarted after a fault: " + juce::String (restarts) + (restarts == 1 ? " time" : " times"));

    return lines.joinIntoString ("\n");
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
        decay.add (static_cast<double> (current != nullptr ? current->room.decaySeconds[band] : params.decay[band].load()));

    object->setProperty ("bands", decay);

    if (current != nullptr)
    {
        object->setProperty ("ch", current->channels);

        if (current->ambisonic)
            object->setProperty ("bformat", true);
        else if (current->isSurround())
            object->setProperty ("layout", channelLayouts::nameOf (current->layout));
        object->setProperty ("rate", current->responses.sampleRate);
        object->setProperty ("W", static_cast<double> (current->room.width));
        object->setProperty ("D", static_cast<double> (current->room.depth));
        object->setProperty ("H", static_cast<double> (current->room.height));
    }

    return juce::var (object.release());
}

//==============================================================================
juce::ValueTree DrenchedProcessor::scenes()
{
    return state.state.getOrCreateChildWithName (SceneIDs::tree, nullptr);
}

juce::ValueTree DrenchedProcessor::findScene (int number)
{
    return number > 0 ? scenes().getChildWithProperty (SceneIDs::number, number) : juce::ValueTree();
}

juce::String DrenchedProcessor::spaceName (const juce::String& id, const juce::String& irName, const juce::String& roomName) const
{
    if (id == "user")
        return irName.isNotEmpty() ? irName : juce::String ("Your impulse response");

    if (id == "described")
        return roomName.isNotEmpty() ? roomName : juce::String ("Described room");

    if (const auto* space = library::find (id))
        return space->info["name"].toString();

    for (const auto& space : bundledSpaces)
        if (id == space.id)
            return space.name;

    return id;
}

juce::var DrenchedProcessor::sceneList()
{
    juce::Array<juce::var> list;

    for (int number = 1; number <= SceneIDs::maxScenes; ++number)
    {
        if (const auto scene = findScene (number); scene.isValid())
        {
            auto object = std::make_unique<juce::DynamicObject>();
            object->setProperty ("number", number);
            object->setProperty ("name", scene.getProperty (SceneIDs::name).toString());
            object->setProperty ("space", spaceName (scene.getProperty (SceneIDs::spaceId).toString(),
                                                     scene.getProperty (SceneIDs::irName).toString(),
                                                     scene.getProperty (UiIDs::roomName).toString()));
            list.add (juce::var (object.release()));
        }
    }

    return list;
}

int DrenchedProcessor::storeScene (int number, const juce::String& name)
{
    if (number == 0)
        for (int free = 1; free <= SceneIDs::maxScenes && number == 0; ++free)
            if (! findScene (free).isValid())
                number = free;

    if (number < 1 || number > SceneIDs::maxScenes)
        return 0;

    auto scene = findScene (number);
    const auto oldName = scene.isValid() ? scene.getProperty (SceneIDs::name).toString() : juce::String();
    juce::ValueTree stored (SceneIDs::scene);
    stored.setProperty (SceneIDs::number, number, nullptr);
    stored.setProperty (SceneIDs::name, name.trim().isNotEmpty() ? name.trim().substring (0, 64)
                                        : oldName.isNotEmpty() ? oldName : "Scene " + juce::String (number), nullptr);

    for (const auto& id : ParamIDs::all())
        if (SceneIDs::isStored (id))
            if (auto* raw = state.getRawParameterValue (id); raw != nullptr && std::isfinite (raw->load()))
                stored.setProperty (id, static_cast<double> (raw->load()), nullptr);

    auto ui = getUiState();
    const auto spaceId = chosenSpaceId();
    stored.setProperty (SceneIDs::spaceId, spaceId, nullptr);
    stored.setProperty (SceneIDs::speakers, ui.getProperty (UiIDs::speakers, "").toString(), nullptr);

    if (spaceId == "user")
    {
        stored.setProperty (SceneIDs::irPath, ui.getProperty (UiIDs::irPath, "").toString(), nullptr);
        stored.setProperty (SceneIDs::irName, ui.getProperty (UiIDs::irName, "").toString(), nullptr);
    }
    else if (spaceId == "described")
    {
        // The room as described, so the scene plays it however the room is described later.
        for (const auto* property : UiIDs::roomProperties)
            if (ui.hasProperty (*property))
                stored.setProperty (*property, ui.getProperty (*property), nullptr);
    }

    if (scene.isValid())
        scene.copyPropertiesFrom (stored, nullptr);
    else
        scenes().appendChild (stored, nullptr);

    return number;
}

void DrenchedProcessor::renameScene (int number, const juce::String& name)
{
    if (auto scene = findScene (number); scene.isValid() && name.trim().isNotEmpty())
        scene.setProperty (SceneIDs::name, name.trim().substring (0, 64), nullptr);
}

void DrenchedProcessor::deleteScene (int number)
{
    // Other scenes keep their numbers, so automation that recalls them still does.
    if (auto scene = findScene (number); scene.isValid())
        scenes().removeChild (scene, nullptr);
}

void DrenchedProcessor::recallScene (int number, bool loadNow)
{
    const auto scene = findScene (number);

    if (! scene.isValid())
        return;

    followedScene = number;

    {
        const std::lock_guard lock (recordedLock);
        recordedWanted = {}; // until this scene's file needs reading
    }

    if (auto* parameter = state.getParameter (ParamIDs::scene); parameter != nullptr && juce::roundToInt (params.scene.load()) != number)
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (number)));
        parameter->endChangeGesture();
    }

    for (const auto& id : ParamIDs::all())
    {
        auto* parameter = state.getParameter (id);

        if (! SceneIDs::isStored (id) || parameter == nullptr)
            continue;

        // A parameter newer than the scene goes to its default, which is how the scene
        // sounded when it was stored.
        const auto value = static_cast<float> (static_cast<double> (scene.getProperty (id, juce::var())));

        if (! scene.hasProperty (id))
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
        else if (std::isfinite (value))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    auto ui = getUiState();
    const auto spaceId = scene.getProperty (SceneIDs::spaceId, "").toString();
    ui.setProperty (UiIDs::speakers, scene.getProperty (SceneIDs::speakers, "").toString(), nullptr);

    if (spaceId == "user")
    {
        // The scene names the file where it was when stored; it may have moved since.
        const auto path = scene.getProperty (SceneIDs::irPath, "").toString();
        const auto found = findImpulseResponse (path);
        const auto now = found != juce::File() ? found.getFullPathName() : path;
        bool loaded = false;

        {
            const std::lock_guard lock (recordedLock);
            loaded = now == recordedPath && recorded != nullptr;
        }

        if (! loaded && juce::File::isAbsolutePath (now))
        {
            const juce::File file (now);
            ui.setProperty (UiIDs::irPath, now, nullptr);
            ui.setProperty (UiIDs::irName, scene.getProperty (SceneIDs::irName, "").toString(), nullptr);

            // A file played recently is still prepared, so the scene changes at once.
            if (loadNow || (movedFiles::mayOpen (now, settings::loadTrustedShares())
                            && RecordingCache::shared().find (RecordingCache::keyOf (file)) != nullptr))
            {
                juce::String error;
                auto response = readImpulseResponse (file, error);
                applyLoadedImpulseResponse (file, std::move (response), error, false);
            }
            else
            {
                {
                    const std::lock_guard lock (recordedLock);
                    recordedWanted = now;
                }

                loadSceneImpulseResponseAsync (file);
            }
        }
    }

    if (spaceId == "described" && scene.hasProperty (UiIDs::roomSize))
        for (const auto* property : UiIDs::roomProperties)
            ui.setProperty (*property, scene.getProperty (*property), nullptr);

    if (spaceId.isNotEmpty())
        ui.setProperty (UiIDs::spaceId, spaceId, nullptr);

    if (loadNow)
        loadLibrarySpace (chosenSpaceId(), true);

    cacheUiState();

    // Bouncing, the block about to play builds the room itself. Otherwise it is built at
    // once: a scene changes rooms on the cut, so there is no value to wait to settle.
    if (const auto rate = preparedRate.load(); rate > 0.0 && ! loadNow)
    {
        updateLatency (rate);

        if (auto request = currentRequest (rate))
            builder->request (*request, false);
    }
}

void DrenchedProcessor::followSceneOffline()
{
    const auto number = juce::roundToInt (params.scene.load());

    if (number == followedScene.load())
        return;

    // A recall changes the session, which belongs to the message thread. Some hosts bounce
    // on that thread, so it happens here; otherwise this waits for it, but not for ever,
    // as a host may hold the message thread until the bounce is done. The recall then
    // lands late, as it did before, rather than never.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        const juce::ScopedValueSetter<bool> quiet (quietChanges, true);
        recallScene (number, true);
    }
    else
    {
        auto done = std::make_shared<juce::WaitableEvent>();
        std::weak_ptr<int> alive = lifetime;

        juce::MessageManager::callAsync ([this, alive, number, done]
        {
            if (! alive.expired() && juce::roundToInt (params.scene.load()) == number)
            {
                const juce::ScopedValueSetter<bool> quiet (quietChanges, true);
                recallScene (number, true);
            }

            done->signal();
        });

        done->wait (2000);
    }

    // Also for a number with no scene, so it isn't tried again every block.
    followedScene = number;
    offlineSinceBuild = std::numeric_limits<juce::int64>::max() / 2; // build before this block plays
}

void DrenchedProcessor::followSceneParameter()
{
    if (const auto number = juce::roundToInt (params.scene.load()); number != followedScene)
    {
        // Automation plays the scenes back; the host doesn't count that as a change.
        const juce::ScopedValueSetter<bool> quiet (quietChanges, true);
        followedScene = number;
        recallScene (number);
    }
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

void DrenchedProcessor::prepareToPlay (double hostRate, int)
{
    // A host can report a rate no audio runs at: 0 before its audio has started, or a
    // value it failed to read. Building a room for that could take forever, so the plugin
    // prepares at the nearest rate it can play; the host prepares it again before playing.
    const auto sampleRate = std::isfinite (hostRate) && hostRate > 0.0 ? std::clamp (hostRate, lowestRate, highestRate) : 48000.0;
    crashReports::step ("host prepared " + juce::String (hostRate, 0) + " Hz, buffer " + juce::String (getBlockSize()));

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

    // Some hosts prepare on their own thread while the interface may be changing its state,
    // and a tree read as it is written can corrupt the heap. There the space chosen when the
    // message thread last looked is used; the timer catches up with anything newer.
    if (const auto* messages = juce::MessageManager::getInstanceWithoutCreating();
        messages == nullptr || messages->isThisTheMessageThread())
    {
        loadLibrarySpace (chosenSpaceId(), true);
        cacheUiState();
    }
    else
    {
        juce::String id;

        {
            const std::lock_guard lock (recordedLock);
            id = keptSpaceId;
        }

        loadLibrarySpace (id, true);
    }

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

    offlineSinceBuild = 0;

    for (auto& peak : peaks)
        peak.store (0.0f);
}

void DrenchedProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) DRENCHED_NONBLOCKING
{
    render (buffer, false);
}

void DrenchedProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) DRENCHED_NONBLOCKING
{
    render (buffer, true);
}

void DrenchedProcessor::render (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;

    // A host can hand over fewer channels than the buses have (a bus switched off, a call
    // only to pass on automation): only the channels that are there are read or written.
    const auto numInputs = std::min (getTotalNumInputChannels(), buffer.getNumChannels());
    const auto numOutputs = std::min (getTotalNumOutputChannels(), buffer.getNumChannels());
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

    // Before the controls are read, so the block plays the session's settings. Bouncing,
    // the host waits for each block, so this may wait for the message thread and build a
    // room; it is the only part of the audio path that may (see Realtime.h).
    if (isNonRealtime())
    {
        const ScopedRealtimeCheckOff offline;
        applySessionOffline();
    }

    ReverbControls controls;
    controls.mix = params.mix.load() / 100.0f;
    controls.outputGain = decibelsToGain (params.output.load());
    controls.predelayMs = predelayMs (params.predelay.load());
    controls.brightnessDb = params.brightness.load();
    controls.lowCutHz = params.wetLowCut.load();
    controls.highCutHz = params.wetHighCut.load();
    controls.widthPercent = params.width.load();
    controls.cloudPercent = params.cloudSize.load();
    controls.ducking = params.ducking.load() / 100.0f;
    controls.monoBelowHz = params.monoBelow.load();
    controls.freeze = params.freeze.load() >= 0.5f;
    controls.crossoverHz = params.crossover.load();
    controls.movement = params.movement.load() / 100.0f;
    controls.movementRateHz = params.movementRate.load();

    for (size_t band = 0; band < WetEqualiser::numBands; ++band)
    {
        auto& eq = controls.eq[band];
        eq.frequencyHz = params.eqFrequency[band].load();
        eq.gainDb = params.eqOn[band].load() >= 0.5f ? params.eqGain[band].load() : 0.0f; // off: flat
        eq.q = params.eqQ[band].load();
        eq.shape = static_cast<WetEqualiser::Shape> (std::clamp (juce::roundToInt (params.eqShape[band].load()), 0, 2));
    }

    // Soloing a band is for listening at the window; a bounce plays everything.
    controls.soloBand = isNonRealtime() ? WetEqualiser::noSolo : eqSoloBand();

    for (size_t k = 0; k < maxChannels; ++k)
    {
        controls.speakerLevelDb[k] = speakerLevelDb[k].load (std::memory_order_relaxed);
        controls.speakerDelayMs[k] = speakerDelayMs[k].load (std::memory_order_relaxed);
    }

    // After the trial, without a licence, only the dry signal plays.
    if (! licence->isAudioAllowed())
        controls.mix = 0.0f;

    // Bypassed, the dry signal plays as it came in (but still in time).
    if (bypassed)
    {
        controls.mix = 0.0f;
        controls.outputGain = 1.0f;
    }

    // A solo mutes the dry and plays the band's reverberation, so it would undo both of
    // those: bypassed or unlicensed, nothing is soloed.
    if (bypassed || ! licence->isAudioAllowed())
        controls.soloBand = WetEqualiser::noSolo;

    if (isNonRealtime())
    {
        const ScopedRealtimeCheckOff offline;
        followSceneOffline();
        buildChangedRoom (numSamples);
    }

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

void DrenchedProcessor::buildChangedRoom (int numSamples)
{
    // Offline, the host waits for every block, so the room can be rebuilt here. A change
    // then lands at the same place in every bounce, rather than wherever the builder
    // thread caught up with audio running faster than real time. Rebuilds are spaced
    // like the builder's settling time, in audio time, so a sweep stays quick to bounce.
    const auto rate = preparedRate.load();
    offlineSinceBuild += numSamples;

    if (rate <= 0.0 || offlineSinceBuild < static_cast<juce::int64> (0.08 * rate))
        return;

    const auto request = currentRequest (rate);

    if (! request || request == builder->lastBuilt())
        return;

    try
    {
        builder->buildNow (*request);
        offlineSinceBuild = 0;
    }
    catch (const std::exception&)
    {
        // Out of memory: the current room plays on.
    }
}

juce::AudioProcessorEditor* DrenchedProcessor::createEditor()
{
    return new DrenchedEditor (*this);
}

void DrenchedProcessor::keepSessionForSaving()
{
    if (! sessionChanged.load())
        return;

    // Copying flushes the parameters into the tree, which can mark it changed again;
    // the next tick then copies once more, which is harmless.
    auto copy = state.copyState();
    sessionChanged.store (false);

    const std::lock_guard lock (savedLock);
    savedSession = std::move (copy);
}

void DrenchedProcessor::noteChangeForHost() noexcept
{
    if (! quietChanges)
        hostShouldMarkChanged = true;
}

void DrenchedProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    sessionChanged.store (true);

    // Parameters reach the host by themselves (copyState writes them into the tree too, on
    // every save). The window's width, and how the last file load went, aren't choices.
    if (tree.hasType ("PARAM"))
        return;

    if (tree.hasType (UiIDs::tree) && (property == UiIDs::width || property == UiIDs::irEvent || property == UiIDs::irError))
        return;

    noteChangeForHost();
}

void DrenchedProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    sessionChanged.store (true);

    // An empty child is only room made for state (getUiState makes one), not a change.
    if (! child.hasType ("PARAM") && (child.getNumProperties() > 0 || child.getNumChildren() > 0))
        noteChangeForHost();
}

void DrenchedProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    valueTreeChildAdded (parent, child);
}

void DrenchedProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Hosts may save from a thread of their own (autosave, undo history). Only the message
    // thread may touch the state's value tree, so other threads save the copy the timer
    // keeps, at most a tick behind the interface, with the parameters as they are now.
    juce::ValueTree session;

    if (juce::MessageManager::existsAndIsLockedByCurrentThread())
    {
        applyPendingSession();
        session = state.copyState();
    }
    else
    {
        auto loaded = false;

        {
            const std::lock_guard lock (savedLock);

            // A session loaded a moment ago, which the timer hasn't applied yet, saves as
            // it was loaded.
            loaded = pendingSession.isValid();
            session = (loaded ? pendingSession : savedSession).createCopy();
        }

        if (! loaded)
        {
            for (auto* parameter : getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                {
                    auto child = session.getChildWithProperty ("id", ranged->paramID);

                    if (! child.isValid())
                    {
                        child = juce::ValueTree ("PARAM");
                        child.setProperty ("id", ranged->paramID, nullptr);
                        session.appendChild (child, nullptr);
                    }

                    child.setProperty ("value", state.getRawParameterValue (ranged->paramID)->load(), nullptr);
                }
            }
        }
    }

    session.setProperty (stateVersionID, stateVersion, nullptr);

    if (auto xml = session.createXml())
        copyXmlToBinary (*xml, destData);
}

void DrenchedProcessor::migrateState (juce::ValueTree& session) const
{
    // Sessions without a version (the betas) mean the same as version 1. Parameters a
    // session doesn't mention, because they came later, are set to their defaults by
    // the value tree state, and every default is how the versions before it played
    // (tests/plugin/SessionTests.cpp checks both).
    const auto version = static_cast<int> (session.getProperty (stateVersionID, 1));

    // Version 2 made the listener's place across the room a parameter, so it can be
    // automated; before, the interface kept it as a fraction of the width.
    if (auto ui = session.getChildWithName (UiIDs::tree); version < 2 && ui.hasProperty (UiIDs::listenerX))
    {
        if (! session.getChildWithProperty ("id", ParamIDs::listenerX).isValid())
        {
            juce::ValueTree param ("PARAM");
            param.setProperty ("id", ParamIDs::listenerX, nullptr);
            param.setProperty ("value", juce::jlimit (0.0, 100.0, static_cast<double> (ui.getProperty (UiIDs::listenerX)) * 100.0), nullptr);
            session.appendChild (param, nullptr);
        }

        ui.removeProperty (UiIDs::listenerX, nullptr);
    }

    // A damaged session can hold settings that aren't numbers. They take their
    // defaults; left in, they would make the reverb play nothing but NaN. Numbers outside
    // a setting's range are brought to its nearest end here, rather than left to each
    // parameter's conversion to make of them; one in range, as every
    // session Drenched saves holds, is left exactly as it is.
    for (int i = session.getNumChildren(); --i >= 0;)
    {
        auto child = session.getChild (i);

        if (! child.hasType ("PARAM"))
            continue;

        const auto value = static_cast<double> (child.getProperty ("value"));

        if (! std::isfinite (value))
        {
            session.removeChild (i, nullptr);
            continue;
        }

        if (const auto* parameter = state.getParameter (child.getProperty ("id").toString()))
        {
            const auto range = parameter->getNormalisableRange().getRange();

            if (value < range.getStart() || value > range.getEnd())
                child.setProperty ("value", juce::jlimit (static_cast<double> (range.getStart()), static_cast<double> (range.getEnd()), value), nullptr);
        }
    }

    session.setProperty (stateVersionID, stateVersion, nullptr);
}

void DrenchedProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    crashReports::step ("host loaded a session (" + juce::String (sizeInBytes) + " bytes)");
    juce::ValueTree session;

    if (auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (stateType))
    {
        session = juce::ValueTree::fromXml (*xml);
        migrateState (session);
    }

    if (juce::MessageManager::existsAndIsLockedByCurrentThread())
    {
        {
            const std::lock_guard lock (savedLock);
            pendingSession = {}; // this load is newer
            sessionWaiting.store (false);
        }

        applySession (session);
        return;
    }

    // Hosts may load from a thread of their own too. Only the message thread may touch the
    // state's value tree, so the timer applies the session, at most a tick later. The
    // responses it names are read now, on this thread, which is what takes the time.
    if (! session.isValid())
        return;

    auto ui = session.getOrCreateChildWithName (UiIDs::tree, nullptr);
    restoreImpulseResponse (ui);
    loadLibrarySpace (chosenSpaceId (ui), true);

    const std::lock_guard lock (savedLock);
    pendingSession = session;
    sessionWaiting.store (true);
}

void DrenchedProcessor::applyPendingSession()
{
    juce::ValueTree session;

    {
        const std::lock_guard lock (savedLock);
        session = std::exchange (pendingSession, {});
        sessionWaiting.store (false);
    }

    if (session.isValid())
        applySession (session);
}

void DrenchedProcessor::applySessionOffline()
{
    // Once per session loaded: a wait that timed out isn't tried again every block.
    if (! sessionWaiting.exchange (false))
        return;

    // Applying a session changes the state, which belongs to the message thread. As for a
    // scene (see followSceneOffline): here if the host bounces on that thread, otherwise
    // waiting for it, but not for ever, as a host may hold it until the bounce is done.
    // The session then lands late, at the timer's next tick, as it did before.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyPendingSession();
    }
    else
    {
        auto done = std::make_shared<juce::WaitableEvent>();
        std::weak_ptr<int> alive = lifetime;

        juce::MessageManager::callAsync ([this, alive, done]
        {
            if (! alive.expired())
                applyPendingSession();

            done->signal();
        });

        done->wait (2000);
    }

    offlineSinceBuild = std::numeric_limits<juce::int64>::max() / 2; // build before this block plays
}

void DrenchedProcessor::applySession (const juce::ValueTree& session)
{
    // The host loaded this session, so it knows what it holds; a change made before it
    // was replaced along with everything else.
    const juce::ScopedValueSetter<bool> quiet (quietChanges, true);
    hostShouldMarkChanged = false;

    if (session.isValid())
        state.replaceState (session);

    restoreImpulseResponse (getUiState());
    loadLibrarySpace (chosenSpaceId(), true);
    cacheUiState();
    keepSessionForSaving();

    // The session already holds the settings it was saved with, which may have been
    // changed since the scene it names was recalled; only a move of the parameter recalls.
    followedScene = juce::roundToInt (params.scene.load());

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

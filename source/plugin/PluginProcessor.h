#pragma once

#include "Library.h"
#include "LicenceManager.h"
#include "Parameters.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/RecordedResponse.h>
#include <drenched/ResponseAnalysis.h>
#include <drenched/Spaces.h>
#include <drenched/SweepCapture.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace drenched
{

class DrenchedProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    DrenchedProcessor();
    ~DrenchedProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return reverb.tailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** The version written into every saved session. Raise it when the meaning of
        saved state changes, and teach migrateState() to bring older sessions up to it.
        1: the first versioned sessions (0.1); sessions without a version are from the
        betas before it and need nothing else. */
    static constexpr int stateVersion = 2;

    /** Brings a loaded session up to date before it replaces the current state. */
    void migrateState (juce::ValueTree& session) const;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

    /** The interface's session state (space, locks, view, skin), kept inside the plugin state. */
    juce::ValueTree getUiState();

    /** The licence on this computer, shared by every instance. Message thread. */
    LicenceManager& getLicence() noexcept { return *licence; }

    /** Engine-side space indices: the modelled spaces come first, then a loaded impulse
        response, then the chosen library recording. The interface and sessions use ids. */
    static constexpr int userSpace = static_cast<int> (bundledSpaces.size());
    static constexpr int librarySpace = userSpace + 1;

    /** The id of the chosen space: a library id, a modelled id or "user". */
    juce::String chosenSpaceId();

    /** Reads and analyses an impulse response file on a background thread, then (on the
        message thread) plays it, sets the decay graph to its measured decay and saves its
        path in the session. A file that can't be used leaves an error in the interface state
        instead. Message thread only. */
    void loadImpulseResponseAsync (const juce::File& file);

    /** The same for a scene's file: plays it without changing the decay graph, and only if
        the session still wants that file when it has loaded. */
    void loadSceneImpulseResponseAsync (const juce::File& file);

    /** Match a room: listens to a recording on a background thread for how long the room it
        was made in rings (see RoomListener), then calls `done` on the message thread with the
        result for the interface: the decay per band (bands), how many decays each band was
        heard in (heard), the closest space (spaceId, spaceName) and the seconds listened to;
        or with error, saying why the recording could not be matched. Message thread only. */
    void matchRoomAsync (const juce::File& file, std::function<void (const juce::var&)> done);

    /** The same, at once: what matchRoomAsync hands back. Allocates and takes a while; any
        thread but the audio thread. */
    static juce::var matchRecording (const juce::File& file);

    /** The stored scenes for the interface, by number: number, name and the space's name.
        Message thread only. */
    juce::var sceneList();

    /** Stores every setting and the space as scene `number` (replacing what it held), or
        with 0 as the lowest free number. Returns the number, or 0 when all are taken. An
        empty name keeps the scene's old name, or names a new one "Scene n". Message thread. */
    int storeScene (int number, const juce::String& name);

    void renameScene (int number, const juce::String& name);
    void deleteScene (int number);

    /** Sets every setting and the space to scene `number`'s and moves the scene parameter
        to it, telling the host. Automating the scene parameter does the same within a
        timer tick (see followSceneParameter). Does nothing for an empty number. Message thread. */
    void recallScene (int number);

    /** Recalls the scene the scene parameter has moved to, if it has moved since the last
        recall or session load. Called by the timer; public for tests. Message thread. */
    void followSceneParameter();

    /** What a bug report needs to say about where Drenched is running: the version and
        build, the plug-in format, the host, the computer, the audio settings, the space and
        the look. Plain text, one fact per line. Message thread only. */
    juce::String bugReportDetails();

    /** The loaded impulse response for the interface: name, channels, rate, measured decay
        and room size, or whether its file is missing. Void when none is loaded. */
    juce::var impulseResponseInfo();

    /** The length in seconds the playing response was shortened to so it fits in a
        32-bit process, or 0 when it plays in full (see KernelBuilder). From any thread. */
    double shortenedSeconds() const noexcept { return builder->shortenedTo(); }

    /** Changes whenever a new response starts playing. From any thread. */
    std::uint64_t responseVersion() const noexcept { return builder->buildCount(); }

    /** The decay displays: analyses the playing response on a background thread (see
        analyseResponse), then calls `done` on the message thread with the result for the
        interface (analysisToVar), or with void when nothing has been built. Message thread. */
    void analyseResponseAsync (std::function<void (const juce::var&)> done);

    /** An analysis as the interface takes it: seconds, the band centres (bands), the
        waterfall (rows of dB, sliceSeconds apart), the decay curve (decay, decayStep) and
        the measured rt60 per octave band, in tenths of a dB or millisecond. */
    static juce::var analysisToVar (const ResponseAnalysis& analysis);

    /** The live spectrum of the reverb alone, third-octave levels in dB. Message thread only. */
    SpectrumTap::Levels wetSpectrum();

    /** Measures a space: plays a sine sweep from the outputs (the left one, or left then
        right) while recording the inputs, then works out the impulse response, saves it as a
        WAV file in the captures folder and loads it like any other file. options: speakers
        (1 or 2), sweepSeconds, tailSeconds and levelDb. With method "clap" it instead
        listens for listenSeconds and keeps the best hand clap it heard (see extractClap).
        Message thread only. */
    void startCapture (const juce::var& options);

    /** Stops a capture that is playing. Message thread only. */
    void cancelCapture();

    /** Where a capture is up to, for the interface: stage (idle, playing, working or
        failed), progress from 0 to 1, the loudest input so far in dB, and a message. */
    juce::var captureStatus() const;

    /** Peak input and output levels since the last call, in linear gain, then reset to zero.
        Read from the message thread by the editor's meters. */
    struct Peaks { float inL, inR, outL, outR; };
    Peaks takePeaks() noexcept;

    /** Each output channel's peak since the last call, the same way, in the bus's order. */
    std::array<float, maxChannels> takeSpeakerPeaks() noexcept;

    /** The output bus's speakers by their short names (L, R, C, LFE, Ls, ...), and which
        of them carry reverberation (all but the LFE). Message thread only. */
    juce::var outputSpeakerNames() const;

private:
    void timerCallback() override;

    /** What the impulse response should be built from, given the parameters and the chosen
        space; nothing while the chosen recording is still loading. */
    std::optional<ImpulseResponseRequest> currentRequest (double sampleRate) const;

    /** The latency Low CPU asks for at a sample rate, or 0, and telling the host. */
    int wantedLatency (double sampleRate) const;
    void updateLatency (double sampleRate);

    /** Copies the chosen space and listener position out of the interface state, for other threads. */
    void cacheUiState();

    float predelayMs (float requested);

    /** Decodes and prepares a file. On failure returns null and says why in `error`. */
    static std::shared_ptr<const RecordedResponse> readImpulseResponse (const juce::File& file, juce::String& error);
    static std::shared_ptr<const RecordedResponse> readSofaFile (const juce::File& file, juce::String& error);

    /** After a session loads: reloads the impulse response it names, if it isn't loaded already. */
    void restoreImpulseResponse();

    /** Makes sure the chosen library recording is the one loaded: at once when `now` (a
        session loading before playback), otherwise on the loader thread. */
    void loadLibrarySpace (const juce::String& id, bool now);

    void applyLoadedImpulseResponse (const juce::File& file, std::shared_ptr<const RecordedResponse> loaded, const juce::String& error,
                                     bool measuredDecay = true);

    juce::ValueTree scenes();
    juce::ValueTree findScene (int number);
    juce::String spaceName (const juce::String& id, const juce::String& irName) const;
    int followedScene = 0; // the scene parameter's value at the last recall or session load

    /** Notices a capture that has finished playing, then deconvolves and saves it on the
        loader thread. Message thread. */
    void pollCapture();

    /** Writes a captured impulse response as a 32-bit float WAV file in the captures folder. */
    static juce::File writeCapture (const std::vector<std::vector<float>>& channels, double sampleRate);

    juce::AudioProcessorValueTreeState state;

    struct Params
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* predelay = nullptr;
        std::atomic<float>* predelaySync = nullptr;
        std::atomic<float>* size = nullptr;
        std::atomic<float>* brightness = nullptr;
        std::atomic<float>* earlyLate = nullptr;
        std::atomic<float>* distance = nullptr;
        std::atomic<float>* wetLowCut = nullptr;
        std::atomic<float>* wetHighCut = nullptr;
        std::atomic<float>* width = nullptr;
        std::atomic<float>* ducking = nullptr;
        std::atomic<float>* monoBelow = nullptr;
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* crossover = nullptr;
        std::atomic<float>* living = nullptr;
        std::atomic<float>* movement = nullptr;
        std::atomic<float>* movementRate = nullptr;
        std::atomic<float>* reverse = nullptr;
        std::atomic<float>* gate = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* directLevel = nullptr;
        std::atomic<float>* directDelay = nullptr;
        std::atomic<float>* directColour = nullptr;
        std::atomic<float>* crossFeed = nullptr;
        std::atomic<float>* cloudSize = nullptr;
        std::atomic<float>* listenerX = nullptr;
        std::atomic<float>* sourceX = nullptr;
        std::atomic<float>* lowCpu = nullptr;
        std::atomic<float>* scene = nullptr;
        std::atomic<float>* decayLink = nullptr;
        std::array<std::atomic<float>*, 8> decay {}, decayRight {};
        std::array<std::atomic<float>*, 6> eqFrequency {}, eqGain {};
        std::array<std::atomic<float>*, 4> eqQ {};
    } params;

    // The output speakers, for kernel requests. Written in prepareToPlay, read by the timer.
    mutable std::mutex layoutLock;
    SpeakerLayout outputSpeakers = layouts::stereo();

    std::atomic<int> spaceIndex { 0 };
    std::atomic<double> preparedRate { 0.0 };

    // The loaded impulse response, shared with the builder through requests. Read and
    // written on the message thread and in prepareToPlay and setStateInformation, never on
    // the audio thread.
    mutable std::mutex recordedLock;
    std::shared_ptr<const RecordedResponse> recorded;
    juce::String recordedPath;
    bool recordedMissing = false;

    // The chosen library recording, shared with the builder the same way. libraryWanted is
    // the id asked for most recently, so a slow load that is no longer wanted is dropped.
    std::shared_ptr<const RecordedResponse> libraryResponse;
    juce::String libraryLoadedId, libraryWanted;

    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withThreadName ("Drenched IR loader").withNumberOfThreads (1) };
    juce::ThreadPool analyser { juce::ThreadPoolOptions{}.withThreadName ("Drenched analysis").withNumberOfThreads (1) };
    std::shared_ptr<int> lifetime = std::make_shared<int> (0); // guards callbacks posted by the loader and analyser

    // Capturing a space. The recorder runs in the audio callback; the stage and message
    // are the message thread's. While working, the loader thread reads the recordings, so
    // no new capture starts until it is done.
    enum class CaptureStage { idle, playing, working, failed };
    CaptureRecorder capture;
    CaptureStage captureStage = CaptureStage::idle;
    juce::String captureMessage;
    std::atomic<bool> captureInterrupted { false };

    juce::SharedResourcePointer<LicenceManager> licence;

    ConvolutionReverb reverb;
    std::unique_ptr<KernelBuilder> builder; // after reverb: stops before it is destroyed

    std::array<std::atomic<float>, 4> peaks { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<std::atomic<float>, maxChannels> speakerPeaks {};

    // Each output speaker's reverb level and delay, from the interface state.
    std::array<std::atomic<float>, maxChannels> speakerLevelDb {}, speakerDelayMs {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedProcessor)
};

} // namespace drenched

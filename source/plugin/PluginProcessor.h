#pragma once

#include "Library.h"
#include "LicenceManager.h"
#include "Parameters.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>
#include <drenched/RecordedResponse.h>
#include <drenched/Spaces.h>
#include <drenched/SweepCapture.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
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

    /** The loaded impulse response for the interface: name, channels, rate, measured decay
        and room size, or whether its file is missing. Void when none is loaded. */
    juce::var impulseResponseInfo();

    /** The length in seconds the playing response was shortened to so it fits in a
        32-bit process, or 0 when it plays in full (see KernelBuilder). From any thread. */
    double shortenedSeconds() const noexcept { return builder->shortenedTo(); }

    /** Measures a space: plays a sine sweep from the outputs (the left one, or left then
        right) while recording the inputs, then works out the impulse response, saves it as a
        WAV file in the captures folder and loads it like any other file. options: speakers
        (1 or 2), sweepSeconds, tailSeconds and levelDb. Message thread only. */
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

    /** After a session loads: reloads the impulse response it names, if it isn't loaded already. */
    void restoreImpulseResponse();

    /** Makes sure the chosen library recording is the one loaded: at once when `now` (a
        session loading before playback), otherwise on the loader thread. */
    void loadLibrarySpace (const juce::String& id, bool now);

    void applyLoadedImpulseResponse (const juce::File& file, std::shared_ptr<const RecordedResponse> loaded, const juce::String& error);

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
        std::atomic<float>* lowCpu = nullptr;
        std::array<std::atomic<float>*, 8> decay {};
    } params;

    // The output speakers, for kernel requests. Written in prepareToPlay, read by the timer.
    mutable std::mutex layoutLock;
    SpeakerLayout outputSpeakers = layouts::stereo();

    std::atomic<int> spaceIndex { 0 };
    std::atomic<float> listenerX { 0.5f };
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
    std::shared_ptr<int> lifetime = std::make_shared<int> (0); // guards callbacks posted by the loader

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

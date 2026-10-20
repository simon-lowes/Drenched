#pragma once

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

    /** The space index a loaded impulse response plays in: just after the bundled spaces. */
    static constexpr int userSpace = static_cast<int> (bundledSpaces.size());

    /** Reads and analyses an impulse response file on a background thread, then (on the
        message thread) plays it, sets the decay graph to its measured decay and saves its
        path in the session. A file that can't be used leaves an error in the interface state
        instead. Message thread only. */
    void loadImpulseResponseAsync (const juce::File& file);

    /** The loaded impulse response for the interface: name, channels, rate, measured decay
        and room size, or whether its file is missing. Void when none is loaded. */
    juce::var impulseResponseInfo();

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

private:
    void timerCallback() override;

    /** What the impulse response should be built from, given the parameters and the chosen space. */
    ImpulseResponseRequest currentRequest (double sampleRate) const;

    /** Copies the chosen space and listener position out of the interface state, for other threads. */
    void cacheUiState();

    float predelayMs (float requested);

    /** Decodes and prepares a file. On failure returns null and says why in `error`. */
    static std::shared_ptr<const RecordedResponse> readImpulseResponse (const juce::File& file, juce::String& error);

    /** After a session loads: reloads the impulse response it names, if it isn't loaded already. */
    void restoreImpulseResponse();

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

    ConvolutionReverb reverb;
    std::unique_ptr<KernelBuilder> builder; // after reverb: stops before it is destroyed

    std::array<std::atomic<float>, 4> peaks { 0.0f, 0.0f, 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedProcessor)
};

} // namespace drenched

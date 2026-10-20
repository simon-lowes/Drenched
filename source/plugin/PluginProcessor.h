#pragma once

#include "Parameters.h"

#include <drenched/ConvolutionReverb.h>
#include <drenched/KernelBuilder.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

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

    std::atomic<int> spaceIndex { 0 };
    std::atomic<float> listenerX { 0.5f };
    std::atomic<double> preparedRate { 0.0 };

    ConvolutionReverb reverb;
    std::unique_ptr<KernelBuilder> builder; // after reverb: stops before it is destroyed

    std::array<std::atomic<float>, 4> peaks { 0.0f, 0.0f, 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedProcessor)
};

} // namespace drenched

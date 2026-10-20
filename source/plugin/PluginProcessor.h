#pragma once

#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace drenched
{

class DrenchedProcessor final : public juce::AudioProcessor
{
public:
    DrenchedProcessor();
    ~DrenchedProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

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
    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    juce::SmoothedValue<float> dryGain, wetGain, outputGain;
    std::array<std::atomic<float>, 4> peaks { 0.0f, 0.0f, 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedProcessor)
};

} // namespace drenched

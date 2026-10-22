#include "PluginTestHelpers.h"

#include <drenched/ImpulseResponseSynth.h>
#include <drenched/PartitionedConvolver.h>

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <random>

using namespace drenched;

namespace
{
    constexpr double rate = 44100.0;

    /** Bursts of noise with pauses, played in a modelled space, saved as a stereo WAV file. */
    juce::File recordingIn (int space, const juce::File& file, double seconds = 30.0)
    {
        ImpulseResponseRequest r;
        r.sampleRate = rate;
        r.space = space;
        r.decaySeconds = bundledSpaces[static_cast<size_t> (space)].decaySeconds;
        const auto room = synthesiseImpulseResponse (r);

        std::mt19937 rng (7u);
        std::uniform_real_distribution<double> length (0.15, 0.5), pause (0.6, 1.6);
        const auto total = static_cast<size_t> (seconds * rate);
        auto dry = testing::noise (total + room.length(), 8u);
        size_t pos = 0;

        while (pos < total)
        {
            pos += static_cast<size_t> (length (rng) * rate);
            const auto end = std::min (total, pos + static_cast<size_t> (pause (rng) * rate));
            std::fill (dry.begin() + static_cast<std::ptrdiff_t> (pos), dry.begin() + static_cast<std::ptrdiff_t> (end), 0.0f);
            pos = end;
        }

        std::fill (dry.begin() + static_cast<std::ptrdiff_t> (total), dry.end(), 0.0f);

        juce::AudioBuffer<float> buffer (2, static_cast<int> (dry.size()));

        for (int ch = 0; ch < 2; ++ch)
        {
            PartitionedConvolver convolver ({ std::span<const float> (room.paths[ch == 0 ? ImpulseResponseSet::leftToLeft : ImpulseResponseSet::leftToRight]) });
            float* out[] { buffer.getWritePointer (ch) };
            convolver.process (dry.data(), out, dry.size());
        }

        buffer.applyGain (0.25f);
        file.deleteFile();
        auto stream = file.createOutputStream();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_CASE ("Match a room hears the decay of a recording and names the closest space", "[match]")
{
    const auto file = recordingIn (3, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-match-room.wav"));
    const auto result = DrenchedProcessor::matchRecording (file);
    file.deleteFile();

    REQUIRE (result["error"].isVoid());
    REQUIRE (result["bands"].size() == 8);
    CHECK (result["spaceId"].toString().isNotEmpty());
    CHECK (result["spaceName"].toString().isNotEmpty());
    CHECK (static_cast<double> (result["seconds"]) > 30.0);

    const auto& want = bundledSpaces[3].decaySeconds;

    for (int b = 2; b <= 6; ++b)
    {
        const auto heard = static_cast<double> (result["bands"][b]);
        INFO (decayBandCentres[static_cast<size_t> (b)] << " Hz: heard " << heard << " s, room " << want[static_cast<size_t> (b)] << " s");
        CHECK (std::abs (heard / want[static_cast<size_t> (b)] - 1.0) < 0.2);
        CHECK (static_cast<int> (result["heard"][b]) > 0);
    }
}

TEST_CASE ("Match a room says why a file can't be matched", "[match]")
{
    using Catch::Matchers::ContainsSubstring;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory);

    const auto missing = DrenchedProcessor::matchRecording (folder.getChildFile ("drenched-no-such-recording.wav"));
    CHECK_THAT (missing["error"].toString().toStdString(), ContainsSubstring ("can't find"));

    const auto text = folder.getChildFile ("drenched-not-audio.wav");
    text.replaceWithText ("not audio");
    const auto notAudio = DrenchedProcessor::matchRecording (text);
    text.deleteFile();
    CHECK_THAT (notAudio["error"].toString().toStdString(), ContainsSubstring ("isn't an audio file"));

    const auto shortFile = recordingIn (3, folder.getChildFile ("drenched-short-recording.wav"), 0.5);
    const auto tooShort = DrenchedProcessor::matchRecording (shortFile);
    shortFile.deleteFile();
    CHECK_THAT (tooShort["error"].toString().toStdString(), ContainsSubstring ("too short"));
}

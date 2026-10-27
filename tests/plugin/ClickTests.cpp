#include "PluginTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace drenched;

/*  A host's automation, a scene change or a careless mouse can move any control from
    one end of its range to the other in a single block. However far it moves, the sound
    must change without a click: here a steady 110 Hz tone plays through the plugin while
    each control in turn jumps end to end. A click is anything sharp in what comes out
    (its third difference, which a 110 Hz tone has next to none of), and it must stay
    40 dB under the tone, or under how rough the new setting sounds anyway (a frozen
    tail moves a little). */

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    juce::RangedAudioParameter* find (DrenchedProcessor& p, const juce::String& id)
    {
        for (auto* each : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each); ranged != nullptr && ranged->paramID == id)
                return ranged;

        return nullptr;
    }

    struct Tone
    {
        DrenchedProcessor& p;
        double phase = 0.0;
        int blocks = 0;
        std::vector<float> left, right;

        void play (double seconds)
        {
            juce::MidiBuffer midi;

            for (int b = 0; b < static_cast<int> (seconds * rate) / blockSize; ++b)
            {
                juce::AudioBuffer<float> buffer (2, blockSize);

                for (int i = 0; i < blockSize; ++i)
                {
                    const auto x = 0.25f * static_cast<float> (std::sin (phase));
                    phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / rate;
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x);
                }

                p.processBlock (buffer, midi);

                for (int i = 0; i < blockSize; ++i)
                {
                    left.push_back (buffer.getSample (0, i));
                    right.push_back (buffer.getSample (1, i));
                }

                // The interface's timer, about every 50 ms.
                if (++blocks % 9 == 0)
                    ((juce::Timer&) p).timerCallback();
            }
        }

        size_t now() const noexcept { return left.size(); }

        float roughness (size_t from, size_t to) const
        {
            float most = 0.0f;

            for (const auto* x : { &left, &right })
                for (auto i = std::max<size_t> (from, 3); i < to; ++i)
                    most = std::max (most, std::abs ((*x)[i] - 3.0f * (*x)[i - 1] + 3.0f * (*x)[i - 2] - (*x)[i - 3]));

            return most;
        }

        float peak (size_t from, size_t to) const
        {
            float most = 0.0f;

            for (const auto* x : { &left, &right })
                for (auto i = from; i < to; ++i)
                    most = std::max (most, std::abs ((*x)[i]));

            return most;
        }
    };

    float decibels (float x) { return juce::Decibels::gainToDecibels (x, -200.0f); }
}

TEST_CASE ("No control clicks when it jumps from one end of its range to the other", "[plugin][clicks]")
{
    for (const auto* id : ParamIDs::ordered)
    {
        for (const auto [from, to] : { std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 0.0f } })
        {
            testing::Plugin p;

            // Bouncing offline, so each change is built at the block it happens.
            testing::onMessageThread ([&] { p->setNonRealtime (true); p->prepareToPlay (rate, blockSize); });
            auto* control = find (*p, id);
            REQUIRE (control != nullptr);
            auto* mix = find (*p, ParamIDs::mix);
            mix->setValueNotifyingHost (mix->convertTo0to1 (50.0f));
            control->setValueNotifyingHost (from);

            Tone tone { *p };
            tone.play (1.5);
            const auto jump = tone.now();
            const auto before = tone.peak (jump - 12000, jump);
            control->setValueNotifyingHost (to);
            tone.play (1.5);

            const auto level = std::max (before, tone.peak (tone.now() - 12000, tone.now()));
            const auto click = decibels (tone.roughness (jump, jump + 24000) / level);
            const auto settled = decibels (tone.roughness (tone.now() - 12000, tone.now()) / level);

            INFO (id << " from " << from << " to " << to << ": click " << click << " dB, settled " << settled << " dB");
            CHECK (click < std::max (-40.0f, settled + 6.0f));
        }
    }
}

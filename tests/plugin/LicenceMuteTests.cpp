#include "LicenceTestHelpers.h"

#include <drenched/Mix.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace drenched;

/*  What the copy protection does to the sound: once the trial has ended without a
    licence, the reverb goes quiet and the dry signal plays on at full level (a fully wet
    send turns into a straight wire, not silence), and a licence brings the reverb back
    without reloading anything. The plugin reads its own licence file here
    (LicenceManager::Environment::system()), which LicenceMain.cpp has moved to an empty
    home folder. */

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;

    juce::RangedAudioParameter* find (DrenchedProcessor& p, const juce::String& id)
    {
        for (auto* each : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each); ranged != nullptr && ranged->paramID == id)
                return ranged;

        return nullptr;
    }

    /** Noise through a plugin instance, kept so the output can be held against the input. */
    struct Noise
    {
        explicit Noise (DrenchedProcessor& processor) : p (processor) {}

        void play (double seconds)
        {
            juce::MidiBuffer midi;

            for (int b = 0; b < static_cast<int> (seconds * rate) / blockSize; ++b)
            {
                juce::AudioBuffer<float> buffer (2, blockSize);

                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (c, i, 0.5f * random.nextFloat() - 0.25f);

                for (int c = 0; c < 2; ++c)
                    in[c].insert (in[c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);

                p.processBlock (buffer, midi);

                for (int c = 0; c < 2; ++c)
                    out[c].insert (out[c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
        }

        size_t now() const noexcept { return in[0].size(); }

        /** The most the output strays from the input times a gain (delayed by the
            plugin's latency) from one sample to another: the wet part, when the gain
            is the dry level. */
        float strayFrom (float gain, size_t from, size_t to) const
        {
            const auto latency = static_cast<size_t> (p.getLatencySamples());
            float most = 0.0f;

            for (int c = 0; c < 2; ++c)
                for (auto i = std::max (from, latency); i < to; ++i)
                    most = std::max (most, std::abs (out[c][i] - gain * in[c][i - latency]));

            return most;
        }

        DrenchedProcessor& p;
        juce::Random random { 7 };
        std::vector<float> in[2], out[2];
    };

    struct LicenceFile
    {
        LicenceFile()
        {
            file = LicenceManager::Environment::system().file;
            today = LicenceManager::Environment::system().today();
        }

        ~LicenceFile() { file.deleteFile(); }

        /** True when the plugin's licence file is the test's own, not the developer's. */
        bool isolated() const { return file.isAChildOf (testing::isolatedHome()); }

        void write (const juce::String& key, const juce::String& value) const
        {
            auto f = testing::openLicenceFile (file);
            f->setValue (key, value);
            f->saveIfNeeded();
        }

        void erase (const juce::String& key) const
        {
            auto f = testing::openLicenceFile (file);
            f->removeValue (key);
            f->saveIfNeeded();
        }

        juce::File file;
        std::int64_t today = 0;
    };

    void prepare (testing::Plugin& p, float mixPercent)
    {
        testing::onMessageThread ([&] { p->prepareToPlay (rate, blockSize); });
        auto* mix = find (*p, ParamIDs::mix);
        REQUIRE (mix != nullptr);
        mix->setValueNotifyingHost (mix->convertTo0to1 (mixPercent));
    }
}

TEST_CASE ("After the trial without a licence, the reverb mutes and the dry signal still plays", "[licence][plugin]")
{
    LicenceFile licenceFile;

    if (! licenceFile.isolated())
        SKIP ("The plugin's licence file, " << licenceFile.file.getFullPathName() << ", isn't in the test's own home folder");

    const auto fullDry = equalPowerMix (0.0f).dry;
    REQUIRE (std::abs (fullDry - 1.0f) < 1.0e-7f);

    SECTION ("In the trial the reverb plays")
    {
        licenceFile.write ("trialStart", juce::String (licenceFile.today));

        testing::Plugin p;
        prepare (p, 100.0f);
        REQUIRE (p->getLicence().isAudioAllowed());

        Noise noise (*p);
        noise.play (1.0);

        // Fully wet: what comes out is the reverb, nothing like the input.
        CHECK (noise.strayFrom (fullDry, 0, noise.now()) > 0.05f);
        CHECK (noise.strayFrom (0.0f, 0, noise.now()) > 0.01f);
    }

    SECTION ("After it, only the dry signal plays, at any mix")
    {
        licenceFile.write ("trialStart", juce::String (licenceFile.today - licence::trialDays));

        for (const auto mixPercent : { 100.0f, 50.0f, 0.0f })
        {
            INFO ("mix " << mixPercent << "%");
            testing::Plugin p;
            prepare (p, mixPercent);
            REQUIRE_FALSE (p->getLicence().isAudioAllowed());

            Noise noise (*p);
            noise.play (1.0);

            CHECK (noise.strayFrom (fullDry, 0, noise.now()) < 1.0e-6f); // no reverb at all
            CHECK (noise.strayFrom (0.0f, 0, noise.now()) > 0.2f);       // and not silence
        }
    }

    SECTION ("A licence brings the reverb back, and losing it mutes it again")
    {
        licenceFile.write ("trialStart", juce::String (licenceFile.today - 100));

        testing::Plugin p;
        prepare (p, 100.0f);
        Noise noise (*p);
        noise.play (0.5);
        CHECK (noise.strayFrom (fullDry, 0, noise.now()) < 1.0e-6f);

        // Activated in another host; this one's periodic check would find it within a minute.
        licenceFile.write ("code", testing::codeFor (p->getLicence().machine()));
        testing::onMessageThread ([&] { p->getLicence().refresh(); });
        REQUIRE (p->getLicence().isAudioAllowed());

        auto from = noise.now();
        noise.play (1.0);
        CHECK (noise.strayFrom (fullDry, from + static_cast<size_t> (rate / 2), noise.now()) > 0.05f);

        // Deactivated: the reverb fades out over a few milliseconds, then is gone.
        licenceFile.erase ("code");
        testing::onMessageThread ([&] { p->getLicence().refresh(); });
        REQUIRE_FALSE (p->getLicence().isAudioAllowed());

        from = noise.now();
        noise.play (1.0);
        CHECK (noise.strayFrom (fullDry, from + static_cast<size_t> (rate / 2), noise.now()) < 1.0e-5f);
    }
}

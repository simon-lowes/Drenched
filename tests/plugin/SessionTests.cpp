#include "PluginTestHelpers.h"

#include <drenched/Biquad.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <vector>

using namespace drenched;

/*  Every file in tests/sessions is a session saved by a released version of Drenched
    (see tests/sessions/README.md). Each must load in this version with the same settings
    and play the same sound. Its .expected file holds what it sounded like: the space it
    picks, the latency, and the energy of its response in octave bands over time.

    To accept a deliberate change of sound, run the tests with DRENCHED_UPDATE_SESSIONS=1
    and commit the rewritten .expected files with the change that explains them. */

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;
    constexpr std::array<double, 4> bands { 125.0, 500.0, 2000.0, 8000.0 };
    constexpr std::array<double, 6> windows { 0.0, 0.05, 0.2, 0.5, 1.0, 1.5 }; // seconds

    juce::File sessionsDir() { return juce::File (DRENCHED_SESSIONS_DIR); }

    juce::Array<juce::File> savedSessions()
    {
        auto files = sessionsDir().findChildFiles (juce::File::findFiles, false, "*.session");
        files.sort();
        return files;
    }

    juce::MemoryBlock bytesOf (const juce::File& file)
    {
        juce::MemoryBlock block;
        file.loadFileAsData (block);
        return block;
    }

    /** The PARAM values written in a session. */
    std::map<juce::String, float> savedValues (const juce::MemoryBlock& session)
    {
        std::map<juce::String, float> values;

        if (auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize())))
            for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
                values[param->getStringAttribute ("id")] = static_cast<float> (param->getDoubleAttribute ("value"));

        return values;
    }

    /** Every parameter's value, by id. */
    std::map<juce::String, float> valuesOf (DrenchedProcessor& p)
    {
        std::map<juce::String, float> values;

        for (auto* param : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                values[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());

        return values;
    }

    void load (DrenchedProcessor& p, const juce::MemoryBlock& session)
    {
        testing::onMessageThread ([&] { p.setStateInformation (session.getData(), static_cast<int> (session.getSize())); });
    }

    /** Prepares the plugin (which builds the response for the loaded session there and
        then) and plays an impulse through both inputs. */
    std::array<std::vector<float>, 2> render (DrenchedProcessor& p)
    {
        testing::onMessageThread ([&] { p.prepareToPlay (rate, blockSize); });

        const auto length = static_cast<int> (windows.back() * rate);
        std::array<std::vector<float>, 2> out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < length; pos += blockSize)
        {
            buffer.clear();

            if (pos == 0)
            {
                buffer.setSample (0, 0, 1.0f);
                buffer.setSample (1, 0, 1.0f);
            }

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                out[static_cast<size_t> (ch)].insert (out[static_cast<size_t> (ch)].end(), buffer.getReadPointer (ch),
                                                      buffer.getReadPointer (ch) + blockSize);
        }

        return out;
    }

    /** Energy in dB per channel, octave band and time window. */
    std::vector<double> fingerprint (const std::array<std::vector<float>, 2>& sound)
    {
        std::vector<double> print;

        for (const auto& channel : sound)
        {
            for (const auto centre : bands)
            {
                auto band = channel;

                for (int pass = 0; pass < 2; ++pass)
                {
                    Biquad hp, lp;
                    hp.setCoefficients (Biquad::highPass (rate, centre / std::sqrt (2.0)));
                    lp.setCoefficients (Biquad::lowPass (rate, std::min (centre * std::sqrt (2.0), 0.45 * rate)));
                    hp.process (band.data(), band.size());
                    lp.process (band.data(), band.size());
                }

                for (size_t w = 0; w + 1 < windows.size(); ++w)
                {
                    double energy = 0.0;

                    for (auto n = static_cast<size_t> (windows[w] * rate); n < static_cast<size_t> (windows[w + 1] * rate) && n < band.size(); ++n)
                        energy += static_cast<double> (band[n]) * band[n];

                    print.push_back (std::max (-150.0, 10.0 * std::log10 (energy + 1.0e-30)));
                }
            }
        }

        return print;
    }

    struct Expected
    {
        juce::String space;
        int latency = 0;
        std::vector<double> print;
    };

    juce::File expectedFile (const juce::File& session) { return session.withFileExtension ("expected"); }

    std::optional<Expected> readExpected (const juce::File& session)
    {
        const auto file = expectedFile (session);

        if (! file.existsAsFile())
            return std::nullopt;

        Expected e;

        for (const auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
        {
            const auto key = line.upToFirstOccurrenceOf (" ", false, false);
            const auto rest = line.fromFirstOccurrenceOf (" ", false, false).trim();

            if (key == "space")
                e.space = rest;
            else if (key == "latency")
                e.latency = rest.getIntValue();
            else if (key == "energy")
                for (const auto& value : juce::StringArray::fromTokens (rest, " ", ""))
                    e.print.push_back (value.getDoubleValue());
        }

        return e;
    }

    void writeExpected (const juce::File& session, const Expected& e)
    {
        juce::String text;
        text << "space " << e.space << "\n" << "latency " << e.latency << "\n";

        // One line per channel and band, one value per time window.
        const auto perLine = windows.size() - 1;

        for (size_t i = 0; i < e.print.size(); i += perLine)
        {
            text << "energy";

            for (size_t k = i; k < i + perLine; ++k)
                text << " " << juce::String (e.print[k], 2);

            text << "\n";
        }

        expectedFile (session).replaceWithText (text);
    }

    bool updating() { return std::getenv ("DRENCHED_UPDATE_SESSIONS") != nullptr; }
}

TEST_CASE ("There are saved sessions to check", "[sessions]")
{
    REQUIRE (savedSessions().size() >= 2);
}

TEST_CASE ("Sessions from every released version load with the same settings", "[sessions]")
{
    for (const auto& file : savedSessions())
    {
        INFO (file.getFileName());
        const auto session = bytesOf (file);
        const auto saved = savedValues (session);
        REQUIRE (! saved.empty());

        testing::Plugin plugin;
        load (*plugin, session);

        // What the session says, and the default (how that version played) for what
        // it doesn't mention because it came later.
        for (const auto& [id, value] : valuesOf (*plugin))
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (plugin->getState().getParameter (id));
            const auto wanted = saved.count (id) > 0 ? param->convertFrom0to1 (param->convertTo0to1 (saved.at (id)))
                                                     : param->convertFrom0to1 (param->getDefaultValue());
            INFO (id << (saved.count (id) > 0 ? " (saved)" : " (added later)"));
            CHECK (std::abs (value - wanted) <= 1.0e-3f * std::max (1.0f, std::abs (wanted)));
        }
    }
}

TEST_CASE ("A session loads the same into an instance whose settings were changed", "[sessions]")
{
    for (const auto& file : savedSessions())
    {
        INFO (file.getFileName());
        const auto session = bytesOf (file);

        testing::Plugin fresh, used;
        load (*fresh, session);

        // Every parameter away from its default, and another space chosen.
        testing::onMessageThread ([&]
        {
            for (auto* param : used->getParameters())
                param->setValueNotifyingHost (param->getDefaultValue() > 0.5f ? 0.1f : 0.9f);

            used->getUiState().setProperty ("spaceId", "user", nullptr);
        });

        load (*used, session);

        const auto a = valuesOf (*fresh), b = valuesOf (*used);

        for (const auto& [id, value] : a)
        {
            INFO (id);
            CHECK (std::abs (b.at (id) - value) <= 1.0e-4f * std::max (1.0f, std::abs (value)));
        }

        CHECK (testing::onMessageThread ([&] { return fresh->chosenSpaceId() == used->chosenSpaceId(); }));
    }
}

TEST_CASE ("Sessions from every released version play the same sound", "[sessions][sound]")
{
    for (const auto& file : savedSessions())
    {
        INFO (file.getFileName());
        testing::Plugin plugin;
        load (*plugin, bytesOf (file));

        Expected now;
        now.print = fingerprint (render (*plugin));
        now.space = testing::onMessageThread ([&] { return plugin->chosenSpaceId(); });
        now.latency = plugin->getLatencySamples();

        if (updating())
        {
            writeExpected (file, now);
            continue;
        }

        const auto expected = readExpected (file);
        REQUIRE (expected.has_value());
        CHECK (now.space == expected->space);
        CHECK (now.latency == expected->latency);
        REQUIRE (now.print.size() == expected->print.size());

        // Within half a decibel in every band and stretch of time that is audible at all.
        for (size_t i = 0; i < now.print.size(); ++i)
        {
            const auto perChannel = bands.size() * (windows.size() - 1);
            const auto band = (i % perChannel) / (windows.size() - 1);
            const auto window = i % (windows.size() - 1);
            INFO ((i < perChannel ? "left" : "right") << ", " << bands[band] << " Hz, " << windows[window] << " to "
                                                       << windows[window + 1] << " s: " << now.print[i] << " dB, expected "
                                                       << expected->print[i] << " dB");

            if (expected->print[i] > -100.0 || now.print[i] > -100.0)
                CHECK (std::abs (now.print[i] - expected->print[i]) < 0.5);
        }
    }
}

TEST_CASE ("A session saved now loads back exactly", "[sessions]")
{
    testing::Plugin first, second;

    juce::MemoryBlock saved;
    testing::onMessageThread ([&]
    {
        for (auto* param : first->getParameters())
            param->setValueNotifyingHost (0.37f);

        first->getUiState().setProperty ("spaceId", "stpauls-hall", nullptr);
        first->getUiState().setProperty ("skin", "rotunda", nullptr);
        first->getStateInformation (saved);
    });

    load (*second, saved);

    const auto a = valuesOf (*first), b = valuesOf (*second);

    for (const auto& [id, value] : a)
    {
        INFO (id);
        CHECK (std::abs (b.at (id) - value) <= 1.0e-4f * std::max (1.0f, std::abs (value)));
    }

    CHECK (testing::onMessageThread ([&] { return second->getUiState().getProperty ("skin").toString(); }) == "rotunda");

    // And it says which version wrote it.
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("version") == DrenchedProcessor::stateVersion);
}

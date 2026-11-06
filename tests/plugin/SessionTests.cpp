#include "PluginTestHelpers.h"

#include <Library.h>

#include <drenched/Biquad.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numbers>
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

    // The first fingerprint: an impulse into both inputs at once, four octaves, 1.5 s. Kept
    // as it was, so every session still sounds the same by it as it always has.
    constexpr std::array<double, 4> bands { 125.0, 500.0, 2000.0, 8000.0 };
    constexpr std::array<double, 6> windows { 0.0, 0.05, 0.2, 0.5, 1.0, 1.5 }; // seconds

    // With both inputs at once, each output is the sum of two paths, so two paths swapped,
    // or one turned upside down, could sound the same by it. So each input is also played
    // on its own and each of the four paths (left input to left output, left to right,
    // right to left, right to right) checked by itself, from the lowest octave to the
    // highest and on well past the longest decay a saved session has (2.9 s at 63 Hz).
    constexpr std::array<double, 6> pathBands { 63.0, 125.0, 500.0, 2000.0, 8000.0, 16000.0 };
    constexpr std::array<double, 10> pathWindows { 0.0, 0.05, 0.2, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0 };

    // And the waveform of each path's first half second, sign and all, which energy can't
    // show, in three stretches: the first 50 ms, and after it, since where the pre-delay
    // is long the first 50 ms is only the dry sound or nothing.
    constexpr std::array<double, 4> waveStretches { 0.0, 0.05, 0.2, 0.5 }; // seconds after the latency
    constexpr int waveSums = 16;

    /*  How close is the same: within half a decibel in every band and window. Rounding
        differs between platforms and processors, and it matters most where the sound is
        quietest: a window silent on one machine can hold a little noise on another. So
        rather than skip quiet windows, both energies (now and as expected) have -110 dB of
        noise added, as powers, before they are compared. A loud window compares as before,
        two far below -110 dB compare as equal, and a window that should be silent but now
        sounds at -100 dB, or should sound at -100 dB but has gone silent, still fails. */
    constexpr double toleranceDb = 0.5;
    constexpr double noiseFloorDb = -110.0;

    double asHeard (double db)
    {
        return 10.0 * std::log10 (std::pow (10.0, db / 10.0) + std::pow (10.0, noiseFloorDb / 10.0));
    }

    const char* sideName (int side) { return side == 0 ? "left" : "right"; }

    juce::File sessionsDir() { return juce::File (DRENCHED_SESSIONS_DIR); }

    juce::Array<juce::File> savedSessions()
    {
        auto files = sessionsDir().findChildFiles (juce::File::findFiles, false, "*.session");
        files.sort();
        return files;
    }

    /** The session as a host hands it back, from this computer. A session that plays a
        file of the person's own names it by the path it had on the computer that saved it;
        that file is kept next to the session here (v0.2-everything.wav), so the path is
        pointed at it, as a person who copied the file over would find it. */
    juce::MemoryBlock bytesOf (const juce::File& file)
    {
        juce::MemoryBlock block;
        file.loadFileAsData (block);

        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));

        if (xml == nullptr)
            return block;

        auto moved = false;
        const auto place = [&moved] (juce::XmlElement& element)
        {
            const auto saved = element.getStringAttribute ("irPath");
            const auto name = saved.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);

            if (name.isNotEmpty() && sessionsDir().getChildFile (name).existsAsFile())
            {
                element.setAttribute ("irPath", sessionsDir().getChildFile (name).getFullPathName());
                moved = true;
            }
        };

        if (auto* ui = xml->getChildByName ("ui"))
            place (*ui);

        if (auto* scenes = xml->getChildByName ("scenes"))
            for (auto* scene : scenes->getChildIterator())
                place (*scene);

        if (! moved)
            return block;

        juce::MemoryBlock placed;
        juce::AudioProcessor::copyXmlToBinary (*xml, placed);
        return placed;
    }

    /** The PARAM values written in a session, plus settings older versions kept
        elsewhere that are parameters now (see DrenchedProcessor::migrateState()). */
    std::map<juce::String, float> savedValues (const juce::MemoryBlock& session)
    {
        std::map<juce::String, float> values;

        if (auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize())))
        {
            for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
                values[param->getStringAttribute ("id")] = static_cast<float> (param->getDoubleAttribute ("value"));

            // Before version 2 the listener's place across the room was interface state.
            if (auto* ui = xml->getChildByName ("ui"); ui != nullptr && ui->hasAttribute ("lx") && values.count ("listenerX") == 0)
                values["listenerX"] = static_cast<float> (ui->getDoubleAttribute ("lx") * 100.0);
        }

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

    using Sound = std::array<std::vector<float>, 2>;

    /** Prepares the plugin (which builds the response for the loaded session there and
        then) and plays a unit impulse into the inputs asked for, for `seconds`, at sampleRate. */
    Sound render (DrenchedProcessor& p, bool intoLeft, bool intoRight, double seconds, double sampleRate = rate)
    {
        testing::onMessageThread ([&] { p.prepareToPlay (sampleRate, blockSize); });

        const auto length = static_cast<int> (seconds * sampleRate);
        Sound out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < length; pos += blockSize)
        {
            buffer.clear();

            if (pos == 0)
            {
                buffer.setSample (0, 0, intoLeft ? 1.0f : 0.0f);
                buffer.setSample (1, 0, intoRight ? 1.0f : 0.0f);
            }

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                out[static_cast<size_t> (ch)].insert (out[static_cast<size_t> (ch)].end(), buffer.getReadPointer (ch),
                                                      buffer.getReadPointer (ch) + blockSize);
        }

        return out;
    }

    /** The first fingerprint: energy in dB per channel, octave band and time window. */
    std::vector<double> fingerprint (const Sound& sound)
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

    /** Biquad's Butterworth high- or low-pass, worked in double precision, so that the
        test's own rounding (at 63 Hz a float filter's is far from small) is the same on
        every platform. */
    void filter (std::vector<double>& x, double frequency, bool highPass)
    {
        const auto w = 2.0 * std::numbers::pi * frequency / rate;
        const auto cosw = std::cos (w), alpha = std::sin (w) / std::numbers::sqrt2;
        const auto a0 = 1.0 + alpha;
        const auto b1 = (highPass ? -(1.0 + cosw) : 1.0 - cosw) / a0, b0 = b1 * (highPass ? -0.5 : 0.5), b2 = b0;
        const auto a1 = -2.0 * cosw / a0, a2 = (1.0 - alpha) / a0;
        double z1 = 0.0, z2 = 0.0;

        for (auto& sample : x)
        {
            const auto in = sample;
            sample = b0 * in + z1;
            z1 = b1 * in - a1 * sample + z2;
            z2 = b2 * in - a2 * sample;
        }
    }

    /** Energy in dB of one path in each time window, in the octave band around `centre`. */
    std::vector<double> bandEnergies (const std::vector<float>& path, double centre)
    {
        std::vector<double> band (path.begin(), path.end());

        for (int pass = 0; pass < 2; ++pass)
        {
            filter (band, centre / std::numbers::sqrt2, true);
            filter (band, std::min (centre * std::numbers::sqrt2, 0.45 * rate), false);
        }

        std::vector<double> energies;

        for (size_t w = 0; w + 1 < pathWindows.size(); ++w)
        {
            double energy = 0.0;

            for (auto n = static_cast<size_t> (pathWindows[w] * rate); n < static_cast<size_t> (pathWindows[w + 1] * rate) && n < band.size(); ++n)
                energy += band[n] * band[n];

            energies.push_back (std::max (-150.0, 10.0 * std::log10 (energy + 1.0e-30)));
        }

        return energies;
    }

    /** A sketch of the waveform of one stretch of a path as the host hears it (counted from
        the latency on): sixteen sums of its samples, each sum with them signed by a fixed
        random pattern of its own. Turn the path upside down and every sum turns too; put
        another path in its place and the sums are different ones. */
    std::vector<double> waveSketch (const std::vector<float>& path, int latency, size_t stretch)
    {
        const auto from = static_cast<size_t> (latency) + static_cast<size_t> (waveStretches[stretch] * rate);
        const auto to = static_cast<size_t> (latency) + static_cast<size_t> (waveStretches[stretch + 1] * rate);
        juce::Random signs (20261101);
        std::vector<double> sums;

        for (int k = 0; k < waveSums; ++k)
        {
            double sum = 0.0;

            for (auto i = from; i < to; ++i)
            {
                const auto x = i < path.size() ? static_cast<double> (path[i]) : 0.0;
                sum += signs.nextBool() ? x : -x;
            }

            sums.push_back (sum);
        }

        return sums;
    }

    /** How far a sketch is from the one expected, in dB of the expected (with the same
        noise floor as the energies, so two silent paths match). A path turned upside down
        is +6 dB off, another path in its place about +3 dB. */
    double sketchErrorDb (const std::vector<double>& now, const std::vector<double>& expected)
    {
        double error = 0.0, level = 0.0;

        for (size_t k = 0; k < now.size(); ++k)
        {
            error += (now[k] - expected[k]) * (now[k] - expected[k]);
            level += expected[k] * expected[k];
        }

        // Each sum's square is about the energy of the stretch, so all of them waveSums times it.
        const auto floor = static_cast<double> (waveSums) * std::pow (10.0, noiseFloorDb / 10.0);
        return 10.0 * std::log10 ((error + 1.0e-30) / (level + floor));
    }

    /** One line of an .expected file after the first fingerprint: its key ("path left right
        63" for the 63 Hz band from the left input to the right output, "wave left right 50"
        for the sketch of that path from 50 ms on) and its values. */
    struct Line
    {
        juce::String key;
        std::vector<double> values;
    };

    struct Expected
    {
        juce::String space;
        int latency = 0;
        double tail = 0.0;
        std::vector<double> print;
        std::vector<Line> lines;

        const Line* find (const juce::String& key) const
        {
            for (const auto& line : lines)
                if (line.key == key)
                    return &line;

            return nullptr;
        }
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
            const auto tokens = juce::StringArray::fromTokens (line, " ", "");

            if (tokens.isEmpty())
                continue;

            const auto& key = tokens[0];
            const auto rest = line.fromFirstOccurrenceOf (" ", false, false).trim();

            if (key == "space")
            {
                e.space = rest;
            }
            else if (key == "latency")
            {
                e.latency = rest.getIntValue();
            }
            else if (key == "tail")
            {
                e.tail = rest.getDoubleValue();
            }
            else if (key == "energy")
            {
                for (int i = 1; i < tokens.size(); ++i)
                    e.print.push_back (tokens[i].getDoubleValue());
            }
            else if (key == "path" || key == "wave")
            {
                Line read;

                for (int i = 0; i < tokens.size(); ++i)
                {
                    if (i < 4)
                        read.key << (i > 0 ? " " : "") << tokens[i];
                    else
                        read.values.push_back (tokens[i].getDoubleValue());
                }

                e.lines.push_back (read);
            }
        }

        return e;
    }

    void writeExpected (const juce::File& session, const Expected& e)
    {
        juce::String text;
        text << "space " << e.space << "\n" << "latency " << e.latency << "\n" << "tail " << juce::String (e.tail, 4) << "\n";

        // The first fingerprint: one line per channel and band, one value per time window.
        const auto perLine = windows.size() - 1;

        for (size_t i = 0; i < e.print.size(); i += perLine)
        {
            text << "energy";

            for (size_t k = i; k < i + perLine; ++k)
                text << " " << juce::String (e.print[k], 2);

            text << "\n";
        }

        // Then for each path a line per band, one value per time window, and its sketch.
        for (const auto& line : e.lines)
        {
            text << line.key;

            for (const auto value : line.values)
                text << " " << (line.key.startsWith ("wave") ? juce::String::formatted ("%.6g", value) : juce::String (value, 2));

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
            INFO (id << (saved.count (id) > 0 ? " (saved)" : " (added later)") << ": " << value << ", wanted " << wanted);
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
        const auto session = bytesOf (file);
        Expected now;

        {
            testing::Plugin plugin;
            load (*plugin, session);
            now.print = fingerprint (render (*plugin, true, true, windows.back()));
            now.space = testing::onMessageThread ([&] { return plugin->chosenSpaceId(); });
            now.latency = plugin->getLatencySamples();
            now.tail = plugin->getTailLengthSeconds();
        }

        // Each input on its own, in an instance of its own so that nothing else is ringing.
        for (int input = 0; input < 2; ++input)
        {
            testing::Plugin plugin;
            load (*plugin, session);
            const auto sound = render (*plugin, input == 0, input == 1, pathWindows.back());

            for (int output = 0; output < 2; ++output)
            {
                const auto& path = sound[static_cast<size_t> (output)];
                const auto name = juce::String (sideName (input)) + " " + sideName (output);

                for (const auto centre : pathBands)
                    now.lines.push_back ({ "path " + name + " " + juce::String (juce::roundToInt (centre)), bandEnergies (path, centre) });

                for (size_t s = 0; s + 1 < waveStretches.size(); ++s)
                    now.lines.push_back ({ "wave " + name + " " + juce::String (juce::roundToInt (waveStretches[s] * 1000.0)),
                                           waveSketch (path, now.latency, s) });
            }
        }

        if (updating())
        {
            writeExpected (file, now);
            continue;
        }

        const auto expected = readExpected (file);
        REQUIRE (expected.has_value());
        CHECK (now.space == expected->space);
        CHECK (now.latency == expected->latency);

        // How long a host lets it ring on after the input stops.
        {
            INFO ("tail " << now.tail << " s, expected " << expected->tail << " s");
            CHECK (std::abs (now.tail - expected->tail) < 0.01);
        }

        // Within half a decibel in every band and stretch of time, quiet ones included.
        REQUIRE (now.print.size() == expected->print.size());

        for (size_t i = 0; i < now.print.size(); ++i)
        {
            const auto perChannel = bands.size() * (windows.size() - 1);
            const auto band = (i % perChannel) / (windows.size() - 1);
            const auto window = i % (windows.size() - 1);
            INFO ("both inputs, " << (i < perChannel ? "left" : "right") << " output, " << bands[band] << " Hz, "
                                  << windows[window] << " to " << windows[window + 1] << " s: " << now.print[i]
                                  << " dB, expected " << expected->print[i] << " dB");
            CHECK (std::abs (asHeard (now.print[i]) - asHeard (expected->print[i])) < toleranceDb);
        }

        {
            INFO ("A path or band not in " << expectedFile (file).getFileName() << " is added with DRENCHED_UPDATE_SESSIONS=1, "
                  "from a build that plays the session as it was released");
            REQUIRE (now.lines.size() == expected->lines.size());
        }

        for (const auto& line : now.lines)
        {
            const auto words = juce::StringArray::fromTokens (line.key, " ", "");
            const auto* wanted = expected->find (line.key);
            INFO (words[1] << " input to " << words[2] << " output");
            REQUIRE (wanted != nullptr);
            REQUIRE (wanted->values.size() == line.values.size());

            if (words[0] == "wave")
            {
                // The waveform itself: its polarity, and which path it is.
                const auto off = sketchErrorDb (line.values, wanted->values);
                INFO ("waveform from " << words[3] << " ms after the latency: " << off
                                       << " dB off what it was (upside down is +6 dB, another path about +3 dB)");
                CHECK (off < -20.0);
                continue;
            }

            INFO (words[3] << " Hz");

            for (size_t w = 0; w < line.values.size(); ++w)
            {
                INFO (pathWindows[w] << " to " << pathWindows[w + 1] << " s: " << line.values[w] << " dB, expected "
                                     << wanted->values[w] << " dB");
                CHECK (std::abs (asHeard (line.values[w]) - asHeard (wanted->values[w])) < toleranceDb);
            }
        }
    }
}

TEST_CASE ("A session saved now loads back exactly", "[sessions]")
{
    // Every parameter gets a value of its own, and every switch the opposite of its
    // default, so a value saved under another parameter's id, or dropped, shows. The
    // interface state and two scenes (one of them a loaded file) come back too.
    testing::Plugin first, second;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Drenched round trip.wav");
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (file.createOutputStream().release(), 48000.0, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> ir (2, 4800);
        juce::Random random (5);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < ir.getNumSamples(); ++n)
                ir.setSample (ch, n, (random.nextFloat() - 0.5f) * std::exp (-0.001f * static_cast<float> (n)));

        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
    }

    juce::MemoryBlock saved;
    testing::onMessageThread ([&]
    {
        const auto params = first->getParameters();

        for (int i = 0; i < params.size(); ++i)
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
            REQUIRE (ranged != nullptr);
            const auto distinct = static_cast<float> (i + 1) / static_cast<float> (params.size() + 2);
            const auto isSwitch = ranged->getNumSteps() == 2;
            ranged->setValueNotifyingHost (isSwitch ? 1.0f - std::round (ranged->getDefaultValue()) : distinct);
        }

        auto ui = first->getUiState();
        ui.setProperty (UiIDs::locks, "decay predelay", nullptr);
        ui.setProperty (UiIDs::mode, "stage", nullptr);
        ui.setProperty (UiIDs::view, "waterfall", nullptr);
        ui.setProperty (UiIDs::skin, "rotunda", nullptr);
        ui.setProperty (UiIDs::width, 1111, nullptr);
        ui.setProperty (UiIDs::speakers, "-3 0;2.5 12", nullptr);

        // Scene 1 a library space, scene 2 a file of the person's own, which stays chosen.
        ui.setProperty (UiIDs::spaceId, "stpauls-hall", nullptr);
        REQUIRE (first->storeScene (0, "Hall") == 1);
        ui.setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
        ui.setProperty (UiIDs::irName, "Round trip", nullptr);
        ui.setProperty (UiIDs::spaceId, "user", nullptr);
        REQUIRE (first->storeScene (0, "My file") == 2);

        // And a room described in words, the space when the session was saved.
        REQUIRE (first->describeRoom ("wooden chapel", "Chapel of words", true)["kind"].toString() == "chapel");
        first->getStateInformation (saved);
    });

    load (*second, saved);

    const auto a = valuesOf (*first), b = valuesOf (*second);

    for (const auto& [id, value] : a)
    {
        INFO (id);
        CHECK (std::abs (b.at (id) - value) <= 1.0e-4f * std::max (1.0f, std::abs (value)));
    }

    testing::onMessageThread ([&]
    {
        for (const auto& id : { UiIDs::spaceId, UiIDs::locks, UiIDs::mode, UiIDs::view, UiIDs::skin, UiIDs::width,
                                UiIDs::speakers, UiIDs::irPath, UiIDs::irName, UiIDs::roomWords, UiIDs::roomName,
                                UiIDs::roomFrom, UiIDs::roomKind, UiIDs::roomSize, UiIDs::roomDecay, UiIDs::roomPredelay,
                                UiIDs::roomPlace, UiIDs::roomMaterials })
        {
            INFO (id.toString());
            CHECK (second->getUiState().getProperty (id).toString() == first->getUiState().getProperty (id).toString());
        }

        CHECK (juce::JSON::toString (second->sceneList()) == juce::JSON::toString (first->sceneList()));
        CHECK (second->chosenSpaceId() == "described");
        CHECK (juce::JSON::toString (second->describedRoomInfo()) == juce::JSON::toString (first->describedRoomInfo()));

        // Saving what was loaded gives the same session, byte for byte.
        juce::MemoryBlock again;
        second->getStateInformation (again);
        CHECK (again == saved);
    });

    // And it says which version wrote it.
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("version") == DrenchedProcessor::stateVersion);
    file.deleteFile();
}

namespace
{
    /** The 1 kHz octave of what both outputs played, in dB per second: an impulse of one
        sample holds less of each band the higher the rate. */
    double kilohertzDb (const std::array<std::vector<float>, 2>& sound, double sampleRate)
    {
        double energy = 0.0;

        for (auto band : sound)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                Biquad hp, lp;
                hp.setCoefficients (Biquad::highPass (sampleRate, 1000.0 / std::sqrt (2.0)));
                lp.setCoefficients (Biquad::lowPass (sampleRate, 1000.0 * std::sqrt (2.0)));
                hp.process (band.data(), band.size());
                lp.process (band.data(), band.size());
            }

            for (const auto v : band)
                energy += static_cast<double> (v) * v;
        }

        return 10.0 * std::log10 (energy * sampleRate + 1.0e-30);
    }

    /** Whether a session says how loud its loaded responses play (sessions saved before
        that was written don't). */
    bool hasLoadedLevel (const juce::MemoryBlock& session)
    {
        const auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
        return xml != nullptr && xml->hasAttribute ("loadedLevel");
    }

    /** The session as a version before the marker would have saved it. */
    juce::MemoryBlock withoutLoadedLevel (const juce::MemoryBlock& session)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
        REQUIRE (xml != nullptr);
        xml->removeAttribute ("loadedLevel");

        juce::MemoryBlock older;
        juce::AudioProcessor::copyXmlToBinary (*xml, older);
        return older;
    }
}

TEST_CASE ("A new session plays a loaded file at its own level at any rate, an older one as it always did", "[sessions][level]")
{
    // A 96 kHz file of the person's own, fully wet.
    const auto file = sessionsDir().getChildFile ("v0.2-file-level.wav");
    REQUIRE (file.existsAsFile());

    juce::MemoryBlock fresh;
    {
        testing::Plugin first;
        testing::onMessageThread ([&]
        {
            auto* mix = first->getState().getParameter (ParamIDs::mix);
            mix->setValueNotifyingHost (mix->convertTo0to1 (100.0f));

            auto ui = first->getUiState();
            ui.setProperty (UiIDs::irPath, file.getFullPathName(), nullptr);
            ui.setProperty (UiIDs::irName, "Level", nullptr);
            ui.setProperty (UiIDs::spaceId, "user", nullptr);
            first->getStateInformation (fresh);
        });
    }

    // A new instance says so in what it saves. An older session doesn't, and saved again
    // still doesn't.
    REQUIRE (hasLoadedLevel (fresh));
    const auto older = withoutLoadedLevel (fresh);
    REQUIRE_FALSE (hasLoadedLevel (older));

    for (const auto& session : { fresh, older })
    {
        testing::Plugin plugin;
        load (*plugin, session);
        juce::MemoryBlock again;
        testing::onMessageThread ([&] { plugin->getStateInformation (again); });
        CHECK (hasLoadedLevel (again) == hasLoadedLevel (session));
        CHECK (again == session);
    }

    const auto played = [] (const juce::MemoryBlock& session, double sampleRate)
    {
        testing::Plugin plugin;
        load (*plugin, session);
        return kilohertzDb (render (*plugin, true, true, windows.back(), sampleRate), sampleRate);
    };

    // At the file's own rate both play the same.
    const auto newAtFileRate = played (fresh, 96000.0), oldAtFileRate = played (older, 96000.0);
    CHECK (std::abs (newAtFileRate - oldAtFileRate) < 0.01);

    for (const auto sessionRate : { 48000.0, 192000.0 })
    {
        // A new session keeps the file's level. An older one is quieter at 48 kHz (what the
        // file has above 24 kHz, next to nothing, made up across the whole band) and louder
        // at 192 kHz (the band above 48 kHz the file doesn't have, made up), as it was.
        const auto kept = played (fresh, sessionRate) - newAtFileRate;
        const auto asBefore = played (older, sessionRate) - oldAtFileRate;
        INFO (sessionRate << " Hz: " << kept << " dB in a new session, " << asBefore << " dB in an older one");
        CHECK (std::abs (kept) < 0.2);
        CHECK (std::abs (asBefore - 10.0 * std::log10 (sessionRate / 96000.0)) < 0.3);
    }

    // A scene recalled in an older session plays at that session's level too.
    testing::Plugin plugin;
    load (*plugin, older);
    testing::onMessageThread ([&]
    {
        REQUIRE (plugin->storeScene (0, "File") == 1);
        plugin->getUiState().setProperty (UiIDs::spaceId, bundledSpaces[0].id, nullptr);
        plugin->recallScene (1, true);
        CHECK (plugin->chosenSpaceId() == "user");
    });

    CHECK (std::abs (kilohertzDb (render (*plugin, true, true, windows.back(), 192000.0), 192000.0) - played (older, 192000.0)) < 0.01);
}

TEST_CASE ("The session saved with loaded files at their own level would sound different by the old rule", "[sessions][level]")
{
    // tests/sessions/v0.2-file-level.session plays a 96 kHz file at 48 kHz: by the rule
    // of the sessions before it, 3 dB quieter. So its .expected sound holds the new rule.
    const auto session = bytesOf (sessionsDir().getChildFile ("v0.2-file-level.session"));
    REQUIRE (hasLoadedLevel (session));

    // The reverb alone, without the dry sound the session mixes in.
    const auto played = [] (const juce::MemoryBlock& s)
    {
        testing::Plugin plugin;
        load (*plugin, s);
        testing::onMessageThread ([&]
        {
            auto* mix = plugin->getState().getParameter (ParamIDs::mix);
            mix->setValueNotifyingHost (mix->convertTo0to1 (100.0f));
        });
        return kilohertzDb (render (*plugin, true, true, windows.back()), rate);
    };

    const auto change = played (withoutLoadedLevel (session)) - played (session);
    INFO ("by the old rule: " << change << " dB");
    CHECK ((change < -2.5 && change > -3.5));

    // Every session saved before it plays by the old rule; those saved since say so too.
    for (const auto& file : savedSessions())
    {
        INFO (file.getFileName());
        const auto since = file.getFileName() == "v0.2-file-level.session" || file.getFileName() == "v0.2-described.session";
        CHECK (hasLoadedLevel (bytesOf (file)) == since);
    }
}

TEST_CASE ("The session saved with a described room brings the room back as described", "[sessions][describe]")
{
    // tests/sessions/v0.2-described.session: a room described in words playing (a stone
    // chapel with a reshaped decay), and scenes of a described bathroom and a modelled
    // space. Its .expected sound checks the room is built the same; here, that it comes
    // back as the same description. DRENCHED_UPDATE_SESSIONS=1 writes the session when it
    // is missing: the fixture is made by the build that first saved described rooms.
    const auto file = sessionsDir().getChildFile ("v0.2-described.session");

    if (updating() && ! file.existsAsFile())
    {
        testing::Plugin plugin;
        testing::onMessageThread ([&]
        {
            REQUIRE (plugin->describeRoom ("small tiled bathroom", "", true)["place"].toString().contains ("bathroom"));
            REQUIRE (plugin->storeScene (0, "Bathroom") == 1);
            plugin->getUiState().setProperty (UiIDs::spaceId, bundledSpaces[0].id, nullptr);
            REQUIRE (plugin->storeScene (0, "Hall") == 2);
            REQUIRE (plugin->describeRoom ("stone chapel with wooden pews, 12 m long", "Village chapel", true)["place"].toString().contains ("chapel"));

            auto* mix = plugin->getState().getParameter (ParamIDs::mix);
            mix->setValueNotifyingHost (mix->convertTo0to1 (70.0f));
            auto* top = plugin->getState().getParameter (ParamIDs::decay[7]);
            top->setValueNotifyingHost (top->convertTo0to1 (0.6f));

            juce::MemoryBlock saved;
            plugin->getStateInformation (saved);
            REQUIRE (file.replaceWithData (saved.getData(), saved.getSize()));
        });
    }

    REQUIRE (file.existsAsFile());
    testing::Plugin plugin;
    load (*plugin, bytesOf (file));

    testing::onMessageThread ([&]
    {
        CHECK (plugin->chosenSpaceId() == "described");
        const auto room = plugin->describedRoomInfo();
        CHECK (room["name"].toString() == "Village chapel");
        CHECK (room["kind"].toString() == "chapel");
        CHECK (room["from"].toString() == "words");
        CHECK (room["words"].toString() == "stone chapel with wooden pews, 12 m long");
        CHECK (std::abs (static_cast<double> (room["D"]) - 12.0) < 0.01);
        CHECK (room["bands"].size() == 8);

        // The room as the words gave it then, not as the describer would give it now:
        // a session keeps playing the same however the describer is improved later.
        const auto saved = room["bands"];
        const auto fresh = plugin->describeRoom (room["words"].toString(), "", false)["bands"];
        INFO ("saved " << juce::JSON::toString (saved) << ", described now " << juce::JSON::toString (fresh));
        CHECK (saved.size() == fresh.size());

        plugin->recallScene (1, true);
        CHECK (plugin->chosenSpaceId() == "described");
        CHECK (plugin->describedRoomInfo()["place"].toString().contains ("bathroom"));
        plugin->recallScene (2, true);
        CHECK (plugin->chosenSpaceId() == juce::String (bundledSpaces[0].id));
    });
}

TEST_CASE ("Scenes in saved sessions bring back what they stored", "[sessions][scenes]")
{
    // A scene names its parameters by id, as the session does, and its space; recalled
    // in this version it plays what it stored, with a parameter newer than the scene at
    // its default.
    auto scenesSeen = 0;

    for (const auto& file : savedSessions())
    {
        INFO (file.getFileName());
        const auto session = bytesOf (file);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
        REQUIRE (xml != nullptr);
        const auto* stored = xml->getChildByName ("scenes");

        if (stored == nullptr)
            continue;

        testing::Plugin plugin;
        load (*plugin, session);

        for (const auto* scene : stored->getChildIterator())
        {
            const auto number = scene->getIntAttribute ("number");
            INFO ("scene " << number << " (" << scene->getStringAttribute ("name") << ")");
            ++scenesSeen;

            testing::onMessageThread ([&] { plugin->recallScene (number, true); });

            for (const auto& [id, value] : valuesOf (*plugin))
            {
                if (! SceneIDs::isStored (id))
                    continue;

                auto* param = dynamic_cast<juce::RangedAudioParameter*> (plugin->getState().getParameter (id));
                const auto wanted = scene->hasAttribute (id) ? param->convertFrom0to1 (param->convertTo0to1 (static_cast<float> (scene->getDoubleAttribute (id))))
                                                             : param->convertFrom0to1 (param->getDefaultValue());
                INFO (id << ": " << value << ", wanted " << wanted);
                CHECK (std::abs (value - wanted) <= 1.0e-3f * std::max (1.0f, std::abs (wanted)));
            }

            testing::onMessageThread ([&]
            {
                CHECK (plugin->chosenSpaceId() == scene->getStringAttribute ("spaceId"));
                CHECK (plugin->getUiState().getProperty (UiIDs::speakers).toString() == scene->getStringAttribute ("speakers"));

                if (scene->getStringAttribute ("spaceId") == "user")
                {
                    const auto info = plugin->impulseResponseInfo();
                    CHECK_FALSE (static_cast<bool> (info["missing"]));
                    CHECK (plugin->getUiState().getProperty (UiIDs::irName).toString() == scene->getStringAttribute ("irName"));
                }
            });
        }
    }

    CHECK (scenesSeen >= 3);
}

TEST_CASE ("Every parameter that has shipped is still there, the same", "[sessions]")
{
    // Sessions, scenes and host automation name a parameter by id, and a session that
    // doesn't mention one plays it at its default, as the version before it did. So no
    // parameter that has shipped may go or be renamed, nor its default or range change,
    // unnoticed, and a new one's default is signed off when it is added:
    // tests/sessions/parameters.txt lists them all, in the order hosts see them.
    const auto listFile = sessionsDir().getChildFile ("parameters.txt");
    constexpr std::array<float, 3> marks { 0.25f, 0.5f, 0.75f };

    const auto number = [] (float v) { return juce::String::formatted ("%.6g", static_cast<double> (v)); };
    const auto describe = [&] (const juce::RangedAudioParameter& param)
    {
        juce::StringArray fields { param.paramID, number (param.convertFrom0to1 (param.getDefaultValue())),
                                   number (param.getNormalisableRange().start), number (param.getNormalisableRange().end) };

        for (const auto mark : marks)
            fields.add (number (param.convertFrom0to1 (mark)));

        return fields;
    };

    testing::Plugin plugin;
    juce::StringArray shipped, rewritten;
    std::map<juce::String, juce::StringArray> now;

    for (auto* parameter : plugin->getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);
        const auto fields = describe (*ranged);
        shipped.add (ranged->paramID);
        now[ranged->paramID] = fields;
        rewritten.add (fields.joinIntoString (" "));
    }

    juce::StringArray listed, lines;
    lines.addLines (listFile.loadFileAsString());

    if (updating())
    {
        // A deliberate change, or a new parameter whose default is how the plugin played
        // before it existed: DRENCHED_UPDATE_SESSIONS=1, saying why.
        juce::StringArray kept;

        for (const auto& line : lines)
            if (line.trim().startsWith ("#"))
                kept.add (line);

        kept.addArray (rewritten);
        listFile.replaceWithText (kept.joinIntoString ("\n") + "\n");
        return;
    }

    REQUIRE (listFile.existsAsFile());

    for (const auto& line : lines)
    {
        const auto fields = juce::StringArray::fromTokens (line.upToFirstOccurrenceOf ("#", false, false), " ", "");

        if (fields.isEmpty())
            continue;

        const auto& id = fields[0];
        INFO (id);
        listed.add (id);

        // Gone or renamed: every session that set it would play it at its default.
        CHECK (now.count (id) > 0);

        if (now.count (id) == 0)
            continue;

        const auto& current = now.at (id);
        REQUIRE (fields.size() == current.size());

        for (int i = 1; i < fields.size(); ++i)
        {
            const auto wanted = fields[i].getDoubleValue(), value = current[i].getDoubleValue();
            INFO ((i == 1 ? "default" : i == 2 ? "lowest" : i == 3 ? "highest" : "value at a mark of the host's range")
                  << ": " << current[i] << ", listed " << fields[i]);
            CHECK (std::abs (value - wanted) <= 1.0e-4 * std::max (1.0, std::abs (wanted)));
        }
    }

    juce::StringArray unlisted;

    for (const auto& id : shipped)
        if (! listed.contains (id))
            unlisted.add (id);

    INFO ("Not in tests/sessions/parameters.txt (once its default is how the plugin played before it, run with "
          "DRENCHED_UPDATE_SESSIONS=1 to add): " << unlisted.joinIntoString (", "));
    CHECK (unlisted.isEmpty());

    // In the same order, which some hosts follow.
    if (unlisted.isEmpty() && listed.size() == shipped.size())
        CHECK (listed == shipped);
}

TEST_CASE ("Every parameter is in the list the interface follows", "[session]")
{
    // The editor keeps a flag per entry of ParamIDs::ordered and the page is sent only
    // those parameters, so a parameter left out of it would never reach the page.
    testing::Plugin plugin;
    const auto& parameters = plugin->getParameters();
    REQUIRE (parameters.size() == static_cast<int> (ParamIDs::ordered.size()));

    for (auto* parameter : parameters)
    {
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        REQUIRE (withId != nullptr);
        INFO (withId->paramID);
        CHECK (ParamIDs::all().indexOf (withId->paramID) >= 0);
    }
}

TEST_CASE ("Every space a session or scene can name is still there, the same", "[sessions]")
{
    // Sessions and scenes name their space by id (beta 1's modelled spaces by place in the
    // list), and a name not found plays the first modelled space instead. So no space that
    // has shipped may go, move or be renamed, nor its recording change unnoticed:
    // tests/sessions/spaces.txt lists them all, with a fingerprint of each recording.
    const auto listFile = sessionsDir().getChildFile ("spaces.txt");
    const auto fingerprint = [] (const LibrarySpace& space)
    {
        auto hash = 14695981039346656037ull; // 64-bit FNV-1a

        for (int i = 0; i < space.size; ++i)
            hash = (hash ^ static_cast<unsigned char> (space.data[i])) * 1099511628211ull;

        return juce::String::toHexString (static_cast<juce::int64> (hash)).paddedLeft ('0', 16);
    };

    juce::StringArray listed, lines;
    lines.addLines (listFile.loadFileAsString());
    int modelled = 0;

    for (const auto& line : lines)
    {
        const auto fields = juce::StringArray::fromTokens (line.upToFirstOccurrenceOf ("#", false, false), " ", "");

        if (fields.isEmpty())
            continue;

        const auto& id = fields[0];
        INFO (id);
        listed.add (id);

        if (id.startsWith ("modelled-"))
        {
            // In their original order, for beta 1's sessions.
            REQUIRE (modelled < static_cast<int> (bundledSpaces.size()));
            CHECK (id == bundledSpaces[static_cast<size_t> (modelled++)].id);
            continue;
        }

        const auto* space = library::find (id);
        REQUIRE (space != nullptr);

        // A deliberate change of recording: DRENCHED_UPDATE_SESSIONS=1, saying why.
        if (! updating())
            CHECK (fields[1] == fingerprint (*space));
    }

    CHECK (modelled == static_cast<int> (bundledSpaces.size()));

    // A space added since is listed too, so it is kept from now on.
    juce::StringArray unlisted, rewritten;

    for (const auto& space : library::spaces())
    {
        if (! listed.contains (space.id))
            unlisted.add (space.id);

        rewritten.add (space.id + " " + fingerprint (space));
    }

    if (updating())
    {
        juce::StringArray kept;

        for (const auto& line : lines)
            if (line.trim().startsWith ("#") || line.startsWith ("modelled-"))
                kept.add (line);

        kept.addArray (rewritten);
        listFile.replaceWithText (kept.joinIntoString ("\n") + "\n");
    }
    else
    {
        INFO ("Not in tests/sessions/spaces.txt (run with DRENCHED_UPDATE_SESSIONS=1 to add): " << unlisted.joinIntoString (", "));
        CHECK (unlisted.isEmpty());
    }
}


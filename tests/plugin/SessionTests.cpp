#include "PluginTestHelpers.h"

#include <Library.h>

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
                                UiIDs::speakers, UiIDs::irPath, UiIDs::irName })
        {
            INFO (id.toString());
            CHECK (second->getUiState().getProperty (id).toString() == first->getUiState().getProperty (id).toString());
        }

        CHECK (juce::JSON::toString (second->sceneList()) == juce::JSON::toString (first->sceneList()));

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

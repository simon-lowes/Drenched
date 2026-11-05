#include "MessageLoop.h"
#include "PageTestHelpers.h"

#include <LicenceManager.h>
#include <PageMessages.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>

using namespace drenched;
using Catch::Matchers::ContainsSubstring;

/*  The window's side of the bridge to the page (PageMessages): what each event the page
    sends does to the parameters, the gestures the host sees, the interface state, the
    scenes, the licence and the window. Messages are written as the page writes them and
    parsed as JSON, so numbers, true and null arrive as they do from the web view.

    The record the interface tests check the page against, tests/ui/bridge.json (every
    parameter with its range, and every event name each side uses), is made here: the
    last test fails when it is out of date, and DRENCHED_UPDATE_SESSIONS=1 rewrites it. */

namespace
{
    /** What a host hears of change gestures and values, per parameter id. */
    struct Host final : juce::AudioProcessorListener
    {
        explicit Host (DrenchedProcessor& p) : processor (p) { processor.addListener (this); }
        ~Host() override { processor.removeListener (this); }

        juce::String idOf (int index) const
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters()[index]);
            return ranged != nullptr ? ranged->paramID : juce::String();
        }

        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { ++changes[idOf (index)]; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { ++begins[idOf (index)]; ++holding[idOf (index)]; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { ++ends[idOf (index)]; --holding[idOf (index)]; }

        bool holdsNothing() const
        {
            for (const auto& [id, count] : holding)
                if (count != 0)
                    return false;

            return true;
        }

        DrenchedProcessor& processor;
        std::map<juce::String, int> changes, begins, ends, holding;
    };

    /** A licence manager that never reads the computer's licence file or reaches the server. */
    struct OfflineLicence
    {
        OfflineLicence()
        {
            LicenceManager::Environment e;
            e.today = [] { return std::int64_t { 20758 }; };
            e.file = file;
            e.post = [] (const juce::String&, const juce::var&) { return LicenceManager::Reply {}; };
            manager = std::make_unique<LicenceManager> (e);
        }

        ~OfflineLicence()
        {
            manager.reset();
            file.deleteFile();
        }

        juce::File file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("drenched-page", ".licence", false);
        std::unique_ptr<LicenceManager> manager;
    };

    /** A plugin, a window and the messages between them. */
    struct Bridge
    {
        Bridge() : watch (plugin->getState()), messages (*plugin, *licence.manager, reports, watch, window, importFolder, crashFolder) {}
        ~Bridge() { importFolder.getParentDirectory().deleteRecursively(); crashFolder.getParentDirectory().deleteRecursively(); }

        bool send (const juce::String& name, const juce::String& json = "{}") { return messages.handle (name, testing::fromPage (json)); }

        float plain (const juce::String& id) { return plugin->getState().getRawParameterValue (id)->load(); }

        juce::var ui (const juce::Identifier& key) { return plugin->getUiState().getProperty (key); }

        testing::Plugin plugin;
        OfflineLicence licence;
        ParameterWatch watch;
        testing::FakeWindow window;
        juce::File importFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getNonexistentChildFile ("drenched-page-drops", {}, false).getChildFile ("Imported");
        juce::File crashFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("drenched-page-crashes", {}, false).getChildFile ("Crash reports");
        // The relay, answering 200 to everything; what it was sent is kept.
        std::shared_ptr<juce::StringArray> posted = std::make_shared<juce::StringArray>();
        std::shared_ptr<std::atomic<int>> relayStatus = std::make_shared<std::atomic<int>> (200);
        ReportSender reports { [posted = posted, status = relayStatus] (const juce::String& json)
                               {
                                   static std::mutex lock;
                                   const std::lock_guard guard (lock);
                                   posted->add (json);
                                   return status->load();
                               } };
        PageMessages messages;
    };

    juce::RangedAudioParameter& parameter (Bridge& b, const juce::String& id)
    {
        auto* p = b.plugin->getState().getParameter (id);
        REQUIRE (p != nullptr);
        return *p;
    }

    bool updating() { return std::getenv ("DRENCHED_UPDATE_SESSIONS") != nullptr; }

    bool near (float a, float b) { return std::abs (a - b) <= 1.0e-3f * std::max (1.0f, std::abs (b)); }
}

TEST_CASE ("params sets each parameter to the plain value the page sends", "[bridge]")
{
    Bridge b;
    Host host (*b.plugin);

    for (const auto& id : ParamIDs::all())
    {
        INFO (id);
        auto& p = parameter (b, id);

        // A value the parameter can hold, written in its own units, not the host's 0 to 1.
        const auto wanted = p.convertFrom0to1 (p.convertTo0to1 (p.convertFrom0to1 (0.37f)));
        REQUIRE (b.send ("params", "{\"" + id + "\": " + juce::String (wanted, 6) + "}"));

        CHECK (near (b.plain (id), wanted));

        // One gesture around it, so a host writing automation in touch mode takes it.
        CHECK (host.begins[id] == 1);
        CHECK (host.ends[id] == 1);
    }

    CHECK (host.holdsNothing());

    SECTION ("plain, so a value in the host's 0 to 1 means that value")
    {
        b.send ("params", R"({"size": 1, "output": 0.5, "wetHighCut": 0.9})");
        CHECK (near (b.plain (ParamIDs::size), 50.0f)); // the lowest size there is, not the largest
        CHECK (near (b.plain (ParamIDs::output), 0.5f));
        CHECK (near (b.plain (ParamIDs::wetHighCut), 1000.0f));
    }

    SECTION ("several in one message")
    {
        b.send ("params", R"({"mix": 12, "predelay": 40, "freeze": 1, "decay1k": 4.5})");
        CHECK (near (b.plain (ParamIDs::mix), 12.0f));
        CHECK (near (b.plain (ParamIDs::predelay), 40.0f));
        CHECK (near (b.plain (ParamIDs::freeze), 1.0f));
        CHECK (near (b.plain (ParamIDs::decay[4]), 4.5f));
    }

    SECTION ("past either end, the nearest end")
    {
        b.send ("params", R"({"mix": 250, "output": -100})");
        CHECK (near (b.plain (ParamIDs::mix), 100.0f));
        CHECK (near (b.plain (ParamIDs::output), -24.0f));
    }
}

TEST_CASE ("params ignores ids it doesn't know and values that aren't numbers", "[bridge]")
{
    Bridge b;
    Host host (*b.plugin);
    const auto mix = b.plain (ParamIDs::mix), size = b.plain (ParamIDs::size), output = b.plain (ParamIDs::output);

    CHECK (b.send ("params", R"({"Mix": 90, "nosuch": 1, "mix": "50", "size": true, "output": null, "decay1k": [2], "predelay": {"v": 3}})"));
    CHECK (b.send ("params", R"([1, 2, 3])"));
    CHECK (b.send ("params", R"("mix")"));

    CHECK (near (b.plain (ParamIDs::mix), mix));
    CHECK (near (b.plain (ParamIDs::size), size));
    CHECK (near (b.plain (ParamIDs::output), output));
    CHECK (host.changes.empty());
    CHECK (host.begins.empty());
    CHECK (b.window.calls() == 0);
}

TEST_CASE ("What the page sets isn't sent back to it, and what the host sets is", "[bridge]")
{
    Bridge b;
    b.watch.takeChanges (false, [] (const juce::String&, float) {});
    REQUIRE_FALSE (b.watch.anyChanged());

    b.send ("params", R"({"mix": 64})");
    CHECK_FALSE (b.watch.anyChanged());

    auto& mix = parameter (b, ParamIDs::mix);
    mix.setValueNotifyingHost (mix.convertTo0to1 (20.0f));
    CHECK (b.watch.anyChanged());
}

TEST_CASE ("A drag takes its controls once and lets them go when it ends, or when the page reloads", "[bridge][gestures]")
{
    Bridge b;
    Host host (*b.plugin);

    b.send ("gesture", R"({"ids": ["mix", "decay63", "decay125", "nosuch"], "begin": true})");
    CHECK (host.begins[ParamIDs::mix] == 1);
    CHECK (host.begins[ParamIDs::decay[0]] == 1);
    CHECK (host.begins[ParamIDs::decay[1]] == 1);
    CHECK (b.messages.heldControls().isHeld (ParamIDs::mix));

    // Values during the drag are inside its gesture: no gesture each.
    b.send ("params", R"({"mix": 10})");
    b.send ("params", R"({"mix": 20, "decay63": 3})");
    b.send ("gesture", R"({"ids": ["mix"], "begin": true})");
    CHECK (host.begins[ParamIDs::mix] == 1);
    CHECK (host.ends[ParamIDs::mix] == 0);
    CHECK (near (b.plain (ParamIDs::mix), 20.0f));

    SECTION ("the drag ends")
    {
        b.send ("gesture", R"({"ids": ["mix", "decay63", "decay125"], "begin": false})");
        b.send ("gesture", R"({"ids": ["mix"], "begin": false})");
        CHECK (host.ends[ParamIDs::mix] == 1);
        CHECK (host.holdsNothing());
        CHECK (b.window.ready == 0);
    }

    SECTION ("the page reloads mid-drag, and can't end it")
    {
        CHECK (b.send ("ready"));
        CHECK (host.holdsNothing());
        CHECK_FALSE (b.messages.heldControls().isHeld (ParamIDs::mix));
        CHECK (b.window.ready == 1);
    }

    SECTION ("the window closes mid-drag")
    {
        {
            PageMessages other (*b.plugin, *b.licence.manager, b.reports, b.watch, b.window, b.importFolder, b.crashFolder);
            other.handle ("gesture", testing::fromPage (R"({"ids": ["size"], "begin": true})"));
            CHECK (host.holding[ParamIDs::size] == 1);
        }

        CHECK (host.holding[ParamIDs::size] == 0);
    }
}

TEST_CASE ("ui sets the interface state the page owns, and nothing else", "[bridge]")
{
    Bridge b;
    auto tree = b.plugin->getUiState();

    // While it is set, the window knows not to send it back (DrenchedEditor::valueTreePropertyChanged).
    struct Watcher final : juce::ValueTree::Listener
    {
        explicit Watcher (const PageMessages& m) : messages (m) {}
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& key) override { seen[key.toString()] = messages.isSettingInterface(); }
        const PageMessages& messages;
        std::map<juce::String, bool> seen;
    } watcher (b.messages);

    tree.addListener (&watcher);

    REQUIRE (b.send ("ui", R"({"spaceId": "chapel", "locks": "mix,decay", "mode": "stage", "view": "section",
                               "speakers": "-3 10;0 0", "skin": "catalogue"})"));
    CHECK (b.ui (UiIDs::spaceId) == "chapel");
    CHECK (b.ui (UiIDs::locks) == "mix,decay");
    CHECK (b.ui (UiIDs::mode) == "stage");
    CHECK (b.ui (UiIDs::view) == "section");
    CHECK (b.ui (UiIDs::speakers) == "-3 10;0 0");
    CHECK (b.ui (UiIDs::skin) == "catalogue");
    CHECK (b.window.looks == juce::StringArray { "catalogue" });
    CHECK_FALSE (b.messages.isSettingInterface());

    // Every key the page may send is one that is set, and set while the window knows it.
    for (const auto& key : PageMessages::interfaceKeys())
    {
        INFO (key);
        CHECK (watcher.seen.count (key) == 1);
        CHECK (watcher.seen[key]);
    }

    CHECK (watcher.seen.size() == static_cast<size_t> (PageMessages::interfaceKeys().size()));

    SECTION ("unknown keys, and keys the plugin sets itself, are refused")
    {
        watcher.seen.clear();
        const auto before = tree.createCopy();

        b.send ("ui", R"({"width": 2400, "irPath": "/etc/passwd", "irName": "x", "irError": "x", "irEvent": 9,
                         "space": 3, "lx": 10, "Skin": "aperture", "spaceID": "hall", "nosuch": "x"})");

        CHECK (watcher.seen.empty());
        CHECK (tree.isEquivalentTo (before));
        CHECK (b.window.looks.size() == 1);
    }

    SECTION ("speaker trims are kept to 1024 characters")
    {
        b.send ("ui", "{\"speakers\": \"" + juce::String::repeatedString ("-3 10;", 400) + "\"}");
        CHECK (b.ui (UiIDs::speakers).toString().length() == 1024);
    }

    tree.removeListener (&watcher);
}

TEST_CASE ("The scene messages store, rename, delete and recall scenes", "[bridge][scenes]")
{
    Bridge b;
    const auto names = [&b]
    {
        juce::StringArray out;
        const auto list = b.plugin->sceneList();

        for (const auto& scene : *list.getArray())
            out.add (scene["number"].toString() + " " + scene["name"].toString());

        return out;
    };

    b.send ("params", R"({"mix": 42})");
    REQUIRE (b.send ("sceneStore", R"({"number": 0, "name": ""})"));
    CHECK (names() == juce::StringArray { "1 Scene 1" }); // named for its number
    CHECK (near (b.plain (ParamIDs::scene), 1.0f)); // a new scene is the one playing
    CHECK (b.window.interface == 1);

    b.send ("params", R"({"mix": 7})");
    b.send ("sceneStore", R"({"number": 0, "name": "Wide"})");
    CHECK (names() == juce::StringArray { "1 Scene 1", "2 Wide" });
    CHECK (near (b.plain (ParamIDs::scene), 2.0f));

    b.send ("sceneRename", R"({"number": 1, "name": "Close"})");
    CHECK (names() == juce::StringArray { "1 Close", "2 Wide" });

    b.send ("sceneRecall", R"({"number": 1})");
    CHECK (near (b.plain (ParamIDs::mix), 42.0f));
    CHECK (near (b.plain (ParamIDs::scene), 1.0f));

    // Updating a scene keeps the one playing.
    b.send ("params", R"({"mix": 55})");
    b.send ("sceneStore", R"({"number": 2})");
    CHECK (near (b.plain (ParamIDs::scene), 1.0f));
    b.send ("sceneRecall", R"({"number": 2})");
    CHECK (near (b.plain (ParamIDs::mix), 55.0f));

    b.send ("sceneDelete", R"({"number": 1})");
    CHECK (names() == juce::StringArray { "2 Wide" });

    // Each one has the window send the page the new list.
    CHECK (b.window.interface == 7);

    SECTION ("a scene that isn't there changes nothing")
    {
        b.send ("sceneRecall", R"({"number": 9})");
        b.send ("sceneDelete", R"({"number": 9})");
        b.send ("sceneRecall", R"({})");
        CHECK (names() == juce::StringArray { "2 Wide" });
        CHECK (near (b.plain (ParamIDs::mix), 55.0f));
    }
}

TEST_CASE ("The window's own messages go to the window", "[bridge]")
{
    Bridge b;

    b.send ("loadIr");
    b.send ("loadIr", R"({"keep": true})");
    b.send ("match");
    CHECK (b.window.chosen == std::vector<juce::String> { "new", "keep", "match" });

    b.send ("displays", R"({"open": true})");
    b.send ("displays", R"({"open": false})");
    b.send ("displays");
    CHECK (b.window.displays == std::vector<bool> { true, false, false });

    b.send ("copyText", R"({"text": "ABC-123"})");
    CHECK (b.window.clipboard == "ABC-123");

    b.send ("copyReport", R"({"description": "Froze the tail"})");
    CHECK (b.window.clipboard.startsWith ("What I was doing:\nFroze the tail\n\nSetup:\n" + b.plugin->bugReportDetails() + "\n\n"));
    b.send ("copyReport");
    CHECK (b.window.clipboard.startsWith ("What I was doing:\n(not written)\n\nSetup:\nDrenched "));

    b.window.clipboard = juce::String::repeatedString ("x", 5000);
    b.send ("pasteText", R"({"target": "licKey"})");
    REQUIRE (b.window.sent.size() == 1);
    CHECK (b.window.sent[0].first == "pasted");
    CHECK (b.window.sent[0].second["target"] == "licKey");
    CHECK (b.window.sent[0].second["text"].toString().length() == 4096);

    b.send ("licencePage");
    CHECK (b.window.opened == juce::StringArray { b.licence.manager->status()["page"].toString() });

    // The tips were closed: the window remembers, so they are offered once.
    b.send ("tipsSeen");
    b.send ("tipsSeen", R"({"anything": 1})");
    CHECK (b.window.tips == 2);
}

TEST_CASE ("Report a bug shows what a report sends, and sends it to the relay", "[bridge][report][messageloop]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    Bridge b;

    // Opening the window says exactly what would be sent about this setup.
    b.send ("reportOpen");
    REQUIRE (b.window.sent.size() == 1);
    CHECK (b.window.sent[0].first == "report");
    const auto info = b.window.sent[0].second;
    CHECK (info["state"] == "idle");
    CHECK (info["details"] == b.plugin->bugReportDetails());
    CHECK (info["crash"] == juce::var (false));
    CHECK (info["steps"].toString().contains ("reportOpen"));

    // Nothing written, nothing sent.
    b.send ("reportSend", R"({"description": "  "})");
    CHECK (b.posted->isEmpty());

    b.send ("params", R"({"mix": 40})");
    b.send ("reportSend", R"({"description": "Freeze stuck on", "replyTo": "tester@example.org"})");
    CHECK (b.reports.status()["state"] == "sending");
    REQUIRE (testing::pumpUntil ([&] { return b.reports.status()["state"] == "sent"; }));
    REQUIRE (b.posted->size() == 1);

    const auto report = juce::JSON::parse ((*b.posted)[0]);
    CHECK (report["kind"] == "bug");
    CHECK (report["description"] == "Freeze stuck on");
    CHECK (report["replyTo"] == "tester@example.org");
    CHECK (report["details"] == b.plugin->bugReportDetails());
    CHECK (report["steps"].toString().contains ("changed mix"));
    CHECK_FALSE (report.hasProperty ("crash"));
    CHECK (b.reports.status()["message"].toString().startsWith ("Sent."));

    // The relay can't be reached: the page is told, in words.
    b.relayStatus->store (0);
    b.send ("reportSend", R"({"description": "Again"})");
    REQUIRE (testing::pumpUntil ([&] { return b.reports.status()["state"] == "failed"; }));
    CHECK (b.reports.status()["message"].toString().startsWith ("Couldn't reach the report server"));

    // Opening the window again forgets the last answer.
    b.send ("reportOpen");
    CHECK (b.reports.status()["state"] == "idle");
}

TEST_CASE ("A waiting crash report is offered, sent with the report, then let go", "[bridge][report][messageloop]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    Bridge b;
    REQUIRE (b.crashFolder.createDirectory());
    const auto file = b.crashFolder.getChildFile ("crash-1800000000.txt");
    REQUIRE (file.replaceWithText ("Drenched crashed: signal 11 (SIGSEGV)"));

    const auto shown = b.messages.reportInfo (false);
    CHECK (shown["crash"].toString().contains ("2027"));
    CHECK_FALSE (shown.hasProperty ("crashText"));

    b.send ("reportOpen");
    CHECK (b.window.sent.back().second["crashText"] == "Drenched crashed: signal 11 (SIGSEGV)");

    b.send ("copyReport", R"({"crash": true})");
    CHECK (b.window.clipboard.endsWith ("Crash:\nDrenched crashed: signal 11 (SIGSEGV)"));

    // A crash report needs no description, and it is deleted once it has arrived.
    b.relayStatus->store (502);
    b.send ("reportSend", R"({"crash": true})");
    REQUIRE (testing::pumpUntil ([&] { return b.reports.status()["state"] == "failed"; }));
    CHECK (file.existsAsFile());

    b.relayStatus->store (200);
    b.send ("reportSend", R"({"crash": true, "description": "Loading a big IR"})");
    REQUIRE (testing::pumpUntil ([&] { return b.reports.status()["state"] == "sent"; }));
    const auto report = juce::JSON::parse (b.posted->strings.getLast());
    CHECK (report["kind"] == "crash");
    CHECK (report["crash"] == "Drenched crashed: signal 11 (SIGSEGV)");
    CHECK (report["description"] == "Loading a big IR");
    CHECK_FALSE (file.existsAsFile());
    CHECK (b.messages.reportInfo (false)["crash"] == juce::var (false));

    // Or the person lets it go without sending it.
    REQUIRE (file.replaceWithText ("another"));
    b.send ("reportDismissCrash");
    CHECK_FALSE (file.existsAsFile());
    CHECK (b.window.sent.back().first == "report");
    CHECK (b.window.sent.back().second["crash"] == juce::var (false));
}

TEST_CASE ("Capture messages start and stop a capture, and the window shows it", "[bridge]")
{
    Bridge b;

    // Not prepared to play, so there is nothing to capture through: it fails, and says so.
    b.send ("startCapture", R"({"method": "clap", "listenSeconds": 4})");
    CHECK (b.window.capture == 1);
    CHECK (b.plugin->captureStatus()["stage"] == "failed");
    CHECK (b.plugin->captureStatus()["message"].toString().contains ("no audio input"));

    b.send ("cancelCapture");
    CHECK (b.window.capture == 2);
}

TEST_CASE ("The licence messages go to the licence manager", "[bridge][licence]")
{
    // The licence itself is tested in drenched_licence_tests (LicenceManagerTests.cpp), in
    // a build where it is checked. Here, in whichever build this is: an empty number is
    // refused with a message, or ignored in a beta build, which has no licence.
    Bridge b;

    b.send ("licenceActivate", R"({"licence": ""})");
    const auto status = b.licence.manager->status();

    if (LicenceManager::isBeta())
    {
        CHECK (status["state"] == "beta");
        CHECK (status["message"].toString().isEmpty());
    }
    else
    {
        CHECK_THAT (status["message"].toString().toStdString(), ContainsSubstring ("Type your licence number"));
    }

    b.send ("licenceCode", R"({"code": "DRN1.not-a-code"})");

    if (LicenceManager::isBeta())
        CHECK (b.licence.manager->status()["message"].toString().isEmpty());
    else
        CHECK_THAT (b.licence.manager->status()["message"].toString().toStdString(), ContainsSubstring ("isn't a whole activation code"));

    b.send ("licenceOpen");
    CHECK (b.licence.manager->status()["message"].toString().isEmpty());

    b.send ("licenceDeactivate"); // nothing to give back
    CHECK_FALSE (static_cast<bool> (b.licence.manager->status()["busy"]));
}

TEST_CASE ("A drop the plugin can't take is refused, and the page told", "[bridge][drop]")
{
    Bridge b;

    SECTION ("too large to send")
    {
        b.send ("dropIr", R"({"stage": "start", "id": 5, "name": "huge.wav", "size": 99999999999})");
        REQUIRE (b.window.sent.size() == 1);
        CHECK (b.window.sent[0].first == "dropFailed");
        CHECK (b.window.sent[0].second["name"] == "huge.wav");
        CHECK (static_cast<bool> (b.window.sent[0].second["large"]));
    }

    SECTION ("a piece that isn't base64, or runs past the size")
    {
        b.send ("dropIr", R"({"stage": "start", "id": 5, "name": "hall.wav", "size": 3})");
        b.send ("dropIr", R"({"stage": "piece", "id": 6, "data": "!!!"})"); // another drop's: ignored
        CHECK (b.window.sent.empty());

        b.send ("dropIr", R"({"stage": "piece", "id": 5, "data": "AQIDBAU="})");
        REQUIRE (b.window.sent.size() == 1);
        CHECK (b.window.sent[0].first == "dropFailed");
        CHECK (b.window.sent[0].second["name"] == "hall.wav");
        CHECK_FALSE (static_cast<bool> (b.window.sent[0].second["large"]));

        // Abandoned, so its end says nothing more.
        b.send ("dropIr", R"({"stage": "end", "id": 5})");
        CHECK (b.window.sent.size() == 1);
    }

    SECTION ("cancelled")
    {
        b.send ("dropIr", R"({"stage": "start", "id": 5, "name": "hall.wav", "size": 3})");
        b.send ("dropIr", R"({"stage": "cancel", "id": 5})");
        b.send ("dropIr", R"({"stage": "end", "id": 5})");
        CHECK (b.window.sent.empty());
        CHECK_FALSE (b.importFolder.exists());
    }

    SECTION ("ended before all of it arrived")
    {
        b.send ("dropIr", R"({"stage": "start", "id": 5, "name": "hall.wav", "size": 3})");
        b.send ("dropIr", R"({"stage": "piece", "id": 5, "data": "AQI="})");
        b.send ("dropIr", R"({"stage": "end", "id": 5})");
        REQUIRE (b.window.sent.size() == 1);
        CHECK (b.window.sent[0].first == "dropFailed");
    }
}

TEST_CASE ("A whole dropped file is kept in the import folder and loaded", "[bridge][drop][messageloop]")
{
    DRENCHED_NEEDS_MESSAGE_LOOP();
    Bridge b;

    juce::MemoryBlock data;
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::MemoryOutputStream (data, false), 48000.0, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> ir (2, 4800);
        juce::Random random (3);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < ir.getNumSamples(); ++n)
                ir.setSample (ch, n, (random.nextFloat() - 0.5f) * std::exp (-n / 800.0f));

        REQUIRE (writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()));
    }

    const auto half = data.getSize() / 2;
    const auto piece = [&] (size_t from, size_t to) { return juce::Base64::toBase64 (static_cast<const char*> (data.getData()) + from, to - from); };

    b.send ("dropIr", "{\"stage\": \"start\", \"id\": 8, \"name\": \"plate.wav\", \"size\": " + juce::String (static_cast<juce::int64> (data.getSize())) + "}");
    b.send ("dropIr", "{\"stage\": \"piece\", \"id\": 8, \"data\": \"" + piece (0, half) + "\"}");
    b.send ("dropIr", "{\"stage\": \"piece\", \"id\": 8, \"data\": \"" + piece (half, data.getSize()) + "\"}");
    b.send ("dropIr", R"({"stage": "end", "id": 8})");
    CHECK (b.window.sent.empty());

    const auto kept = b.importFolder.getChildFile ("plate.wav");
    CHECK (testing::pumpUntil ([&] { return b.plugin->impulseResponseInfo()["name"].toString().contains ("plate"); }));

    juce::MemoryBlock read;
    REQUIRE (kept.loadFileAsData (read));
    CHECK (read == data);
}

TEST_CASE ("Every event name the page may send is handled, and no other", "[bridge]")
{
    Bridge b;
    const auto names = PageMessages::names();

    auto once = names;
    once.removeDuplicates (false);
    CHECK (once == names);
    CHECK (names.contains ("params"));

    for (const auto* name : { "", "Params", "scene", "sceneStore ", "licenceactivate", "state", "nosuch" })
    {
        INFO (name);
        CHECK_FALSE (b.send (name, R"({"mix": 1})"));
    }

    CHECK (b.window.calls() == 0);
}

TEST_CASE ("The record the interface tests follow is up to date", "[bridge]")
{
    // tests/ui/bridge.test.mjs moves every control the page has and checks each message it
    // sends against this: every parameter id and range (the page sends values in the
    // parameter's own units), every event name the window listens for, every event the
    // window sends the page, and the interface keys ui takes. The interface tests run
    // without building the plugin, so the record is kept in the repository.
    testing::Plugin plugin;
    auto& state = plugin->getState();

    // Six significant digits, and zero for a value within float rounding of zero: a default of
    // 0 read back through the 0 to 1 range comes out as about 3e-7 on some platforms (macOS).
    const auto number = [] (float v, float span)
    {
        if (std::abs (v) < span * 1.0e-5f)
            v = 0.0f;

        return juce::var (juce::String::formatted ("%.6g", static_cast<double> (v)).getDoubleValue());
    };
    const auto list = [] (const juce::StringArray& items)
    {
        juce::Array<juce::var> array;

        for (const auto& item : items)
            array.add (item);

        return juce::JSON::toString (juce::var (array), true);
    };

    juce::StringArray lines { "{",
                              "  \"about\": \"Made by tests/plugin/PageMessageTests.cpp from ParamIDs, PageMessages and PageEvents; "
                              "rewrite it with DRENCHED_UPDATE_SESSIONS=1 drenched_plugin_tests [bridge]\",",
                              "  \"parameters\": [" };

    for (const auto& id : ParamIDs::all())
    {
        auto* p = state.getParameter (id);
        REQUIRE (p != nullptr);
        const auto& range = p->getNormalisableRange();

        auto* object = new juce::DynamicObject();
        object->setProperty ("id", id);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            object->setProperty ("kind", "choice");
            juce::Array<juce::var> choices;

            for (const auto& c : choice->choices)
                choices.add (c);

            object->setProperty ("choices", choices);
        }
        else
        {
            object->setProperty ("kind", dynamic_cast<juce::AudioParameterBool*> (p) != nullptr ? "bool"
                                         : dynamic_cast<juce::AudioParameterInt*> (p) != nullptr ? "int" : "float");
        }

        const auto span = range.end - range.start;
        object->setProperty ("min", number (range.start, span));
        object->setProperty ("max", number (range.end, span));
        object->setProperty ("step", number (range.interval, span));
        object->setProperty ("default", number (p->convertFrom0to1 (p->getDefaultValue()), span));
        lines.add ("    " + juce::JSON::toString (juce::var (object), true) + (id == ParamIDs::all()[ParamIDs::all().size() - 1] ? "" : ","));
    }

    lines.add ("  ],");
    lines.add ("  \"toPlugin\": " + list (PageMessages::names()) + ",");
    lines.add ("  \"toPage\": " + list (PageEvents::all()) + ",");
    lines.add ("  \"interfaceKeys\": " + list (PageMessages::interfaceKeys()));
    lines.add ("}");

    const auto wanted = lines.joinIntoString ("\n") + "\n";
    const auto file = juce::File (DRENCHED_UI_TESTS_DIR).getChildFile ("bridge.json");

    // The record must be JSON the interface tests can read.
    REQUIRE (juce::JSON::parse (wanted).isObject());

    if (updating())
    {
        file.replaceWithText (wanted, false, false, "\n");
        return;
    }

    INFO ("tests/ui/bridge.json is out of date: an id, a range or an event name changed. Rewrite it with "
          "DRENCHED_UPDATE_SESSIONS=1 drenched_plugin_tests [bridge] and run the interface tests (tests/ui).");
    CHECK (file.loadFileAsString().removeCharacters ("\r") == wanted);
}

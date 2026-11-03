#include "LicenceTestHelpers.h"
#include "PageTestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <mutex>
#include <thread>

using namespace drenched;
using Catch::Matchers::ContainsSubstring;

/*  The trial and the licence (LicenceManager), with a fake clock, a licence file of their
    own and a fake activation server put in through LicenceManager::Environment. Codes are
    signed with the published test key, which this build carries (checked below). */

namespace
{
    constexpr std::int64_t firstDay = 20758; // 1 November 2026

    /** One computer: its clock, its licence file and the activation server it reaches. */
    struct Computer
    {
        struct Server
        {
            std::mutex lock;
            LicenceManager::Reply reply;
            juce::StringArray paths;
            juce::Array<juce::var> bodies;
        };

        Computer() = default;
        ~Computer() { file.deleteFile(); }

        LicenceManager::Environment environment() const
        {
            LicenceManager::Environment e;
            e.today = [d = day] { return d->load(); };
            e.file = file;
            e.post = [s = server] (const juce::String& path, const juce::var& body)
            {
                const std::scoped_lock l (s->lock);
                s->paths.add (path);
                s->bodies.add (body);
                return s->reply;
            };
            return e;
        }

        void answer (int status, juce::var body = {})
        {
            const std::scoped_lock l (server->lock);
            server->reply = { status, body };
        }

        int requests() const
        {
            const std::scoped_lock l (server->lock);
            return server->paths.size();
        }

        juce::String lastPath() const
        {
            const std::scoped_lock l (server->lock);
            return server->paths[server->paths.size() - 1];
        }

        juce::var lastBody() const
        {
            const std::scoped_lock l (server->lock);
            return server->bodies.getLast();
        }

        juce::String saved (const juce::String& key) const { return testing::openLicenceFile (file)->getValue (key); }
        bool hasSaved (const juce::String& key) const { return testing::openLicenceFile (file)->containsKey (key); }

        /** What another host on this computer does to the shared file. */
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

        std::shared_ptr<std::atomic<std::int64_t>> day = std::make_shared<std::atomic<std::int64_t>> (firstDay);
        juce::File file = testing::isolatedHome().getNonexistentChildFile ("test", ".licence", false);
        std::shared_ptr<Server> server = std::make_shared<Server>();
    };

    /** A licence manager made, used and destroyed on the message thread. */
    struct Manager
    {
        explicit Manager (const Computer& computer)
            : manager (testing::onMessageThread ([e = computer.environment()] { return std::make_unique<LicenceManager> (e); }))
        {
        }

        ~Manager() { testing::onMessageThread ([this] { manager.reset(); }); }

        template <typename Fn>
        auto call (Fn fn) { return testing::onMessageThread ([this, &fn] { return fn (*manager); }); }

        juce::var status() { return call ([] (LicenceManager& m) { return m.status(); }); }
        juce::String state() { return status()["state"].toString(); }
        bool allowed() { return manager->isAudioAllowed(); }
        std::string machine() { return manager->machine(); }

        void refresh() { call ([] (LicenceManager& m) { m.refresh(); }); }

        /** The licence manager's periodic check, as its timer makes it. */
        void check() { call ([] (LicenceManager& m) { ((juce::Timer&) m).timerCallback(); }); }

        /** Waits for the activation server's answer to reach the message thread. */
        bool settled() { return testing::waitFor ([this] { return ! static_cast<bool> (manager->status()["busy"]); }); }

        std::unique_ptr<LicenceManager> manager;
    };

    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto* o = new juce::DynamicObject();

        for (const auto& [name, value] : properties)
            o->setProperty (name, value);

        return juce::var (o);
    }

    /** A machine code that isn't this computer's. */
    std::string otherMachine (const std::string& mine)
    {
        const auto other = licence::machineCode ("another computer");
        return other != mine ? other : licence::machineCode ("yet another computer");
    }
}

TEST_CASE ("The licence tests run a release build with the test key", "[licence]")
{
    // Everything here is skipped by a beta build, and the codes below are signed with the
    // test key, which a build with the production key would rightly refuse.
    REQUIRE_FALSE (LicenceManager::isBeta());
    REQUIRE (licence::usingTestKey());
}

TEST_CASE ("The plugin's own environment is the computer's clock, licence file and server", "[licence]")
{
    const auto system = LicenceManager::Environment::system();

    auto options = settings::options();
    options.filenameSuffix = "licence";
    CHECK (system.file == options.getDefaultFile());
    CHECK (system.file.getFileName() == "Drenched.licence");

    const auto today = juce::Time::currentTimeMillis() / (24LL * 60 * 60 * 1000);
    CHECK (std::abs (system.today() - today) <= 1);
    CHECK (system.post != nullptr);
}

TEST_CASE ("The trial starts once, and refreshing never restarts it", "[licence]")
{
    Computer computer;
    Manager m (computer);

    CHECK (computer.saved ("trialStart") == juce::String (firstDay));
    CHECK (m.state() == "trial");
    CHECK (static_cast<int> (m.status()["daysLeft"]) == licence::trialDays);
    CHECK (m.allowed());

    *computer.day = firstDay + 5;
    m.refresh();
    CHECK (computer.saved ("trialStart") == juce::String (firstDay));
    CHECK (static_cast<int> (m.status()["daysLeft"]) == licence::trialDays - 5);
    CHECK (m.allowed());

    // The last day of the trial, then the day after it.
    *computer.day = firstDay + licence::trialDays - 1;
    m.check();
    CHECK (static_cast<int> (m.status()["daysLeft"]) == 1);
    CHECK (m.allowed());

    *computer.day = firstDay + licence::trialDays;
    m.check();
    CHECK (m.state() == "ended");
    CHECK_FALSE (m.allowed());

    // Neither refreshing, re-opening the licence window, the periodic check nor another
    // host starting up gives the trial back.
    *computer.day = firstDay + 40;

    for (int i = 0; i < 3; ++i)
    {
        m.refresh();
        m.call ([] (LicenceManager& lm) { lm.open(); });
        m.check();
    }

    CHECK (m.state() == "ended");
    CHECK_FALSE (m.allowed());

    Manager otherHost (computer);
    CHECK (otherHost.state() == "ended");
    CHECK_FALSE (otherHost.allowed());
    CHECK (computer.saved ("trialStart") == juce::String (firstDay));
}

TEST_CASE ("A trial start edited into the far future starts the trial over instead of never ending", "[licence]")
{
    Computer computer;
    computer.write ("trialStart", "99999999");
    Manager m (computer);

    CHECK (computer.saved ("trialStart") == juce::String (firstDay));
    CHECK (static_cast<int> (m.status()["daysLeft"]) == licence::trialDays);

    *computer.day = firstDay + licence::trialDays;
    m.refresh();
    CHECK (m.state() == "ended");
    CHECK_FALSE (m.allowed());

    // Edited again while the host runs: the periodic check reads it and starts over once
    // more, which is no more than deleting the value would give.
    computer.write ("trialStart", juce::String (*computer.day + 1000));
    m.refresh();
    CHECK (computer.saved ("trialStart") == juce::String (*computer.day));

    // A clock set back by days, not months, keeps the start: no trial days are lost.
    Computer slipped;
    slipped.write ("trialStart", juce::String (firstDay + 3));
    Manager s (slipped);
    CHECK (slipped.saved ("trialStart") == juce::String (firstDay + 3));
    CHECK (static_cast<int> (s.status()["daysLeft"]) == licence::trialDays);
}

TEST_CASE ("A saved activation code for another computer is refused", "[licence]")
{
    Computer computer;
    *computer.day = firstDay + 30;
    computer.write ("trialStart", juce::String (firstDay)); // the trial has ended

    std::string mine;

    {
        Manager probe (computer);
        mine = probe.machine();
    }

    const auto other = otherMachine (mine);
    computer.write ("code", testing::codeFor (other));

    Manager m (computer);
    CHECK (m.state() == "ended");
    CHECK_FALSE (m.allowed());
    CHECK (m.status()["name"].toString().isEmpty());

    // The same code typed in is refused too, and not saved over the file's.
    m.call ([&] (LicenceManager& lm) { lm.useCode (testing::codeFor (other)); });
    CHECK (static_cast<bool> (m.status()["error"]));
    CHECK_THAT (m.status()["message"].toString().toStdString(), ContainsSubstring ("for another computer"));
    CHECK_FALSE (m.allowed());

    // This computer's code, signed the same way, is accepted: the machine is what was refused.
    computer.write ("code", testing::codeFor (mine));
    m.refresh();
    CHECK (m.state() == "licensed");
    CHECK (m.allowed());
    CHECK (m.status()["name"].toString() == juce::String::fromUTF8 ("Jos\xc3\xa9 M\xc3\xbcller"));
}

TEST_CASE ("Another host's licence file is picked up by the periodic check", "[licence]")
{
    Computer computer;
    *computer.day = firstDay + 30;
    computer.write ("trialStart", juce::String (firstDay));

    Manager m (computer);
    CHECK_FALSE (m.allowed());

    // It checks once a minute, so a trial that ends while a session is open, or a licence
    // activated in another host, is noticed without reopening anything.
    CHECK (m.call ([] (LicenceManager& lm) { return ((juce::Timer&) lm).isTimerRunning(); }));
    CHECK (m.call ([] (LicenceManager& lm) { return ((juce::Timer&) lm).getTimerInterval(); }) == 60 * 1000);

    computer.write ("code", testing::codeFor (m.machine()));
    CHECK_FALSE (m.allowed()); // not before the check
    m.check();
    CHECK (m.state() == "licensed");
    CHECK (m.allowed());

    // Deactivated in the other host.
    computer.erase ("code");
    m.check();
    CHECK (m.state() == "ended");
    CHECK_FALSE (m.allowed());
}

TEST_CASE ("activate() turns each answer from the activation server into the right outcome", "[licence]")
{
    struct Answer
    {
        const char* what;
        int status;
        juce::var body;
        const char* message;
    };

    Computer computer;
    *computer.day = firstDay + 30;
    computer.write ("trialStart", juce::String (firstDay));

    std::string mine;

    {
        Manager probe (computer);
        mine = probe.machine();
    }

    const std::vector<Answer> failures {
        { "unreachable", 0, {}, "couldn't reach the activation server" },
        { "computer limit", 409, object ({ { "error", "computer-limit" }, { "message", "server text" } }), "already active on 3 computers" },
        { "revoked", 403, object ({ { "error", "revoked" } }), "has been withdrawn" },
        { "unknown licence", 404, object ({ { "error", "unknown-licence" } }), "isn't one we sold" },
        { "too many moves", 429, object ({ { "error", "too-many-moves" } }), "moved between computers too often" },
        { "another error, with the server's words", 400, object ({ { "error", "invalid-machine" }, { "message", "Bad machine code." } }), "Bad machine code." },
        { "another error, without", 500, {}, "couldn't help with that" },
        { "unreadable code", 200, object ({ { "code", "DRN1.not-a-code.at-all" } }), "can't read" },
        { "no code", 200, object ({ { "name", "Someone" } }), "can't read" },
        { "a code for another computer", 200, object ({ { "code", testing::codeFor (otherMachine (mine)) } }), "can't read" },
    };

    SECTION ("Nothing is saved or unlocked unless the server sends this computer's code")
    {
        for (const auto& answer : failures)
        {
            INFO (answer.what);
            computer.answer (answer.status, answer.body);
            const auto before = computer.requests();

            Manager m (computer);
            // Busy until the server's answer comes back on the message thread.
            const auto busy = m.call ([] (LicenceManager& lm)
            {
                lm.activate (" 0ca2a dj7b1 mqn2w wnpzc zr7cs\n");
                return static_cast<bool> (lm.status()["busy"]);
            });

            CHECK (busy);
            REQUIRE (m.settled());
            CHECK (computer.requests() == before + 1);
            CHECK (computer.lastPath() == "/v1/activate");
            CHECK (computer.lastBody()["licence"].toString() == testing::testLicence); // tidied up
            CHECK (computer.lastBody()["machine"].toString() == juce::String (mine));

            const auto status = m.status();
            CHECK_THAT (status["message"].toString().toStdString(), ContainsSubstring (answer.message));
            CHECK (static_cast<bool> (status["error"]));
            CHECK (status["state"].toString() == "ended");
            CHECK_FALSE (m.allowed());
            CHECK_FALSE (computer.hasSaved ("code"));
        }
    }

    SECTION ("A licence number that can't be one never goes to the server")
    {
        const auto before = computer.requests();
        Manager m (computer);

        m.call ([] (LicenceManager& lm) { lm.activate ("   "); });
        CHECK_THAT (m.status()["message"].toString().toStdString(), ContainsSubstring ("Type your licence number first"));

        m.call ([] (LicenceManager& lm) { lm.activate ("0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CT"); }); // one character wrong
        CHECK_THAT (m.status()["message"].toString().toStdString(), ContainsSubstring ("isn't a Drenched licence number"));
        CHECK (static_cast<bool> (m.status()["error"]));
        CHECK (computer.requests() == before);
    }

    SECTION ("A good code is saved, and works offline from then on")
    {
        const auto code = testing::codeFor (mine);
        computer.answer (200, object ({ { "code", code }, { "name", "Someone" } }));

        {
            Manager m (computer);
            m.call ([] (LicenceManager& lm) { lm.activate (testing::testLicence); });
            REQUIRE (m.settled());

            const auto status = m.status();
            CHECK_THAT (status["message"].toString().toStdString(), ContainsSubstring ("Activated"));
            CHECK_FALSE (static_cast<bool> (status["error"]));
            CHECK (status["state"].toString() == "licensed");
            CHECK (status["licence"].toString() == testing::testLicence);
            CHECK (m.allowed());
            CHECK (computer.saved ("code") == code);
        }

        // The next session: no server, and nothing asked of it.
        computer.answer (0);
        const auto before = computer.requests();
        Manager next (computer);
        CHECK (next.state() == "licensed");
        CHECK (next.allowed());
        CHECK (computer.requests() == before);
    }
}

TEST_CASE ("deactivate() forgets the saved code only when the server agrees", "[licence]")
{
    Computer computer;
    *computer.day = firstDay + 30;
    computer.write ("trialStart", juce::String (firstDay));

    Manager m (computer);
    const auto code = testing::codeFor (m.machine());
    computer.write ("code", code);
    m.refresh();
    REQUIRE (m.state() == "licensed");

    const std::vector<std::pair<int, const char*>> refusals {
        { 0, "needs the internet" },
        { 404, "isn't one we sold" },
        { 403, "didn't accept this computer's activation code" },
        { 429, "moved between computers too often" },
        { 500, "couldn't help with that" },
    };

    for (const auto& [status, message] : refusals)
    {
        INFO ("status " << status);
        computer.answer (status, status == 404 ? object ({ { "error", "unknown-licence" } })
                               : status == 403 ? object ({ { "error", "wrong-code" },
                                                           { "message", "Only the computer holding this licence can deactivate it." } })
                               : status == 429 ? object ({ { "error", "too-many-moves" } })
                                               : juce::var());

        m.call ([] (LicenceManager& lm) { lm.deactivate(); });
        REQUIRE (m.settled());

        CHECK (computer.lastPath() == "/v1/deactivate");
        CHECK (computer.lastBody()["licence"].toString() == testing::testLicence);
        CHECK (computer.lastBody()["machine"].toString() == juce::String (m.machine()));
        CHECK (computer.lastBody()["code"].toString() == code);
        CHECK_THAT (m.status()["message"].toString().toStdString(), ContainsSubstring (message));
        CHECK (static_cast<bool> (m.status()["error"]));
        CHECK (m.state() == "licensed");
        CHECK (m.allowed());
        CHECK (computer.saved ("code") == code);
    }

    computer.answer (200, object ({ { "ok", true } }));
    m.call ([] (LicenceManager& lm) { lm.deactivate(); });
    REQUIRE (m.settled());

    CHECK_THAT (m.status()["message"].toString().toStdString(), ContainsSubstring ("deactivated"));
    CHECK_FALSE (static_cast<bool> (m.status()["error"]));
    CHECK (m.state() == "ended");
    CHECK_FALSE (m.allowed());
    CHECK_FALSE (computer.hasSaved ("code"));
    CHECK (computer.saved ("trialStart") == juce::String (firstDay));

    // Nothing left to give back.
    const auto before = computer.requests();
    m.call ([] (LicenceManager& lm) { lm.deactivate(); });
    CHECK (computer.requests() == before);
    CHECK_FALSE (static_cast<bool> (m.status()["busy"]));
}

TEST_CASE ("The plugin's own post() reads the server's status and answer", "[licence]")
{
    // A stand-in for the activation server at the address this build talks to, so the
    // HTTP layer is checked as well: a refusal must not read as "couldn't reach".
    const juce::URL server (LicenceManager::server());

    if (! LicenceManager::server().startsWith ("http://127.0.0.1:"))
        SKIP ("This build talks to " << LicenceManager::server() << ", not a local address");

    juce::StreamingSocket listener;

    if (! listener.createListener (server.getPort(), "127.0.0.1"))
        SKIP ("Something is already listening on port " << server.getPort());

    struct Exchange
    {
        juce::String response;
        juce::String request;
    };

    std::vector<Exchange> exchanges {
        { "HTTP/1.1 409 Conflict", {} },
        { "HTTP/1.1 200 OK", {} },
    };

    const juce::String replies[] { R"({"error":"computer-limit","message":"full"})", R"({"code":"DRN1.x.y","name":"Someone"})" };

    std::thread fake ([&]
    {
        for (size_t i = 0; i < exchanges.size(); ++i)
        {
            if (listener.waitUntilReady (true, 10000) != 1)
                return;

            std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());

            if (client == nullptr)
                return;

            juce::MemoryBlock received;
            char buffer[4096];

            // The headers, then as much body as they announce.
            for (;;)
            {
                if (client->waitUntilReady (true, 5000) != 1)
                    break;

                const auto n = client->read (buffer, sizeof (buffer), false);

                if (n <= 0)
                    break;

                received.append (buffer, static_cast<size_t> (n));
                const auto text = received.toString();
                const auto headersEnd = text.indexOf ("\r\n\r\n");

                if (headersEnd >= 0)
                {
                    const auto length = text.fromFirstOccurrenceOf ("Content-Length:", false, true).getIntValue();

                    if (text.getNumBytesAsUTF8() >= static_cast<size_t> (headersEnd + 4 + length))
                        break;
                }
            }

            exchanges[i].request = received.toString();
            const auto body = replies[i];
            const auto response = exchanges[i].response + "\r\nContent-Type: application/json\r\nContent-Length: "
                                + juce::String (body.getNumBytesAsUTF8()) + "\r\nConnection: close\r\n\r\n" + body;
            client->write (response.toRawUTF8(), static_cast<int> (response.getNumBytesAsUTF8()));
            client->close();
        }
    });

    const auto post = LicenceManager::Environment::system().post;
    const auto body = object ({ { "licence", testing::testLicence }, { "machine", "2CY9A-GKPDZ-6VH9N-Z3SBP" } });

    const auto refused = post ("/v1/activate", body);
    const auto accepted = post ("/v1/deactivate", body);
    fake.join();
    listener.close();

    CHECK (refused.status == 409);
    CHECK (refused.body["error"].toString() == "computer-limit");
    CHECK (accepted.status == 200);
    CHECK (accepted.body["code"].toString() == "DRN1.x.y");

    CHECK (exchanges[0].request.startsWith ("POST /v1/activate "));
    CHECK (exchanges[1].request.startsWith ("POST /v1/deactivate "));
    CHECK_THAT (exchanges[0].request.toStdString(), ContainsSubstring ("Content-Type: application/json"));
    const auto sent = juce::JSON::parse (exchanges[0].request.fromFirstOccurrenceOf ("\r\n\r\n", false, false));
    CHECK (sent["licence"].toString() == testing::testLicence);
    CHECK (sent["machine"].toString() == "2CY9A-GKPDZ-6VH9N-Z3SBP");

    // With nothing listening, the server is unreachable.
    CHECK (post ("/v1/activate", body).status == 0);
}

TEST_CASE ("The page's licence messages reach the licence manager", "[licence][bridge]")
{
    // The window's side of the bridge (PageMessages), with the licence manager of a fake
    // computer: what the licence window sends is what the manager is asked.
    Computer computer;
    *computer.day = firstDay + 30;
    computer.write ("trialStart", juce::String (firstDay));

    Manager m (computer);
    testing::Plugin plugin;
    testing::FakeWindow window;

    auto watch = testing::onMessageThread ([&] { return std::make_unique<ParameterWatch> (plugin->getState()); });
    auto messages = testing::onMessageThread ([&]
    {
        return std::make_unique<PageMessages> (*plugin, *m.manager, *watch, window, testing::isolatedHome().getChildFile ("Imported"));
    });

    const auto send = [&] (const juce::String& name, const juce::String& json)
    {
        return testing::onMessageThread ([&] { return messages->handle (name, testing::fromPage (json)); });
    };

    REQUIRE (m.state() == "ended");

    // The number as typed, which the manager tidies, goes to the activation server.
    computer.answer (200, object ({ { "code", testing::codeFor (m.machine()) } }));
    REQUIRE (send ("licenceActivate", "{\"licence\": \" " + juce::String (testing::testLicence).toLowerCase() + " \"}"));
    REQUIRE (m.settled());
    CHECK (computer.requests() == 1);
    CHECK (computer.lastPath() == "/v1/activate");
    CHECK (computer.lastBody()["licence"].toString() == testing::testLicence);
    CHECK (computer.lastBody()["machine"].toString() == juce::String (m.machine()));
    CHECK (m.state() == "licensed");

    // The licence window opening forgets the last message.
    REQUIRE (m.status()["message"].toString().isNotEmpty());
    send ("licenceOpen", "{}");
    CHECK (m.status()["message"].toString().isEmpty());

    // Deactivating gives this computer's place back.
    computer.answer (200, object ({ { "ok", true } }));
    REQUIRE (send ("licenceDeactivate", "{}"));
    REQUIRE (m.settled());
    CHECK (computer.lastPath() == "/v1/deactivate");
    CHECK (m.state() == "ended");

    // A code from the activation page, for a computer with no internet.
    REQUIRE (send ("licenceCode", "{\"code\": \"" + testing::codeFor (m.machine()) + "\"}"));
    CHECK (m.state() == "licensed");
    CHECK (computer.requests() == 2);

    // The offline activation page opens in the browser.
    send ("licencePage", "{}");
    CHECK (window.opened == juce::StringArray { m.status()["page"].toString() });

    testing::onMessageThread ([&] { messages.reset(); watch.reset(); });
}

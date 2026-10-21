#include "LicenceManager.h"
#include "Settings.h"

#include <juce_core/juce_core.h>

#ifndef DRENCHED_LICENCE_SERVER
 #define DRENCHED_LICENCE_SERVER "http://127.0.0.1:8787"
#endif

#ifndef DRENCHED_BETA
 #define DRENCHED_BETA 0
#endif

namespace drenched
{

namespace
{
    juce::PropertiesFile::Options fileOptions()
    {
        auto o = settings::options();
        o.filenameSuffix = "licence";
        return o;
    }

    std::int64_t today()
    {
        return juce::Time::currentTimeMillis() / (24LL * 60 * 60 * 1000);
    }

    std::string deviceIdentifier()
    {
        auto id = juce::SystemStats::getUniqueDeviceID();

        // Rare, but it can be empty; the computer's name still keeps one machine to one code.
        if (id.isEmpty())
            id = juce::SystemStats::getComputerName() + "/" + juce::SystemStats::getLogonName();

        return id.toStdString();
    }

    juce::String messageFor (const juce::String& error, const juce::String& serverMessage)
    {
        if (error == "computer-limit")
            return "This licence is already active on " + juce::String (licence::computersPerLicence)
                 + " computers. Deactivate it on one of them first (Licence, Deactivate this computer).";
        if (error == "unknown-licence")
            return "That licence number isn't one we sold. Check it against your purchase email.";
        if (error == "revoked")
            return "This licence number has been withdrawn. Contact support if that's a mistake.";
        if (error == "too-many-moves")
            return "This licence has moved between computers too often this year. Contact support and we'll sort it out.";

        return serverMessage.isNotEmpty() ? serverMessage : "The activation server couldn't help with that. Try again in a minute.";
    }

    const juce::String unreachable = "Drenched couldn't reach the activation server. Check the internet connection, "
                                     "or activate without internet below.";
}

//==============================================================================
LicenceManager::LicenceManager()
    : machineCode (licence::machineCode (deviceIdentifier()))
{
    // A beta never touches the licence file, so a tester's trial starts with the first release.
    if (isBeta())
        return;

    refresh();
    startTimer (60 * 1000); // the trial can end while a session is open
}

LicenceManager::~LicenceManager()
{
    stopTimer();
    network.removeAllJobs (true, 20000);
}

bool LicenceManager::isBeta() noexcept
{
    return DRENCHED_BETA != 0;
}

juce::String LicenceManager::server()
{
    return DRENCHED_LICENCE_SERVER;
}

std::optional<licence::Activation> LicenceManager::readCode (const juce::String& code) const
{
    auto a = licence::verifyActivationCode (code.toStdString(), licence::serverKey());

    if (a.has_value() && a->machine != machineCode)
        return std::nullopt;

    return a;
}

void LicenceManager::refresh()
{
    if (isBeta())
        return;

    juce::PropertiesFile file (fileOptions());

    activation = readCode (file.getValue ("code"));
    trialFirstDay = file.getValue ("trialStart").getLargeIntValue();

    if (trialFirstDay <= 0)
    {
        trialFirstDay = today();
        file.setValue ("trialStart", juce::String (trialFirstDay));
        file.saveIfNeeded();
    }

    update();
}

void LicenceManager::open()
{
    if (busy)
        return;

    message.clear();
    messageIsError = false;
    refresh();
}

void LicenceManager::update()
{
    audioAllowed.store (activation.has_value() || licence::trialDaysLeft (trialFirstDay, today()) > 0);
    sendChangeMessage();
}

void LicenceManager::timerCallback()
{
    if (! busy)
        refresh();
}

void LicenceManager::save (const juce::String& code)
{
    juce::PropertiesFile file (fileOptions());

    if (code.isEmpty())
        file.removeValue ("code");
    else
        file.setValue ("code", code);

    file.saveIfNeeded();
}

void LicenceManager::finish (const juce::String& text, bool isError)
{
    busy = false;
    message = text;
    messageIsError = isError;
    update();
}

LicenceManager::Reply LicenceManager::post (const juce::String& path, const juce::var& body)
{
    Reply reply;
    const auto url = juce::URL (server() + path).withPOSTData (juce::JSON::toString (body, true));
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                             .withExtraHeaders ("Content-Type: application/json")
                             .withConnectionTimeoutMs (15000)
                             .withStatusCode (&reply.status);

    if (auto stream = url.createInputStream (options))
        reply.body = juce::JSON::parse (stream->readEntireStreamAsString());
    else
        reply.status = 0;

    return reply;
}

void LicenceManager::activate (const juce::String& typedLicence)
{
    if (busy || isBeta())
        return;

    const auto key = licence::parseKey (typedLicence.toStdString());

    if (! key.has_value())
    {
        finish (typedLicence.trim().isEmpty() ? "Type your licence number first."
                                              : "That isn't a Drenched licence number. It has 25 letters and digits, "
                                                "like 7Q2KD-M9XRT-0BW4H-NC8VZ-3JPA6. Check for a mistyped character.", true);
        return;
    }

    busy = true;
    message = "Activating…";
    messageIsError = false;
    sendChangeMessage();

    auto* object = new juce::DynamicObject();
    object->setProperty ("licence", juce::String (*key));
    object->setProperty ("machine", juce::String (machineCode));
    const juce::var body (object);

    network.addJob ([weak = juce::WeakReference<LicenceManager> (this), body]
    {
        const auto reply = post ("/v1/activate", body);

        juce::MessageManager::callAsync ([weak, reply]
        {
            auto* self = weak.get();

            if (self == nullptr)
                return;

            if (reply.status == 0)
                return self->finish (unreachable, true);

            if (reply.status != 200)
                return self->finish (messageFor (reply.body["error"].toString(), reply.body["message"].toString()), true);

            const auto code = reply.body["code"].toString();
            self->activation = self->readCode (code);

            if (! self->activation.has_value())
                return self->finish ("The activation server sent a code this copy of Drenched can't read. Update Drenched, then try again.", true);

            self->save (code);
            self->finish ("Activated. Thank you for buying Drenched.", false);
        });
    });
}

void LicenceManager::useCode (const juce::String& code)
{
    if (busy || isBeta())
        return;

    const auto a = licence::verifyActivationCode (code.toStdString(), licence::serverKey());

    if (! a.has_value())
        return finish ("That isn't a whole activation code. Copy all of it from the web page, from DRN1 to the end.", true);

    if (a->machine != machineCode)
        return finish ("That code is for another computer (" + juce::String (a->machine)
                       + "). Make one with this computer's code, " + juce::String (machineCode) + ".", true);

    activation = a;
    save (code.removeCharacters (" \t\r\n"));
    finish ("Activated. Thank you for buying Drenched.", false);
}

void LicenceManager::deactivate()
{
    if (busy || ! activation.has_value())
        return;

    busy = true;
    message = "Deactivating…";
    messageIsError = false;
    sendChangeMessage();

    auto* object = new juce::DynamicObject();
    object->setProperty ("licence", juce::String (activation->key));
    object->setProperty ("machine", juce::String (machineCode));
    const juce::var body (object);

    network.addJob ([weak = juce::WeakReference<LicenceManager> (this), body]
    {
        const auto reply = post ("/v1/deactivate", body);

        juce::MessageManager::callAsync ([weak, reply]
        {
            auto* self = weak.get();

            if (self == nullptr)
                return;

            if (reply.status == 0)
                return self->finish ("Deactivating needs the internet, so the activation server can free this computer's place. "
                                     "Connect and try again.", true);

            if (reply.status != 200)
                return self->finish (messageFor (reply.body["error"].toString(), reply.body["message"].toString()), true);

            self->activation.reset();
            self->save ({});
            self->finish ("This computer is deactivated. Its place is free for another computer.", false);
        });
    });
}

juce::var LicenceManager::status() const
{
    const auto daysLeft = licence::trialDaysLeft (trialFirstDay, today());
    auto* object = new juce::DynamicObject();

    object->setProperty ("state", isBeta() ? "beta" : activation.has_value() ? "licensed" : daysLeft > 0 ? "trial" : "ended");
    object->setProperty ("name", activation.has_value() ? juce::String::fromUTF8 (activation->name.c_str()) : juce::String());
    object->setProperty ("licence", activation.has_value() ? juce::String (activation->key) : juce::String());
    object->setProperty ("daysLeft", daysLeft);
    object->setProperty ("machine", juce::String (machineCode));
    object->setProperty ("page", server() + "/activate");
    object->setProperty ("busy", busy);
    object->setProperty ("message", message);
    object->setProperty ("error", messageIsError);
    object->setProperty ("testKey", licence::usingTestKey());

    return juce::var (object);
}

} // namespace drenched

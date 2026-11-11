#include "ReportSender.h"

#include <mutex>

#ifndef DRENCHED_REPORT_SERVER
 #define DRENCHED_REPORT_SERVER "http://127.0.0.1:8790"
#endif

namespace drenched
{

juce::String ReportSender::server()
{
    return DRENCHED_REPORT_SERVER;
}

namespace
{
    // The system send in flight, so closing the last window can cut it short instead of
    // waiting out a slow network on the host's message thread.
    std::mutex activeLock;
    juce::WebInputStream* active = nullptr;
    bool closing = false;
}

ReportSender::Post ReportSender::system()
{
    return [] (const juce::String& json)
    {
        juce::WebInputStream stream (juce::URL (server() + "/v1/report").withPOSTData (json), true);
        stream.withExtraHeaders ("Content-Type: application/json").withConnectionTimeout (15000);

        {
            const std::lock_guard lock (activeLock);

            if (closing)
                return 0;

            active = &stream;
        }

        const auto status = stream.connect (nullptr) ? stream.getStatusCode() : 0;

        const std::lock_guard lock (activeLock);
        active = nullptr;
        return status;
    };
}

void ReportSender::cancelSystemSends()
{
    const std::lock_guard lock (activeLock);
    closing = true;

    if (active != nullptr)
        active->cancel();
}

ReportSender::ReportSender (Post p)
    : post (std::move (p))
{
}

ReportSender::~ReportSender()
{
    // The last window has closed: a send still going is given up (the report stays in the
    // window's text for next time) rather than holding up the host.
    cancelSystemSends();
    network.removeAllJobs (true, 20000);

    const std::lock_guard lock (activeLock);
    closing = false;
}

juce::String ReportSender::messageFor (int httpStatus)
{
    if (httpStatus == 200)
        return "Sent. Thank you: it helps a lot.";

    if (httpStatus == 0)
        return "Couldn't reach the report server. Check the internet connection and try again, "
               "or use Copy instead and email it to whoever sent you Drenched.";

    if (httpStatus == 429)
        return "Lots of reports from this computer just now. Wait ten minutes and try again, or use Copy instead.";

    if (httpStatus == 400 || httpStatus == 413)
        return "The report server didn't accept this report. Use Copy instead and email it to whoever sent you Drenched.";

    return "The report server couldn't pass it on just now. Try again later, or use Copy instead.";
}

void ReportSender::send (const juce::var& report, std::function<void()> onSent)
{
    if (isSending())
        return;

    state = "sending";
    message = juce::String (juce::CharPointer_UTF8 ("Sending\xe2\x80\xa6"));
    sendChangeMessage();

    const auto json = juce::JSON::toString (report, true);

    network.addJob ([weak = juce::WeakReference<ReportSender> (this), post = post, json, onSent = std::move (onSent)]
    {
        const auto status = post (json);

        juce::MessageManager::callAsync ([weak, status, onSent]
        {
            if (auto* self = weak.get())
                self->finish (status, onSent);
        });
    });
}

void ReportSender::finish (int httpStatus, std::function<void()> onSent)
{
    state = httpStatus == 200 ? "sent" : "failed";
    message = messageFor (httpStatus);

    if (httpStatus == 200 && onSent)
        onSent();

    sendChangeMessage();
}

void ReportSender::reset()
{
    if (isSending())
        return;

    state = "idle";
    message = {};
    sendChangeMessage();
}

juce::var ReportSender::status() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("state", state);
    object->setProperty ("message", message);
    return juce::var (object);
}

} // namespace drenched

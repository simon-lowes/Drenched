#include "ReportSender.h"

#ifndef DRENCHED_REPORT_SERVER
 #define DRENCHED_REPORT_SERVER "http://127.0.0.1:8790"
#endif

namespace drenched
{

juce::String ReportSender::server()
{
    return DRENCHED_REPORT_SERVER;
}

ReportSender::Post ReportSender::system()
{
    return [] (const juce::String& json)
    {
        int status = 0;
        const auto url = juce::URL (server() + "/v1/report").withPOSTData (json);
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                 .withExtraHeaders ("Content-Type: application/json")
                                 .withConnectionTimeoutMs (15000)
                                 .withStatusCode (&status);

        if (auto stream = url.createInputStream (options))
            stream->readEntireStreamAsString();
        else
            status = 0;

        return status;
    };
}

ReportSender::ReportSender (Post p)
    : post (std::move (p))
{
}

ReportSender::~ReportSender()
{
    network.removeAllJobs (true, 20000);
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

#pragma once

#include <juce_events/juce_events.h>

#include <functional>

namespace drenched
{

/** Sends bug and crash reports to the report relay (server/src/reports.js), which emails
    them on. The relay's address is DRENCHED_REPORT_SERVER; where the email goes is only
    known to the relay. Shared by every Drenched window in the process.

    The page asks for a send (PageMessages); the answer comes back as status(), and each
    change is broadcast, so the window can tell the page. Message thread, except post,
    which runs on the sender's own thread. */
class ReportSender final : public juce::ChangeBroadcaster
{
public:
    /** Sends the report's JSON and returns the HTTP status (0 when the relay couldn't be
        reached). Called on the sender's thread. */
    using Post = std::function<int (const juce::String& json)>;

    /** Posts to DRENCHED_REPORT_SERVER + "/v1/report". */
    static Post system();

    /** The relay's address (DRENCHED_REPORT_SERVER). */
    static juce::String server();

    explicit ReportSender (Post = system());
    ~ReportSender() override;

    /** Sends a report (an object with kind, description, details and so on). onSent is
        called on the message thread if it arrives. Ignored while one is being sent. */
    void send (const juce::var& report, std::function<void()> onSent = {});

    /** The report window has opened: forget the last answer. */
    void reset();

    /** { state: "idle" | "sending" | "sent" | "failed", message } for the page. */
    juce::var status() const;

    bool isSending() const noexcept { return state == "sending"; }

    /** The words the page shows for an answer from the relay. */
    static juce::String messageFor (int httpStatus);

private:
    void finish (int httpStatus, std::function<void()> onSent);

    Post post;
    juce::String state { "idle" }, message;
    juce::ThreadPool network { juce::ThreadPoolOptions().withThreadName ("Drenched reports").withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (ReportSender)
    JUCE_DECLARE_NON_COPYABLE (ReportSender)
};

} // namespace drenched

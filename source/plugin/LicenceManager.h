#pragma once

#include <drenched/Licence.h>

#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <optional>

namespace drenched
{

/** The licence on this computer, shared by every Drenched instance in the process.

    A new computer runs the full plugin for a 14-day trial. Entering the licence number
    activates it online once; the signed activation code that comes back is kept in the
    user's settings folder and checked offline from then on. A computer with no internet
    gets the same code from the activation web page on another device. Once the trial
    has ended without a licence, the reverb is muted (the dry signal still passes), so a
    session never goes silent or makes a noise.

    Everything runs on the message thread except isAudioAllowed(), which the audio
    thread reads, and the network calls, which run on the manager's own thread. */
class LicenceManager final : public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    LicenceManager();
    ~LicenceManager() override;

    /** True while licensed or in the trial. Real-time safe. */
    bool isAudioAllowed() const noexcept { return audioAllowed.load (std::memory_order_relaxed); }

    /** Re-reads the licence file, which another host on this computer may have changed. */
    void refresh();

    /** The licence window has opened: forget the last message and re-read the file. */
    void open();

    /** Activates this computer online with a typed licence number. */
    void activate (const juce::String& typedLicence);

    /** Activates with a code from the activation web page. */
    void useCode (const juce::String& code);

    /** Gives this computer's place back, so the licence can move to another one. */
    void deactivate();

    /** Where things stand, for the interface: state (licensed, trial or ended), name,
        licence, daysLeft, machine, page (the offline activation page), busy, message,
        error and testKey. */
    juce::var status() const;

    const std::string& machine() const noexcept { return machineCode; }

    /** The activation server, set at build time (DRENCHED_LICENCE_SERVER). */
    static juce::String server();

private:
    void timerCallback() override;
    void update();
    void finish (const juce::String& message, bool isError);
    std::optional<licence::Activation> readCode (const juce::String& code) const;
    void save (const juce::String& code);

    struct Reply
    {
        int status = 0;
        juce::var body;
    };

    static Reply post (const juce::String& path, const juce::var& body);

    std::string machineCode;
    std::optional<licence::Activation> activation;
    std::int64_t trialFirstDay = 0;
    bool busy = false;
    juce::String message;
    bool messageIsError = false;
    std::atomic<bool> audioAllowed { true };

    juce::ThreadPool network { juce::ThreadPoolOptions{}.withThreadName ("Drenched licence").withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenceManager)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenceManager)
};

} // namespace drenched

#pragma once

#include "DroppedFile.h"
#include "HeldControls.h"
#include "ParameterWatch.h"
#include "PluginProcessor.h"
#include "ReportSender.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <utility>
#include <vector>

namespace drenched
{

/** The events the window sends the page (source/ui/js/app.js listens for each). */
namespace PageEvents
{
    inline const juce::Identifier state { "state" };
    inline const juce::Identifier meters { "meters" };
    inline const juce::Identifier capture { "capture" };
    inline const juce::Identifier licence { "licence" };
    inline const juce::Identifier matched { "matched" };
    inline const juce::Identifier analysis { "analysis" };
    inline const juce::Identifier spectrum { "spectrum" };
    inline const juce::Identifier pasted { "pasted" };
    inline const juce::Identifier dropFailed { "dropFailed" };
    inline const juce::Identifier report { "report" };

    /** Every one, for the record the interface tests check the page against. */
    inline juce::StringArray all()
    {
        return { state.toString(), meters.toString(), capture.toString(), licence.toString(), matched.toString(),
                 analysis.toString(), spectrum.toString(), pasted.toString(), dropFailed.toString(), report.toString() };
    }
}

/** What the page's messages do: each event the page sends, turned into parameter changes,
    gestures, saved interface state and calls on the processor and the licence.

    This is the window's side of the bridge without the web view, so it can be tested on its
    own (tests/plugin/PageMessageTests.cpp). The few things only the window can do (send the
    page an event, open a file chooser, reach the clipboard or the browser, start its timer)
    it asks of its Window. DrenchedEditor registers a listener for every name in names().
    Message thread only. */
class PageMessages
{
public:
    /** What the messages need of the window. DrenchedEditor is one; tests fake it. */
    struct Window
    {
        virtual ~Window() = default;

        /** Sends the page an event (one of PageEvents). */
        virtual void send (const juce::Identifier& event, const juce::var& payload) = 0;

        /** The page has loaded: send it everything and start keeping it up to date. */
        virtual void pageReady() = 0;

        /** The interface state the page shows (the scenes, say) has changed. */
        virtual void interfaceChanged() = 0;

        /** A capture has started or stopped. */
        virtual void captureChanged() = 0;

        /** The decay displays have opened or closed. */
        virtual void displaysOpened (bool open) = 0;

        /** Opens the file chooser for an impulse response (keep: a session's missing file). */
        virtual void chooseImpulseResponse (bool keepSettings) = 0;

        /** Opens the file chooser for a recording to match a room from. */
        virtual void chooseRecordingToMatch() = 0;

        /** A look was chosen: new windows open in it. */
        virtual void lookChosen (const juce::String& skin) = 0;

        /** The tips have been seen (closed, however): they are not offered again. */
        virtual void tipsSeen() = 0;

        virtual void openInBrowser (const juce::URL&) = 0;
        virtual void copyToClipboard (const juce::String&) = 0;
        virtual juce::String clipboardText() = 0;
    };

    /** importFolder is where copies of dropped impulse responses are kept, crashFolder
        where crash reports wait to be sent (crashReports). */
    PageMessages (DrenchedProcessor&, LicenceManager&, ReportSender&, ParameterWatch&, Window&,
                  juce::File importFolder, juce::File crashFolder);

    /** Every event name the page may send, which the window listens for. */
    static juce::StringArray names();

    /** Does what the page's message says. False for a name that isn't one of names(). */
    bool handle (const juce::String& name, const juce::var& payload);

    /** True while a ui message is being applied, so the window doesn't send it back. */
    bool isSettingInterface() const noexcept { return settingInterface; }

    /** The controls the page holds in a drag. */
    HeldControls& heldControls() noexcept { return held; }

    /** Interface state keys the page may set (each a UiIDs property). */
    static juce::StringArray interfaceKeys();

    /** What the report window shows: the sender's status, and whether a crash report is
        waiting (crash: its time, crashText: the report). With details, also exactly what
        a report would send about this setup. */
    juce::var reportInfo (bool withDetails) const;

    /** The report as plain text, for Copy instead (and the clipboard when sending fails). */
    juce::String reportText (const juce::String& description, bool crash) const;

private:
    void onReady (const juce::var&);
    void onParams (const juce::var&);
    void onGesture (const juce::var&);
    void onUi (const juce::var&);
    void onDropIr (const juce::var&);
    void onScene (const juce::String& action, const juce::var&);
    void onPasteText (const juce::var&);
    void onReportSend (const juce::var&);
    void noteStep (const juce::String& name, const juce::var&);

    using Handler = std::function<void (PageMessages&, const juce::var&)>;
    static const std::vector<std::pair<const char*, Handler>>& table();

    DrenchedProcessor& processor;
    LicenceManager& licence;
    ReportSender& reports;
    juce::AudioProcessorValueTreeState& state;
    ParameterWatch& watch;
    Window& window;
    const juce::File importFolder, crashFolder;

    HeldControls held { state };
    DroppedFile dropped;
    bool settingInterface = false;
    juce::String lastStep;

    JUCE_DECLARE_NON_COPYABLE (PageMessages)
};

} // namespace drenched

#pragma once

#include "PageMessages.h"
#include "ParameterWatch.h"
#include "PluginProcessor.h"
#include "WebView.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <set>

namespace drenched
{

/** The plugin window: one web view showing the interface in source/ui, with every skin.

    The page owns the drawing and the interaction. This class owns the bridge: it serves the
    page's files from the binary, pushes parameter and session state to the page, and turns
    the page's events into parameter changes, gestures and saved interface state (what each
    event does is in PageMessages, which this window serves). */
class DrenchedEditor final : public juce::AudioProcessorEditor,
                             private PageMessages::Window,
                             private juce::ValueTree::Listener,
                             private juce::AsyncUpdater,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit DrenchedEditor (DrenchedProcessor&);
    ~DrenchedEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /** The design size of every skin. The window keeps this aspect ratio at any size. */
    static constexpr int designWidth = 1200;
    static constexpr int designHeight = 760;
    static constexpr int minWidth = 900;
    static constexpr int maxWidth = 2400;

private:
    juce::WebBrowserComponent::Options makeOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    juce::var parameterValues (bool onlyChanged);
    juce::var uiValues();
    void sendFullState();

    // PageMessages::Window
    void send (const juce::Identifier& event, const juce::var& payload) override;
    void pageReady() override;
    void interfaceChanged() override;
    void captureChanged() override;
    void displaysOpened (bool open) override;
    void chooseImpulseResponse (bool keepSettings) override;
    void chooseRecordingToMatch() override;
    void lookChosen (const juce::String& skin) override;
    void tipsSeen() override;
    void openInBrowser (const juce::URL&) override;
    void copyToClipboard (const juce::String&) override;
    juce::String clipboardText() override;

    void sendDisplays();
    void sendCaptureStatus();
    void sendLicenceStatus();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    DrenchedProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    // The parameters are looked at every tick, not listened to (see ParameterWatch).
    ParameterWatch watch { state };
    std::atomic<bool> uiDirty { true };
    bool pageLoaded = false;
    std::array<float, 4> meterDb { -60.0f, -60.0f, -60.0f, -60.0f };
    std::array<float, maxChannels> speakerDb = [] { std::array<float, maxChannels> a {}; a.fill (-60.0f); return a; }();
    bool metersWereLive = false;

    // The decay displays, while the page has one open: the live spectrum every tick, and
    // the response's analysis whenever a new response plays.
    bool displaysOpen = false, analysing = false;
    std::uint64_t analysedVersion = 0;
    bool analysedAny = false;
    double sentShortened = 0.0;
    juce::String sentLayout;
    juce::String lastCaptureStatus;

    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false;

    // Report a bug's sender, shared by every window in the process.
    juce::SharedResourcePointer<ReportSender> reports;

    // What the page's events do. Before the web view, whose listeners call it.
    PageMessages messages;

    /** The interface's web view. It shows only the interface's own page (webView::navigationFor):
        the page takes files dropped on it itself (PageMessages, dropIr), and one that gets past it
        is handed to onFile instead of opening in place of the interface. A web page opens in the
        person's browser, never in the window, where it would get the native bridge. */
    struct Page final : juce::WebBrowserComponent
    {
        using juce::WebBrowserComponent::WebBrowserComponent;

        bool pageAboutToLoad (const juce::String& url) override
        {
            switch (webView::navigationFor (url, getResourceProviderRoot()))
            {
                case webView::Navigation::load:
                    return true;

                case webView::Navigation::openFile:
                    if (onFile != nullptr)
                        onFile (juce::URL (url).getLocalFile());
                    return false;

                case webView::Navigation::openInBrowser:
                    juce::URL (url).launchInDefaultBrowser();
                    return false;

                case webView::Navigation::refuse:
                    break;
            }

            return false;
        }

        std::function<void (const juce::File&)> onFile;
    };

    Page web;

    // Shown instead of the page where the system has no web view to show it in.
    std::unique_ptr<juce::Component> missingWebView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedEditor)
};

} // namespace drenched

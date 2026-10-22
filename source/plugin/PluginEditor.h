#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <atomic>
#include <optional>
#include <set>

namespace drenched
{

/** The plugin window: one web view showing the interface in source/ui, with every skin.

    The page owns the drawing and the interaction. This class owns the bridge: it serves the
    page's files from the binary, pushes parameter and session state to the page, and turns
    the page's events into parameter changes, gestures and saved interface state. */
class DrenchedEditor final : public juce::AudioProcessorEditor,
                             private juce::AudioProcessorValueTreeState::Listener,
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
    static constexpr int minWidth = 600;
    static constexpr int maxWidth = 2400;

private:
    juce::WebBrowserComponent::Options makeOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    juce::var parameterValues (bool onlyDirty);
    juce::var uiValues();
    void sendFullState();

    void onReady (const juce::var&);
    void onParams (const juce::var&);
    void onGesture (const juce::var&);
    void onUi (const juce::var&);
    void onLoadIr (const juce::var&);
    void onMatch (const juce::var&);
    void sendCaptureStatus();
    void sendLicenceStatus();

    void parameterChanged (const juce::String& id, float value) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    DrenchedProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    static constexpr size_t numParams = 23;
    std::array<std::atomic<bool>, numParams> dirty {};
    std::atomic<bool> uiDirty { true };
    std::atomic<bool> settingFromUi { false };
    bool pageReady = false;
    std::set<juce::String> activeGestures;
    std::array<float, 4> meterDb { -60.0f, -60.0f, -60.0f, -60.0f };
    std::array<float, maxChannels> speakerDb = [] { std::array<float, maxChannels> a {}; a.fill (-60.0f); return a; }();
    bool metersWereLive = false;
    double sentShortened = 0.0;
    juce::String lastCaptureStatus;

    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false;

    juce::WebBrowserComponent web;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrenchedEditor)
};

} // namespace drenched

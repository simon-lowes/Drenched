#include "PluginEditor.h"
#include "ChannelLayouts.h"
#include "Settings.h"
#include "WebView.h"

#include <DrenchedUI.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace drenched
{

namespace
{
    juce::String mimeTypeFor (const juce::String& path)
    {
        const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();

        if (ext == "html")  return "text/html; charset=utf-8";
        if (ext == "css")   return "text/css; charset=utf-8";
        if (ext == "js")    return "text/javascript; charset=utf-8";
        if (ext == "woff2") return "font/woff2";
        if (ext == "jpg")   return "image/jpeg";
        if (ext == "png")   return "image/png";
        if (ext == "svg")   return "image/svg+xml";
        if (ext == "json")  return "application/json";

        return "application/octet-stream";
    }

    /** The output's usual name, for the page's Stage meters: 2.0, 5.1, 7.1.4 and so on. */
    juce::String outputLayoutName (const juce::AudioProcessor& processor)
    {
        return channelLayouts::nameOf (channelLayouts::speakersOf (processor.getChannelLayoutOfBus (false, 0)));
    }

    /** What the window shows where Windows has no WebView2 runtime: what's missing, and a
        way to get it. The plug-in plays as usual meanwhile. */
    class MissingWebView final : public juce::Component
    {
    public:
        MissingWebView()
        {
            message.setText ("Drenched's window needs Microsoft's WebView2 runtime, which isn't on this computer. "
                             "Install it, then close this window and open it again. Drenched plays as usual meanwhile.",
                             juce::dontSendNotification);
            message.setJustificationType (juce::Justification::centred);
            message.setFont (juce::FontOptions (17.0f));
            message.setColour (juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible (message);

            download.setButtonText ("Get WebView2 from Microsoft");
            download.onClick = [] { juce::URL (webView::downloadUrl).launchInDefaultBrowser(); };
            addAndMakeVisible (download);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black);
        }

        void resized() override
        {
            auto area = getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 150);
            message.setBounds (area.removeFromTop (100));
            download.setBounds (area.removeFromBottom (36).withSizeKeepingCentre (260, 36));
        }

    private:
        juce::Label message;
        juce::TextButton download;
    };
}

DrenchedEditor::DrenchedEditor (DrenchedProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      state (p.getState()),
      messages (p, p.getLicence(), *reports, watch, *this, settings::importedFolder(), settings::crashFolder()),
      web (makeOptions())
{
    state.state.addListener (this);
    processor.getLicence().addChangeListener (this);
    reports->addChangeListener (this);

    web.onFile = [safe = juce::Component::SafePointer<DrenchedEditor> (this)] (const juce::File& file)
    {
        juce::MessageManager::callAsync ([safe, file]
        {
            // As the page does with a file it takes: a session whose file is missing keeps its settings.
            if (safe != nullptr && file.hasFileExtension ("wav;wave;amb;aif;aiff;flac;sofa"))
            {
                settings::trustShareOf (file); // dropped by the person, so chosen by them
                safe->processor.loadImpulseResponseAsync (file, static_cast<bool> (safe->processor.impulseResponseInfo()["missing"]));
            }
        });
    };

    // Without a web view the page never loads, so nothing is ever sent to it.
    if (webView::runtimeAvailable())
    {
        addAndMakeVisible (web);
        web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    }
    else
    {
        missingWebView = std::make_unique<MissingWebView>();
        addAndMakeVisible (*missingWebView);
    }

    // Read the saved size before anything can call resized(), which stores the size.
    const auto savedWidth = static_cast<int> (processor.getUiState().getProperty (UiIDs::width, designWidth));
    const auto width = juce::jlimit (minWidth, maxWidth, savedWidth > 0 ? savedWidth : designWidth);

    setResizable (true, true);
    setResizeLimits (minWidth, minWidth * designHeight / designWidth, maxWidth, maxWidth * designHeight / designWidth);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (width, juce::roundToInt (width * static_cast<double> (designHeight) / designWidth));
}

DrenchedEditor::~DrenchedEditor()
{
    stopTimer();
    processor.setEqSolo (WetEqualiser::noSolo); // a solo is a thing of the window
    cancelPendingUpdate();
    reports->removeChangeListener (this);
    processor.getLicence().removeChangeListener (this);
    state.state.removeListener (this);
}

//==============================================================================
juce::WebBrowserComponent::Options DrenchedEditor::makeOptions()
{
    using Options = juce::WebBrowserComponent::Options;

    auto options = Options()
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const auto& url) { return getResource (url); });

    // Every event the page sends goes to PageMessages, which does what it says.
    for (const auto& name : PageMessages::names())
        options = options.withEventListener (name, [this, name] (const juce::var& v) { messages.handle (name, v); });

    auto initial = std::make_unique<juce::DynamicObject>();
    initial->setProperty ("params", parameterValues (false));
    initial->setProperty ("ui", uiValues());
    initial->setProperty ("licence", processor.getLicence().status());
    initial->setProperty ("report", messages.reportInfo (false));
    initial->setProperty ("bits", static_cast<int> (sizeof (void*) * 8));
    initial->setProperty ("tipsSeen", settings::loadTipsSeen());
    initial->setProperty ("library", library::forInterface());
    options = options.withInitialisationData ("drenched", juce::var (initial.release()));

   #if JUCE_WINDOWS
    options = options.withBackend (Options::Backend::webview2)
                     .withWinWebView2Options (Options::WinWebView2()
                                                  .withUserDataFolder (settings::webViewDataFolder())
                                                  .withStatusBarDisabled()
                                                  .withBuiltInErrorPageDisabled()
                                                  .withBackgroundColour (juce::Colours::black));
   #endif

    return options;
}

std::optional<juce::WebBrowserComponent::Resource> DrenchedEditor::getResource (const juce::String& url)
{
    // "/" is the page itself; everything else is addressed by file name, which is unique
    // across the interface tree (fonts, scripts, stylesheets and the space images).
    auto path = url.upToFirstOccurrenceOf ("?", false, false);
    auto name = path == "/" ? juce::String ("index.html") : path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < DrenchedUI::namedResourceListSize; ++i)
    {
        const auto* resourceName = DrenchedUI::namedResourceList[i];

        if (name != DrenchedUI::getNamedResourceOriginalFilename (resourceName))
            continue;

        int size = 0;
        const auto* data = DrenchedUI::getNamedResource (resourceName, size);

        if (data == nullptr || size <= 0)
            return std::nullopt;

        juce::WebBrowserComponent::Resource resource;
        resource.data.resize (static_cast<size_t> (size));
        std::memcpy (resource.data.data(), data, static_cast<size_t> (size));
        resource.mimeType = mimeTypeFor (name);
        return resource;
    }

    return std::nullopt;
}

//==============================================================================
juce::var DrenchedEditor::parameterValues (bool onlyChanged)
{
    auto object = std::make_unique<juce::DynamicObject>();

    watch.takeChanges (onlyChanged, [this, &object] (const juce::String& id, float value)
    {
        // Automation that isn't a number plays the default (DrenchedProcessor::Setting), so show that.
        if (! std::isfinite (value))
            if (auto* parameter = state.getParameter (id))
                value = parameter->convertFrom0to1 (parameter->getDefaultValue());

        object->setProperty (id, static_cast<double> (value));
    });

    return juce::var (object.release());
}

juce::var DrenchedEditor::uiValues()
{
    auto ui = processor.getUiState();
    auto object = std::make_unique<juce::DynamicObject>();

    object->setProperty ("spaceId", processor.chosenSpaceId());
    object->setProperty ("locks", ui.getProperty (UiIDs::locks, "").toString());
    object->setProperty ("mode", ui.getProperty (UiIDs::mode, "studio").toString());
    object->setProperty ("view", ui.getProperty (UiIDs::view, "plan").toString());
    object->setProperty ("graph", ui.getProperty (UiIDs::graph, "decay").toString());
    object->setProperty ("skin", ui.getProperty (UiIDs::skin, settings::loadDefaultSkin()).toString());
    object->setProperty ("speakers", ui.getProperty (UiIDs::speakers, "").toString());
    object->setProperty ("outputs", processor.outputSpeakerNames());
    sentLayout = outputLayoutName (processor);
    object->setProperty ("layout", sentLayout);
    object->setProperty ("ir", processor.impulseResponseInfo());
    object->setProperty ("irError", ui.getProperty (UiIDs::irError, "").toString());
    object->setProperty ("irEvent", static_cast<int> (ui.getProperty (UiIDs::irEvent, 0)));
    object->setProperty ("scenes", processor.sceneList());
    sentShortened = processor.shortenedSeconds();
    object->setProperty ("shortened", sentShortened);

    return juce::var (object.release());
}

void DrenchedEditor::sendFullState()
{
    uiDirty.store (false);

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("params", parameterValues (false));
    object->setProperty ("ui", uiValues());
    web.emitEventIfBrowserIsVisible (PageEvents::state, juce::var (object.release()));

    lastCaptureStatus.clear();
    sendCaptureStatus();
    sendLicenceStatus();
}

//==============================================================================
void DrenchedEditor::send (const juce::Identifier& event, const juce::var& payload)
{
    web.emitEventIfBrowserIsVisible (event, payload);
}

void DrenchedEditor::pageReady()
{
    pageLoaded = true;
    sendFullState();
    startTimerHz (25);
}

void DrenchedEditor::interfaceChanged()
{
    uiDirty.store (true);
    triggerAsyncUpdate();
}

void DrenchedEditor::captureChanged()
{
    sendCaptureStatus();
}

void DrenchedEditor::displaysOpened (bool open)
{
    displaysOpen = open;
    analysedAny = analysedAny && displaysOpen; // reopening sends the analysis again
}

void DrenchedEditor::lookChosen (const juce::String& skin)
{
    settings::saveDefaultSkin (skin);
}

void DrenchedEditor::tipsSeen()
{
    settings::saveTipsSeen();
}

void DrenchedEditor::openInBrowser (const juce::URL& url)
{
    url.launchInDefaultBrowser();
}

void DrenchedEditor::copyToClipboard (const juce::String& text)
{
    juce::SystemClipboard::copyTextToClipboard (text);
}

juce::String DrenchedEditor::clipboardText()
{
    return juce::SystemClipboard::getTextFromClipboard();
}

void DrenchedEditor::chooseImpulseResponse (bool keep)
{
    if (choosing)
        return; // already open

    // Finding a session's missing file keeps the session's settings; loading a new one
    // starts from the file's own decay.
    choosing = true;
    chooser = std::make_unique<juce::FileChooser> (keep ? "Find this session's impulse response" : "Load an impulse response",
                                                   settings::loadIrFolder(), "*.wav;*.wave;*.amb;*.aif;*.aiff;*.flac;*.sofa");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, keep] (const juce::FileChooser& fc)
                          {
                              // The chooser stays alive until the next one replaces it, since
                              // it may still be in use while this runs.
                              const auto file = fc.getResult();
                              choosing = false;

                              if (file == juce::File())
                                  return; // cancelled

                              settings::saveIrFolder (file.getParentDirectory());
                              settings::trustShareOf (file);
                              processor.loadImpulseResponseAsync (file, keep);
                          });
}

void DrenchedEditor::chooseRecordingToMatch()
{
    if (choosing)
        return;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    choosing = true;
    chooser = std::make_unique<juce::FileChooser> ("Match a room: choose a recording made in it", settings::loadMatchFolder(),
                                                   formats.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              choosing = false;

                              if (file == juce::File())
                                  return; // cancelled

                              settings::saveMatchFolder (file.getParentDirectory());

                              auto listening = std::make_unique<juce::DynamicObject>();
                              listening->setProperty ("listening", true);
                              listening->setProperty ("file", file.getFileName());
                              web.emitEventIfBrowserIsVisible (PageEvents::matched, juce::var (listening.release()));

                              processor.matchRoomAsync (file, [editor = juce::Component::SafePointer<DrenchedEditor> (this)] (const juce::var& result)
                              {
                                  if (editor != nullptr)
                                      editor->web.emitEventIfBrowserIsVisible (PageEvents::matched, result);
                              });
                          });
}

//==============================================================================
void DrenchedEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    const auto isUi = tree.getType() == UiIDs::tree && property != UiIDs::width;

    if ((! isUi && tree.getType() != SceneIDs::scene) || messages.isSettingInterface())
        return;

    uiDirty.store (true);
    triggerAsyncUpdate();
}

void DrenchedEditor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.getType() == SceneIDs::tree)
    {
        uiDirty.store (true);
        triggerAsyncUpdate();
    }
}

void DrenchedEditor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    valueTreeChildAdded (parent, parent);
}

void DrenchedEditor::valueTreeRedirected (juce::ValueTree&)
{
    // A session or preset replaced the whole state: the page needs every value again.
    watch.resendAll();

    uiDirty.store (true);
    triggerAsyncUpdate();
}

void DrenchedEditor::handleAsyncUpdate()
{
    if (! pageLoaded || ! web.isShowing())
        return; // the flags stay set, and visibilityChanged() delivers them later

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("params", parameterValues (true));

    if (uiDirty.exchange (false))
        object->setProperty ("ui", uiValues());

    web.emitEventIfBrowserIsVisible (PageEvents::state, juce::var (object.release()));
}

void DrenchedEditor::sendCaptureStatus()
{
    if (! pageLoaded || ! web.isShowing())
        return;

    // Only changes are sent, so an idle capture costs nothing.
    const auto status = processor.captureStatus();
    const auto text = juce::JSON::toString (status, true);

    if (text == lastCaptureStatus)
        return;

    lastCaptureStatus = text;
    web.emitEventIfBrowserIsVisible (PageEvents::capture, status);
}

void DrenchedEditor::sendLicenceStatus()
{
    if (pageLoaded)
        web.emitEventIfBrowserIsVisible (PageEvents::licence, processor.getLicence().status());
}

void DrenchedEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &reports.get())
    {
        if (pageLoaded)
            web.emitEventIfBrowserIsVisible (PageEvents::report, messages.reportInfo (false));
        return;
    }

    sendLicenceStatus();
}

void DrenchedEditor::timerCallback()
{
    if (! pageLoaded || ! web.isShowing())
        return;

    sendCaptureStatus();
    sendDisplays();

    // Host automation, presets and the processor's own changes (a scene, a file's decay).
    if (watch.anyChanged())
        handleAsyncUpdate();

    // The host can change the output layout with the window open; the page's meters follow.
    if (processor.shortenedSeconds() != sentShortened || outputLayoutName (processor) != sentLayout)
    {
        uiDirty.store (true);
        triggerAsyncUpdate();
    }

    const auto peaks = processor.takePeaks();
    const std::array<float, 4> fresh { peaks.inL, peaks.inR, peaks.outL, peaks.outR };
    const auto speakerPeaks = processor.takeSpeakerPeaks();
    constexpr float fallPerTick = 1.4f; // dB at 25 Hz, about 35 dB per second
    bool anyAboveFloor = false;

    for (size_t i = 0; i < meterDb.size(); ++i)
    {
        const auto db = juce::Decibels::gainToDecibels (fresh[i], -60.0f);
        meterDb[i] = std::max (db, meterDb[i] - fallPerTick);
        anyAboveFloor = anyAboveFloor || meterDb[i] > -60.0f;
    }

    for (size_t i = 0; i < speakerDb.size(); ++i)
        speakerDb[i] = std::max (juce::Decibels::gainToDecibels (speakerPeaks[i], -60.0f), speakerDb[i] - fallPerTick);

    if (! anyAboveFloor && ! metersWereLive)
        return;

    metersWereLive = anyAboveFloor;

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("inL", meterDb[0]);
    object->setProperty ("inR", meterDb[1]);
    object->setProperty ("outL", meterDb[2]);
    object->setProperty ("outR", meterDb[3]);

    juce::Array<juce::var> speakers;

    for (int i = 0; i < std::min (processor.getTotalNumOutputChannels(), static_cast<int> (maxChannels)); ++i)
        speakers.add (speakerDb[static_cast<size_t> (i)]);

    object->setProperty ("spk", speakers);
    web.emitEventIfBrowserIsVisible (PageEvents::meters, juce::var (object.release()));
}

void DrenchedEditor::sendDisplays()
{
    if (! displaysOpen)
        return;

    juce::Array<juce::var> levels;

    for (const auto level : processor.wetSpectrum())
        levels.add (std::round (static_cast<double> (level) * 10.0) / 10.0);

    auto spectrum = std::make_unique<juce::DynamicObject>();
    spectrum->setProperty ("levels", levels);
    web.emitEventIfBrowserIsVisible (PageEvents::spectrum, juce::var (spectrum.release()));

    const auto version = processor.responseVersion();

    if (analysing || (analysedAny && version == analysedVersion))
        return;

    analysing = true;
    analysedVersion = version;

    processor.analyseResponseAsync ([editor = juce::Component::SafePointer<DrenchedEditor> (this)] (const juce::var& result)
    {
        if (editor == nullptr)
            return;

        editor->analysing = false;
        editor->analysedAny = ! result.isVoid();

        if (editor->analysedAny)
            editor->web.emitEventIfBrowserIsVisible (PageEvents::analysis, result);
    });
}

//==============================================================================
void DrenchedEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void DrenchedEditor::resized()
{
    web.setBounds (getLocalBounds());

    if (missingWebView != nullptr)
        missingWebView->setBounds (getLocalBounds());

    if (getWidth() >= minWidth)
        processor.getUiState().setProperty (UiIDs::width, getWidth(), nullptr);
}

void DrenchedEditor::visibilityChanged()
{
    if (isShowing())
        triggerAsyncUpdate();
}

} // namespace drenched

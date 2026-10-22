#include "PluginEditor.h"
#include "Settings.h"

#include <DrenchedUI.h>

#include <algorithm>
#include <cstring>

namespace drenched
{

namespace
{
    const juce::Identifier eventState { "state" };
    const juce::Identifier eventMeters { "meters" };
    const juce::Identifier eventCapture { "capture" };
    const juce::Identifier eventLicence { "licence" };

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

    template <typename Fn>
    void forEachProperty (const juce::var& object, Fn&& fn)
    {
        if (auto* obj = object.getDynamicObject())
            for (const auto& prop : obj->getProperties())
                fn (prop.name.toString(), prop.value);
    }
}

DrenchedEditor::DrenchedEditor (DrenchedProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      state (p.getState()),
      web (makeOptions())
{
    jassert (ParamIDs::all().size() == static_cast<int> (numParams));

    for (auto& flag : dirty)
        flag.store (true);

    for (const auto& id : ParamIDs::all())
        state.addParameterListener (id, this);

    state.state.addListener (this);
    processor.getLicence().addChangeListener (this);

    addAndMakeVisible (web);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

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
    cancelPendingUpdate();
    processor.getLicence().removeChangeListener (this);
    state.state.removeListener (this);

    for (const auto& id : ParamIDs::all())
        state.removeParameterListener (id, this);
}

//==============================================================================
juce::WebBrowserComponent::Options DrenchedEditor::makeOptions()
{
    using Options = juce::WebBrowserComponent::Options;

    auto options = Options()
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([] (const auto& url) { return getResource (url); })
        .withEventListener ("ready",   [this] (const juce::var& v) { onReady (v); })
        .withEventListener ("params",  [this] (const juce::var& v) { onParams (v); })
        .withEventListener ("gesture", [this] (const juce::var& v) { onGesture (v); })
        .withEventListener ("ui",      [this] (const juce::var& v) { onUi (v); })
        .withEventListener ("loadIr",  [this] (const juce::var& v) { onLoadIr (v); })
        .withEventListener ("startCapture",  [this] (const juce::var& v) { processor.startCapture (v); sendCaptureStatus(); })
        .withEventListener ("cancelCapture", [this] (const juce::var&) { processor.cancelCapture(); sendCaptureStatus(); })
        .withEventListener ("licenceOpen",       [this] (const juce::var&) { processor.getLicence().open(); })
        .withEventListener ("licenceActivate",   [this] (const juce::var& v) { processor.getLicence().activate (v["licence"].toString()); })
        .withEventListener ("licenceCode",       [this] (const juce::var& v) { processor.getLicence().useCode (v["code"].toString()); })
        .withEventListener ("licenceDeactivate", [this] (const juce::var&) { processor.getLicence().deactivate(); })
        .withEventListener ("licencePage",       [this] (const juce::var&)
        {
            juce::URL (processor.getLicence().status()["page"].toString()).launchInDefaultBrowser();
        })
        // Some hosts keep copy and paste shortcuts from reaching a plugin window, so the page
        // has its own buttons for them, served by the system clipboard.
        .withEventListener ("copyText", [] (const juce::var& v) { juce::SystemClipboard::copyTextToClipboard (v["text"].toString()); })
        .withEventListener ("pasteText", [this] (const juce::var& v)
        {
            auto reply = std::make_unique<juce::DynamicObject>();
            reply->setProperty ("target", v["target"]);
            reply->setProperty ("text", juce::SystemClipboard::getTextFromClipboard().substring (0, 4096));
            web.emitEventIfBrowserIsVisible ("pasted", juce::var (reply.release()));
        });

    auto initial = std::make_unique<juce::DynamicObject>();
    initial->setProperty ("params", parameterValues (false));
    initial->setProperty ("ui", uiValues());
    initial->setProperty ("licence", processor.getLicence().status());
    initial->setProperty ("bits", static_cast<int> (sizeof (void*) * 8));
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
juce::var DrenchedEditor::parameterValues (bool onlyDirty)
{
    auto object = std::make_unique<juce::DynamicObject>();
    const auto& ids = ParamIDs::all();

    for (int i = 0; i < ids.size(); ++i)
    {
        if (onlyDirty && ! dirty[static_cast<size_t> (i)].exchange (false))
            continue;

        if (auto* raw = state.getRawParameterValue (ids[i]))
            object->setProperty (ids[i], static_cast<double> (raw->load()));
    }

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
    object->setProperty ("skin", ui.getProperty (UiIDs::skin, settings::loadDefaultSkin()).toString());
    object->setProperty ("speakers", ui.getProperty (UiIDs::speakers, "").toString());
    object->setProperty ("outputs", processor.outputSpeakerNames());
    object->setProperty ("ir", processor.impulseResponseInfo());
    object->setProperty ("irError", ui.getProperty (UiIDs::irError, "").toString());
    object->setProperty ("irEvent", static_cast<int> (ui.getProperty (UiIDs::irEvent, 0)));
    sentShortened = processor.shortenedSeconds();
    object->setProperty ("shortened", sentShortened);

    return juce::var (object.release());
}

void DrenchedEditor::sendFullState()
{
    for (auto& flag : dirty)
        flag.store (false);

    uiDirty.store (false);

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("params", parameterValues (false));
    object->setProperty ("ui", uiValues());
    web.emitEventIfBrowserIsVisible (eventState, juce::var (object.release()));

    lastCaptureStatus.clear();
    sendCaptureStatus();
    sendLicenceStatus();
}

//==============================================================================
void DrenchedEditor::onReady (const juce::var&)
{
    pageReady = true;
    sendFullState();
    startTimerHz (25);
}

void DrenchedEditor::onParams (const juce::var& payload)
{
    settingFromUi.store (true);

    forEachProperty (payload, [this] (const juce::String& id, const juce::var& value)
    {
        auto* parameter = state.getParameter (id);

        if (parameter == nullptr || ! (value.isDouble() || value.isInt() || value.isInt64()))
            return;

        const auto inGesture = activeGestures.count (id) > 0;

        if (! inGesture)
            parameter->beginChangeGesture();

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (static_cast<double> (value))));

        if (! inGesture)
            parameter->endChangeGesture();
    });

    settingFromUi.store (false);
}

void DrenchedEditor::onGesture (const juce::var& payload)
{
    const auto begin = static_cast<bool> (payload.getProperty ("begin", false));
    const auto ids = payload.getProperty ("ids", juce::var());

    if (auto* array = ids.getArray())
    {
        for (const auto& idVar : *array)
        {
            const auto id = idVar.toString();
            auto* parameter = state.getParameter (id);

            if (parameter == nullptr)
                continue;

            if (begin && activeGestures.insert (id).second)
                parameter->beginChangeGesture();
            else if (! begin && activeGestures.erase (id) > 0)
                parameter->endChangeGesture();
        }
    }
}

void DrenchedEditor::onUi (const juce::var& payload)
{
    auto ui = processor.getUiState();
    settingFromUi.store (true);

    forEachProperty (payload, [&ui] (const juce::String& key, const juce::var& value)
    {
        if (key == "spaceId")    ui.setProperty (UiIDs::spaceId, value.toString(), nullptr);
        else if (key == "locks") ui.setProperty (UiIDs::locks, value.toString(), nullptr);
        else if (key == "mode")  ui.setProperty (UiIDs::mode, value.toString(), nullptr);
        else if (key == "view")  ui.setProperty (UiIDs::view, value.toString(), nullptr);
        else if (key == "speakers") ui.setProperty (UiIDs::speakers, value.toString().substring (0, 1024), nullptr);
        else if (key == "skin")
        {
            ui.setProperty (UiIDs::skin, value.toString(), nullptr);
            settings::saveDefaultSkin (value.toString());
        }
    });

    settingFromUi.store (false);
}

void DrenchedEditor::onLoadIr (const juce::var&)
{
    if (choosing)
        return; // already open

    choosing = true;
    chooser = std::make_unique<juce::FileChooser> ("Load an impulse response", settings::loadIrFolder(),
                                                   "*.wav;*.wave;*.aif;*.aiff;*.flac");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              // The chooser stays alive until the next one replaces it, since
                              // it may still be in use while this runs.
                              const auto file = fc.getResult();
                              choosing = false;

                              if (file == juce::File())
                                  return; // cancelled

                              settings::saveIrFolder (file.getParentDirectory());
                              processor.loadImpulseResponseAsync (file);
                          });
}

//==============================================================================
void DrenchedEditor::parameterChanged (const juce::String& id, float)
{
    // Changes the page itself made come straight back through this listener on the message
    // thread; the page already shows them, so only host and preset changes travel back.
    if (settingFromUi.load() && juce::MessageManager::getInstance()->isThisTheMessageThread())
        return;

    const auto index = ParamIDs::all().indexOf (id);

    if (index >= 0)
    {
        dirty[static_cast<size_t> (index)].store (true);
        triggerAsyncUpdate();
    }
}

void DrenchedEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.getType() != UiIDs::tree || property == UiIDs::width || settingFromUi.load())
        return;

    uiDirty.store (true);
    triggerAsyncUpdate();
}

void DrenchedEditor::valueTreeRedirected (juce::ValueTree&)
{
    // A session or preset replaced the whole state: the page needs every value again.
    for (auto& flag : dirty)
        flag.store (true);

    uiDirty.store (true);
    triggerAsyncUpdate();
}

void DrenchedEditor::handleAsyncUpdate()
{
    if (! pageReady || ! web.isShowing())
        return; // the flags stay set, and visibilityChanged() delivers them later

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("params", parameterValues (true));

    if (uiDirty.exchange (false))
        object->setProperty ("ui", uiValues());

    web.emitEventIfBrowserIsVisible (eventState, juce::var (object.release()));
}

void DrenchedEditor::sendCaptureStatus()
{
    if (! pageReady || ! web.isShowing())
        return;

    // Only changes are sent, so an idle capture costs nothing.
    const auto status = processor.captureStatus();
    const auto text = juce::JSON::toString (status, true);

    if (text == lastCaptureStatus)
        return;

    lastCaptureStatus = text;
    web.emitEventIfBrowserIsVisible (eventCapture, status);
}

void DrenchedEditor::sendLicenceStatus()
{
    if (pageReady)
        web.emitEventIfBrowserIsVisible (eventLicence, processor.getLicence().status());
}

void DrenchedEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    sendLicenceStatus();
}

void DrenchedEditor::timerCallback()
{
    if (! pageReady || ! web.isShowing())
        return;

    sendCaptureStatus();

    if (processor.shortenedSeconds() != sentShortened)
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
    web.emitEventIfBrowserIsVisible (eventMeters, juce::var (object.release()));
}

//==============================================================================
void DrenchedEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void DrenchedEditor::resized()
{
    web.setBounds (getLocalBounds());

    if (getWidth() >= minWidth)
        processor.getUiState().setProperty (UiIDs::width, getWidth(), nullptr);
}

void DrenchedEditor::visibilityChanged()
{
    if (isShowing())
        triggerAsyncUpdate();
}

} // namespace drenched

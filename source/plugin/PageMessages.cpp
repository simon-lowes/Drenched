#include "PageMessages.h"

namespace drenched
{

namespace
{
    template <typename Fn>
    void forEachProperty (const juce::var& object, Fn&& fn)
    {
        if (auto* obj = object.getDynamicObject())
            for (const auto& prop : obj->getProperties())
                fn (prop.name.toString(), prop.value);
    }
}

PageMessages::PageMessages (DrenchedProcessor& p, LicenceManager& l, ParameterWatch& w, Window& win, juce::File folder)
    : processor (p),
      licence (l),
      state (p.getState()),
      watch (w),
      window (win),
      importFolder (std::move (folder))
{
}

const std::vector<std::pair<const char*, PageMessages::Handler>>& PageMessages::table()
{
    using M = PageMessages;
    using V = const juce::var&;

    static const std::vector<std::pair<const char*, Handler>> handlers {
        { "ready",    [] (M& m, V v) { m.onReady (v); } },
        { "params",   [] (M& m, V v) { m.onParams (v); } },
        { "gesture",  [] (M& m, V v) { m.onGesture (v); } },
        { "ui",       [] (M& m, V v) { m.onUi (v); } },
        { "loadIr",   [] (M& m, V v) { m.window.chooseImpulseResponse (static_cast<bool> (v["keep"])); } },
        { "dropIr",   [] (M& m, V v) { m.onDropIr (v); } },
        { "match",    [] (M& m, V)   { m.window.chooseRecordingToMatch(); } },
        { "displays", [] (M& m, V v) { m.window.displaysOpened (static_cast<bool> (v.getProperty ("open", false))); } },
        { "sceneStore",  [] (M& m, V v) { m.onScene ("store", v); } },
        { "sceneRename", [] (M& m, V v) { m.onScene ("rename", v); } },
        { "sceneDelete", [] (M& m, V v) { m.onScene ("delete", v); } },
        { "sceneRecall", [] (M& m, V v) { m.onScene ("recall", v); } },
        { "startCapture",  [] (M& m, V v) { m.processor.startCapture (v); m.window.captureChanged(); } },
        { "cancelCapture", [] (M& m, V)   { m.processor.cancelCapture(); m.window.captureChanged(); } },
        { "licenceOpen",       [] (M& m, V)   { m.licence.open(); } },
        { "licenceActivate",   [] (M& m, V v) { m.licence.activate (v["licence"].toString()); } },
        { "licenceCode",       [] (M& m, V v) { m.licence.useCode (v["code"].toString()); } },
        { "licenceDeactivate", [] (M& m, V)   { m.licence.deactivate(); } },
        { "licencePage",       [] (M& m, V)   { m.window.openInBrowser (juce::URL (m.licence.status()["page"].toString())); } },
        // Some hosts keep copy and paste shortcuts from reaching a plugin window, so the page
        // has its own buttons for them, served by the system clipboard.
        { "copyText",   [] (M& m, V v) { m.window.copyToClipboard (v["text"].toString()); } },
        { "copyReport", [] (M& m, V)   { m.window.copyToClipboard (m.processor.bugReportDetails()); } },
        { "pasteText",  [] (M& m, V v) { m.onPasteText (v); } },
    };

    return handlers;
}

juce::StringArray PageMessages::names()
{
    juce::StringArray out;

    for (const auto& [name, handler] : table())
        out.add (name);

    return out;
}

juce::StringArray PageMessages::interfaceKeys()
{
    return { "spaceId", "locks", "mode", "view", "speakers", "skin" };
}

bool PageMessages::handle (const juce::String& name, const juce::var& payload)
{
    for (const auto& [each, handler] : table())
    {
        if (name == each)
        {
            handler (*this, payload);
            return true;
        }
    }

    return false;
}

//==============================================================================
void PageMessages::onReady (const juce::var&)
{
    // A page that reloaded mid-drag can't end that drag.
    held.releaseAll();
    window.pageReady();
}

void PageMessages::onParams (const juce::var& payload)
{
    forEachProperty (payload, [this] (const juce::String& id, const juce::var& value)
    {
        auto* parameter = state.getParameter (id);

        if (parameter == nullptr || ! (value.isDouble() || value.isInt() || value.isInt64()))
            return;

        const auto inGesture = held.isHeld (id);

        if (! inGesture)
            parameter->beginChangeGesture();

        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (static_cast<double> (value))));

        if (! inGesture)
            parameter->endChangeGesture();

        // The page already shows it, so only host and preset changes travel back.
        watch.noteShown (id);
    });
}

void PageMessages::onGesture (const juce::var& payload)
{
    const auto begin = static_cast<bool> (payload.getProperty ("begin", false));
    const auto ids = payload.getProperty ("ids", juce::var());

    if (auto* array = ids.getArray())
    {
        for (const auto& idVar : *array)
        {
            if (begin)
                held.hold (idVar.toString());
            else
                held.release (idVar.toString());
        }
    }
}

void PageMessages::onUi (const juce::var& payload)
{
    auto ui = processor.getUiState();
    settingInterface = true;

    forEachProperty (payload, [this, &ui] (const juce::String& key, const juce::var& value)
    {
        if (key == "spaceId")    ui.setProperty (UiIDs::spaceId, value.toString(), nullptr);
        else if (key == "locks") ui.setProperty (UiIDs::locks, value.toString(), nullptr);
        else if (key == "mode")  ui.setProperty (UiIDs::mode, value.toString(), nullptr);
        else if (key == "view")  ui.setProperty (UiIDs::view, value.toString(), nullptr);
        else if (key == "speakers") ui.setProperty (UiIDs::speakers, value.toString().substring (0, 1024), nullptr);
        else if (key == "skin")
        {
            ui.setProperty (UiIDs::skin, value.toString(), nullptr);
            window.lookChosen (value.toString());
        }
    });

    settingInterface = false;
}

void PageMessages::onDropIr (const juce::var& payload)
{
    const auto stage = payload["stage"].toString();
    const auto id = static_cast<int> (payload["id"]);
    const auto name = dropped.fileName();
    auto ok = true;

    if (stage == "start")
        ok = dropped.start (id, payload["name"].toString(), static_cast<juce::int64> (payload["size"]), static_cast<bool> (payload["keep"]));
    else if (stage == "piece")
        ok = dropped.add (id, payload["data"].toString());
    else if (stage == "cancel")
        dropped.cancel (id);
    else if (stage == "end" && dropped.complete (id))
    {
        const auto keep = dropped.keepsSettings();
        processor.importImpulseResponseAsync (name, dropped.take(), keep, importFolder);
    }
    else if (stage == "end")
        ok = ! dropped.isCurrent (id); // a drop abandoned earlier has said so already

    if (! ok)
    {
        dropped.cancel (id);
        auto reply = std::make_unique<juce::DynamicObject>();
        reply->setProperty ("name", stage == "start" ? payload["name"].toString() : name);
        reply->setProperty ("large", stage == "start"); // no room for it: the file chooser reads only what it needs
        window.send (PageEvents::dropFailed, juce::var (reply.release()));
    }
}

void PageMessages::onScene (const juce::String& action, const juce::var& payload)
{
    const auto number = static_cast<int> (payload.getProperty ("number", 0));
    const auto name = payload.getProperty ("name", "").toString();

    if (action == "store")
    {
        if (const auto stored = processor.storeScene (number, name); stored > 0 && number == 0)
            processor.recallScene (stored); // a new scene is the one playing
    }
    else if (action == "rename")
    {
        processor.renameScene (number, name);
    }
    else if (action == "delete")
    {
        processor.deleteScene (number);
    }
    else if (action == "recall")
    {
        processor.recallScene (number);
    }

    window.interfaceChanged();
}

void PageMessages::onPasteText (const juce::var& payload)
{
    auto reply = std::make_unique<juce::DynamicObject>();
    reply->setProperty ("target", payload["target"]);
    reply->setProperty ("text", window.clipboardText().substring (0, 4096));
    window.send (PageEvents::pasted, juce::var (reply.release()));
}

} // namespace drenched

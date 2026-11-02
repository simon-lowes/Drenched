#pragma once

#include "PluginTestHelpers.h"

#include <PageMessages.h>

#include <utility>
#include <vector>

namespace testing
{
    /** Stands in for the plugin window behind PageMessages: records what the messages ask of
        it, and keeps a clipboard of its own so the computer's is never touched. */
    struct FakeWindow final : drenched::PageMessages::Window
    {
        void send (const juce::Identifier& event, const juce::var& payload) override { sent.emplace_back (event.toString(), payload); }
        void pageReady() override { ++ready; }
        void interfaceChanged() override { ++interface; }
        void captureChanged() override { ++capture; }
        void displaysOpened (bool open) override { displays.push_back (open); }
        void chooseImpulseResponse (bool keep) override { chosen.push_back (keep ? "keep" : "new"); }
        void chooseRecordingToMatch() override { chosen.push_back ("match"); }
        void lookChosen (const juce::String& skin) override { looks.add (skin); }
        void openInBrowser (const juce::URL& url) override { opened.add (url.toString (true)); }
        void copyToClipboard (const juce::String& text) override { clipboard = text; }
        juce::String clipboardText() override { return clipboard; }

        /** How many times anything at all was asked. */
        size_t calls() const
        {
            return sent.size() + static_cast<size_t> (ready + interface + capture) + displays.size() + chosen.size()
                 + static_cast<size_t> (looks.size() + opened.size()) + (clipboard.isEmpty() ? 0u : 1u);
        }

        std::vector<std::pair<juce::String, juce::var>> sent;
        int ready = 0, interface = 0, capture = 0;
        std::vector<bool> displays;
        std::vector<juce::String> chosen;
        juce::StringArray looks, opened;
        juce::String clipboard;
    };

    /** A message as the page sends it: through JSON, so numbers, true and null arrive as
        they do from the web view. */
    inline juce::var fromPage (const juce::String& json)
    {
        return juce::JSON::parse (json);
    }
}

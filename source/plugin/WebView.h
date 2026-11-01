#pragma once

#include <juce_core/juce_core.h>

namespace drenched::webView
{
    /** Whether this computer can show the interface. On Windows it runs in Microsoft's
        WebView2 runtime, which most Windows 10 and 11 PCs have but some don't (older or
        managed installs); without it JUCE would fall back to Internet Explorer, which can't
        show the page. Elsewhere the system web view is always there. */
    bool runtimeAvailable();

    /** Microsoft's download of the WebView2 runtime installer. */
    inline constexpr const char* downloadUrl = "https://go.microsoft.com/fwlink/p/?LinkId=2124703";

    /** What the plugin window does when something asks it to open a URL. */
    enum class Navigation
    {
        load,          // the interface's own page, served from the binary, or a blank page
        openFile,      // a file dropped past the page: handed to the plugin, never shown
        openInBrowser, // a web page: shown in the person's own browser, never in the window
        refuse
    };

    /** The window shows only the interface. JUCE gives every page in the window the native
        bridge (clipboard, files dropped as IRs, parameters, the licence), so a page from
        anywhere else must never open in it. pageRoot is where the interface is served from
        (juce::WebBrowserComponent::getResourceProviderRoot()). */
    Navigation navigationFor (const juce::String& url, const juce::String& pageRoot);
}

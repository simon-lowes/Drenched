#pragma once

namespace drenched::webView
{
    /** Whether this computer can show the interface. On Windows it runs in Microsoft's
        WebView2 runtime, which most Windows 10 and 11 PCs have but some don't (older or
        managed installs); without it JUCE would fall back to Internet Explorer, which can't
        show the page. Elsewhere the system web view is always there. */
    bool runtimeAvailable();

    /** Microsoft's download of the WebView2 runtime installer. */
    inline constexpr const char* downloadUrl = "https://go.microsoft.com/fwlink/p/?LinkId=2124703";
}

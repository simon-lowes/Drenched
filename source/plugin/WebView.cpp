#include "WebView.h"

#if defined (_WIN32)
 #include <windows.h>
 #include <WebView2.h>

 #if defined (_MSC_VER)
  #pragma comment (lib, "ole32.lib") // CoTaskMemFree
 #endif
#endif

namespace drenched::webView
{

bool runtimeAvailable()
{
   #if defined (_WIN32)
    // Microsoft's own check: a version string means an installed runtime the loader can use.
    LPWSTR version = nullptr;
    const auto found = SUCCEEDED (GetAvailableCoreWebView2BrowserVersionString (nullptr, &version)) && version != nullptr;

    if (version != nullptr)
        CoTaskMemFree (version);

    return found;
   #else
    return true;
   #endif
}

Navigation navigationFor (const juce::String& url, const juce::String& pageRoot)
{
    // The root ends in "/", so a host that merely begins with the root's host doesn't match.
    if (pageRoot.endsWithChar ('/') && url.startsWithIgnoreCase (pageRoot))
        return Navigation::load;

    if (url.equalsIgnoreCase ("about:blank"))
        return Navigation::load;

    if (url.startsWithIgnoreCase ("file:"))
        return Navigation::openFile;

    if (url.startsWithIgnoreCase ("https://"))
        return Navigation::openInBrowser;

    return Navigation::refuse;
}

} // namespace drenched::webView

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

} // namespace drenched::webView

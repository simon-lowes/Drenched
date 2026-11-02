#include <catch2/catch_test_macros.hpp>

#include <WebView.h>

using drenched::webView::Navigation;
using drenched::webView::navigationFor;

// JUCE serves the interface from juce://juce.backend/ on macOS and Linux and from
// https://juce.backend/ on Windows. The window must show nothing else: every page in it gets
// the native bridge.
TEST_CASE ("The plugin window shows only the interface's own page", "[webview]")
{
    for (const juce::String root : { "juce://juce.backend/", "https://juce.backend/" })
    {
        INFO (root);

        SECTION ("its own page and files load")
        {
            CHECK (navigationFor (root, root) == Navigation::load);
            CHECK (navigationFor (root + "?k=anodised#anodised", root) == Navigation::load);
            CHECK (navigationFor (root + "js/app.js", root) == Navigation::load);
            CHECK (navigationFor (root.toUpperCase(), root) == Navigation::load);
            CHECK (navigationFor ("about:blank", root) == Navigation::load);
        }

        SECTION ("a file dropped past the page goes to the plugin")
        {
            CHECK (navigationFor ("file:///Users/me/Hall.wav", root) == Navigation::openFile);
            CHECK (navigationFor ("FILE:///C:/IRs/Hall.wav", root) == Navigation::openFile);
        }

        SECTION ("a web page opens in the person's browser")
        {
            CHECK (navigationFor ("https://zenodo.org/records/4116247", root) == Navigation::openInBrowser);
        }

        SECTION ("a host that only starts like the interface's is someone else's")
        {
            const auto lookalike = root.upToLastOccurrenceOf ("/", false, false) + ".example.com/";
            CHECK (navigationFor (lookalike, root) != Navigation::load);
            CHECK (navigationFor (root.dropLastCharacters (1) + "@example.com/", root) != Navigation::load);
        }

        SECTION ("nothing else opens at all")
        {
            for (const juce::String url : { "http://example.com/", "javascript:alert(1)", "data:text/html,<p>x",
                                            "blob:https://juce.backend/1", "about:srcdoc", "ftp://example.com/",
                                            "juce://other.backend/", "" })
            {
                INFO (url);
                CHECK (navigationFor (url, root) == Navigation::refuse);
            }
        }
    }

    SECTION ("with no root known, only a blank page loads")
    {
        CHECK (navigationFor ("juce://juce.backend/", "") == Navigation::refuse);
        CHECK (navigationFor ("about:blank", "") == Navigation::load);
    }
}

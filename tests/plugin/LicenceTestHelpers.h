#pragma once

#include "PluginTestHelpers.h"
#include "LicenceSigning.h"

#include <LicenceManager.h>
#include <Settings.h>

#include <drenched/Licence.h>

namespace testing
{
    /** The empty folder HOME points at while the licence tests run (LicenceMain.cpp). */
    juce::File isolatedHome();

    /** Waits, off the message thread, until a condition checked on the message thread
        holds; false after ten seconds. */
    template <typename Fn>
    bool waitFor (Fn condition)
    {
        const auto end = juce::Time::getMillisecondCounter() + 10000;

        while (! onMessageThread (condition))
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;

            juce::Thread::sleep (2);
        }

        return true;
    }

    /** A licence number with the right check characters (server/test/vectors.json). */
    inline constexpr auto testLicence = "0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS";

    /** An activation code for a machine, signed with the test key as the server would. */
    inline juce::String codeFor (const std::string& machine, const std::string& name = "Jos\xc3\xa9 M\xc3\xbcller")
    {
        return signWithTestKey (drenched::licence::activationPayload ({ testLicence, machine, name, "2026-11-01" }));
    }

    /** Opens a licence file as another host on the same computer would. */
    inline std::unique_ptr<juce::PropertiesFile> openLicenceFile (const juce::File& file)
    {
        auto options = drenched::settings::options();
        options.filenameSuffix = "licence";
        return std::make_unique<juce::PropertiesFile> (file, options);
    }
}

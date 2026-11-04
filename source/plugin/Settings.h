#pragma once

#include "MovedFiles.h"

#include <juce_data_structures/juce_data_structures.h>

namespace drenched
{

/** Per-user settings that outlive a session: the default look (skin) a new instance opens
    with, and the folder impulse responses were last loaded from. Stored as a small XML file in the user's application data folder. */
namespace settings
{
    inline juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Drenched";
        o.filenameSuffix = "settings";
        o.folderName = "Drenched";
        o.osxLibrarySubFolder = "Application Support";
        o.commonToAllUsers = false;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }

    inline constexpr auto defaultSkin = "aperture";

    inline juce::String loadDefaultSkin()
    {
        juce::PropertiesFile file (options());
        return file.getValue ("skin", defaultSkin);
    }

    inline void saveDefaultSkin (const juce::String& skin)
    {
        juce::PropertiesFile file (options());
        file.setValue ("skin", skin);
        file.saveIfNeeded();
    }

    /** The folder impulse responses were last loaded from, so the file chooser opens there. */
    inline juce::File loadIrFolder()
    {
        juce::PropertiesFile file (options());
        const juce::File folder (file.getValue ("irFolder"));
        return folder.isDirectory() ? folder : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    inline void saveIrFolder (const juce::File& folder)
    {
        juce::PropertiesFile file (options());
        file.setValue ("irFolder", folder.getFullPathName());
        file.saveIfNeeded();
    }

    /** The network shares (\\host\share, lower case) this person has chosen impulse
        responses from on this computer. Only these are opened when a session or scene names
        a file on a share; see movedFiles::mayOpen. */
    inline juce::StringArray loadTrustedShares()
    {
        juce::PropertiesFile file (options());
        auto shares = juce::StringArray::fromLines (file.getValue ("trustedShares"));
        shares.removeEmptyStrings();
        return shares;
    }

    /** Remembers the share a file the person chose is on, if it is on one. */
    inline void trustShareOf (const juce::File& chosen)
    {
        const auto share = movedFiles::shareOf (chosen.getFullPathName());

        if (share.isEmpty())
            return;

        auto shares = loadTrustedShares();

        if (shares.contains (share))
            return;

        shares.insert (0, share);
        shares.removeRange (32, shares.size()); // the most recent few are plenty
        juce::PropertiesFile file (options());
        file.setValue ("trustedShares", shares.joinIntoString ("\n"));
        file.saveIfNeeded();
    }

    /** The folder recordings were last matched from (Match a room). */
    inline juce::File loadMatchFolder()
    {
        juce::PropertiesFile file (options());
        const juce::File folder (file.getValue ("matchFolder"));
        return folder.isDirectory() ? folder : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    inline void saveMatchFolder (const juce::File& folder)
    {
        juce::PropertiesFile file (options());
        file.setValue ("matchFolder", folder.getFullPathName());
        file.saveIfNeeded();
    }

    /** Where captured spaces are saved: Documents/Drenched/Captures. */
    inline juce::File capturesFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Drenched").getChildFile ("Captures");
    }

    /** Where copies of impulse responses dropped on the interface are kept, since the
        interface can't say where a dropped file came from. */
    inline juce::File importedFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Drenched").getChildFile ("Imported");
    }

    /** Where crash reports wait until they are sent or let go (crashReports): beside the
        settings file, in the user's application data. */
    inline juce::File crashFolder()
    {
        return options().getDefaultFile().getParentDirectory().getChildFile ("Crash reports");
    }

    /** Where the Windows WebView2 runtime may keep its cache for this plugin. */
    inline juce::File webViewDataFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Drenched").getChildFile ("WebView2");
    }
}

} // namespace drenched

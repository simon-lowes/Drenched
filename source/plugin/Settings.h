#pragma once

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

    /** Where captured spaces are saved: Documents/Drenched/Captures. */
    inline juce::File capturesFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Drenched").getChildFile ("Captures");
    }

    /** Where the Windows WebView2 runtime may keep its cache for this plugin. */
    inline juce::File webViewDataFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Drenched").getChildFile ("WebView2");
    }
}

} // namespace drenched

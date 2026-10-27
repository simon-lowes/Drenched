#pragma once

#include <juce_core/juce_core.h>

namespace drenched::movedFiles
{
    /** Finds a file a session names that isn't where the session says: a session made on
        another computer or user account, or a file moved since. In order, it tries the saved
        path; the same place under this account's home folder (whatever followed Users/<name>/
        or home/<name>/, with either kind of slash, so Mac, Windows and Linux sessions all
        work); then a file of the same name in each of the folders given. Returns {} when
        none exists. */
    inline juce::File find (const juce::String& savedPath, const juce::File& home, const juce::Array<juce::File>& folders)
    {
        if (juce::File::isAbsolutePath (savedPath) && juce::File (savedPath).existsAsFile())
            return juce::File (savedPath);

        auto parts = juce::StringArray::fromTokens (savedPath.replaceCharacter ('\\', '/'), "/", "");
        parts.removeEmptyStrings();

        if (parts.isEmpty() || parts.contains (".."))
            return {};

        for (int i = 0; i + 2 < parts.size(); ++i)
        {
            if (! parts[i].equalsIgnoreCase ("Users") && ! parts[i].equalsIgnoreCase ("home"))
                continue;

            auto file = home;

            for (int j = i + 2; j < parts.size(); ++j)
                file = file.getChildFile (parts[j]);

            if (file.existsAsFile())
                return file;

            break;
        }

        for (const auto& folder : folders)
            if (const auto file = folder.getChildFile (parts[parts.size() - 1]); file.existsAsFile())
                return file;

        return {};
    }
}

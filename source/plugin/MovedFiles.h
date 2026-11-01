#pragma once

#include <juce_core/juce_core.h>

namespace drenched::movedFiles
{
    /** A path that names another computer: a network share (\\host\share\..., //host/share/...,
        \\?\UNC\host\share\...) or a Windows device (\\.\...). On Windows, merely looking
        at one connects to that computer and signs in to it as this user, so a session or
        scene from someone else must not make Drenched do that. A local long path
        (\\?\C:\...) isn't one. */
    inline bool isNetworkPath (const juce::String& path)
    {
        const auto p = path.replaceCharacter ('/', '\\');

        if (! p.startsWith ("\\\\"))
            return false;

        const auto rest = p.substring (2);
        return ! (rest.startsWith ("?\\") && ! rest.startsWithIgnoreCase ("?\\UNC\\"));
    }

    /** The share a network path is on, as \\host\share in lower case; {} for any other path,
        and for a device path, which is never trusted. */
    inline juce::String shareOf (const juce::String& path)
    {
        if (! isNetworkPath (path))
            return {};

        auto rest = path.replaceCharacter ('/', '\\').substring (2);

        if (rest.startsWithIgnoreCase ("?\\UNC\\"))
            rest = rest.substring (6);
        else if (rest.startsWith (".\\") || rest.startsWith ("?\\"))
            return {};

        auto parts = juce::StringArray::fromTokens (rest, "\\", "");
        parts.removeEmptyStrings();

        if (parts.size() < 2)
            return {};

        return ("\\\\" + parts[0] + "\\" + parts[1]).toLowerCase();
    }

    /** Whether a path may be opened as it stands: any local path, or a path on one of the
        shares given (those this person has chosen files from on this computer). */
    inline bool mayOpen (const juce::String& path, const juce::StringArray& trustedShares)
    {
        if (! isNetworkPath (path))
            return true;

        const auto share = shareOf (path);
        return share.isNotEmpty() && trustedShares.contains (share);
    }

    /** Finds a file a session names that isn't where the session says: a session made on
        another computer or user account, or a file moved since. In order, it tries the saved
        path; the same place under this account's home folder (whatever followed Users/<name>/
        or home/<name>/, with either kind of slash, so Mac, Windows and Linux sessions all
        work); then a file of the same name in each of the folders given. Returns {} when
        none exists. A saved path on a network share is only tried if the share is one of
        trustedShares (see mayOpen). */
    inline juce::File find (const juce::String& savedPath, const juce::File& home, const juce::Array<juce::File>& folders,
                            const juce::StringArray& trustedShares = {})
    {
        if (juce::File::isAbsolutePath (savedPath) && mayOpen (savedPath, trustedShares) && juce::File (savedPath).existsAsFile())
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

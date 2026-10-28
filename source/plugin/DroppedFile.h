#pragma once

#include <juce_core/juce_core.h>

#include <new>

namespace drenched
{
    /** A file dropped on the interface, as the page sends it: a start naming it and giving its
        size, then its bytes in base64 pieces, then an end (see app.js). The web view can't
        tell the plugin where a dropped file is, only what is in it. Pieces of any other drop
        are ignored, so a drop that is replaced part way can't mix into the next. */
    class DroppedFile
    {
    public:
        /** The largest file the page sends; larger ones are loaded with the file chooser. */
        static constexpr juce::int64 largest = 512 * 1024 * 1024;

        /** Starts a drop. False, and nothing kept, for a size that can't be, or one there
            isn't the memory for (a 32-bit host has little room to spare). */
        bool start (int id, const juce::String& fileName, juce::int64 bytes, bool keepSettings)
        {
            *this = {};

            if (bytes < 0 || bytes > largest)
                return false;

            try
            {
                data.setSize (static_cast<size_t> (bytes), false);
            }
            catch (const std::bad_alloc&)
            {
                *this = {};
                return false;
            }

            current = id;
            name = fileName;
            keep = keepSettings;
            return true;
        }

        /** Adds the next piece. False, and the drop abandoned, when the piece isn't base64 or
            runs past the size the drop started with; true for a piece of another drop, which
            is ignored. */
        bool add (int id, const juce::String& base64)
        {
            if (! isCurrent (id))
                return true;

            juce::MemoryOutputStream piece;
            auto decoded = false;

            try
            {
                decoded = juce::Base64::convertFromBase64 (piece, base64);
            }
            catch (const std::bad_alloc&) {}

            if (! decoded || received + piece.getDataSize() > data.getSize())
            {
                *this = {};
                return false;
            }

            data.copyFrom (piece.getData(), static_cast<int> (received), piece.getDataSize());
            received += piece.getDataSize();
            return true;
        }

        bool isCurrent (int id) const noexcept  { return current >= 0 && id == current; }

        /** Whether the drop with this id has all of its bytes. */
        bool complete (int id) const noexcept  { return isCurrent (id) && received == data.getSize(); }

        void cancel (int id)                    { if (id == current) *this = {}; }

        const juce::String& fileName() const noexcept    { return name; }
        bool keepsSettings() const noexcept              { return keep; }

        /** The file's bytes, leaving nothing behind. */
        juce::MemoryBlock take()
        {
            auto bytes = std::move (data);
            *this = {};
            return bytes;
        }

    private:
        int current = -1;
        juce::String name;
        bool keep = false;
        juce::MemoryBlock data;
        size_t received = 0;
    };
}

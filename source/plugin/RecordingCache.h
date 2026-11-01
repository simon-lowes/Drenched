#pragma once

#include <drenched/RecordedResponse.h>

#include <juce_core/juce_core.h>

#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#include <list>
#include <memory>
#include <mutex>
#include <utility>

namespace drenched
{
    /** The recordings played most recently, read and prepared, so going back to one (a
        scene recalled again, a space chosen again in the browser) needn't read it again:
        that takes up to a quarter of a second for a long recording, which is heard when a
        scene changes rooms on the cut. Shared by every instance in the process, so two on
        the same space share it too. Holds up to a budget of memory; the longest unused go
        first. Thread-safe. */
    class RecordingCache
    {
    public:
        /** 192 MB in a 64-bit process; a 32-bit one has less room to spare. */
        static constexpr std::size_t defaultBudget = sizeof (void*) < 8 ? 48u << 20 : 192u << 20;

        explicit RecordingCache (std::size_t budgetBytes = defaultBudget) : budget (budgetBytes) {}

        static RecordingCache& shared()
        {
            static RecordingCache cache;
            return cache;
        }

        /** What a library space is kept under. */
        static juce::String keyOf (const juce::String& libraryId)  { return "library:" + libraryId; }

        /** What a file is kept under: changing the file (or replacing it with another of the
            same name) changes its key, so it is read again. */
        static juce::String keyOf (const juce::File& file)
        {
            return "file:" + file.getFullPathName() + "|" + juce::String (file.getSize())
                 + "|" + changeStampOf (file);
        }

        /** Something that changes whenever the file's content can have. JUCE's modification
            time is whole seconds on macOS and Linux, so a file exported again at the same length
            within the second (a DAW bouncing an IR over the last one) kept its key, and the old
            recording played. So there it is the file's identity (a replacement saved beside it
            and renamed over it is another file) with its modification and status-change times
            to the nanosecond; the status-change time can't be set back, so even a rewrite in
            place whose time was restored is seen. Windows times are already finer. */
        static juce::String changeStampOf (const juce::File& file)
        {
           #if JUCE_WINDOWS
            return juce::String (file.getLastModificationTime().toMilliseconds());
           #else
            struct stat info {};

            if (stat (file.getFullPathName().toRawUTF8(), &info) != 0)
                return "missing";

           #if JUCE_MAC || JUCE_IOS
            const auto& modified = info.st_mtimespec;
            const auto& changed = info.st_ctimespec;
           #else
            const auto& modified = info.st_mtim;
            const auto& changed = info.st_ctim;
           #endif

            const auto stamp = [] (const timespec& t)
            {
                return juce::String (static_cast<juce::int64> (t.tv_sec)) + "." + juce::String (static_cast<juce::int64> (t.tv_nsec));
            };

            return juce::String (static_cast<juce::int64> (info.st_dev)) + ":" + juce::String (static_cast<juce::int64> (info.st_ino))
                 + "|" + stamp (modified) + "|" + stamp (changed);
           #endif
        }

        std::shared_ptr<const RecordedResponse> find (const juce::String& key)
        {
            const std::lock_guard lock (mutex);

            for (auto it = entries.begin(); it != entries.end(); ++it)
            {
                if (it->first == key)
                {
                    entries.splice (entries.begin(), entries, it); // most recently used first
                    return entries.front().second;
                }
            }

            return nullptr;
        }

        void add (const juce::String& key, std::shared_ptr<const RecordedResponse> response)
        {
            if (response == nullptr || bytesOf (*response) > budget)
                return;

            const std::lock_guard lock (mutex);
            entries.remove_if ([&key] (const auto& entry) { return entry.first == key; });
            entries.emplace_front (key, std::move (response));

            auto total = std::size_t {};

            for (auto it = entries.begin(); it != entries.end();)
            {
                total += bytesOf (*it->second);
                it = total > budget ? entries.erase (it) : std::next (it);
            }
        }

        std::size_t size()
        {
            const std::lock_guard lock (mutex);
            return entries.size();
        }

        /** Forgets every recording, so the next choice of each is read again. For tests of
            the background reads, which the shared cache otherwise skips. */
        void clear()
        {
            const std::lock_guard lock (mutex);
            entries.clear();
        }

        static std::size_t bytesOf (const RecordedResponse& response) noexcept
        {
            auto samples = std::size_t {};

            for (const auto& path : response.responses.paths)
                samples += path.size();

            for (const auto& speaker : response.speakers)
                samples += speaker.size();

            return samples * sizeof (float);
        }

    private:
        const std::size_t budget;
        std::mutex mutex;
        std::list<std::pair<juce::String, std::shared_ptr<const RecordedResponse>>> entries;
    };
}

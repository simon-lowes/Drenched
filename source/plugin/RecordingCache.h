#pragma once

#include <drenched/RecordedResponse.h>

#include <juce_core/juce_core.h>

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
                 + "|" + juce::String (file.getLastModificationTime().toMilliseconds());
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

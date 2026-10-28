#include "PluginTestHelpers.h"

#include <RecordingCache.h>

#include <catch2/catch_test_macros.hpp>

using namespace drenched;

/*  Recordings played recently stay prepared, up to a budget, so going back to one is
    quick: a scene changing rooms on the cut, or a space chosen again in the browser. */

namespace
{
    std::shared_ptr<const RecordedResponse> recording (std::size_t samplesPerPath)
    {
        auto r = std::make_shared<RecordedResponse>();

        for (auto& path : r->responses.paths)
            path.assign (samplesPerPath, 0.0f);

        return r;
    }
}

TEST_CASE ("Recent recordings are kept within a budget, the longest unused going first", "[cache]")
{
    // Each recording below is 4 paths of 1000 floats: 16 kB.
    RecordingCache cache (40'000);
    const auto a = recording (1000), b = recording (1000), c = recording (1000);
    CHECK (RecordingCache::bytesOf (*a) == 16'000);

    cache.add ("a", a);
    cache.add ("b", b);
    CHECK (cache.find ("a") == a); // now the most recently used
    cache.add ("c", c);            // b goes

    CHECK (cache.find ("a") == a);
    CHECK (cache.find ("b") == nullptr);
    CHECK (cache.find ("c") == c);
    CHECK (cache.size() == 2);

    // Adding one again replaces it rather than keeping two.
    const auto newer = recording (1000);
    cache.add ("a", newer);
    CHECK (cache.find ("a") == newer);
    CHECK (cache.size() == 2);

    // One larger than the whole budget isn't kept, and doesn't push the others out.
    cache.add ("huge", recording (100'000));
    CHECK (cache.find ("huge") == nullptr);
    CHECK (cache.size() == 2);

    cache.add ("nothing", nullptr);
    CHECK (cache.size() == 2);
}

TEST_CASE ("A changed file isn't taken for the one kept", "[cache]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drenched-cache-key");
    dir.deleteRecursively();
    REQUIRE (dir.createDirectory());
    const auto file = dir.getChildFile ("hall.wav");

    REQUIRE (file.replaceWithText ("first"));
    const auto first = RecordingCache::keyOf (file);
    CHECK (RecordingCache::keyOf (file) == first);

    REQUIRE (file.replaceWithText ("a longer second"));
    CHECK (RecordingCache::keyOf (file) != first);

    REQUIRE (file.replaceWithText ("same length 1"));
    const auto third = RecordingCache::keyOf (file);
    REQUIRE (file.replaceWithText ("same length 2"));
    REQUIRE (file.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5)));
    CHECK (RecordingCache::keyOf (file) != third);

    CHECK (RecordingCache::keyOf (file) != RecordingCache::keyOf ("hall"));
    dir.deleteRecursively();
}

#pragma once

#include <PluginProcessor.h>
#include <TestHelpers.h>

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace testing
{
    template <std::size_t... place>
    std::tuple<std::integral_constant<std::size_t, place>...> parameterPlaces (std::index_sequence<place...>);

    /** One type per parameter, its place in ParamIDs::ordered as ::value, for
        TEMPLATE_LIST_TEST_CASE: a test case per parameter, which catch_discover_tests
        makes a CTest entry of its own (GENERATE would keep them all in one). */
    using EachParameter = decltype (parameterPlaces (std::make_index_sequence<drenched::ParamIDs::ordered.size()> {}));

    /** While one lives, JUCE's own assertions are expected: a test playing a host that
        misbehaves (automation outside 0 to 1, or not a number) trips JUCE's checks, which
        then clamp the value. Anywhere else an assertion fails the run in a Debug build
        (see Main.cpp). Any thread. */
    class ExpectJuceAssertions
    {
    public:
        ExpectJuceAssertions() noexcept { count().fetch_add (1); }
        ~ExpectJuceAssertions() { count().fetch_sub (1); }

        ExpectJuceAssertions (const ExpectJuceAssertions&) = delete;
        ExpectJuceAssertions& operator= (const ExpectJuceAssertions&) = delete;

        static bool active() noexcept { return count().load() > 0; }

    private:
        static std::atomic<int>& count() noexcept
        {
            static std::atomic<int> expecting { 0 };
            return expecting;
        }
    };

    /** Waits until the plugin's builder has built every room asked of it so far: whether
        it has. Gives up only after buildTimeout, a guard against a hang. A moved control
        reaches the builder from the timer, so run that first. Not the audio thread. */
    inline bool builderIdle (const drenched::DrenchedProcessor& p)
    {
        return p.waitForBuilder (static_cast<int> (buildTimeout.count()));
    }

    /** Runs fn on the message thread and waits for its result. The tests themselves run
        on the message thread (see Main.cpp), so this normally just calls it. */
    template <typename Fn>
    auto onMessageThread (Fn fn)
    {
        if (juce::MessageManager::existsAndIsCurrentThread())
            return fn();

        using Result = decltype (fn());
        juce::WaitableEvent done;
        std::optional<std::conditional_t<std::is_void_v<Result>, int, Result>> result;

        juce::MessageManager::callAsync ([&]
        {
            if constexpr (std::is_void_v<Result>)
            {
                fn();
                result = 0;
            }
            else
            {
                result = fn();
            }

            done.signal();
        });

        done.wait();

        if constexpr (! std::is_void_v<Result>)
            return std::move (*result);
    }

    /** A plugin instance made and destroyed on the message thread. */
    struct Plugin
    {
        Plugin()
            : processor (onMessageThread ([] { return dynamic_cast<drenched::DrenchedProcessor*> (createPluginFilter()); }))
        {
        }

        ~Plugin() { onMessageThread ([this] { delete processor; }); }

        drenched::DrenchedProcessor* operator->() const noexcept { return processor; }
        drenched::DrenchedProcessor& operator*() const noexcept { return *processor; }

        drenched::DrenchedProcessor* processor;
    };
}

#pragma once

#include <PluginProcessor.h>

#include <juce_events/juce_events.h>

#include <memory>
#include <optional>
#include <type_traits>

namespace testing
{
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

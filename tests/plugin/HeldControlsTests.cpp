#include "PluginTestHelpers.h"

#include <HeldControls.h>

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace drenched;

namespace
{
    /** What a host hears of change gestures, per parameter: taken minus let go. */
    struct Host final : juce::AudioProcessorListener
    {
        std::map<int, int> holding;
        int begins = 0, ends = 0;

        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { ++holding[index]; ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { --holding[index]; ++ends; }

        bool holdsNothing() const
        {
            for (const auto& [index, count] : holding)
                if (count != 0)
                    return false;

            return true;
        }
    };
}

TEST_CASE ("The host is told once when a control is taken and let go", "[plugin][gestures]")
{
    testing::Plugin p;
    Host host;
    p->addListener (&host);

    {
        HeldControls held (p->getState());

        held.hold (ParamIDs::mix);
        held.hold (ParamIDs::mix);
        CHECK (held.isHeld (ParamIDs::mix));
        CHECK (host.begins == 1);

        held.release (ParamIDs::mix);
        held.release (ParamIDs::mix);
        CHECK_FALSE (held.isHeld (ParamIDs::mix));
        CHECK (host.ends == 1);

        // Ids the plugin doesn't have, or controls never taken, tell the host nothing.
        held.hold ("noSuchControl");
        held.release (ParamIDs::size);
        CHECK (host.begins == 1);
        CHECK (host.ends == 1);
        CHECK (host.holdsNothing());

        // A page that reloads mid-drag lets go of everything it held.
        held.hold (ParamIDs::decay[0]);
        held.hold (ParamIDs::decay[1]);
        held.releaseAll();
        CHECK (host.holdsNothing());
        CHECK_FALSE (held.isHeld (ParamIDs::decay[0]));

        // And so does a window closed mid-drag.
        held.hold (ParamIDs::listenerX);
        held.hold (ParamIDs::distance);
        CHECK_FALSE (host.holdsNothing());
    }

    CHECK (host.holdsNothing());
    CHECK (host.begins == host.ends);
    p->removeListener (&host);
}

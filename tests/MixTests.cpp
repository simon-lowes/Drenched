#include <drenched/Mix.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("Equal-power mix keeps total power constant", "[mix]")
{
    for (float amount = 0.0f; amount <= 1.0f; amount += 0.05f)
    {
        const auto g = drenched::equalPowerMix (amount);
        REQUIRE_THAT (g.dry * g.dry + g.wet * g.wet, WithinAbs (1.0, 1.0e-5));
    }
}

TEST_CASE ("Equal-power mix endpoints and clamping", "[mix]")
{
    REQUIRE_THAT (drenched::equalPowerMix (0.0f).dry, WithinAbs (1.0, 1.0e-6));
    REQUIRE_THAT (drenched::equalPowerMix (0.0f).wet, WithinAbs (0.0, 1.0e-6));
    REQUIRE_THAT (drenched::equalPowerMix (1.0f).wet, WithinAbs (1.0, 1.0e-6));
    REQUIRE_THAT (drenched::equalPowerMix (-3.0f).dry, WithinAbs (1.0, 1.0e-6));
    REQUIRE_THAT (drenched::equalPowerMix (7.0f).wet, WithinAbs (1.0, 1.0e-6));
}

TEST_CASE ("Decibels convert to linear gain", "[mix]")
{
    REQUIRE_THAT (drenched::decibelsToGain (0.0f), WithinAbs (1.0, 1.0e-6));
    REQUIRE_THAT (drenched::decibelsToGain (-6.0206f), WithinAbs (0.5, 1.0e-4));
    REQUIRE (drenched::decibelsToGain (-120.0f) == 0.0f);
}

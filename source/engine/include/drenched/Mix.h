#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace drenched
{

/** Gains for an equal-power crossfade between a dry and a wet signal. */
struct MixGains
{
    float dry = 1.0f;
    float wet = 0.0f;
};

/** Returns equal-power gains for a mix amount in [0, 1], where 0 is fully dry
    and 1 is fully wet. Out-of-range values are clamped. Total power stays
    constant across the sweep, so the perceived level doesn't dip at 50%. */
inline MixGains equalPowerMix (float amount) noexcept
{
    const auto clamped = std::clamp (amount, 0.0f, 1.0f);
    const auto angle = clamped * std::numbers::pi_v<float> * 0.5f;
    return { std::cos (angle), std::sin (angle) };
}

/** Converts decibels to linear gain. Anything at or below -120 dB is silence. */
inline float decibelsToGain (float decibels) noexcept
{
    return decibels <= -120.0f ? 0.0f : std::pow (10.0f, decibels / 20.0f);
}

} // namespace drenched

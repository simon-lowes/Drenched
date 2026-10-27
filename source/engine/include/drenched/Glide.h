#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace drenched
{

/** Filter settings that glide to where a control sends them, a step every
    `block` samples, so a control that jumps never clicks. Each glide lands
    exactly on its goal, so settings left alone sound exactly as they always did.
    Real-time safe. */
namespace glide
{
    /** Samples between steps; filters are redesigned at most this often. */
    inline constexpr std::size_t block = 32;

    /** The step for gliding most of the way in about `seconds`. */
    inline float coefficient (double sampleRate, double seconds) noexcept
    {
        return static_cast<float> (std::exp (-static_cast<double> (block) / (seconds * sampleRate)));
    }

    /** One step towards `goal`, for gains in dB. */
    inline float towards (float now, float goal, float coef) noexcept
    {
        const auto next = goal + coef * (now - goal);
        return std::abs (next - goal) < 1.0e-3f ? goal : next;
    }

    /** One step towards `goal` by ratio, for frequencies and Qs (both above zero). */
    inline float towardsRatio (float now, float goal, float coef) noexcept
    {
        const auto next = goal * std::pow (now / goal, coef);
        return std::abs (next / goal - 1.0f) < 1.0e-4f ? goal : next;
    }
}

} // namespace drenched

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace drenched
{

/** One loudspeaker of a channel layout, by direction from the listener:
    azimuth in degrees (negative to the left, 0 straight ahead, 180 behind)
    and elevation in degrees (0 at ear height, 90 overhead). The low-frequency
    effects channel has no direction. */
struct Speaker
{
    float azimuth = 0.0f;
    float elevation = 0.0f;
    bool lfe = false;

    bool operator== (const Speaker&) const = default;

    bool isLeft() const noexcept { return ! lfe && azimuth < -0.5f && azimuth > -179.5f; }
    bool isRight() const noexcept { return ! lfe && azimuth > 0.5f && azimuth < 179.5f; }
    bool isCentred() const noexcept { return ! lfe && ! isLeft() && ! isRight(); }

    /** Within the front stage at ear height, where the stereo image lives. */
    bool isFront() const noexcept { return ! lfe && std::abs (azimuth) <= 45.0f && std::abs (elevation) < 10.0f; }
};

/** The speakers of a channel layout, in channel order. */
using SpeakerLayout = std::vector<Speaker>;

/** Up to 9.1.6. */
inline constexpr std::size_t maxChannels = 16;

namespace layouts
{
    inline SpeakerLayout mono() { return { Speaker { 0.0f, 0.0f } }; }
    inline SpeakerLayout stereo() { return { Speaker { -30.0f, 0.0f }, Speaker { 30.0f, 0.0f } }; }

    /** 5.1 in the usual film order: L R C LFE Ls Rs. */
    inline SpeakerLayout fivePointOne()
    {
        return { { -30.0f, 0.0f }, { 30.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, true }, { -110.0f, 0.0f }, { 110.0f, 0.0f } };
    }

    /** 7.1.4: L R C LFE Lss Rss Lsr Rsr Ltf Rtf Ltr Rtr. */
    inline SpeakerLayout sevenPointOnePointFour()
    {
        return { { -30.0f, 0.0f }, { 30.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, true },
                 { -90.0f, 0.0f }, { 90.0f, 0.0f }, { -150.0f, 0.0f }, { 150.0f, 0.0f },
                 { -45.0f, 45.0f }, { 45.0f, 45.0f }, { -135.0f, 45.0f }, { 135.0f, 45.0f } };
    }

    inline std::size_t countDirectional (const SpeakerLayout& layout) noexcept
    {
        std::size_t n = 0;

        for (const auto& s : layout)
            n += s.lfe ? 0 : 1;

        return n;
    }
}

} // namespace drenched

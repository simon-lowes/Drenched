#include "drenched/Ambisonics.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace drenched
{

std::vector<std::vector<float>> toAmbiX (const std::vector<std::vector<float>>& channels, AmbisonicFormat format)
{
    if (channels.size() < 4)
        throw std::invalid_argument ("A B-format impulse response needs four channels: W, X, Y and Z.");

    std::vector<std::vector<float>> out (channels.begin(), channels.begin() + 4);

    if (format == AmbisonicFormat::fuMa)
    {
        // W X Y Z, W 3 dB down -> W Y Z X at full level.
        out = { channels[0], channels[2], channels[3], channels[1] };

        for (auto& w : out[0])
            w *= std::numbers::sqrt2_v<float>;
    }
    else if (format == AmbisonicFormat::ambiXN3D)
    {
        const auto toSn3d = 1.0f / std::sqrt (3.0f);

        for (std::size_t c = 1; c < 4; ++c)
            for (auto& v : out[c])
                v *= toSn3d;
    }

    return out;
}

std::vector<float> virtualMicrophone (const std::vector<std::vector<float>>& ambiX, const Speaker& direction, float pattern)
{
    constexpr auto radians = static_cast<float> (std::numbers::pi / 180.0);

    // Ambisonic azimuth runs anticlockwise, so to the left is positive.
    const auto azimuth = -direction.azimuth * radians;
    const auto elevation = direction.elevation * radians;
    const auto w = 1.0f - pattern;
    const auto y = pattern * std::sin (azimuth) * std::cos (elevation);
    const auto z = pattern * std::sin (elevation);
    const auto x = pattern * std::cos (azimuth) * std::cos (elevation);

    std::size_t length = ambiX[0].size();

    for (std::size_t c = 1; c < 4; ++c)
        length = std::min (length, ambiX[c].size());

    std::vector<float> out (length);

    for (std::size_t n = 0; n < length; ++n)
        out[n] = w * ambiX[0][n] + y * ambiX[1][n] + z * ambiX[2][n] + x * ambiX[3][n];

    return out;
}

std::vector<std::vector<float>> decodeAmbisonic (const std::vector<std::vector<float>>& ambiX, const SpeakerLayout& outputs)
{
    std::vector<std::vector<float>> out;
    out.reserve (outputs.size());

    for (const auto& speaker : outputs)
    {
        if (speaker.lfe)
            out.emplace_back (ambiX[0].size(), 0.0f);
        else
            out.push_back (virtualMicrophone (ambiX, speaker, ambisonicPattern));
    }

    return out;
}

} // namespace drenched

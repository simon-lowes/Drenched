#include "drenched/SurroundResponse.h"
#include "drenched/PartitionedConvolver.h"
#include "drenched/RecordedResponse.h"
#include "drenched/Spaces.h"

#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <random>
#include <stdexcept>

namespace drenched
{

namespace
{
    constexpr double decorrelatorSeconds = 0.02;
    constexpr float sideEarlyGain = 0.5f;    // -6 dB
    constexpr float heightEarlyGain = 0.35f; // -9 dB

    bool atStereoPosition (const Speaker& s, float azimuth) noexcept
    {
        return ! s.lfe && std::abs (s.azimuth - azimuth) < 0.5f && std::abs (s.elevation) < 0.5f;
    }

    /** An all-pass FIR with random phase: flat in magnitude, so it keeps the
        reverberation's colour, but uncorrelated with any other seed. */
    std::vector<float> decorrelator (double sampleRate, unsigned seed)
    {
        std::size_t n = 64;

        while (static_cast<double> (n) < decorrelatorSeconds * sampleRate)
            n *= 2;

        std::unique_ptr<PFFFT_Setup, void (*) (PFFFT_Setup*)> setup (pffft_new_setup (static_cast<int> (n), PFFFT_REAL), pffft_destroy_setup);
        std::unique_ptr<float, void (*) (void*)> spectrum (static_cast<float*> (pffft_aligned_malloc (n * sizeof (float))), pffft_aligned_free);
        std::unique_ptr<float, void (*) (void*)> work (static_cast<float*> (pffft_aligned_malloc (n * sizeof (float))), pffft_aligned_free);

        if (setup == nullptr || spectrum == nullptr || work == nullptr)
            throw std::bad_alloc();

        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> phase (0.0f, 2.0f * std::numbers::pi_v<float>);
        auto* x = spectrum.get();

        x[0] = 1.0f;                                  // DC
        x[1] = (rng() & 1u) != 0 ? 1.0f : -1.0f;      // Nyquist

        for (std::size_t k = 1; k < n / 2; ++k)
        {
            const auto p = phase (rng);
            x[2 * k] = std::cos (p);
            x[2 * k + 1] = std::sin (p);
        }

        pffft_transform_ordered (setup.get(), x, x, work.get(), PFFFT_BACKWARD);

        std::vector<float> h (x, x + n);

        for (auto& v : h)
            v /= static_cast<float> (n);

        return h;
    }

    std::vector<float> convolve (const std::vector<float>& x, const std::vector<float>& h, std::size_t outLength)
    {
        PartitionedConvolver convolver ({ std::span<const float> (h) });
        std::vector<float> in (x);
        in.resize (outLength, 0.0f);
        std::vector<float> out (outLength);
        float* outputs[] = { out.data() };
        convolver.process (in.data(), outputs, outLength);
        return out;
    }

    double energyOf (const std::vector<float>& x) noexcept
    {
        double e = 0.0;

        for (auto v : x)
            e += static_cast<double> (v) * v;

        return e;
    }
}

//==============================================================================
SpatialResponseSet spreadResponses (const ImpulseResponseSet& stereo, const SpeakerLayout& outputs, double mixingTime)
{
    SpatialResponseSet out;
    out.sampleRate = stereo.sampleRate;

    const auto length = stereo.length();
    const auto sr = stereo.sampleRate;

    const std::array<const std::vector<float>*, 2> toLeft { &stereo.paths[ImpulseResponseSet::leftToLeft], &stereo.paths[ImpulseResponseSet::rightToLeft] };
    const std::array<const std::vector<float>*, 2> toRight { &stereo.paths[ImpulseResponseSet::leftToRight], &stereo.paths[ImpulseResponseSet::rightToRight] };

    // Early reflections end and the diffuse tail takes over across one
    // mixing time, with a raised-cosine crossfade.
    const auto tm = std::clamp (mixingTime, 0.005, 0.25) * sr;
    std::vector<float> early (length);

    for (std::size_t n = 0; n < length; ++n)
    {
        const auto t = (static_cast<double> (n) - tm) / tm;
        early[n] = static_cast<float> (t <= 0.0 ? 1.0 : t >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos (std::numbers::pi * t));
    }

    bool decorrelated = false;
    const auto padded = length + static_cast<std::size_t> (std::ceil (decorrelatorSeconds * sr * 2.0));

    for (std::size_t k = 0; k < outputs.size(); ++k)
    {
        const auto& speaker = outputs[k];

        for (std::size_t s = 0; s < 2; ++s)
        {
            if (speaker.lfe)
            {
                out.paths[s].emplace_back (length, 0.0f);
                continue;
            }

            if (atStereoPosition (speaker, -30.0f) || atStereoPosition (speaker, 30.0f))
            {
                out.paths[s].push_back (speaker.azimuth < 0.0f ? *toLeft[s] : *toRight[s]);
                continue;
            }

            // The stereo pair blended towards the speaker's direction.
            float gl, gr;

            if (speaker.isFront())
            {
                const auto p = std::clamp (speaker.azimuth / 30.0f, -1.0f, 1.0f);
                const auto angle = (p + 1.0f) * std::numbers::pi_v<float> / 4.0f;
                gl = std::cos (angle);
                gr = std::sin (angle);
            }
            else
            {
                gl = speaker.isLeft() ? 1.0f : speaker.isRight() ? 0.0f : std::numbers::sqrt2_v<float> / 2.0f;
                gr = speaker.isRight() ? 1.0f : speaker.isLeft() ? 0.0f : std::numbers::sqrt2_v<float> / 2.0f;
            }

            std::vector<float> blend (length);

            for (std::size_t n = 0; n < length; ++n)
                blend[n] = gl * (*toLeft[s])[n] + gr * (*toRight[s])[n];

            // One decorrelator per speaker, shared by both sources, so the
            // two inputs keep their relationship at every speaker.
            const auto h = decorrelator (sr, 7919u * static_cast<unsigned> (k + 1));
            std::vector<float> path;

            if (speaker.isFront())
            {
                std::vector<float> late (length);

                for (std::size_t n = 0; n < length; ++n)
                    late[n] = blend[n] * (1.0f - early[n]);

                path = convolve (late, h, padded);

                for (std::size_t n = 0; n < length; ++n)
                    path[n] += blend[n] * early[n];
            }
            else
            {
                const auto earlyGain = speaker.elevation >= 10.0f ? heightEarlyGain : sideEarlyGain;

                for (std::size_t n = 0; n < length; ++n)
                    blend[n] *= 1.0f - (1.0f - earlyGain) * early[n];

                path = convolve (blend, h, padded);
            }

            decorrelated = true;
            out.paths[s].push_back (std::move (path));
        }
    }

    const auto finalLength = decorrelated ? padded : length;
    double total = 0.0;

    for (auto& source : out.paths)
    {
        for (auto& p : source)
        {
            p.resize (finalLength, 0.0f);
            total += energyOf (p);
        }
    }

    // Same overall level as the stereo response.
    double reference = 0.0;

    for (const auto& p : stereo.paths)
        reference += energyOf (p);

    if (total > 0.0 && reference > 0.0 && std::abs (total / reference - 1.0) > 1.0e-9)
    {
        const auto g = static_cast<float> (std::sqrt (reference / total));

        for (auto& source : out.paths)
            for (auto& p : source)
                for (auto& v : p)
                    v *= g;
    }

    return out;
}

double mixingTimeSeconds (const ImpulseResponseRequest& request) noexcept
{
    if (request.recorded != nullptr)
        return request.recorded->room.mixingTimeSeconds * sizeScale (request);

    const auto& space = bundledSpaces[static_cast<std::size_t> (std::clamp (request.space, 0, static_cast<int> (bundledSpaces.size()) - 1))];
    const auto scale = std::clamp (request.sizePercent, 10.0f, 400.0f) / 100.0;
    const auto volume = static_cast<double> (space.width) * space.depth * space.height * scale * scale * scale;

    // About the square root of the volume in milliseconds (Polack).
    return std::clamp (std::sqrt (volume) / 1000.0, 0.01, 0.2);
}

} // namespace drenched

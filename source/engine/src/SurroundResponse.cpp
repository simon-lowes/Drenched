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

SpatialResponseSet placeSpeakerResponses (const std::vector<std::vector<float>>& channels, const SpeakerLayout& recorded,
                                          const ImpulseResponseSet& stereo, const SpeakerLayout& outputs,
                                          double mixingTime, float crossFeedPercent)
{
    constexpr double nearDegrees = 20.0;
    const auto count = std::min (channels.size(), recorded.size());

    const auto directionOf = [] (const Speaker& s)
    {
        constexpr auto radians = std::numbers::pi / 180.0;
        const auto az = s.azimuth * radians, el = s.elevation * radians;
        return std::array<double, 3> { std::sin (az) * std::cos (el), std::cos (az) * std::cos (el), std::sin (el) };
    };

    const auto degreesBetween = [&] (const Speaker& a, const Speaker& b)
    {
        const auto u = directionOf (a), v = directionOf (b);
        const auto dot = std::clamp (u[0] * v[0] + u[1] * v[1] + u[2] * v[2], -1.0, 1.0);
        return std::acos (dot) * 180.0 / std::numbers::pi;
    };

    // Which captured channels each output speaker plays.
    std::vector<std::vector<std::size_t>> plays (outputs.size());
    std::vector<bool> used (count, false);

    for (std::size_t k = 0; k < outputs.size(); ++k)
    {
        if (outputs[k].lfe)
            continue;

        auto best = count;

        for (std::size_t c = 0; c < count; ++c)
            if (! recorded[c].lfe && degreesBetween (outputs[k], recorded[c]) <= nearDegrees
                && (best == count || degreesBetween (outputs[k], recorded[c]) < degreesBetween (outputs[k], recorded[best])))
                best = c;

        if (best < count)
        {
            plays[k].push_back (best);
            used[best] = true;
        }
    }

    for (std::size_t c = 0; c < count; ++c)
    {
        if (used[c] || recorded[c].lfe)
            continue;

        auto nearest = outputs.size();

        for (std::size_t k = 0; k < outputs.size(); ++k)
            if (! outputs[k].lfe && (nearest == outputs.size() || degreesBetween (outputs[k], recorded[c]) < degreesBetween (outputs[nearest], recorded[c])))
                nearest = k;

        if (nearest < outputs.size())
            plays[nearest].push_back (c);
    }

    bool missing = false;

    for (std::size_t k = 0; k < outputs.size(); ++k)
        missing = missing || (! outputs[k].lfe && plays[k].empty());

    SpatialResponseSet spread;

    if (missing)
        spread = spreadResponses (stereo, outputs, mixingTime);

    SpatialResponseSet out;
    out.sampleRate = stereo.sampleRate;
    auto length = stereo.length();

    for (const auto& c : channels)
        length = std::max (length, c.size());

    if (missing)
        length = std::max (length, spread.length());

    for (std::size_t k = 0; k < outputs.size(); ++k)
    {
        std::vector<float> path (length, 0.0f);

        if (! outputs[k].lfe && plays[k].empty())
        {
            // Both sources share the speaker's fill-in: the recording is one source.
            const auto& a = spread.paths[0][k];
            const auto& b = spread.paths[1][k];

            for (std::size_t n = 0; n < std::min (a.size(), b.size()); ++n)
                path[n] = std::numbers::sqrt2_v<float> / 2.0f * (a[n] + b[n]);
        }

        for (const auto c : plays[k])
            for (std::size_t n = 0; n < channels[c].size(); ++n)
                path[n] += channels[c][n];

        out.paths[0].push_back (path);
        out.paths[1].push_back (std::move (path));
    }

    // Cross-feed: each input quieter on the other side, louder on its own, so
    // its total stays the same.
    const auto feed = std::clamp (crossFeedPercent, 0.0f, 100.0f) / 100.0f;

    if (feed < 1.0f)
    {
        for (std::size_t s = 0; s < 2; ++s)
        {
            double before = 0.0, after = 0.0;

            for (std::size_t k = 0; k < outputs.size(); ++k)
            {
                const auto e = energyOf (out.paths[s][k]);
                const auto across = s == 0 ? outputs[k].isRight() : outputs[k].isLeft();
                before += e;
                after += across ? e * feed * feed : e;
            }

            const auto makeUp = after > 0.0 ? static_cast<float> (std::sqrt (before / after)) : 1.0f;

            for (std::size_t k = 0; k < outputs.size(); ++k)
            {
                const auto across = s == 0 ? outputs[k].isRight() : outputs[k].isLeft();
                const auto g = makeUp * (across ? feed : 1.0f);

                for (auto& v : out.paths[s][k])
                    v *= g;
            }
        }
    }

    // Same overall level as the stereo response.
    double total = 0.0, reference = 0.0;

    for (const auto& source : out.paths)
        for (const auto& p : source)
            total += energyOf (p);

    for (const auto& p : stereo.paths)
        reference += energyOf (p);

    if (total > 0.0 && reference > 0.0)
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

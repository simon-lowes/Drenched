#include "drenched/ImpulseResponseSynth.h"
#include "drenched/Spaces.h"
#include "Bands.h"

#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>

namespace drenched
{

namespace
{

using bands::bandOf;
using bands::energyOf;
using bands::raisedCosine;

constexpr double speedOfSound = 343.0;   // m/s at 20 °C
constexpr double maxLengthSeconds = 25.0;
constexpr double leadInSeconds = 0.02;   // room for zero-phase filter pre-ringing, trimmed off
constexpr double directMarginSeconds = 0.001;
constexpr double tailRangeDb = 70.0;     // the tail runs until its slowest band is 70 dB down
constexpr double endFadeSeconds = 0.05;
constexpr double defaultEarlyLateAngle = 0.55 * std::numbers::pi / 2.0;

struct Vec3 { double x, y, z; };

double distance (const Vec3& a, const Vec3& b) noexcept
{
    return std::sqrt ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

/** Small, fast, well-mixed generator (SplitMix64) so every platform gets the same tail. */
class Noise
{
public:
    explicit Noise (std::uint64_t seed) : state (seed) {}

    /** Uniform in [-1, 1). */
    float next() noexcept
    {
        state += 0x9E3779B97F4A7C15ull;
        auto z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float> (static_cast<double> (z >> 11) * (2.0 / 9007199254740992.0) - 1.0);
    }

private:
    std::uint64_t state;
};

void addInterpolated (std::vector<float>& buffer, double position, double gain) noexcept
{
    if (position < 0.0)
        return;

    const auto index = static_cast<std::size_t> (position);

    if (index + 1 >= buffer.size())
        return;

    const auto frac = position - static_cast<double> (index);
    buffer[index] += static_cast<float> (gain * (1.0 - frac));
    buffer[index + 1] += static_cast<float> (gain * frac);
}


/** RT60 at any frequency: straight lines between the octave-band values on a
    log-frequency axis, held flat beyond the outer bands. */
double rtAt (const std::array<double, 8>& rt, double frequency) noexcept
{
    if (frequency <= decayBandCentres.front())
        return rt.front();

    if (frequency >= decayBandCentres.back())
        return rt.back();

    const auto position = std::log2 (frequency / decayBandCentres.front()); // octaves above 63 Hz
    const auto band = std::min (static_cast<std::size_t> (position), std::size_t (6));
    const auto frac = std::clamp (position - static_cast<double> (band), 0.0, 1.0);
    return rt[band] + frac * (rt[band + 1] - rt[band]);
}

/** Shapes a noise signal so every frequency decays to -60 dB at its own RT60,
    by short-time Fourier analysis and resynthesis (square-root Hann windows,
    75% overlap). Working bin by bin rather than band by band means the decay
    follows the curve exactly: a fast-decaying band is not masked by the
    slower skirts of its neighbours. Time zero of the decay is sample 'start'. */
std::vector<float> shapeDecay (const std::vector<float>& noise, std::size_t start, const std::array<double, 8>& rt, double sampleRate)
{
    std::size_t frame = 256;

    while (static_cast<double> (frame) < 0.021 * sampleRate)
        frame *= 2;

    const auto hop = frame / 4;
    const auto bins = frame / 2;

    std::unique_ptr<PFFFT_Setup, void (*) (PFFFT_Setup*)> setup (pffft_new_setup (static_cast<int> (frame), PFFFT_REAL), pffft_destroy_setup);
    auto* buffer = static_cast<float*> (pffft_aligned_malloc (frame * sizeof (float)));
    auto* spectrum = static_cast<float*> (pffft_aligned_malloc (frame * sizeof (float)));
    auto* work = static_cast<float*> (pffft_aligned_malloc (frame * sizeof (float)));

    std::vector<float> window (frame);

    for (std::size_t n = 0; n < frame; ++n)
        window[n] = static_cast<float> (std::sin (std::numbers::pi * (static_cast<double> (n) + 0.5) / static_cast<double> (frame)));

    // Per-bin decay rate in dB per second.
    std::vector<double> dbPerSecond (bins + 1);

    for (std::size_t k = 0; k <= bins; ++k)
        dbPerSecond[k] = 60.0 / rtAt (rt, static_cast<double> (k) * sampleRate / static_cast<double> (frame));

    std::vector<float> out (noise.size() + frame, 0.0f);
    const auto norm = 1.0f / (static_cast<float> (frame) * 2.0f); // inverse FFT scale and Hann overlap sum

    for (std::size_t pos = 0; pos < noise.size(); pos += hop)
    {
        for (std::size_t n = 0; n < frame; ++n)
            buffer[n] = pos + n < noise.size() ? noise[pos + n] * window[n] : 0.0f;

        pffft_transform_ordered (setup.get(), buffer, spectrum, work, PFFFT_FORWARD);

        const auto centre = static_cast<double> (pos + frame / 2) - static_cast<double> (start);
        const auto seconds = std::max (0.0, centre / sampleRate);
        const auto gainAt = [&] (std::size_t k) { return static_cast<float> (std::pow (10.0, -dbPerSecond[k] * seconds / 20.0)); };

        spectrum[0] *= gainAt (0);
        spectrum[1] *= gainAt (bins);

        for (std::size_t k = 1; k < bins; ++k)
        {
            const auto g = gainAt (k);
            spectrum[2 * k] *= g;
            spectrum[2 * k + 1] *= g;
        }

        pffft_transform_ordered (setup.get(), spectrum, buffer, work, PFFFT_BACKWARD);

        for (std::size_t n = 0; n < frame; ++n)
            out[pos + n] += buffer[n] * window[n] * norm;
    }

    pffft_aligned_free (buffer);
    pffft_aligned_free (spectrum);
    pffft_aligned_free (work);

    out.resize (noise.size());
    return out;
}

} // namespace

double sizeScale (const ImpulseResponseRequest& request) noexcept
{
    return std::clamp (static_cast<double> (request.sizePercent), 50.0, 150.0) / 100.0;
}

std::array<double, 8> decayFor (const ImpulseResponseRequest& request, double right) noexcept
{
    const auto& r = request.rightDecaySeconds;
    const auto split = std::any_of (r.begin(), r.end(), [] (float seconds) { return seconds > 0.0f; });
    const auto w = split ? std::clamp (right, 0.0, 1.0) : 0.0;

    std::array<double, 8> rt {};

    for (std::size_t b = 0; b < 8; ++b)
    {
        const auto onLeft = std::clamp (static_cast<double> (request.decaySeconds[b]), 0.05, 20.0);
        const auto onRight = std::clamp (static_cast<double> (r[b]), 0.05, 20.0);

        // A decay's rate (dB per second) goes as 1 / RT60, so a centred output
        // hearing both sides decays at the mean of their rates.
        rt[b] = w <= 0.0 ? onLeft : w >= 1.0 ? onRight : 1.0 / ((1.0 - w) / onLeft + w / onRight);
    }

    return rt;
}

float listenerDistanceMetres (int space, float sizePercent, float distancePercent) noexcept
{
    const auto& room = bundledSpaces[static_cast<std::size_t> (std::clamp (space, 0, static_cast<int> (bundledSpaces.size()) - 1))];
    const auto depth = room.depth * sizePercent / 100.0f;
    const auto nearest = std::min (1.5f, depth * 0.15f);
    return nearest + std::clamp (distancePercent, 0.0f, 100.0f) / 100.0f * (depth * 0.85f - nearest);
}

ImpulseResponseSet synthesiseImpulseResponse (const ImpulseResponseRequest& request)
{
    const auto sr = request.sampleRate;
    const auto spaceIndex = std::clamp (request.space, 0, static_cast<int> (bundledSpaces.size()) - 1);
    const auto& room = bundledSpaces[static_cast<std::size_t> (spaceIndex)];
    const auto scale = sizeScale (request);

    const auto W = room.width * scale, D = room.depth * scale, H = room.height * scale;
    const auto volume = W * D * H;
    const auto surface = 2.0 * (W * D + W * H + D * H);

    // Each receiver (the left and right outputs) hears its own side's decay.
    std::array<std::array<double, 8>, 2> rt {};
    std::array<std::array<double, 8>, 2> reflection {};
    std::array<double, 2> betaMid {}, reverberantEnergy {};

    for (std::size_t side = 0; side < 2; ++side)
    {
        rt[side] = decayFor (request, static_cast<double> (side));

        for (std::size_t b = 0; b < 8; ++b)
        {
            // Sabine: the mean absorption this band's RT60 implies, then the
            // pressure reflection coefficient of one wall bounce.
            const auto alpha = std::clamp (0.161 * volume / (surface * rt[side][b]), 0.02, 0.98);
            reflection[side][b] = std::sqrt (1.0 - alpha);
        }

        betaMid[side] = 0.5 * (reflection[side][3] + reflection[side][4]);

        // Diffuse-field theory: reverberant energy relative to a direct sound at
        // 1 m is 1 / rc^2, with rc the critical distance.
        const auto rtMid = 0.5 * (rt[side][3] + rt[side][4]);
        const auto criticalDistance = 0.057 * std::sqrt (volume / rtMid);
        reverberantEnergy[side] = 1.0 / (criticalDistance * criticalDistance);
    }

    // Positions: two sources across the stage, either side of where the sound
    // is placed (kept clear of the side walls), two receivers 60 cm apart.
    const auto sourceY = std::clamp (D * 0.12, 0.5, 4.0);
    const auto sourceZ = std::min (1.6, H * 0.6);
    const auto sourceSpread = std::min (1.5, W * 0.15);
    const auto sourceX = std::clamp (static_cast<double> (request.sourceX) * W, sourceSpread + 0.3, W - sourceSpread - 0.3);
    const auto listenerY = std::clamp (static_cast<double> (listenerDistanceMetres (spaceIndex, request.sizePercent, request.distancePercent)),
                                       sourceY + 0.5, D - 0.3);
    const auto listenerX = std::clamp (static_cast<double> (request.listenerX) * W, 0.6, W - 0.6);
    const auto listenerZ = std::min (1.4, H * 0.5);

    const std::array<Vec3, 2> sources { Vec3 { sourceX - sourceSpread, sourceY, sourceZ },
                                        Vec3 { sourceX + sourceSpread, sourceY, sourceZ } };
    const std::array<Vec3, 2> receivers { Vec3 { listenerX - 0.3, listenerY, listenerZ },
                                          Vec3 { listenerX + 0.3, listenerY, listenerZ } };

    // Time zero of the response sits just before the earliest direct sound.
    double earliest = 1.0e9;

    for (const auto& s : sources)
        for (const auto& r : receivers)
            earliest = std::min (earliest, distance (s, r) / speedOfSound);

    const auto offset = std::max (0.0, earliest - directMarginSeconds);

    const auto mixingTime = std::clamp (std::sqrt (volume) / 1000.0, 0.01, 0.2);
    const auto earlyEnd = 2.0 * mixingTime;
    const auto slowest = std::max (*std::max_element (rt[0].begin(), rt[0].end()), *std::max_element (rt[1].begin(), rt[1].end()));
    const auto tailSeconds = std::min (maxLengthSeconds, std::max (earlyEnd + 0.05, slowest * tailRangeDb / 60.0));

    const auto lead = static_cast<std::size_t> (std::ceil (leadInSeconds * sr));
    const auto length = static_cast<std::size_t> (std::ceil (tailSeconds * sr));
    const auto earlyLength = lead + static_cast<std::size_t> (std::ceil ((earlyEnd + 0.01) * sr));

    const auto angle = std::clamp (static_cast<double> (request.earlyLatePercent), 0.0, 100.0) / 100.0 * std::numbers::pi / 2.0;
    const auto earlyGain = std::cos (angle) / std::cos (defaultEarlyLateAngle);
    const auto lateGain = std::sin (angle) / std::sin (defaultEarlyLateAngle);

    ImpulseResponseSet result;
    result.sampleRate = sr;

    for (std::size_t path = 0; path < 4; ++path)
    {
        const auto& source = sources[path / 2];
        const auto side = path % 2;
        const auto& receiver = receivers[side];

        // ---- Early reflections: image sources, one train per octave band ----
        std::array<std::vector<float>, 8> early;

        for (auto& train : early)
            train.assign (earlyLength, 0.0f);

        double imageEnergyMid = 0.0;

        const auto reach = (offset + earlyEnd) * speedOfSound;
        const auto nx = static_cast<int> (std::ceil (reach / (2.0 * W))) + 1;
        const auto ny = static_cast<int> (std::ceil (reach / (2.0 * D))) + 1;
        const auto nz = static_cast<int> (std::ceil (reach / (2.0 * H))) + 1;

        for (int px = 0; px <= 1; ++px)
        for (int i = -nx; i <= nx; ++i)
        for (int py = 0; py <= 1; ++py)
        for (int j = -ny; j <= ny; ++j)
        for (int pz = 0; pz <= 1; ++pz)
        for (int k = -nz; k <= nz; ++k)
        {
            const Vec3 image { (1 - 2 * px) * source.x + 2.0 * i * W,
                               (1 - 2 * py) * source.y + 2.0 * j * D,
                               (1 - 2 * pz) * source.z + 2.0 * k * H };

            const auto r = std::max (0.1, distance (image, receiver));
            const auto t = r / speedOfSound - offset;

            if (t > earlyEnd)
                continue;

            const auto bounces = std::abs (i - px) + std::abs (i) + std::abs (j - py) + std::abs (j) + std::abs (k - pz) + std::abs (k);
            const auto taper = 1.0 - raisedCosine ((t - mixingTime) / (earlyEnd - mixingTime));
            const auto position = static_cast<double> (lead) + t * sr;

            for (std::size_t b = 0; b < 8; ++b)
                addInterpolated (early[b], position, std::pow (reflection[side][b], bounces) / r * taper);

            if (bounces > 0)
            {
                const auto g = std::pow (betaMid[side], bounces) / r * taper;
                imageEnergyMid += g * g;
            }
        }

        std::vector<float> earlyPart (earlyLength, 0.0f);

        for (std::size_t b = 0; b < 8; ++b)
        {
            const auto band = bandOf (std::move (early[b]), b, sr);

            for (std::size_t n = 0; n < earlyLength; ++n)
                earlyPart[n] += band[n];
        }

        // ---- Late tail: decorrelated noise, each band decaying at its RT60 ----
        std::vector<float> noise (lead + length, 0.0f);
        Noise generator (0xD5E7C1A0u + 7919u * path);

        for (std::size_t n = lead; n < noise.size(); ++n)
        {
            const auto t = static_cast<double> (n - lead) / sr;
            noise[n] = generator.next() * static_cast<float> (raisedCosine (t / mixingTime));
        }

        const auto late = shapeDecay (noise, lead, rt[side], sr);

        // Scale the tail to the diffuse-field level not already carried by the early images.
        const auto lateTarget = std::max (reverberantEnergy[side] - imageEnergyMid, 0.25 * reverberantEnergy[side]);
        const auto lateEnergy = energyOf (late);
        const auto lateScale = lateEnergy > 0.0 ? std::sqrt (lateTarget / lateEnergy) : 0.0;

        auto& out = result.paths[path];
        out.assign (length, 0.0f);

        for (std::size_t n = 0; n < length; ++n)
        {
            const auto src = n + lead;
            const auto e = src < earlyLength ? static_cast<double> (earlyPart[src]) : 0.0;
            out[n] = static_cast<float> (earlyGain * e + lateGain * lateScale * late[src]);
        }
    }

    // One gain for all four paths keeps the stereo image; each output then
    // carries about half the input energy (-3 dB) whatever the space.
    double total = 0.0;

    for (const auto& p : result.paths)
        total += energyOf (p);

    const auto gain = total > 0.0 ? static_cast<float> (std::sqrt (1.0 / total)) : 0.0f;
    const auto fade = std::min (length, static_cast<std::size_t> (endFadeSeconds * sr));

    for (auto& p : result.paths)
    {
        for (auto& v : p)
            v *= gain;

        for (std::size_t n = 0; n < fade; ++n)
            p[length - 1 - n] *= static_cast<float> (raisedCosine (static_cast<double> (n) / static_cast<double> (fade)));
    }

    return result;
}

} // namespace drenched

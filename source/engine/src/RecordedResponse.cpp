#include "drenched/RecordedResponse.h"
#include "drenched/Spaces.h"
#include "Bands.h"

#include <CDSPResampler.h>
#include <pffft/pffft.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace drenched
{

namespace
{

constexpr double leadSeconds = 0.001;       // kept before the direct sound
constexpr double maxSeconds = 30.0;         // longer recordings are cut here
constexpr double endFadeSeconds = 0.05;
constexpr double windowSeconds = 0.01;      // envelope resolution for finding the noise floor
constexpr double minimumRangeDb = 25.0;     // less decay than this is not an impulse response
constexpr double maxBoostDb = 30.0;         // how far stretching a decay may lift the tail
constexpr double defaultEarlyLateAngle = 0.55 * std::numbers::pi / 2.0;
constexpr double maxColourDb = 12.0;        // how far Size may correct a band's level

std::size_t samplesOf (double seconds, double sampleRate) noexcept
{
    return static_cast<std::size_t> (std::lround (seconds * sampleRate));
}

double totalEnergy (const ImpulseResponseSet& set) noexcept
{
    double sum = 0.0;

    for (const auto& p : set.paths)
        sum += bands::energyOf (p);

    return sum;
}

void normalise (ImpulseResponseSet& set) noexcept
{
    const auto total = totalEnergy (set);

    if (total <= 0.0)
        return;

    const auto gain = static_cast<float> (std::sqrt (1.0 / total));

    for (auto& p : set.paths)
        for (auto& v : p)
            v *= gain;
}

void resize (ImpulseResponseSet& set, std::size_t length)
{
    for (auto& p : set.paths)
        p.resize (length, 0.0f);
}

void fadeOut (ImpulseResponseSet& set, std::size_t fade) noexcept
{
    const auto length = set.length();
    fade = std::min (fade, length);

    for (auto& p : set.paths)
        for (std::size_t n = 0; n < fade; ++n)
            p[length - 1 - n] *= static_cast<float> (bands::raisedCosine (static_cast<double> (n) / static_cast<double> (fade)));
}

/** Energy of all four paths together, in windows of `window` samples. */
std::vector<double> envelope (const ImpulseResponseSet& set, std::size_t window)
{
    const auto length = set.length();
    std::vector<double> env ((length + window - 1) / window, 0.0);

    for (const auto& p : set.paths)
        for (std::size_t n = 0; n < length; ++n)
            env[n / window] += static_cast<double> (p[n]) * p[n];

    return env;
}

/** Where the recording sinks into its noise floor, or where it falls
    silent. Throws if it never decays enough to be an impulse response. */
std::size_t usefulLength (const ImpulseResponseSet& set)
{
    const auto window = std::max<std::size_t> (1, samplesOf (windowSeconds, set.sampleRate));
    const auto env = envelope (set, window);
    const auto peak = *std::max_element (env.begin(), env.end());

    // The noise floor: the mean level of the last tenth.
    const auto tailWindows = std::max<std::size_t> (1, env.size() / 10);
    const auto noise = std::accumulate (env.end() - static_cast<std::ptrdiff_t> (tailWindows), env.end(), 0.0)
                     / static_cast<double> (tailWindows);

    const auto rangeDb = 10.0 * std::log10 (peak / std::max (noise, peak * 1.0e-30));

    if (rangeDb < minimumRangeDb)
        throw std::invalid_argument ("This doesn't sound like an impulse response: its level never falls far "
                                     "enough. Load a recording of a room's response to a click or sweep.");

    // A clean, digital tail: cut only the silence at the very end.
    // Otherwise stop where the room falls to 10 dB above the noise.
    const auto threshold = rangeDb > 100.0 ? peak * 1.0e-14 : noise * 10.0;

    auto last = env.size();

    while (last > 0 && env[last - 1] <= threshold)
        --last;

    return std::min (set.length(), std::max<std::size_t> (1, last) * window);
}

/** Abel and Huang's normalised echo density: the fraction of samples in a
    sliding window that stand out from the window's standard deviation,
    relative to Gaussian noise. It reaches 1 once the reflections are dense
    enough to be heard as a diffuse tail: the mixing time. */
double mixingTime (const ImpulseResponseSet& set)
{
    const auto sr = set.sampleRate;
    const auto length = set.length();
    const auto window = samplesOf (0.02, sr);
    const auto hop = std::max<std::size_t> (1, samplesOf (0.001, sr));
    const auto gaussianShare = std::erfc (1.0 / std::numbers::sqrt2);
    const auto searchEnd = std::min (length, samplesOf (0.25, sr) + window);

    std::vector<float> mono (searchEnd);

    for (std::size_t n = 0; n < searchEnd; ++n)
        mono[n] = set.paths[0][n] + set.paths[3][n];

    for (std::size_t start = samplesOf (0.005, sr); start + window <= searchEnd; start += hop)
    {
        double sumSquares = 0.0;

        for (std::size_t n = start; n < start + window; ++n)
            sumSquares += static_cast<double> (mono[n]) * mono[n];

        const auto deviation = std::sqrt (sumSquares / static_cast<double> (window));
        std::size_t outliers = 0;

        for (std::size_t n = start; n < start + window; ++n)
            if (std::abs (mono[n]) > deviation)
                ++outliers;

        const auto density = static_cast<double> (outliers) / static_cast<double> (window) / gaussianShare;

        if (density >= 1.0)
            return std::clamp ((static_cast<double> (start) + 0.5 * static_cast<double> (window)) / sr, 0.01, 0.2);
    }

    return 0.2;
}

/** A shoebox of the volume the mixing time implies (mixing time in seconds
    is about the square root of the volume in cubic metres, over 1000). */
void estimateRoom (RoomAnalysis& room)
{
    const auto volume = std::clamp (std::pow (static_cast<double> (room.mixingTimeSeconds) * 1000.0, 2.0), 30.0, 60000.0);
    constexpr double w = 1.0, d = 1.4, h = 0.55;
    const auto k = std::cbrt (volume / (w * d * h));
    room.width = static_cast<float> (w * k);
    room.depth = static_cast<float> (d * k);
    room.height = static_cast<float> (h * k);
}

double fitT20 (const std::vector<double>& energy, double sampleRate)
{
    std::vector<double> schroeder (energy.size());
    double sum = 0.0;

    for (std::size_t n = energy.size(); n-- > 0;)
    {
        sum += energy[n];
        schroeder[n] = sum;
    }

    if (sum <= 0.0)
        return std::nan ("");

    // Fit between -5 and -25 dB; fall back to -5 to -15 dB (T10) for a
    // band with little range.
    for (const auto bottom : { -25.0, -15.0 })
    {
        double st = 0.0, sd = 0.0, stt = 0.0, std2 = 0.0;
        double count = 0.0;

        for (std::size_t n = 0; n < energy.size(); ++n)
        {
            const auto level = 10.0 * std::log10 (std::max (schroeder[n], 1.0e-300) / sum);

            if (level < bottom)
                break;

            if (level <= -5.0)
            {
                const auto t = static_cast<double> (n) / sampleRate;
                st += t;
                sd += level;
                stt += t * t;
                std2 += t * level;
                count += 1.0;
            }
        }

        const auto denominator = count * stt - st * st;

        if (count > 8.0 && denominator > 0.0)
        {
            const auto slope = (count * std2 - st * sd) / denominator; // dB per second

            if (slope < 0.0)
                return -60.0 / slope;
        }
    }

    return std::nan ("");
}

/** A per-band value at any frequency, interpolated between the band
    centres on a log frequency axis and held beyond the end bands. */
template <typename Value>
double atFrequency (const std::array<Value, 8>& perBand, double f) noexcept
{
    if (f <= decayBandCentres.front())
        return static_cast<double> (perBand.front());

    if (f >= decayBandCentres.back())
        return static_cast<double> (perBand.back());

    std::size_t band = 0;

    while (band < 6 && f >= static_cast<double> (decayBandCentres[band + 1]))
        ++band;

    const auto frac = std::clamp (std::log (f / static_cast<double> (decayBandCentres[band]))
                                      / std::log (static_cast<double> (decayBandCentres[band + 1]) / static_cast<double> (decayBandCentres[band])),
                                  0.0, 1.0);
    return static_cast<double> (perBand[band]) + frac * (static_cast<double> (perBand[band + 1]) - static_cast<double> (perBand[band]));
}

/** Tilts the decay of every frequency by short-time Fourier analysis and
    resynthesis (square-root Hann windows, 75% overlap), the inverse of how
    the bundled spaces are synthesised. `tilt` is the change in log10 gain
    per second at each band centre, interpolated between them on a log
    frequency axis; `offset` is a fixed change in log10 gain, in the same
    way. Working bin by bin keeps a band's new decay from being
    masked by its neighbours, which a filter bank cannot. With no tilt the
    signal comes back unchanged (to rounding). */
std::vector<float> tiltDecay (const std::vector<float>& x, const std::array<double, 8>& tilt, const std::array<double, 8>& offset,
                              double lead, double sampleRate)
{
    std::size_t frame = 256;

    while (static_cast<double> (frame) < 0.021 * sampleRate)
        frame *= 2;

    const auto hop = frame / 4;
    const auto bins = frame / 2;
    const auto ceiling = maxBoostDb / 20.0;

    std::unique_ptr<PFFFT_Setup, void (*) (PFFFT_Setup*)> setup (pffft_new_setup (static_cast<int> (frame), PFFFT_REAL), pffft_destroy_setup);
    std::unique_ptr<float, void (*) (void*)> buffer (static_cast<float*> (pffft_aligned_malloc (frame * sizeof (float))), pffft_aligned_free);
    std::unique_ptr<float, void (*) (void*)> spectrum (static_cast<float*> (pffft_aligned_malloc (frame * sizeof (float))), pffft_aligned_free);
    std::unique_ptr<float, void (*) (void*)> work (static_cast<float*> (pffft_aligned_malloc (frame * sizeof (float))), pffft_aligned_free);

    std::vector<float> window (frame);

    for (std::size_t n = 0; n < frame; ++n)
        window[n] = static_cast<float> (std::sin (std::numbers::pi * (static_cast<double> (n) + 0.5) / static_cast<double> (frame)));

    std::vector<double> perBin (bins + 1), offsetPerBin (bins + 1);

    for (std::size_t k = 0; k <= bins; ++k)
    {
        const auto f = static_cast<double> (k) * sampleRate / static_cast<double> (frame);
        perBin[k] = atFrequency (tilt, f);
        offsetPerBin[k] = atFrequency (offset, f);
    }

    // Pad by a frame each side so every sample is covered by full overlap.
    const auto length = x.size();
    std::vector<float> padded (length + 2 * frame, 0.0f), out (length + 2 * frame, 0.0f);
    std::copy (x.begin(), x.end(), padded.begin() + static_cast<std::ptrdiff_t> (frame));

    const auto norm = 1.0f / (static_cast<float> (frame) * 2.0f); // inverse FFT scale and Hann overlap sum

    for (std::size_t pos = 0; pos + frame <= padded.size(); pos += hop)
    {
        for (std::size_t n = 0; n < frame; ++n)
            buffer.get()[n] = padded[pos + n] * window[n];

        pffft_transform_ordered (setup.get(), buffer.get(), spectrum.get(), work.get(), PFFFT_FORWARD);

        const auto centre = static_cast<double> (pos + frame / 2) - static_cast<double> (frame) - lead;
        const auto seconds = std::max (0.0, centre / sampleRate);
        const auto gainAt = [&] (std::size_t k) { return static_cast<float> (std::pow (10.0, std::min (ceiling, offsetPerBin[k] + perBin[k] * seconds))); };

        auto* sp = spectrum.get();
        sp[0] *= gainAt (0);
        sp[1] *= gainAt (bins);

        for (std::size_t k = 1; k < bins; ++k)
        {
            const auto g = gainAt (k);
            sp[2 * k] *= g;
            sp[2 * k + 1] *= g;
        }

        pffft_transform_ordered (setup.get(), spectrum.get(), buffer.get(), work.get(), PFFFT_BACKWARD);

        for (std::size_t n = 0; n < frame; ++n)
            out[pos + n] += buffer.get()[n] * window[n] * norm;
    }

    return { out.begin() + static_cast<std::ptrdiff_t> (frame), out.begin() + static_cast<std::ptrdiff_t> (frame + length) };
}


/** Plays the response back `scale` times slower: every reflection arrives
    that much later and echoes thin out as they would in a room that much
    bigger. Pitch drops with it, which the caller corrects. The direct sound
    stays where it was. */
ImpulseResponseSet stretchTime (const ImpulseResponseSet& set, double scale)
{
    const auto sr = set.sampleRate;
    const auto length = set.length();
    const auto stretched = static_cast<std::size_t> (std::ceil (static_cast<double> (length) * scale));
    const auto lead = static_cast<double> (samplesOf (leadSeconds, sr));
    const auto moved = static_cast<std::ptrdiff_t> (std::lround (lead * scale - lead));

    ImpulseResponseSet out;
    out.sampleRate = sr;

    for (std::size_t p = 0; p < 4; ++p)
    {
        auto& path = out.paths[p];
        path.assign (stretched, 0.0f);

        if (bands::energyOf (set.paths[p]) > 0.0)
        {
            r8b::CDSPResampler24 resampler (sr, sr * scale, 8192);
            resampler.oneshot (set.paths[p].data(), static_cast<int> (length), path.data(), static_cast<int> (stretched));
        }

        if (moved > 0)
            path.erase (path.begin(), path.begin() + moved);
        else if (moved < 0)
            path.insert (path.begin(), static_cast<std::size_t> (-moved), 0.0f);
    }

    const auto shortest = std::min_element (out.paths.begin(), out.paths.end(), [] (const auto& a, const auto& b) { return a.size() < b.size(); })->size();
    resize (out, shortest);
    return out;
}

/** Each octave band's power over the response's first `seconds`. */
std::array<double, 8> startColour (const ImpulseResponseSet& set, double seconds)
{
    const auto sr = set.sampleRate;
    const auto end = std::min (set.length(), samplesOf (leadSeconds + seconds, sr));
    const auto margin = std::min (set.length(), end + samplesOf (0.02, sr)); // room for the filters to settle
    std::array<double, 8> power {};

    for (const auto& path : set.paths)
    {
        if (bands::energyOf (path) <= 0.0)
            continue;

        const auto split = bands::split ({ path.begin(), path.begin() + static_cast<std::ptrdiff_t> (margin) }, sr);

        for (std::size_t b = 0; b < 8; ++b)
            for (std::size_t n = 0; n < end; ++n)
                power[b] += static_cast<double> (split[b][n]) * split[b][n];
    }

    for (auto& p : power)
        p /= seconds;

    return power;
}
} // namespace

std::array<float, 8> measureDecay (const std::array<std::vector<float>, 4>& paths, double sampleRate)
{
    const auto length = paths[0].size();
    std::array<std::vector<double>, 8> energy;

    for (auto& e : energy)
        e.assign (length, 0.0);

    for (const auto& path : paths)
    {
        if (bands::energyOf (path) <= 0.0)
            continue;

        const auto split = bands::split (path, sampleRate);

        for (std::size_t b = 0; b < 8; ++b)
            for (std::size_t n = 0; n < length; ++n)
                energy[b][n] += static_cast<double> (split[b][n]) * split[b][n];
    }

    std::array<double, 8> rt {};
    const auto window = std::max<std::size_t> (1, samplesOf (windowSeconds, sampleRate));

    for (std::size_t b = 0; b < 8; ++b)
    {
        auto& e = energy[b];

        // Lundeby, simplified: if the recording ends in a steady noise floor
        // (its last two twentieths are within 3 dB of each other), estimate
        // the floor there, stop integrating where the decay meets it, and
        // subtract it. A response that is still decaying at its end has no
        // floor to remove.
        const auto twentieth = std::max<std::size_t> (1, length / 20);
        const auto meanOf = [&e, twentieth] (std::size_t from)
        {
            return std::accumulate (e.begin() + static_cast<std::ptrdiff_t> (from),
                                    e.begin() + static_cast<std::ptrdiff_t> (from + twentieth), 0.0) / static_cast<double> (twentieth);
        };

        const auto last = meanOf (length - twentieth);
        const auto before = length >= 2 * twentieth ? meanOf (length - 2 * twentieth) : last;
        const auto noise = before > 0.0 && last >= 0.5 * before && last <= 2.0 * before ? 0.5 * (last + before) : 0.0;

        auto end = length;

        if (noise > 0.0)
        {
            while (end > window)
            {
                const auto mean = std::accumulate (e.begin() + static_cast<std::ptrdiff_t> (end - window),
                                                   e.begin() + static_cast<std::ptrdiff_t> (end), 0.0) / static_cast<double> (window);

                if (mean > 2.0 * noise)
                    break;

                end -= window;
            }
        }

        e.resize (end);

        for (auto& v : e)
            v = std::max (0.0, v - noise);

        rt[b] = fitT20 (e, sampleRate);
    }

    // A band that could not be measured takes its neighbour's value.
    for (std::size_t pass = 0; pass < 8; ++pass)
        for (std::size_t b = 0; b < 8; ++b)
            if (std::isnan (rt[b]))
                rt[b] = b > 0 && ! std::isnan (rt[b - 1]) ? rt[b - 1] : (b < 7 ? rt[b + 1] : std::nan (""));

    std::array<float, 8> out {};

    for (std::size_t b = 0; b < 8; ++b)
        out[b] = std::isnan (rt[b]) ? 1.0f : static_cast<float> (std::clamp (rt[b], 0.1, 20.0));

    return out;
}

std::shared_ptr<const RecordedResponse> prepareRecordedResponse (const std::vector<std::vector<float>>& channels,
                                                                 double sampleRate,
                                                                 std::string name)
{
    const auto count = channels.size();

    if (count != 1 && count != 2 && count != 4)
        throw std::invalid_argument ("Drenched loads mono, stereo or four-channel (true stereo) impulse responses. "
                                     "This file has " + std::to_string (count) + " channels.");

    if (! (sampleRate >= 8000.0 && sampleRate <= 384000.0))
        throw std::invalid_argument ("The file's sample rate isn't one Drenched can use.");

    auto length = channels[0].size();

    for (const auto& c : channels)
        length = std::min (length, c.size());

    if (length < samplesOf (0.01, sampleRate))
        throw std::invalid_argument ("The file is too short to be an impulse response.");

    float peak = 0.0f;

    for (const auto& c : channels)
    {
        for (std::size_t n = 0; n < length; ++n)
        {
            if (! std::isfinite (c[n]))
                throw std::invalid_argument ("The file is damaged: it contains invalid samples.");

            peak = std::max (peak, std::abs (c[n]));
        }
    }

    if (peak < 1.0e-6f)
        throw std::invalid_argument ("The file is silent.");

    auto recorded = std::make_shared<RecordedResponse>();
    recorded->name = std::move (name);
    recorded->channels = static_cast<int> (count);

    auto& set = recorded->responses;
    set.sampleRate = sampleRate;

    using P = ImpulseResponseSet::Path;
    const auto source = [&] (std::size_t c) { return std::vector<float> (channels[c].begin(), channels[c].begin() + static_cast<std::ptrdiff_t> (length)); };

    if (count == 1)
    {
        set.paths[P::leftToLeft] = source (0);
        set.paths[P::rightToRight] = source (0);
        set.paths[P::leftToRight].assign (length, 0.0f);
        set.paths[P::rightToLeft].assign (length, 0.0f);
    }
    else if (count == 2)
    {
        set.paths[P::leftToLeft] = source (0);
        set.paths[P::rightToLeft] = source (0);
        set.paths[P::leftToRight] = source (1);
        set.paths[P::rightToRight] = source (1);
    }
    else
    {
        for (std::size_t c = 0; c < 4; ++c)
            set.paths[c] = source (c);
    }

    // Trim the silence before the direct sound (the first sound within 20 dB
    // of the peak, on any path), keeping the paths' relative timing.
    auto onset = length;

    for (const auto& p : set.paths)
        for (std::size_t n = 0; n < std::min (onset, length); ++n)
            if (std::abs (p[n]) >= 0.1f * peak)
            {
                onset = std::min (onset, n);
                break;
            }

    const auto start = onset > samplesOf (leadSeconds, sampleRate) ? onset - samplesOf (leadSeconds, sampleRate) : 0;

    for (auto& p : set.paths)
        p.erase (p.begin(), p.begin() + static_cast<std::ptrdiff_t> (start));

    // Measure the decay while the noise floor is still there to be estimated.
    recorded->room.decaySeconds = measureDecay (set.paths, sampleRate);

    resize (set, std::min (usefulLength (set), samplesOf (maxSeconds, sampleRate)));
    fadeOut (set, std::min (samplesOf (endFadeSeconds, sampleRate), set.length() / 4));
    normalise (set);
    recorded->room.mixingTimeSeconds = static_cast<float> (mixingTime (set));
    estimateRoom (recorded->room);

    return recorded;
}

ImpulseResponseSet resampleResponses (const ImpulseResponseSet& responses, double newRate)
{
    if (responses.sampleRate == newRate)
        return responses;

    const auto length = responses.length();
    const auto newLength = static_cast<std::size_t> (std::ceil (static_cast<double> (length) * newRate / responses.sampleRate));
    constexpr int block = 8192;

    ImpulseResponseSet out;
    out.sampleRate = newRate;

    for (std::size_t p = 0; p < 4; ++p)
    {
        out.paths[p].assign (newLength, 0.0f);

        if (bands::energyOf (responses.paths[p]) <= 0.0)
            continue;

        r8b::CDSPResampler24 resampler (responses.sampleRate, newRate, block);
        resampler.oneshot (responses.paths[p].data(), static_cast<int> (length), out.paths[p].data(), static_cast<int> (newLength));
    }

    return out;
}

ImpulseResponseSet shapeRecordedResponse (ImpulseResponseSet set, const RecordedResponse& recorded,
                                          const ImpulseResponseRequest& request)
{
    const auto sr = set.sampleRate;
    const auto lead = static_cast<double> (samplesOf (leadSeconds, sr));
    const auto seconds = [&] (std::size_t n) { return std::max (0.0, (static_cast<double> (n) - lead) / sr); };
    const auto scale = sizeScale (request);
    const auto tm = static_cast<double> (recorded.room.mixingTimeSeconds) * scale;

    // ---- Size: the room played back slower or faster, then its colour put back ----
    std::array<double, 8> offset {}; // log10 of a fixed gain change
    const auto resized = std::abs (scale - 1.0) > 1.0e-4;

    if (resized)
    {
        const auto window = std::max (0.08, 2.0 * static_cast<double> (recorded.room.mixingTimeSeconds));
        const auto before = startColour (set, window);
        set = stretchTime (set, scale);
        const auto after = startColour (set, window * scale);

        for (std::size_t b = 0; b < 8; ++b)
            if (before[b] > 0.0 && after[b] > 0.0)
                offset[b] = std::clamp (0.5 * std::log10 (before[b] / after[b]), -maxColourDb / 20.0, maxColourDb / 20.0);
    }

    const auto length = set.length();

    // ---- Decay: each band's envelope tilted from the measured RT60 to the requested one ----
    std::array<double, 8> tilt {}; // log10 of the gain change per second
    bool reshape = resized;

    for (std::size_t b = 0; b < 8; ++b)
    {
        // Stretching made each band's decay `scale` times longer, and moved
        // down to it what was `scale` times higher.
        const auto from = scale * atFrequency (recorded.room.decaySeconds, decayBandCentres[b] * scale);
        const auto to = std::clamp (static_cast<double> (request.decaySeconds[b]), 0.05, 20.0);
        tilt[b] = 3.0 * (1.0 / from - 1.0 / to);
        reshape = reshape || std::abs (to - from) > 1.0e-4 * from;
    }

    if (reshape)
    {
        for (auto& path : set.paths)
            if (bands::energyOf (path) > 0.0)
                path = tiltDecay (path, tilt, offset, lead, sr);

        // A shortened decay leaves a long, near-silent end: drop it.
        const auto window = std::max<std::size_t> (1, samplesOf (windowSeconds, sr));
        const auto env = envelope (set, window);
        const auto peak = *std::max_element (env.begin(), env.end());
        auto last = env.size();

        while (last > 1 && env[last - 1] < peak * 1.0e-12) // 120 dB down
            --last;

        resize (set, std::min (length, last * window));
        fadeOut (set, std::min (samplesOf (0.02, sr), set.length() / 4));
    }

    // ---- Early reflections against the tail, crossing over at the mixing time ----
    const auto angle = std::clamp (static_cast<double> (request.earlyLatePercent), 0.0, 100.0) / 100.0 * std::numbers::pi / 2.0;

    if (std::abs (angle - defaultEarlyLateAngle) > 1.0e-6)
    {
        const auto earlyGain = std::cos (angle) / std::cos (defaultEarlyLateAngle);
        const auto lateGain = std::sin (angle) / std::sin (defaultEarlyLateAngle);

        for (auto& path : set.paths)
        {
            for (std::size_t n = 0; n < set.length(); ++n)
            {
                const auto w = bands::raisedCosine ((seconds (n) - tm) / tm);
                path[n] *= static_cast<float> (earlyGain * (1.0 - w) + lateGain * w);
            }
        }
    }

    normalise (set);
    return set;
}

ImpulseResponseSet renderImpulseResponse (const ImpulseResponseRequest& request)
{
    if (request.recorded == nullptr)
        return synthesiseImpulseResponse (request);

    return shapeRecordedResponse (resampleResponses (request.recorded->responses, request.sampleRate), *request.recorded, request);
}

} // namespace drenched

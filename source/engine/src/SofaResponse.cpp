#include "drenched/SofaResponse.h"

#include <mysofa.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace drenched
{

namespace
{
    using Vector = std::array<double, 3>;

    constexpr double degrees = 180.0 / std::numbers::pi;

    double dot (const Vector& a, const Vector& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    Vector cross (const Vector& a, const Vector& b) noexcept
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    }

    Vector minus (const Vector& a, const Vector& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }

    Vector normalised (const Vector& v, const Vector& fallback) noexcept
    {
        const auto length = std::sqrt (dot (v, v));
        return length > 1.0e-9 && std::isfinite (length) ? Vector { v[0] / length, v[1] / length, v[2] / length } : fallback;
    }

    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

        return s;
    }

    /** A SOFA attribute's value, or "" when it has none. */
    std::string attribute (const MYSOFA_ATTRIBUTE* list, const char* name)
    {
        for (; list != nullptr; list = list->next)
            if (list->name != nullptr && std::string (list->name) == name)
                return list->value != nullptr ? list->value : "";

        return {};
    }

    /** The value of the first attribute whose name contains `part`, in lower case. */
    std::string attributeLike (const MYSOFA_ATTRIBUTE* list, const std::string& part)
    {
        for (; list != nullptr; list = list->next)
            if (list->name != nullptr && lower (list->name).find (part) != std::string::npos)
                return lower (list->value != nullptr ? list->value : "");

        return {};
    }

    struct Hrtf
    {
        explicit Hrtf (MYSOFA_HRTF* h) : hrtf (h) {}
        ~Hrtf() { mysofa_free (hrtf); }
        Hrtf (const Hrtf&) = delete;
        Hrtf& operator= (const Hrtf&) = delete;

        MYSOFA_HRTF* hrtf;
    };

    /** Reads coordinate triplets from a SOFA array laid out [rows][C][count]
        (count being 1 or the number of measurements), converting spherical
        coordinates to cartesian. */
    struct Positions
    {
        Positions (const MYSOFA_ARRAY& a, std::size_t rowCount, std::size_t measurementCount)
            : array (a), rows (rowCount), spherical (lower (attribute (a.attributes, "Type")) == "spherical")
        {
            const auto elements = static_cast<std::size_t> (a.elements);
            perMeasurement = measurementCount > 1 && elements == rows * 3 * measurementCount ? measurementCount : 1;
            valid = a.values != nullptr && elements == rows * 3 * perMeasurement;
        }

        Vector at (std::size_t row, std::size_t measurement, const Vector& fallback) const noexcept
        {
            if (! valid || row >= rows)
                return fallback;

            const auto m = perMeasurement > 1 ? measurement : 0;
            Vector v;

            for (std::size_t c = 0; c < 3; ++c)
                v[c] = static_cast<double> (array.values[(row * 3 + c) * perMeasurement + m]);

            if (spherical)
            {
                const auto az = v[0] / degrees, el = v[1] / degrees, r = v[2];
                v = { r * std::cos (el) * std::cos (az), r * std::cos (el) * std::sin (az), r * std::sin (el) };
            }

            for (const auto x : v)
                if (! std::isfinite (x))
                    return fallback;

            return v;
        }

        const MYSOFA_ARRAY& array;
        std::size_t rows, perMeasurement = 1;
        bool spherical, valid = false;
    };

    /** As Positions, for arrays laid out [count][C] (ListenerPosition, SourcePosition, views). */
    Vector rowOf (const MYSOFA_ARRAY& a, std::size_t measurement, std::size_t measurementCount, const Vector& fallback)
    {
        const auto elements = static_cast<std::size_t> (a.elements);

        if (a.values == nullptr || (elements != 3 && elements != 3 * measurementCount))
            return fallback;

        const auto m = elements == 3 ? 0 : measurement;
        const auto spherical = lower (attribute (a.attributes, "Type")) == "spherical";
        Vector v { a.values[m * 3], a.values[m * 3 + 1], a.values[m * 3 + 2] };

        if (spherical)
        {
            const auto az = v[0] / degrees, el = v[1] / degrees, r = v[2];
            v = { r * std::cos (el) * std::cos (az), r * std::cos (el) * std::sin (az), r * std::sin (el) };
        }

        for (const auto x : v)
            if (! std::isfinite (x))
                return fallback;

        return v;
    }

    [[noreturn]] void unreadable (int error)
    {
        if (error == MYSOFA_UNSUPPORTED_FORMAT)
            throw std::invalid_argument ("Drenched can't read this SOFA file's data. It reads SOFA files saved with 64-bit samples, "
                                         "as the SOFA tools for MATLAB and Python write them.");

        throw std::invalid_argument ("This isn't a SOFA file Drenched can read.");
    }

    /** Scores how close a source is to a direction: degrees off, with height counting double. */
    float offBy (const SofaSet::Source& s, float azimuth) noexcept
    {
        return std::abs (s.azimuth - azimuth) + 2.0f * std::abs (s.elevation);
    }
} // namespace

std::vector<float> SofaSet::response (const Source& source, int receiver) const
{
    const auto index = (static_cast<std::size_t> (source.measurement) * static_cast<std::size_t> (receivers) + static_cast<std::size_t> (receiver))
                           * static_cast<std::size_t> (emitters)
                       + static_cast<std::size_t> (source.emitter);
    const auto delay = delays.empty() ? 0 : std::max (0, delays[index]);
    const auto length = static_cast<std::size_t> (samples);

    std::vector<float> out (static_cast<std::size_t> (delay), 0.0f);
    const auto* start = ir.data() + index * length;
    out.insert (out.end(), start, start + length);
    return out;
}

SofaSet readSofa (const void* data, std::size_t size)
{
    int error = 0;
    auto* loaded = mysofa_load_data (static_cast<const char*> (data), size, &error);

    if (loaded == nullptr)
        unreadable (error);

    const Hrtf owner (loaded);
    const auto& h = *loaded;

    const auto conventions = attribute (h.attributes, "SOFAConventions");
    const auto dataType = lower (attribute (h.attributes, "DataType"));

    if (! dataType.empty() && dataType != "fir" && dataType != "fire" && dataType != "fir-e")
        throw std::invalid_argument ("This SOFA file holds filters, not impulse responses (its data type is "
                                     + attribute (h.attributes, "DataType") + ").");

    const auto M = static_cast<std::size_t> (h.M), R = static_cast<std::size_t> (h.R), N = static_cast<std::size_t> (h.N);
    const auto irElements = static_cast<std::size_t> (h.DataIR.elements);
    const auto rateElements = h.DataSamplingRate.elements;

    // [M R N], or [M R E N] for files with several emitters.
    const auto E = M * R * N > 0 && irElements == M * R * static_cast<std::size_t> (h.E) * N ? static_cast<std::size_t> (h.E) : 1;

    if (M == 0 || R == 0 || N == 0 || h.DataIR.values == nullptr || irElements != M * R * E * N || rateElements < 1)
        throw std::invalid_argument ("This SOFA file has no impulse responses in it.");

    constexpr std::size_t maxReceivers = 64, maxSources = 100000;

    if (R > maxReceivers || M * E > maxSources)
        throw std::invalid_argument ("This SOFA file has more responses in it than Drenched can choose from.");

    SofaSet set;
    set.measurements = static_cast<int> (M);
    set.receivers = static_cast<int> (R);
    set.emitters = static_cast<int> (E);
    set.sampleRate = static_cast<double> (h.DataSamplingRate.values[0]);

    if (! (set.sampleRate >= 8000.0 && set.sampleRate <= 384000.0))
        throw std::invalid_argument ("The file's sample rate isn't one Drenched can use.");

    if (lower (conventions).find ("freefield") != std::string::npos)
        throw std::invalid_argument ("This SOFA file holds head-related responses measured without a room, so there is no reverb in it. "
                                     "Load a SOFA file of room responses (SRIR, BRIR or DRIR).");

    // Keep up to 31 seconds of each response, as for audio files.
    const auto keep = std::min (N, static_cast<std::size_t> (31.0 * set.sampleRate));
    set.samples = static_cast<int> (keep);
    set.ir.resize (M * R * E * keep);

    for (std::size_t k = 0; k < M * R * E; ++k)
        std::copy_n (h.DataIR.values + k * N, keep, set.ir.begin() + static_cast<std::ptrdiff_t> (k * keep));

    for (auto& s : set.ir)
        if (! std::isfinite (s))
            s = 0.0f;

    // Data.Delay: [I R], [M R], [I R E] or [M R E], in samples.
    if (h.DataDelay.values != nullptr)
    {
        const auto count = static_cast<std::size_t> (h.DataDelay.elements);
        const auto maxDelay = set.sampleRate; // a second at most
        std::vector<int> delays (M * R * E, 0);
        bool any = false;

        for (std::size_t m = 0; m < M; ++m)
            for (std::size_t r = 0; r < R; ++r)
                for (std::size_t e = 0; e < E; ++e)
                {
                    std::size_t from = 0;

                    if (count == R)
                        from = r;
                    else if (count == M * R)
                        from = m * R + r;
                    else if (count == R * E)
                        from = r * E + e;
                    else if (count == M * R * E)
                        from = (m * R + r) * E + e;
                    else
                        continue;

                    const auto d = static_cast<double> (h.DataDelay.values[from]);
                    const auto samples = std::isfinite (d) ? static_cast<int> (std::lround (std::clamp (d, 0.0, maxDelay))) : 0;
                    delays[(m * R + r) * E + e] = samples;
                    any = any || samples > 0;
                }

        if (any)
            set.delays = std::move (delays);
    }

    // Where each source is, seen from its listener: SOFA's x is the way the
    // listener faces (ListenerView), y is to their left and z up.
    const Positions receiverPositions (h.ReceiverPosition, R, M);
    const Positions emitterPositions (h.EmitterPosition, E, M);
    std::vector<Vector> listeners;

    for (std::size_t m = 0; m < M; ++m)
    {
        const auto listener = rowOf (h.ListenerPosition, m, M, { 0.0, 0.0, 0.0 });
        const auto view = normalised (rowOf (h.ListenerView, m, M, { 1.0, 0.0, 0.0 }), { 1.0, 0.0, 0.0 });
        auto up = rowOf (h.ListenerUp, m, M, { 0.0, 0.0, 1.0 });
        const auto along = dot (up, view);
        up = normalised ({ up[0] - along * view[0], up[1] - along * view[1], up[2] - along * view[2] }, { 0.0, 0.0, 1.0 });
        const auto left = cross (up, view);

        auto place = std::find_if (listeners.begin(), listeners.end(), [&] (const Vector& p)
                                   { const auto d = minus (p, listener); return dot (d, d) < 0.05 * 0.05; });

        if (place == listeners.end())
            place = listeners.insert (listeners.end(), listener);

        const auto source = rowOf (h.SourcePosition, m, M, { 1.0, 0.0, 0.0 });

        for (std::size_t e = 0; e < E; ++e)
        {
            // Emitters sit around their source, a loudspeaker's drivers or
            // an array's speakers; their offset is taken in the room's axes.
            const auto offset = E > 1 ? emitterPositions.at (e, m, { 0.0, 0.0, 0.0 }) : Vector { 0.0, 0.0, 0.0 };
            const auto d = minus ({ source[0] + offset[0], source[1] + offset[1], source[2] + offset[2] }, listener);

            const auto ahead = dot (d, view), side = dot (d, left), height = dot (d, up);
            SofaSet::Source s;
            s.measurement = static_cast<int> (m);
            s.emitter = static_cast<int> (e);
            s.listener = static_cast<int> (place - listeners.begin());
            s.azimuth = static_cast<float> (-std::atan2 (side, ahead) * degrees);
            s.elevation = static_cast<float> (std::atan2 (height, std::hypot (ahead, side)) * degrees);
            set.sources.push_back (s);
        }
    }

    // Receivers are placed in the listener's own axes, so y is to the left.
    for (std::size_t r = 0; r < R; ++r)
        set.receiverSide.push_back (static_cast<float> (receiverPositions.at (r, 0, { 0.0, 0.0, 0.0 })[1]));

    if (lower (conventions).find ("ambisonic") != std::string::npos)
    {
        set.ambisonic = true;
        const auto ordering = attributeLike (h.attributes, "ordering") + attributeLike (h.DataIR.attributes, "ordering");
        const auto normalisation = attributeLike (h.attributes, "normali") + attributeLike (h.DataIR.attributes, "normali");

        if (ordering.find ("fuma") != std::string::npos || normalisation.find ("fuma") != std::string::npos)
            set.ambisonicFormat = AmbisonicFormat::fuMa;
        else if (normalisation.find ("sn3d") == std::string::npos && normalisation.find ("n3d") != std::string::npos)
            set.ambisonicFormat = AmbisonicFormat::ambiXN3D;
    }

    return set;
}

SofaChoice chooseSofaChannels (const SofaSet& set)
{
    if (set.sources.empty() || set.receivers < 1 || set.samples < 1)
        throw std::invalid_argument ("This SOFA file has no impulse responses in it.");

    SofaChoice choice;

    const auto best = [&] (float azimuth, float from, float to, int listener)
    {
        int found = -1;

        for (std::size_t k = 0; k < set.sources.size(); ++k)
        {
            const auto& s = set.sources[k];

            if ((listener >= 0 && s.listener != listener) || s.azimuth < from || s.azimuth > to || std::abs (s.elevation) > 30.0f)
                continue;

            if (found < 0 || offBy (s, azimuth) < offBy (set.sources[static_cast<std::size_t> (found)], azimuth))
                found = static_cast<int> (k);
        }

        return found;
    };

    choice.front = best (0.0f, -180.0f, 180.0f, -1);

    if (choice.front < 0) // every source is high up or low down
        choice.front = 0;

    const auto& front = set.sources[static_cast<std::size_t> (choice.front)];

    if (set.ambisonic)
    {
        if (set.receivers < 4)
            throw std::invalid_argument ("This ambisonic SOFA file has fewer than the four channels of first order.");

        for (int r = 0; r < 4; ++r)
            choice.channels.push_back (set.response (front, r));

        return choice;
    }

    if (set.receivers == 1)
    {
        choice.channels.push_back (set.response (front, 0));
        return choice;
    }

    // The receivers furthest to the left and the right; the first two if
    // the file doesn't say where they are.
    auto leftEar = 0, rightEar = 1;

    for (int r = 0; r < set.receivers; ++r)
    {
        const auto side = set.receiverSide[static_cast<std::size_t> (r)];

        if (side > set.receiverSide[static_cast<std::size_t> (leftEar)])
            leftEar = r;

        if (side < set.receiverSide[static_cast<std::size_t> (rightEar)])
            rightEar = r;
    }

    if (leftEar == rightEar)
    {
        leftEar = 0;
        rightEar = 1;
    }

    choice.left = best (-30.0f, -75.0f, -10.0f, front.listener);
    choice.right = best (30.0f, 10.0f, 75.0f, front.listener);

    if (choice.left >= 0 && choice.right >= 0)
    {
        const auto& l = set.sources[static_cast<std::size_t> (choice.left)];
        const auto& r = set.sources[static_cast<std::size_t> (choice.right)];
        choice.channels = { set.response (l, leftEar), set.response (l, rightEar), set.response (r, leftEar), set.response (r, rightEar) };
        return choice;
    }

    choice.left = choice.right = -1;
    choice.channels = { set.response (front, leftEar), set.response (front, rightEar) };
    return choice;
}

std::shared_ptr<const RecordedResponse> prepareSofaResponse (const void* data, std::size_t size, std::string name)
{
    const auto set = readSofa (data, size);
    auto choice = chooseSofaChannels (set);

    if (set.ambisonic)
        return prepareAmbisonicResponse (choice.channels, set.sampleRate, std::move (name), set.ambisonicFormat);

    return prepareRecordedResponse (choice.channels, set.sampleRate, std::move (name));
}

} // namespace drenched

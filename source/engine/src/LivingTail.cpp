#include "drenched/LivingTail.h"
#include "drenched/RecordedResponse.h"
#include "drenched/Spaces.h"

#include <algorithm>
#include <cmath>

namespace drenched
{

namespace
{

constexpr std::size_t N = LivingTailDesign::numLines;
constexpr std::size_t S = LivingTailDesign::numShelves;
constexpr double speedOfSound = 343.0;

// Fixed sign patterns for feeding the lines and tapping them. Different
// patterns per channel keep the two outputs decorrelated.
constexpr std::array<float, N> inLeft   {  1, -1,  1,  1, -1,  1, -1, -1,  1,  1, -1,  1, -1, -1,  1, -1 };
constexpr std::array<float, N> inRight  {  1,  1, -1,  1,  1, -1, -1,  1, -1,  1,  1, -1, -1,  1, -1, -1 };
constexpr std::array<float, N> outLeft  { -1,  1,  1, -1,  1,  1, -1,  1, -1, -1,  1,  1, -1,  1, -1,  1 };
constexpr std::array<float, N> outRight {  1,  1, -1, -1, -1,  1,  1,  1,  1, -1, -1,  1, -1, -1,  1,  1 };

std::size_t nextPowerOfTwo (std::size_t n) noexcept
{
    std::size_t p = 1;

    while (p < n)
        p <<= 1;

    return p;
}

bool isPrime (std::size_t n) noexcept
{
    if (n < 2)
        return false;

    for (std::size_t d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return false;

    return true;
}

double shelfEdge (std::size_t k) noexcept
{
    return std::sqrt (static_cast<double> (decayBandCentres[k]) * decayBandCentres[k + 1]);
}

/** In place, unnormalised: the 16-point Walsh-Hadamard transform. */
void hadamard (std::array<float, N>& x) noexcept
{
    for (std::size_t h = 1; h < N; h <<= 1)
    {
        for (std::size_t i = 0; i < N; i += h << 1)
        {
            for (std::size_t j = i; j < i + h; ++j)
            {
                const auto a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
        }
    }
}

/** Solves A x = b by Gaussian elimination with partial pivoting. */
template <std::size_t M>
std::array<double, M> solve (std::array<std::array<double, M>, M> A, std::array<double, M> b) noexcept
{
    for (std::size_t col = 0; col < M; ++col)
    {
        auto pivot = col;

        for (auto row = col + 1; row < M; ++row)
            if (std::abs (A[row][col]) > std::abs (A[pivot][col]))
                pivot = row;

        std::swap (A[col], A[pivot]);
        std::swap (b[col], b[pivot]);

        for (auto row = col + 1; row < M; ++row)
        {
            const auto f = A[row][col] / A[col][col];

            for (auto k = col; k < M; ++k)
                A[row][k] -= f * A[col][k];

            b[row] -= f * b[col];
        }
    }

    std::array<double, M> x {};

    for (auto row = M; row-- > 0;)
    {
        auto sum = b[row];

        for (auto k = row + 1; k < M; ++k)
            sum -= A[row][k] * x[k];

        x[row] = sum / A[row][row];
    }

    return x;
}

/** The loss of one line at a frequency, interpolated across the bands on a
    log-frequency scale. */
float lossAt (const std::array<float, 8>& bandLoss, double frequency) noexcept
{
    const auto position = std::log2 (std::max (frequency, 1.0) / decayBandCentres[0]);

    if (position <= 0.0)
        return bandLoss.front();

    if (position >= 7.0)
        return bandLoss.back();

    const auto i = static_cast<std::size_t> (position);
    const auto frac = static_cast<float> (position - static_cast<double> (i));
    return bandLoss[i] + frac * (bandLoss[i + 1] - bandLoss[i]);
}

} // namespace

//==============================================================================
void LivingTail::prepare (double sampleRate)
{
    rate = sampleRate;
    capacity = nextPowerOfTwo (static_cast<std::size_t> (std::ceil (maxDelaySeconds * sampleRate)) + 1);
    mask = capacity - 1;
    buffer.assign (capacity * N, 0.0f);

    if (current.valid && current.sampleRate != sampleRate)
        current = LivingTailDesign {};

    applyLoopFilters();
    reset();
}

void LivingTail::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;

    for (auto* state : { &z1, &z2 })
        for (auto& shelf : *state)
            shelf.fill (0.0f);
}

void LivingTail::setShelf (std::size_t shelf, std::size_t line, const Biquad::Coefficients& c) noexcept
{
    b0[shelf][line] = c.b0;
    b1[shelf][line] = c.b1;
    b2[shelf][line] = c.b2;
    a1[shelf][line] = c.a1;
    a2[shelf][line] = c.a2;
}

void LivingTail::setDesign (const LivingTailDesign& design) noexcept
{
    if (! design.valid || design.sampleRate != rate)
        return;

    current = design;

    for (auto& d : current.delays)
        d = std::clamp<std::size_t> (d, 1, capacity - 1);

    applyLoopFilters();
}

void LivingTail::setFrozen (bool shouldFreeze, float crossoverHz) noexcept
{
    frozen = shouldFreeze;
    crossover = crossoverHz;
    applyLoopFilters();
}

void LivingTail::applyLoopFilters() noexcept
{
    if (! current.valid)
    {
        loopGain.fill (0.0f);
        return;
    }

    for (std::size_t i = 0; i < N; ++i)
    {
        if (frozen)
        {
            // Lossless above the crossover (a hair under unity, so rounding
            // can never make it grow); the room's bass decay below it.
            loopGain[i] = 0.999999f;
            const auto lowLoss = lossAt (current.bandLossDb[i], 0.5 * static_cast<double> (crossover));
            setShelf (0, i, Biquad::lowShelf (rate, crossover, lowLoss));

            for (std::size_t s = 1; s < S; ++s)
                setShelf (s, i, {});
        }
        else
        {
            loopGain[i] = current.broadband[i];

            for (std::size_t s = 0; s < S; ++s)
                setShelf (s, i, current.shelves[i][s]);
        }
    }
}

void LivingTail::process (const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept
{
    if (! current.valid)
    {
        std::fill_n (outL, numSamples, 0.0f);
        std::fill_n (outR, numSamples, 0.0f);
        return;
    }

    const auto gain = current.outputGain;
    std::array<float, N> v {};

    for (std::size_t n = 0; n < numSamples; ++n)
    {
        // Tap before the loop filter, so switching between frozen and
        // decaying changes what recirculates, never what is heard now.
        float l = 0.0f, r = 0.0f;

        for (std::size_t i = 0; i < N; ++i)
        {
            const auto tap = buffer[i * capacity + ((write - current.delays[i]) & mask)];
            l += outLeft[i] * tap;
            r += outRight[i] * tap;
            v[i] = tap * loopGain[i];
        }

        outL[n] = l * gain;
        outR[n] = r * gain;

        for (std::size_t s = 0; s < S; ++s)
        {
            auto& s1 = z1[s];
            auto& s2 = z2[s];

            for (std::size_t i = 0; i < N; ++i)
            {
                const auto x = v[i];
                const auto y = b0[s][i] * x + s1[i];
                s1[i] = b1[s][i] * x - a1[s][i] * y + s2[i];
                s2[i] = b2[s][i] * x - a2[s][i] * y;
                v[i] = y;
            }
        }

        hadamard (v);

        const auto a = inL[n], b = inR[n];

        for (std::size_t i = 0; i < N; ++i)
            buffer[i * capacity + write] = 0.25f * v[i] + inLeft[i] * a + inRight[i] * b;

        write = (write + 1) & mask;
    }

    // Flush state too small to matter, so a dying tail leaves no denormals.
    for (auto* state : { &z1, &z2 })
        for (auto& shelf : *state)
            for (auto& z : shelf)
                if (std::abs (z) < 1.0e-20f)
                    z = 0.0f;
}

//==============================================================================
LivingTailDesign designLivingTail (const ImpulseResponseRequest& request, const ImpulseResponseSet& responses)
{
    LivingTailDesign d;
    d.sampleRate = request.sampleRate;

    const auto sr = request.sampleRate;
    double W, D, H;

    if (request.recorded != nullptr)
    {
        // A loaded response: the shoebox its analysis estimated.
        const auto& room = request.recorded->room;
        W = room.width, D = room.depth, H = room.height;
    }
    else
    {
        const auto spaceIndex = std::clamp (request.space, 0, static_cast<int> (bundledSpaces.size()) - 1);
        const auto& room = bundledSpaces[static_cast<std::size_t> (spaceIndex)];
        const auto scale = std::clamp (static_cast<double> (request.sizePercent), 50.0, 150.0) / 100.0;
        W = room.width * scale, D = room.depth * scale, H = room.height * scale;
    }

    const auto volume = W * D * H;
    const auto surface = 2.0 * (W * D + W * H + D * H);

    // Delays spread around the room's mean free path, on distinct primes so
    // no two lines share a resonance.
    const auto meanFreePath = 4.0 * volume / surface / speedOfSound;
    const auto shortest = std::clamp (0.45 * meanFreePath, 0.011, 0.05);
    const auto longest = std::clamp (1.5 * meanFreePath, shortest * 2.2, 0.15);
    std::size_t previous = 0;

    for (std::size_t i = 0; i < N; ++i)
    {
        const auto t = shortest * std::pow (longest / shortest, static_cast<double> (i) / static_cast<double> (N - 1));
        auto length = std::max (previous + 1, static_cast<std::size_t> (std::lround (t * sr)));

        while (! isPrime (length))
            ++length;

        d.delays[i] = previous = length;
    }

    std::array<double, 8> rt {};

    for (std::size_t b = 0; b < 8; ++b)
        rt[b] = std::clamp (static_cast<double> (request.decaySeconds[b]), 0.05, 20.0);

    d.releaseSeconds = static_cast<float> (std::clamp (0.5 * (rt[3] + rt[4]), 0.3, 10.0));

    // How a 1 dB shelf at each band edge reads at every band centre. The
    // shelves overlap, so solving this system makes the cascade hit each
    // band's target exactly rather than approximately.
    std::array<std::array<double, 8>, 8> interaction {};

    for (std::size_t k = 0; k < S; ++k)
    {
        const auto shelf = Biquad::lowShelf (sr, shelfEdge (k), 1.0);

        for (std::size_t j = 0; j < 8; ++j)
            interaction[j][k] = Biquad::magnitudeDb (shelf, sr, decayBandCentres[j]);
    }

    for (std::size_t j = 0; j < 8; ++j)
        interaction[j][S] = 1.0; // broadband gain

    for (std::size_t i = 0; i < N; ++i)
    {
        std::array<double, 8> target {};

        for (std::size_t b = 0; b < 8; ++b)
        {
            target[b] = -60.0 * static_cast<double> (d.delays[i]) / (rt[b] * sr);
            d.bandLossDb[i][b] = static_cast<float> (target[b]);
        }

        const auto x = solve (interaction, target);

        for (std::size_t k = 0; k < S; ++k)
            d.shelves[i][k] = Biquad::lowShelf (sr, shelfEdge (k), x[k]);

        d.broadband[i] = static_cast<float> (std::pow (10.0, x[S] / 20.0));
    }

    d.valid = true;

    // Match the late level: play an impulse into the left input and compare
    // its energy with the response's left-input paths over the same window,
    // just after the early reflections have died away.
    const auto mixingTime = std::clamp (std::sqrt (volume) / 1000.0, 0.01, 0.2);
    const auto from = static_cast<std::size_t> (2.0 * mixingTime * sr);
    const auto to = from + static_cast<std::size_t> (0.1 * sr);

    LivingTail probe;
    probe.prepare (sr);
    probe.setDesign (d);

    std::vector<float> impulse (to, 0.0f), silence (to, 0.0f), l (to), r (to);
    impulse[0] = 1.0f;
    probe.process (impulse.data(), silence.data(), l.data(), r.data(), to);

    double tailEnergy = 0.0, roomEnergy = 0.0;

    for (auto n = from; n < to; ++n)
        tailEnergy += static_cast<double> (l[n]) * l[n] + static_cast<double> (r[n]) * r[n];

    for (const auto path : { ImpulseResponseSet::leftToLeft, ImpulseResponseSet::leftToRight })
        for (auto n = from; n < std::min (to, responses.paths[path].size()); ++n)
            roomEnergy += static_cast<double> (responses.paths[path][n]) * responses.paths[path][n];

    d.outputGain = tailEnergy > 0.0 && roomEnergy > 0.0 ? static_cast<float> (std::sqrt (roomEnergy / tailEnergy)) : 0.0f;
    return d;
}

} // namespace drenched

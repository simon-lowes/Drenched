#include "drenched/LivingTail.h"
#include "drenched/RecordedResponse.h"
#include "drenched/Spaces.h"

#include "Bands.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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

// Taps for every output: left and right as above, then the left pattern
// modulated by each Walsh function, so surround outputs are decorrelated
// from each other and from the front pair.
constexpr std::array<std::array<float, N>, maxChannels> outputTaps = []
{
    std::array<std::array<float, N>, maxChannels> taps {};
    taps[0] = outLeft;
    taps[1] = outRight;

    for (std::size_t m = 2; m < maxChannels; ++m)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            std::size_t bits = m & i, parity = 0;

            while (bits != 0)
            {
                parity ^= bits & 1u;
                bits >>= 1;
            }

            taps[m][i] = parity != 0 ? -outLeft[i] : outLeft[i];
        }
    }

    return taps;
}();

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

/** How fast each line drifts, relative to the movement rate, and where it
    starts: spread by the golden ratio so no two lines move together. */
double lineRate (std::size_t i) noexcept
{
    const auto f = static_cast<double> (i) * 0.6180339887498949;
    return 0.6 + 0.8 * (f - std::floor (f));
}

double linePhase (std::size_t i) noexcept
{
    const auto f = 0.1 + static_cast<double> (i) * 0.3819660112501051;
    return 2.0 * std::numbers::pi * (f - std::floor (f));
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
    maxExcursion = static_cast<float> (maxMovementSeconds * sampleRate);
    depthStep = maxExcursion / static_cast<float> (0.1 * sampleRate); // full depth in 100 ms
    depthNow = depthTarget = 0.0f;
    movementRate = -1.0f; // still, until setMovement() says otherwise
    rotSin.fill (0.0f);
    rotCos.fill (1.0f);

    for (std::size_t i = 0; i < N; ++i)
    {
        lfoSin[i] = static_cast<float> (std::sin (linePhase (i)));
        lfoCos[i] = static_cast<float> (std::cos (linePhase (i)));
    }

    if (current.valid && current.sampleRate != sampleRate)
        current = LivingTailDesign {};

    applyLoopFilters (false);
    reset();
}

void LivingTail::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;

    for (auto* state : { &z1, &z2 })
        for (auto& shelf : *state)
            shelf.fill (0.0f);

    // Silent, so nothing is left to glide.
    for (std::size_t i = 0; i < N; ++i)
        delayNow[i] = static_cast<float> (current.delays[i]);

    glideRemaining = 0;
    allpass.fill (0.0f);
}

void LivingTail::setShelf (std::size_t shelf, std::size_t line, const Biquad::Coefficients& c) noexcept
{
    to.b0[shelf][line] = c.b0;
    to.b1[shelf][line] = c.b1;
    to.b2[shelf][line] = c.b2;
    to.a1[shelf][line] = c.a1;
    to.a2[shelf][line] = c.a2;
}

void LivingTail::setDesign (const LivingTailDesign& design) noexcept
{
    if (! design.valid || design.sampleRate != rate)
        return;

    const auto wasValid = current.valid;
    current = design;

    // Room for the interpolator and the drift either side of every line.
    const auto margin = static_cast<std::size_t> (std::ceil (maxExcursion)) + 3;

    for (auto& d : current.delays)
        d = std::clamp<std::size_t> (d, margin, capacity - margin);

    glideRemaining = wasValid ? static_cast<std::size_t> (glideSeconds * rate) : 0;
    bool same = true;

    for (std::size_t i = 0; i < N; ++i)
    {
        const auto target = static_cast<float> (current.delays[i]);
        same = same && delayNow[i] == target;

        if (glideRemaining == 0)
            delayNow[i] = target;
        else
            glideStep[i] = (target - delayNow[i]) / static_cast<float> (glideRemaining);
    }

    if (same)
        glideRemaining = 0;

    applyLoopFilters (wasValid);
}

void LivingTail::setMovement (float depth, float rateHz) noexcept
{
    depthTarget = std::clamp (depth, 0.0f, 1.0f) * maxExcursion;
    rateHz = std::clamp (rateHz, 0.01f, 20.0f);

    if (rateHz == movementRate)
        return;

    movementRate = rateHz;

    for (std::size_t i = 0; i < N; ++i)
    {
        const auto w = 2.0 * std::numbers::pi * static_cast<double> (rateHz) * lineRate (i) / rate;
        rotSin[i] = static_cast<float> (std::sin (w));
        rotCos[i] = static_cast<float> (std::cos (w));
    }
}

void LivingTail::setFrozen (bool shouldFreeze, float crossoverHz) noexcept
{
    frozen = shouldFreeze;
    crossover = crossoverHz;
    applyLoopFilters (filterGlideLength > 0); // a new room still gliding in keeps gliding
}

void LivingTail::applyLoopFilters (bool glide) noexcept
{
    if (! current.valid)
    {
        now = to = LoopFilters {};
        filterGlideLength = 0;
        return;
    }

    to.output = current.outputGain;

    for (std::size_t i = 0; i < N; ++i)
    {
        if (frozen)
        {
            // Lossless above the crossover (a hair under unity, so rounding
            // can never make it grow); the room's bass decay below it.
            to.gain[i] = 0.999999f;
            const auto lowLoss = lossAt (current.bandLossDb[i], 0.5 * static_cast<double> (crossover));
            setShelf (0, i, Biquad::lowShelf (rate, crossover, lowLoss));

            for (std::size_t s = 1; s < S; ++s)
                setShelf (s, i, {});
        }
        else
        {
            to.gain[i] = current.broadband[i];

            for (std::size_t s = 0; s < S; ++s)
                setShelf (s, i, current.shelves[i][s]);
        }
    }

    if (glide)
    {
        // Stable second-order denominators form a convex set, so every step
        // between two of them is stable too.
        from = now;
        filterGlideLength = static_cast<std::size_t> (glideSeconds * rate);
        filterGlideDone = 0;
    }
    else
    {
        now = to;
        filterGlideLength = 0;
    }
}

void LivingTail::glideLoopFilters (std::size_t numSamples) noexcept
{
    filterGlideDone = std::min (filterGlideLength, filterGlideDone + numSamples);
    const auto t = static_cast<float> (filterGlideDone) / static_cast<float> (filterGlideLength);

    const auto mix = [t] (const Lines& a, const Lines& b, Lines& out)
    {
        for (std::size_t i = 0; i < N; ++i)
            out[i] = a[i] + t * (b[i] - a[i]);
    };

    for (std::size_t s = 0; s < S; ++s)
    {
        mix (from.b0[s], to.b0[s], now.b0[s]);
        mix (from.b1[s], to.b1[s], now.b1[s]);
        mix (from.b2[s], to.b2[s], now.b2[s]);
        mix (from.a1[s], to.a1[s], now.a1[s]);
        mix (from.a2[s], to.a2[s], now.a2[s]);
    }

    mix (from.gain, to.gain, now.gain);
    now.output = from.output + t * (to.output - from.output);

    if (filterGlideDone == filterGlideLength)
    {
        now = to;
        filterGlideLength = 0;
    }
}

void LivingTail::readFixed (Lines& taps) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        taps[i] = allpass[i] = buffer[i * capacity + ((write - current.delays[i]) & mask)];
}

void LivingTail::readMoving (Lines& taps) noexcept
{
    if (glideRemaining > 0)
    {
        if (--glideRemaining == 0)
            for (std::size_t i = 0; i < N; ++i)
                delayNow[i] = static_cast<float> (current.delays[i]);
        else
            for (std::size_t i = 0; i < N; ++i)
                delayNow[i] += glideStep[i];
    }

    // A frozen room holds still: a moving loop with no loss to spare would
    // slowly drift in level.
    const auto wanted = frozen ? 0.0f : depthTarget;
    depthNow = depthNow < wanted ? std::min (wanted, depthNow + depthStep) : std::max (wanted, depthNow - depthStep);

    for (std::size_t i = 0; i < N; ++i)
    {
        const auto s = lfoSin[i], c = lfoCos[i];
        lfoSin[i] = s * rotCos[i] + c * rotSin[i];
        lfoCos[i] = c * rotCos[i] - s * rotSin[i];

        // The fraction runs 0.5 .. 1.5 samples, through a first-order
        // allpass: its gain is exactly one at every frequency, so moving
        // costs the loop nothing and a frozen room keeps its top end.
        const auto d = delayNow[i] + depthNow * s;
        const auto whole = static_cast<std::size_t> (d - 0.5f);
        const auto fraction = d - static_cast<float> (whole);
        const auto coefficient = (1.0f - fraction) / (1.0f + fraction);
        const auto* line = buffer.data() + i * capacity;

        allpass[i] = coefficient * (line[(write - whole) & mask] - allpass[i]) + line[(write - whole - 1) & mask];
        taps[i] = allpass[i];
    }
}

void LivingTail::process (const float* inL, const float* inR, float* outL, float* outR, std::size_t numSamples) noexcept
{
    float* const outputs[] { outL, outR };
    process (inL, inR, outputs, 2, 1.0f, numSamples);
}

void LivingTail::process (const float* inL, const float* inR, float* const* outputs, std::size_t numOutputs, float scale,
                          std::size_t numSamples) noexcept
{
    numOutputs = std::min (numOutputs, maxChannels);

    if (! current.valid)
    {
        for (std::size_t k = 0; k < numOutputs; ++k)
            std::fill_n (outputs[k], numSamples, 0.0f);

        return;
    }

    std::array<float, N> v {};

    for (std::size_t n = 0; n < numSamples; ++n)
    {
        // A gliding filter moves every 16 samples: small enough steps that
        // they never make a sound.
        if (filterGlideLength > 0 && (n & 15) == 0)
            glideLoopFilters (std::min<std::size_t> (16, numSamples - n));

        const auto gain = now.output * scale;
        // Tap before the loop filter, so switching between frozen and
        // decaying changes what recirculates, never what is heard now.
        std::array<float, N> taps;

        if (glideRemaining == 0 && depthNow == 0.0f && (frozen || depthTarget == 0.0f))
            readFixed (taps);
        else
            readMoving (taps);

        for (std::size_t i = 0; i < N; ++i)
            v[i] = taps[i] * now.gain[i];

        for (std::size_t k = 0; k < numOutputs; ++k)
        {
            float y = 0.0f;

            for (std::size_t i = 0; i < N; ++i)
                y += outputTaps[k][i] * taps[i];

            outputs[k][n] = y * gain;
        }

        for (std::size_t s = 0; s < S; ++s)
        {
            auto& s1 = z1[s];
            auto& s2 = z2[s];

            for (std::size_t i = 0; i < N; ++i)
            {
                const auto x = v[i];
                const auto y = now.b0[s][i] * x + s1[i];
                s1[i] = now.b1[s][i] * x - now.a1[s][i] * y + s2[i];
                s2[i] = now.b2[s][i] * x - now.a2[s][i] * y;
                v[i] = y;
            }
        }

        hadamard (v);

        const auto a = inL[n], b = inR[n];

        for (std::size_t i = 0; i < N; ++i)
            buffer[i * capacity + write] = 0.25f * v[i] + inLeft[i] * a + inRight[i] * b;

        write = (write + 1) & mask;
    }

    // Keep the movement phasors on the unit circle.
    for (std::size_t i = 0; i < N; ++i)
    {
        const auto r = 1.0f / std::sqrt (lfoSin[i] * lfoSin[i] + lfoCos[i] * lfoCos[i]);
        lfoSin[i] *= r;
        lfoCos[i] *= r;
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
        // A loaded response: the shoebox its analysis estimated, resized.
        const auto& room = request.recorded->room;
        const auto scale = sizeScale (request);
        W = room.width * scale, D = room.depth * scale, H = room.height * scale;
    }
    else
    {
        const auto spaceIndex = std::clamp (request.space, 0, static_cast<int> (bundledSpaces.size()) - 1);
        const auto& room = bundledSpaces[static_cast<std::size_t> (spaceIndex)];
        const auto scale = sizeScale (request);
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

    // The first echo leaves after the shortest line; the input waits for the
    // rest of the mixing time.
    const auto mixingTime = std::clamp (std::sqrt (volume) / 1000.0, 0.01, 0.2);
    d.inputDelaySeconds = static_cast<float> (std::clamp (mixingTime - static_cast<double> (d.delays[0]) / sr, 0.0,
                                                          LivingTail::maxInputDelaySeconds));
    d.valid = true;

    // Match the late level: play an impulse into both inputs, a source in
    // the centre, and compare its energy with what the response gives the
    // same source over the same window, just after the early reflections
    // have died away. A centred source hears the two sides' paths summed,
    // so a recording whose sides are alike late on is matched as it sounds.
    // The window starts once the tail has been round its longest line three
    // times: before that its first echoes still stand out.
    const auto settled = d.inputDelaySeconds + 3.0 * static_cast<double> (d.delays[N - 1]) / sr;
    const auto from = static_cast<std::size_t> (std::max (2.0 * mixingTime, settled) * sr);
    const auto to = from + static_cast<std::size_t> (0.15 * sr);

    LivingTail probe;
    probe.prepare (sr);
    probe.setDesign (d);

    std::vector<float> impulse (to, 0.0f), l (to), r (to);
    impulse[static_cast<std::size_t> (std::lround (d.inputDelaySeconds * sr))] = 1.0f; // as the reverb feeds it
    probe.process (impulse.data(), impulse.data(), l.data(), r.data(), to);

    double tailEnergy = 0.0, roomEnergy = 0.0;

    for (auto n = from; n < to; ++n)
        tailEnergy += static_cast<double> (l[n]) * l[n] + static_cast<double> (r[n]) * r[n];

    const auto& p = responses.paths;

    for (auto n = from; n < std::min (to, responses.length()); ++n)
    {
        const auto left = static_cast<double> (p[ImpulseResponseSet::leftToLeft][n]) + p[ImpulseResponseSet::rightToLeft][n];
        const auto right = static_cast<double> (p[ImpulseResponseSet::leftToRight][n]) + p[ImpulseResponseSet::rightToRight][n];
        roomEnergy += left * left + right * right;
    }

    d.outputGain = tailEnergy > 0.0 && roomEnergy > 0.0 ? static_cast<float> (std::sqrt (roomEnergy / tailEnergy)) : 0.0f;
    return d;
}

float applyLivingBlend (ImpulseResponseSet& responses, float livingPercent, double mixingTimeSeconds)
{
    const auto angle = std::clamp (static_cast<double> (livingPercent), 0.0, 100.0) / 100.0 * std::numbers::pi / 2.0;

    if (angle <= 0.0 || responses.sampleRate <= 0.0)
        return 0.0f;

    const auto lateGain = std::cos (angle);
    const auto tm = std::max (mixingTimeSeconds, 0.001);

    for (auto& path : responses.paths)
    {
        for (std::size_t n = 0; n < path.size(); ++n)
        {
            const auto w = bands::raisedCosine ((static_cast<double> (n) / responses.sampleRate - tm) / tm);
            path[n] *= static_cast<float> (1.0 - w + lateGain * w);
        }
    }

    return static_cast<float> (std::sin (angle));
}

} // namespace drenched

#pragma once

#include "drenched/Biquad.h"
#include "drenched/SpeakerLayout.h"

#include <array>
#include <cstddef>

namespace drenched
{

/** One band of the wet equaliser. Shelves ignore `q`. */
struct EqBand
{
    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.9f;
};

/** A six-band parametric EQ on the reverberation: a low shelf, four bells
    and a high shelf. A band at 0 dB is skipped altogether, so a flat EQ
    leaves the sound exactly as it was.

    Everything here is real-time safe. */
class WetEqualiser
{
public:
    static constexpr std::size_t numBands = 6;
    using Bands = std::array<EqBand, numBands>;

    static constexpr float minGainDb = -18.0f, maxGainDb = 18.0f;
    static constexpr float minQ = 0.1f, maxQ = 10.0f;

    /** Flat, with the bands spread across the range. */
    static constexpr Bands flat { { { 100.0f, 0.0f, 0.71f }, { 250.0f, 0.0f, 0.9f }, { 700.0f, 0.0f, 0.9f },
                                    { 2000.0f, 0.0f, 0.9f }, { 5000.0f, 0.0f, 0.9f }, { 10000.0f, 0.0f, 0.71f } } };

    enum class Shape { lowShelf, bell, highShelf };

    static constexpr Shape shapeOf (std::size_t band) noexcept
    {
        return band == 0 ? Shape::lowShelf : band == numBands - 1 ? Shape::highShelf : Shape::bell;
    }

    static Biquad::Coefficients coefficientsOf (std::size_t band, const EqBand& settings, double sampleRate) noexcept;

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    /** Takes new settings; only the bands that changed are recalculated. */
    void setBands (const Bands& newBands) noexcept;

    /** In place on `n` samples of one channel. */
    void process (std::size_t channel, float* data, std::size_t n) noexcept;

    bool isFlat() const noexcept;

    /** The whole EQ's response at one frequency, in dB. */
    double magnitudeDb (double frequency) const noexcept;

private:
    static bool isActive (const EqBand& band) noexcept;

    double sampleRate = 48000.0;
    Bands bands = flat;
    std::array<Biquad::Coefficients, numBands> coefficients {};
    std::array<std::array<Biquad, numBands>, maxChannels> filters {};
    bool valid = false;
};

} // namespace drenched

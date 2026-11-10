#pragma once

#include "drenched/Biquad.h"
#include "drenched/Glide.h"
#include "drenched/Svf.h"
#include "drenched/SpeakerLayout.h"

#include <array>
#include <cstddef>

namespace drenched
{

/** One band of the wet equaliser. Shelves ignore `q`. A band at 0 dB is off. */
struct EqBand
{
    enum class Shape { lowShelf, bell, highShelf };

    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.9f;
    Shape shape = Shape::bell;
};

/** A twelve-band parametric EQ on the reverberation. Each band is a low
    shelf, a bell or a high shelf; the first six start out as a low shelf,
    four bells and a high shelf spread across the range, the other six as
    bells for the person to place. A band at 0 dB is skipped altogether, so a
    flat EQ leaves the sound exactly as it was. New settings glide in with
    advance(), so a band that jumps never clicks.

    Soloing a band plays only its own part of the reverberation: a bell is
    heard through a band-pass at its frequency and Q, a shelf through a low or
    high pass at its frequency. The solo fades in and out so it never clicks.

    Everything here is real-time safe. */
class WetEqualiser
{
public:
    static constexpr std::size_t numBands = 12;
    using Bands = std::array<EqBand, numBands>;
    using Shape = EqBand::Shape;

    static constexpr float minGainDb = -18.0f, maxGainDb = 18.0f;
    static constexpr float minQ = 0.1f, maxQ = 10.0f;
    static constexpr int noSolo = -1;

    /** Flat, with the bands spread across the range. */
    static constexpr Bands flat { { { 100.0f, 0.0f, 0.71f, Shape::lowShelf }, { 250.0f, 0.0f, 0.9f, Shape::bell },
                                    { 700.0f, 0.0f, 0.9f, Shape::bell },      { 2000.0f, 0.0f, 0.9f, Shape::bell },
                                    { 5000.0f, 0.0f, 0.9f, Shape::bell },     { 10000.0f, 0.0f, 0.71f, Shape::highShelf },
                                    { 150.0f, 0.0f, 0.9f, Shape::bell },      { 400.0f, 0.0f, 0.9f, Shape::bell },
                                    { 1200.0f, 0.0f, 0.9f, Shape::bell },     { 3000.0f, 0.0f, 0.9f, Shape::bell },
                                    { 7000.0f, 0.0f, 0.9f, Shape::bell },     { 14000.0f, 0.0f, 0.9f, Shape::bell } } };

    /** The shape each band started out with. */
    static constexpr Shape shapeOf (std::size_t band) noexcept { return flat[band].shape; }

    static Biquad::Coefficients coefficientsOf (const EqBand& settings, double sampleRate) noexcept;

    void prepare (double newSampleRate) noexcept;

    /** Clears the filters and settles every band where it was sent. */
    void reset() noexcept;

    /** Where the bands should go. Until advance() moves them, they stay where
        they are, except before any sound has been through: then they go at once. */
    void setBands (const Bands& newBands) noexcept;

    /** The band to hear on its own, or noSolo. */
    void setSolo (int band) noexcept;

    /** Moves every band one glide::block of samples closer to its settings. */
    void advance() noexcept;

    /** In place on `n` samples of one channel. */
    void process (std::size_t channel, float* data, std::size_t n) noexcept;

    bool isFlat() const noexcept;

    /** True while a solo is heard at all, fading in or out included. */
    bool isSoloing() const noexcept { return soloHeard > 0.0f || soloFrom > 0.0f || solo != noSolo; }

    /** The whole EQ's response at one frequency, in dB (the solo aside). */
    double magnitudeDb (double frequency) const noexcept;

private:
    static bool isActive (const EqBand& band) noexcept;

    void design (std::size_t band, std::size_t rampSamples) noexcept; // 0: at once
    void designSolo (std::size_t rampSamples) noexcept;

    double sampleRate = 48000.0;
    Bands bands = flat, targets = flat; // as heard, and where they are going
    float glideCoef = 0.0f;
    bool heardSinceReset = false;
    std::array<Biquad::Coefficients, numBands> coefficients {};
    std::array<std::array<Svf, numBands>, maxChannels> filters {};

    int solo = noSolo, soloDesigned = noSolo, soloKind = -1;
    float soloHeard = 0.0f, soloFrom = 0.0f; // how much of the soloed band is heard, 0 to 1
    std::array<Svf, maxChannels> soloFilters {};
};

} // namespace drenched

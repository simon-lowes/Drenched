#pragma once

#include <array>

namespace drenched
{

/** A modelled space: its geometry and the decay-by-frequency curve it opens
    with, from which the engine synthesises a response. The interface keeps the
    same list (source/ui/js/engine.js, MODELLED) with the same ids; sessions save
    the id. Recorded spaces come from the bundled library instead. */
struct Space
{
    const char* id;
    const char* name;
    float width, depth, height; // metres
    std::array<float, 8> decaySeconds; // RT60 at 63 Hz .. 8 kHz
    float predelayMs;
};

inline constexpr std::array<Space, 4> bundledSpaces {{
    { "modelled-hall",   "Grand Hall",       28.0f, 42.0f, 16.0f, { 2.9f, 2.8f, 2.6f, 2.4f, 2.3f, 2.0f, 1.6f, 1.1f }, 18.0f },
    { "modelled-chapel", "Stone Chapel",     13.0f, 36.0f, 17.0f, { 3.9f, 4.1f, 3.9f, 3.6f, 3.4f, 3.0f, 2.3f, 1.5f }, 24.0f },
    { "modelled-garage", "Car Park Level 2", 44.0f, 58.0f,  3.2f, { 1.5f, 1.8f, 2.0f, 1.9f, 1.8f, 1.6f, 1.3f, 0.9f },  9.0f },
    { "modelled-room",   "Live Room B",       9.0f, 12.0f,  4.2f, { 0.72f, 0.66f, 0.6f, 0.55f, 0.52f, 0.5f, 0.45f, 0.38f }, 4.0f },
}};

/** Octave band centres the decay curve is defined at. */
inline constexpr std::array<float, 8> decayBandCentres { 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f };

} // namespace drenched

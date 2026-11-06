#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace drenched
{

/** A room worked out from a description in words ("small tiled bathroom",
    "stone cathedral, long and narrow, 60 m long", "carpeted living room with
    curtains"): a shoebox the engine can model, with the decay its size and
    materials give it. Edited afterwards like any modelled space. */
struct DescribedRoom
{
    std::string name;                     // the description, tidied, for the space's name
    std::string kind;                     // the interface's kind: hall, chapel, garage, room, outdoor or underground
    std::string place;                    // a line about the room: what it was taken for
    std::string materials;                // the surfaces, in words
    float width = 0.0f, depth = 0.0f, height = 0.0f; // metres
    std::array<float, 8> decaySeconds {}; // RT60 at 63 Hz .. 8 kHz, 0.1 .. 20 s
    float predelayMs = 0.0f;              // 0 .. 250
    float distancePercent = 40.0f;        // 0 .. 100, the listener from near to far
    float earlyLatePercent = 55.0f;       // 0 .. 100
    std::vector<std::string> understood;  // the words that were used, in the order found
    bool knownRoom = false;               // false when no kind of room was named: a plain room stands in
};

/** Works out the room a description names. Deterministic: the same words
    always give the same room. Never throws; empty words give a plain room.

    Understands kinds of room (bathroom, church, cathedral, concert hall, car
    park, cave, forest ...), sizes (tiny, small, large, huge, long, narrow,
    high, low), measurements ("5 by 4 metres", "12 m long", "3 m high",
    "40 square metres", "seats 800"), materials (tiled, stone, concrete,
    brick, glass, wood, carpet, curtains, plaster, marble, metal, foam,
    books), what is in it (empty, furnished, cluttered, full of people) and
    how it sounds (reverberant, dead, bright, dark, boomy), and where the
    listener is (close, far, at the back). Decay follows Eyring's formula
    from the room's volume, surfaces and absorption, with air absorption at
    the top. Allocates; not for the audio thread. */
DescribedRoom describeRoom (std::string_view words);

/** The kinds of room describeRoom() knows, by their first name, for help
    text and tests. */
std::vector<std::string> describableRooms();

/** The materials describeRoom() knows, by their first name. */
std::vector<std::string> describableMaterials();

} // namespace drenched

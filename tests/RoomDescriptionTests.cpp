#include <drenched/RoomDescription.h>
#include <drenched/Spaces.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <string>

using namespace drenched;
using Catch::Matchers::ContainsSubstring;

namespace
{
    float mid (const DescribedRoom& r) { return 0.5f * (r.decaySeconds[3] + r.decaySeconds[4]); }
    float volume (const DescribedRoom& r) { return r.width * r.depth * r.height; }

    bool understood (const DescribedRoom& r, const std::string& word)
    {
        return std::find (r.understood.begin(), r.understood.end(), word) != r.understood.end();
    }
}

TEST_CASE ("A described room is deterministic", "[describe]")
{
    const auto a = describeRoom ("small tiled bathroom");
    const auto b = describeRoom ("small tiled bathroom");
    REQUIRE (a.decaySeconds == b.decaySeconds);
    REQUIRE (a.width == b.width);
    REQUIRE (a.name == b.name);
    REQUIRE (a.name == "Small tiled bathroom");
}

TEST_CASE ("Rooms of each kind come out the size and length they are", "[describe]")
{
    const auto bathroom = describeRoom ("small tiled bathroom");
    REQUIRE (bathroom.knownRoom);
    REQUIRE (bathroom.kind == "room");
    REQUIRE (volume (bathroom) < 15.0f);
    REQUIRE (mid (bathroom) > 0.6f);
    REQUIRE (mid (bathroom) < 2.5f);
    REQUIRE (bathroom.predelayMs <= 3.0f);
    REQUIRE_THAT (bathroom.materials, ContainsSubstring ("Tiles"));

    const auto bedroom = describeRoom ("bedroom");
    REQUIRE (mid (bedroom) > 0.25f);
    REQUIRE (mid (bedroom) < 0.8f);

    const auto cathedral = describeRoom ("stone cathedral, long and narrow");
    REQUIRE (cathedral.kind == "chapel");
    REQUIRE (volume (cathedral) > 20000.0f);
    REQUIRE (cathedral.depth > 3.0f * cathedral.width);
    REQUIRE (mid (cathedral) > 4.0f);
    REQUIRE (mid (cathedral) < 14.0f);
    REQUIRE (cathedral.predelayMs > 15.0f);
    // stone rings longer below than the air lets it above
    REQUIRE (cathedral.decaySeconds[7] < cathedral.decaySeconds[2]);

    const auto carPark = describeRoom ("underground car park");
    REQUIRE (carPark.kind == "garage");
    REQUIRE (carPark.height < 4.0f);
    REQUIRE (mid (carPark) > 1.5f);
    REQUIRE (mid (carPark) < 5.0f);

    const auto cave = describeRoom ("a damp cave");
    REQUIRE (cave.kind == "underground");
    REQUIRE (understood (cave, "cave"));

    const auto forest = describeRoom ("pine forest");
    REQUIRE (forest.kind == "outdoor");
    REQUIRE (mid (forest) < 1.5f);
    REQUIRE_THAT (forest.materials, ContainsSubstring ("open sky"));

    const auto booth = describeRoom ("vocal booth");
    REQUIRE (mid (booth) < 0.3f);

    const auto hall = describeRoom ("concert hall");
    REQUIRE (hall.kind == "hall");
    REQUIRE (mid (hall) > 1.5f);
    REQUIRE (mid (hall) < 3.5f);
}

TEST_CASE ("The decay is never outside what the graph can show", "[describe]")
{
    for (const auto* words : { "", "qwerty", "tiny ice tank, very reverberant, very reverberant", "vast cavernous cathedral of marble, empty, endless reverb",
                               "anechoic chamber, bone dry, tiny, foam", "400 by 400 by 120 metres of ice", "0.1 m cupboard" })
    {
        const auto room = describeRoom (words);

        for (const auto t : room.decaySeconds)
        {
            REQUIRE (t >= 0.1f);
            REQUIRE (t <= 20.0f);
            REQUIRE (std::isfinite (t));
        }

        REQUIRE (room.width > 0.0f);
        REQUIRE (room.depth > 0.0f);
        REQUIRE (room.height > 0.0f);
        REQUIRE (room.predelayMs >= 0.0f);
        REQUIRE (room.predelayMs <= 250.0f);
        REQUIRE (! room.name.empty());
        REQUIRE (! room.kind.empty());
    }
}

TEST_CASE ("Unknown words give a plain room and say so", "[describe]")
{
    const auto room = describeRoom ("the place where we played last year");
    REQUIRE_FALSE (room.knownRoom);
    REQUIRE (room.kind == "room");
    REQUIRE (room.name == "The place where we played last year");

    const auto empty = describeRoom ("   ");
    REQUIRE_FALSE (empty.knownRoom);
    REQUIRE (empty.name == "Described room");
}

TEST_CASE ("Size words scale the room", "[describe]")
{
    const auto small = describeRoom ("small church");
    const auto plain = describeRoom ("church");
    const auto huge = describeRoom ("huge church");
    REQUIRE (volume (small) < volume (plain));
    REQUIRE (volume (plain) < volume (huge));
    REQUIRE (mid (small) < mid (plain));
    REQUIRE (mid (plain) < mid (huge));

    const auto tall = describeRoom ("high ceilinged room");
    REQUIRE (tall.height > describeRoom ("room").height * 1.3f);

    const auto narrow = describeRoom ("long narrow corridor");
    REQUIRE (narrow.depth > 10.0f * narrow.width);
    REQUIRE (understood (narrow, "long"));
    REQUIRE (understood (narrow, "narrow"));
}

TEST_CASE ("Measurements set the room's sides", "[describe]")
{
    const auto a = describeRoom ("room 5 by 4 by 3 metres");
    REQUIRE (a.depth == 5.0f);
    REQUIRE (a.width == 4.0f);
    REQUIRE (a.height == 3.0f);

    const auto b = describeRoom ("church, 30 m long, 12 m wide, 15 m high");
    REQUIRE (b.depth == 30.0f);
    REQUIRE (b.width == 12.0f);
    REQUIRE (b.height == 15.0f);

    const auto c = describeRoom ("office 6x4m");
    REQUIRE (c.depth == 6.0f);
    REQUIRE (c.width == 4.0f);

    const auto d = describeRoom ("living room with a 2.4 m ceiling");
    REQUIRE (d.height == 2.4f);

    const auto e = describeRoom ("hall, 20 feet long");
    REQUIRE (e.depth == Catch::Approx (6.096f));

    const auto f = describeRoom ("studio of 40 square metres");
    REQUIRE (f.width * f.depth == Catch::Approx (40.0f).margin (0.01));

    const auto g = describeRoom ("concert hall that seats 2,000");
    REQUIRE (volume (g) > 12000.0f);
    REQUIRE (volume (g) < 20000.0f);

    const auto h = describeRoom ("warehouse, 5000 cubic metres");
    REQUIRE (volume (h) == Catch::Approx (5000.0f).margin (1.0));

    const auto k = describeRoom ("bedroom 3,5 by 4,2 m");
    REQUIRE (k.width == 3.5f);
    REQUIRE (k.depth == 4.2f);
}

TEST_CASE ("Materials change the decay the way they absorb", "[describe]")
{
    const auto plain = describeRoom ("empty room");
    const auto carpet = describeRoom ("empty room with carpet");
    const auto tiles = describeRoom ("empty room with tiled walls and floor");
    const auto curtains = describeRoom ("empty room with heavy curtains");
    const auto foam = describeRoom ("empty room lined with acoustic foam");

    // carpet and curtains eat the top first
    REQUIRE (carpet.decaySeconds[7] < plain.decaySeconds[7]);
    REQUIRE (carpet.decaySeconds[7] / carpet.decaySeconds[1] < plain.decaySeconds[7] / plain.decaySeconds[1]);
    REQUIRE (curtains.decaySeconds[6] < plain.decaySeconds[6]);
    REQUIRE (mid (foam) < 0.5f * mid (plain));
    REQUIRE (mid (tiles) > mid (plain));
    REQUIRE_THAT (carpet.materials, ContainsSubstring ("Carpet"));
    REQUIRE_THAT (tiles.materials, ContainsSubstring ("Tiles"));

    // a material on a named surface covers only that surface: the walls are the bigger area
    const auto carpetFloor = describeRoom ("empty room with a carpet floor");
    const auto carpetWalls = describeRoom ("empty room with carpet walls");
    REQUIRE (carpetFloor.decaySeconds[7] < plain.decaySeconds[7]);
    REQUIRE (carpetWalls.decaySeconds[7] < carpetFloor.decaySeconds[7]);
    REQUIRE (carpetFloor.decaySeconds[7] == Catch::Approx (carpet.decaySeconds[7]));

    // a kind's own name is not a material: a swimming pool is not made of pool water
    const auto pool = describeRoom ("swimming pool");
    REQUIRE_FALSE (understood (pool, "pool"));
}

TEST_CASE ("What is in the room and how it sounds", "[describe]")
{
    const auto empty = describeRoom ("empty church");
    const auto full = describeRoom ("church full of people");
    REQUIRE (mid (full) < mid (empty));

    const auto live = describeRoom ("reverberant hall");
    const auto dead = describeRoom ("dead hall");
    const auto plain = describeRoom ("hall");
    REQUIRE (mid (live) > mid (plain));
    REQUIRE (mid (dead) < mid (plain));

    const auto bright = describeRoom ("bright room");
    const auto dark = describeRoom ("dark room");
    REQUIRE (bright.decaySeconds[7] / bright.decaySeconds[0] > dark.decaySeconds[7] / dark.decaySeconds[0]);

    const auto boomy = describeRoom ("boomy cellar");
    const auto cellar = describeRoom ("cellar");
    REQUIRE (boomy.decaySeconds[0] > cellar.decaySeconds[0]);
    REQUIRE (boomy.decaySeconds[5] == Catch::Approx (cellar.decaySeconds[5]));
}

TEST_CASE ("Where the listener is", "[describe]")
{
    REQUIRE (describeRoom ("church, from the back").distancePercent > 70.0f);
    REQUIRE (describeRoom ("church, close up").distancePercent < 20.0f);
    REQUIRE (describeRoom ("church").distancePercent == 50.0f);
}

TEST_CASE ("The first kind named wins, and the longest name at a place", "[describe]")
{
    REQUIRE (describeRoom ("concert hall").place == "a concert hall");
    REQUIRE (describeRoom ("village hall").place == "a concert hall");
    REQUIRE (describeRoom ("a chapel beside a cathedral").place == "a chapel");
    REQUIRE (describeRoom ("car park").kind == "garage");
    REQUIRE (describeRoom ("Multi-storey car park").kind == "garage");
    REQUIRE (describeRoom ("BATHROOM").kind == "room");
    REQUIRE (describeRoom ("two bathrooms").kind == "room");
}

TEST_CASE ("Every kind of room describes itself", "[describe]")
{
    const auto rooms = describableRooms();
    REQUIRE (rooms.size() > 60);

    for (const auto& name : rooms)
    {
        const auto room = describeRoom (name);
        INFO (name);
        REQUIRE (room.knownRoom);
        REQUIRE (room.decaySeconds[4] >= 0.1f);
        REQUIRE (room.decaySeconds[4] <= 20.0f);
        REQUIRE ((room.kind == "hall" || room.kind == "chapel" || room.kind == "garage" || room.kind == "room"
                  || room.kind == "outdoor" || room.kind == "underground"));
    }

    const auto materials = describableMaterials();
    REQUIRE (materials.size() > 25);

    for (const auto& name : materials)
    {
        const auto room = describeRoom ("room of " + name);
        INFO (name);
        REQUIRE (understood (room, name));
    }
}

TEST_CASE ("Long descriptions get a short name", "[describe]")
{
    const auto room = describeRoom ("a very large and very reverberant Victorian railway station concourse with a glass roof and tiled walls");
    REQUIRE (room.name.size() <= 48);
    REQUIRE_THAT (room.name, ContainsSubstring ("..."));
    REQUIRE (room.kind == "garage");
    REQUIRE (understood (room, "glass"));
}

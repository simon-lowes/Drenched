#include <drenched/SceneNet.h>
#include <drenched/RoomDescription.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace drenched;
using Catch::Matchers::ContainsSubstring;

namespace
{
    std::vector<std::uint8_t> readFile (const std::string& path)
    {
        std::ifstream in (path, std::ios::binary);
        REQUIRE (in.good());
        return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
    }

    const SceneNet& model()
    {
        static const auto bytes = readFile (DRENCHED_SCENE_MODEL);
        static const SceneNet net (bytes.data(), bytes.size());
        return net;
    }

    std::vector<std::uint8_t> photo (const char* name)
    {
        const auto bytes = readFile (std::string (DRENCHED_TEST_DATA_DIR) + "/scene/" + name + ".rgb");
        REQUIRE (bytes.size() == static_cast<std::size_t> (SceneNet::side * SceneNet::side * 3));
        return bytes;
    }

    std::string top (const SceneReading& r) { return r.categories.empty() ? "" : r.categories.front().name; }
}

TEST_CASE ("The scene model reads and knows its labels", "[scene]")
{
    const auto& net = model();
    REQUIRE (net.categoryNames().size() == 365);
    REQUIRE (net.attributeNames().size() == 102);
    REQUIRE (net.categoryNames()[0] == "airfield");
    REQUIRE (net.categoryNames()[364] == "zen_garden");
    REQUIRE (net.attributeNames()[0] == "boating");
}

TEST_CASE ("A file that is not a scene model is refused", "[scene]")
{
    const std::vector<std::uint8_t> junk (100, 0x41);
    REQUIRE_THROWS_AS (SceneNet (junk.data(), junk.size()), SceneNetProblem);
    REQUIRE_THROWS_AS (SceneNet (junk.data(), 0), SceneNetProblem);

    auto cut = readFile (DRENCHED_SCENE_MODEL);
    cut.resize (cut.size() / 2);
    REQUIRE_THROWS_AS (SceneNet (cut.data(), cut.size()), SceneNetProblem);
}

TEST_CASE ("The scene model sees what each test photograph shows", "[scene]")
{
    // These are our own renders and screenshots; what the published network says of
    // them (tools/scene/convert_places365.py's source) is what this port should say.
    const auto& net = model();

    const auto chapel = net.look (photo ("chapel").data());
    REQUIRE (top (chapel) == "church/indoor");
    REQUIRE (chapel.categories.front().probability > 0.8f);
    REQUIRE (chapel.categories.front().indoors);
    REQUIRE (chapel.categories.size() == 5);

    const auto garage = net.look (photo ("garage").data());
    REQUIRE (top (garage) == "parking_garage/indoor");
    REQUIRE (garage.categories.front().probability > 0.8f);

    const auto outdoor = net.look (photo ("outdoor").data());
    REQUIRE (top (outdoor) == "courtyard");
    REQUIRE_FALSE (outdoor.categories.front().indoors);

    const auto underground = net.look (photo ("underground").data());
    REQUIRE ((top (underground) == "catacomb" || top (underground) == "corridor"));

    // probabilities add up to one over the five shown or fewer
    float sum = 0.0f;

    for (const auto& c : chapel.categories)
        sum += c.probability;

    REQUIRE (sum <= 1.001f);
    REQUIRE (sum > 0.9f);

    // attributes: enclosed, man-made, no horizon for a church interior
    bool enclosed = false;

    for (const auto& a : chapel.attributes)
        if (a.name == "enclosed area")
            enclosed = true;

    REQUIRE (enclosed);
}

TEST_CASE ("The same photograph always reads the same", "[scene]")
{
    const auto& net = model();
    const auto p = photo ("hall");
    const auto a = net.look (p.data());
    const auto b = net.look (p.data());
    REQUIRE (a.categories.size() == b.categories.size());

    for (std::size_t i = 0; i < a.categories.size(); ++i)
    {
        REQUIRE (a.categories[i].name == b.categories[i].name);
        REQUIRE (a.categories[i].probability == b.categories[i].probability);
    }
}

TEST_CASE ("What the model saw becomes words the room describer knows", "[scene]")
{
    const auto& net = model();

    const auto chapel = sceneWords (net.look (photo ("chapel").data()));
    REQUIRE_THAT (chapel, ContainsSubstring ("church"));
    REQUIRE (describeRoom (chapel).kind == "chapel");

    const auto garage = sceneWords (net.look (photo ("garage").data()));
    REQUIRE (describeRoom (garage).kind == "garage");

    const auto outdoor = sceneWords (net.look (photo ("outdoor").data()));
    REQUIRE (describeRoom (outdoor).kind == "outdoor");

    // every category the model knows gives the describer a room it knows
    for (const auto& name : net.categoryNames())
    {
        SceneReading reading;
        reading.categories.push_back ({ name, 1.0f, true });
        const auto words = sceneWords (reading);
        INFO (name << " -> " << words);
        REQUIRE (describeRoom (words).knownRoom);
    }

    // materials come from the attributes, strongest first, a few at most
    SceneReading reading;
    reading.categories.push_back ({ "bedroom", 1.0f, true });
    reading.attributes = { { "carpet", 0.8f }, { "wood", 0.5f }, { "rock", 0.3f }, { "glass", 0.2f }, { "brick", 0.01f } };
    const auto words = sceneWords (reading);
    REQUIRE (words == "bedroom, carpet, wood, stone");

    SceneReading nothing;
    REQUIRE (sceneWords (nothing) == "room");
}

TEST_CASE ("Photographs of any size scale to the model's", "[scene]")
{
    // a 2 x 2 checker scaled up repeats its pixels; scaled down to one, it averages
    const std::uint8_t checker[] { 255, 0, 0, 0, 0, 255,
                                   0, 255, 0, 255, 255, 255 };
    const auto up = scaleForSceneNet (checker, 2, 2, 3, 6);
    REQUIRE (up.size() == static_cast<std::size_t> (SceneNet::side * SceneNet::side * 3));
    REQUIRE (up[0] == 255);
    REQUIRE (up[1] == 0);
    REQUIRE (up[(SceneNet::side - 1) * 3] == 0);
    REQUIRE (up[(SceneNet::side - 1) * 3 + 2] == 255);
    REQUIRE (up[(SceneNet::side * (SceneNet::side - 1)) * 3 + 1] == 255);

    // a big flat picture with a row stride and an alpha channel comes out flat
    std::vector<std::uint8_t> big (static_cast<std::size_t> (1000 * 700 * 4), 0);

    for (std::size_t i = 0; i < big.size(); i += 4)
    {
        big[i] = 10;
        big[i + 1] = 20;
        big[i + 2] = 30;
        big[i + 3] = 255;
    }

    const auto down = scaleForSceneNet (big.data(), 1000, 700, 4, 4000);

    for (std::size_t i = 0; i < down.size(); i += 3)
    {
        REQUIRE (down[i] == 10);
        REQUIRE (down[i + 1] == 20);
        REQUIRE (down[i + 2] == 30);
    }

    REQUIRE (scaleForSceneNet (nullptr, 0, 0, 3, 0).size() == down.size());
}

TEST_CASE ("A look takes a few seconds at most", "[scene][.timing]")
{
    const auto& net = model();
    const auto p = photo ("room");
    const auto start = std::chrono::steady_clock::now();
    net.look (p.data());
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    INFO ("one look took " << seconds << " s");
    REQUIRE (seconds < 10.0);
}

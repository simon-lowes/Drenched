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

namespace
{
    // A small scene model file, written as tools/scene/convert_places365.py writes one, of
    // the convolutions given: what the reader must refuse when they don't fit together.
    struct ModelWriter
    {
        std::vector<std::uint8_t> bytes;

        void u32 (std::uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back (static_cast<std::uint8_t> ((v >> (8 * i)) & 255)); }
        void raw (const void* p, std::size_t n) { const auto* c = static_cast<const std::uint8_t*> (p); bytes.insert (bytes.end(), c, c + n); }
        void text (const std::string& s) { u32 (static_cast<std::uint32_t> (s.size())); raw (s.data(), s.size()); }
        void floats (float v, std::size_t n) { for (std::size_t i = 0; i < n; ++i) raw (&v, sizeof (v)); }
    };

    struct Layer { std::string name; std::uint32_t out, in, size, stride, pad; };

    std::vector<std::uint8_t> smallModel (const std::vector<Layer>& layers, std::uint32_t features)
    {
        ModelWriter w;
        w.raw ("DRSN", 4);
        w.u32 (1);
        w.u32 (static_cast<std::uint32_t> (layers.size()));
        w.u32 (1); // classes
        w.u32 (1); // attributes
        w.text ("room");
        w.text ("carpet");
        const std::uint8_t indoors = 1;
        w.raw (&indoors, 1);

        for (const auto& l : layers)
        {
            w.text (l.name);
            w.u32 (l.out); w.u32 (l.in); w.u32 (l.size); w.u32 (l.stride); w.u32 (l.pad);
            const std::vector<std::int8_t> q (static_cast<std::size_t> (l.out) * l.in * l.size * l.size, 1);
            w.raw (q.data(), q.size());
            w.floats (0.01f, l.out);
            w.floats (0.0f, l.out);
        }

        w.u32 (1);
        w.u32 (features);
        const std::vector<std::int8_t> fc (features, 1);
        w.raw (fc.data(), fc.size());
        w.floats (1.0f, 1);
        w.floats (0.0f, 1);
        w.floats (1.0f, features);
        return w.bytes;
    }

    // A picture of `width` x `height` with each pixel saying where it is.
    std::vector<std::uint8_t> numbered (int width, int height)
    {
        std::vector<std::uint8_t> rgb (static_cast<std::size_t> (width * height * 3));

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                auto* p = rgb.data() + static_cast<std::size_t> ((y * width + x) * 3);
                p[0] = static_cast<std::uint8_t> (x);
                p[1] = static_cast<std::uint8_t> (y);
                p[2] = 7;
            }

        return rgb;
    }

    std::pair<int, int> at (const std::vector<std::uint8_t>& rgb, int width, int x, int y)
    {
        const auto* p = rgb.data() + static_cast<std::size_t> ((y * width + x) * 3);
        return { p[0], p[1] };
    }

    void bigEndian16 (std::vector<std::uint8_t>& b, unsigned v) { b.push_back (static_cast<std::uint8_t> (v >> 8)); b.push_back (static_cast<std::uint8_t> (v & 255)); }

    // The first bytes of a JPEG: an Exif segment with the orientation (in either byte
    // order), another segment, and the frame header with its size.
    std::vector<std::uint8_t> jpegHeader (unsigned width, unsigned height, int orientation, bool littleEndian)
    {
        std::vector<std::uint8_t> b { 0xFF, 0xD8 };

        if (orientation > 0)
        {
            std::vector<std::uint8_t> tiff;
            const auto u16 = [&] (unsigned v) { if (littleEndian) { tiff.push_back (v & 255); tiff.push_back (static_cast<std::uint8_t> (v >> 8)); } else bigEndian16 (tiff, v); };
            const auto u32 = [&] (unsigned v) { if (littleEndian) { u16 (v & 0xFFFF); u16 (v >> 16); } else { u16 (v >> 16); u16 (v & 0xFFFF); } };
            tiff.push_back (littleEndian ? 'I' : 'M');
            tiff.push_back (littleEndian ? 'I' : 'M');
            u16 (42);
            u32 (8);
            u16 (2);                                                 // two entries
            u16 (0x010F); u16 (2); u32 (4); u32 (0);                 // Make, not the one
            u16 (0x0112); u16 (3); u32 (1); u16 (static_cast<unsigned> (orientation)); u16 (0);
            u32 (0);

            b.push_back (0xFF); b.push_back (0xE1);
            bigEndian16 (b, static_cast<unsigned> (2 + 6 + tiff.size()));
            b.insert (b.end(), { 'E', 'x', 'i', 'f', 0, 0 });
            b.insert (b.end(), tiff.begin(), tiff.end());
        }

        b.insert (b.end(), { 0xFF, 0xDB, 0x00, 0x04, 0x00, 0x00 }); // a table, skipped
        b.insert (b.end(), { 0xFF, 0xC2, 0x00, 0x11, 0x08 });       // progressive frame header
        bigEndian16 (b, height);
        bigEndian16 (b, width);
        b.insert (b.end(), { 0x03, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1 });
        b.insert (b.end(), { 0xFF, 0xDA, 0x00, 0x02 });
        return b;
    }

    std::vector<std::uint8_t> pngHeader (std::uint32_t width, std::uint32_t height)
    {
        std::vector<std::uint8_t> b { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R' };

        for (const auto v : { width, height })
            for (int s = 24; s >= 0; s -= 8)
                b.push_back (static_cast<std::uint8_t> ((v >> s) & 255));

        b.insert (b.end(), { 8, 2, 0, 0, 0, 0, 0, 0, 0 });
        return b;
    }
}

TEST_CASE ("A model file whose layers don't fit together is refused before it is used", "[scene]")
{
    // A well formed one is read, and looks.
    const auto good = smallModel ({ { "conv1", 8, 3, 3, 2, 1 }, { "layer1.0.conv1", 8, 8, 3, 1, 1 }, { "layer1.0.conv2", 8, 8, 3, 1, 1 },
                                    { "layer2.0.conv1", 16, 8, 3, 2, 1 }, { "layer2.0.conv2", 16, 16, 3, 1, 1 }, { "layer2.0.downsample", 16, 8, 1, 2, 0 } }, 16);
    const SceneNet net (good.data(), good.size());
    const std::vector<std::uint8_t> grey (static_cast<std::size_t> (SceneNet::side * SceneNet::side * 3), 128);
    REQUIRE (net.look (grey.data()).categories.size() == 1);

    // A block that widens without a downsample: the shortcut would be read past its end.
    const auto widen = smallModel ({ { "conv1", 8, 3, 3, 1, 1 }, { "layer1.0.conv1", 8, 8, 3, 1, 1 }, { "layer1.0.conv2", 16, 8, 3, 1, 1 } }, 16);
    REQUIRE_THROWS_AS (SceneNet (widen.data(), widen.size()), SceneNetProblem);

    // A block that halves its maps without a downsample.
    const auto halve = smallModel ({ { "conv1", 8, 3, 3, 1, 1 }, { "layer1.0.conv1", 8, 8, 3, 2, 1 }, { "layer1.0.conv2", 8, 8, 3, 1, 1 } }, 8);
    REQUIRE_THROWS_AS (SceneNet (halve.data(), halve.size()), SceneNetProblem);

    // A downsample taking more channels than the block is given.
    const auto down = smallModel ({ { "conv1", 8, 3, 3, 1, 1 }, { "layer1.0.conv1", 8, 8, 3, 1, 1 }, { "layer1.0.conv2", 8, 8, 3, 1, 1 },
                                    { "layer1.0.downsample", 8, 64, 1, 1, 0 } }, 8);
    REQUIRE_THROWS_AS (SceneNet (down.data(), down.size()), SceneNetProblem);

    // A pad that is negative as a signed number.
    const auto pad = smallModel ({ { "conv1", 8, 3, 3, 1, 0x80000000u }, { "layer1.0.conv1", 8, 8, 3, 1, 1 }, { "layer1.0.conv2", 8, 8, 3, 1, 1 } }, 8);
    REQUIRE_THROWS_AS (SceneNet (pad.data(), pad.size()), SceneNetProblem);

    // A kernel larger than the map it would run over.
    const auto shrink = smallModel ({ { "conv1", 8, 3, 15, 4, 0 }, { "layer1.0.conv1", 8, 8, 15, 4, 0 }, { "layer1.0.conv2", 8, 8, 15, 4, 0 } }, 8);
    REQUIRE_THROWS_AS (SceneNet (shrink.data(), shrink.size()), SceneNetProblem);

    // A layer that says it is huge in a file that ends straight after saying so: refused
    // for being cut short, without first making room for 3.8 GB of weights.
    ModelWriter w;
    w.raw ("DRSN", 4);
    w.u32 (1); w.u32 (2); w.u32 (1); w.u32 (1);
    w.text ("room"); w.text ("carpet");
    const std::uint8_t indoors = 1;
    w.raw (&indoors, 1);
    w.text ("conv1"); w.u32 (4096); w.u32 (3); w.u32 (3); w.u32 (1); w.u32 (1);
    const std::vector<std::int8_t> q (4096 * 3 * 9, 1);
    w.raw (q.data(), q.size());
    w.floats (1.0f, 2 * 4096);
    w.text ("layer1.0.conv1"); w.u32 (4096); w.u32 (4096); w.u32 (15); w.u32 (1); w.u32 (0);
    REQUIRE_THROWS_WITH (SceneNet (w.bytes.data(), w.bytes.size()), ContainsSubstring ("cut short"));
}

TEST_CASE ("A picture's header says its size and which way up it is", "[scene]")
{
    const auto png = readPictureHeader (pngHeader (4000, 3000).data(), 33);
    REQUIRE (png.format == PictureHeader::Format::png);
    REQUIRE (png.width == 4000);
    REQUIRE (png.height == 3000);
    REQUIRE (pictureCanBeRead (png));

    // A few bytes that say a million pixels a side: not decoded.
    const auto bomb = readPictureHeader (pngHeader (1000000, 1000000).data(), 33);
    REQUIRE (bomb.width == 1000000);
    REQUIRE_FALSE (pictureCanBeRead (bomb));
    REQUIRE_FALSE (pictureCanBeRead (readPictureHeader (pngHeader (12001, 10).data(), 33)));
    REQUIRE_FALSE (pictureCanBeRead (readPictureHeader (pngHeader (10000, 7000).data(), 33))); // 70 MP
    REQUIRE_FALSE (pictureCanBeRead (readPictureHeader (pngHeader (0, 10).data(), 33)));
    REQUIRE (pictureCanBeRead (readPictureHeader (pngHeader (12000, 5000).data(), 33)));

    for (const auto little : { true, false })
    {
        for (int orientation = 1; orientation <= 8; ++orientation)
        {
            const auto bytes = jpegHeader (4032, 3024, orientation, little);
            const auto jpeg = readPictureHeader (bytes.data(), bytes.size());
            REQUIRE (jpeg.format == PictureHeader::Format::jpeg);
            REQUIRE (jpeg.width == 4032);
            REQUIRE (jpeg.height == 3024);
            REQUIRE (jpeg.orientation == orientation);
            REQUIRE (pictureCanBeRead (jpeg));
        }
    }

    const auto noExif = jpegHeader (65500, 65500, 0, true);
    const auto big = readPictureHeader (noExif.data(), noExif.size());
    REQUIRE (big.orientation == 1);
    REQUIRE (big.width == 65500);
    REQUIRE_FALSE (pictureCanBeRead (big));

    // GIF: the first image's own size counts, not only the screen's.
    const std::vector<std::uint8_t> gif { 'G', 'I', 'F', '8', '9', 'a', 10, 0, 10, 0, 0x80, 0, 0,
                                          0, 0, 0, 255, 255, 255,                         // a palette of two
                                          0x21, 0xF9, 4, 0, 0, 0, 0, 0,                    // an extension
                                          0x2C, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0 };   // an image of 65535 x 65535
    const auto g = readPictureHeader (gif.data(), gif.size());
    REQUIRE (g.format == PictureHeader::Format::gif);
    REQUIRE (g.width == 65535);
    REQUIRE_FALSE (pictureCanBeRead (g));

    // Anything else, and every cut of a header, reads as nothing without reading past it.
    const std::vector<std::uint8_t> junk (64, 0x41);
    REQUIRE (readPictureHeader (junk.data(), junk.size()).format == PictureHeader::Format::unknown);
    REQUIRE_FALSE (pictureCanBeRead (readPictureHeader (nullptr, 0)));

    for (const auto& whole : { jpegHeader (100, 100, 6, true), jpegHeader (100, 100, 3, false), pngHeader (100, 100), gif })
    {
        for (std::size_t n = 0; n < whole.size(); ++n)
        {
            const std::vector<std::uint8_t> cut (whole.begin(), whole.begin() + static_cast<std::ptrdiff_t> (n));
            const auto header = readPictureHeader (cut.empty() ? nullptr : cut.data(), cut.size());
            REQUIRE (header.orientation >= 1);
            REQUIRE (header.orientation <= 8);
        }
    }
}

TEST_CASE ("A picture stored on its side is turned upright", "[scene]")
{
    // 3 wide, 2 high, as stored; each orientation's seen picture, by where its corners came from.
    const auto stored = numbered (3, 2);

    struct Expected { int orientation, width, height; std::pair<int, int> topLeft, topRight, bottomLeft; };

    for (const auto& e : { Expected { 1, 3, 2, { 0, 0 }, { 2, 0 }, { 0, 1 } },
                           Expected { 2, 3, 2, { 2, 0 }, { 0, 0 }, { 2, 1 } },
                           Expected { 3, 3, 2, { 2, 1 }, { 0, 1 }, { 2, 0 } },
                           Expected { 4, 3, 2, { 0, 1 }, { 2, 1 }, { 0, 0 } },
                           Expected { 5, 2, 3, { 0, 0 }, { 0, 1 }, { 2, 0 } },
                           Expected { 6, 2, 3, { 0, 1 }, { 0, 0 }, { 2, 1 } },
                           Expected { 7, 2, 3, { 2, 1 }, { 2, 0 }, { 0, 1 } },
                           Expected { 8, 2, 3, { 2, 0 }, { 2, 1 }, { 0, 0 } } })
    {
        INFO ("orientation " << e.orientation);
        int width = 3, height = 2;
        const auto seen = orientPicture (stored.data(), width, height, e.orientation);
        REQUIRE (width == e.width);
        REQUIRE (height == e.height);
        REQUIRE (seen.size() == stored.size());
        REQUIRE (at (seen, width, 0, 0) == e.topLeft);
        REQUIRE (at (seen, width, width - 1, 0) == e.topRight);
        REQUIRE (at (seen, width, 0, height - 1) == e.bottomLeft);
    }

    int width = 3, height = 2;
    REQUIRE (orientPicture (stored.data(), width, height, 0) == stored); // not a tag: as stored
    REQUIRE (width == 3);
}

TEST_CASE ("A photograph a phone stored on its side reads as the room once turned upright", "[scene]")
{
    // The chapel as a phone stores a portrait shot (a quarter turn anticlockwise, with
    // orientation 6): read as stored the model sees a staircase; turned, the church.
    const auto& net = model();
    const auto upright = photo ("chapel");
    constexpr int side = SceneNet::side;
    std::vector<std::uint8_t> stored (upright.size());

    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            for (int c = 0; c < 3; ++c)
                stored[static_cast<std::size_t> (((side - 1 - x) * side + y) * 3 + c)] = upright[static_cast<std::size_t> ((y * side + x) * 3 + c)];

    REQUIRE (top (net.look (stored.data())) != "church/indoor");

    int width = side, height = side;
    const auto turned = orientPicture (stored.data(), width, height, 6);
    REQUIRE (turned == upright);
    REQUIRE (top (net.look (turned.data())) == "church/indoor");
}

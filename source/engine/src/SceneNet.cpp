#include <drenched/SceneNet.h>
#include <drenched/RoomDescription.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace drenched
{

namespace
{

// One convolution of the file: 8-bit weights, a scale per output channel, and the
// batch normalisation after it folded into a bias.
struct Convolution
{
    std::string name;
    int outChannels = 0, inChannels = 0, size = 0, stride = 0, pad = 0;
    std::vector<std::int8_t> weights; // outChannels x inChannels x size x size
    std::vector<float> scale, bias;   // per output channel
};

struct Reader
{
    const std::uint8_t* data;
    std::size_t size, at = 0;

    /** Throws unless n more bytes are there: called before anything is made to hold
        them, so a cut or broken file is never answered with a huge allocation. */
    void need (std::size_t n) const
    {
        if (n > size - at)
            throw SceneNetProblem ("the scene model file is cut short");
    }

    void take (void* out, std::size_t n)
    {
        need (n);
        std::memcpy (out, data + at, n);
        at += n;
    }

    std::uint32_t u32()
    {
        std::uint8_t b[4];
        take (b, 4);
        return static_cast<std::uint32_t> (b[0]) | (static_cast<std::uint32_t> (b[1]) << 8)
             | (static_cast<std::uint32_t> (b[2]) << 16) | (static_cast<std::uint32_t> (b[3]) << 24);
    }

    int count (std::uint32_t most)
    {
        const auto n = u32();

        if (n == 0 || n > most)
            throw SceneNetProblem ("the scene model file has a size that makes no sense");

        return static_cast<int> (n);
    }

    std::vector<float> floats (std::size_t n)
    {
        if (n > (size - at) / sizeof (float))
            throw SceneNetProblem ("the scene model file is cut short");

        std::vector<float> out (n);
        take (out.data(), n * sizeof (float)); // little-endian file, little-endian hosts

        for (const auto f : out)
            if (! std::isfinite (f))
                throw SceneNetProblem ("the scene model file holds a value that is not a number");

        return out;
    }

    std::vector<std::string> names (int n)
    {
        const auto length = count (1u << 20);
        need (static_cast<std::size_t> (length));
        std::string blob (static_cast<std::size_t> (length), '\0');
        take (blob.data(), blob.size());

        std::vector<std::string> out;
        std::size_t start = 0;

        while (start <= blob.size())
        {
            const auto end = blob.find ('\n', start);
            out.push_back (blob.substr (start, end == std::string::npos ? std::string::npos : end - start));

            if (end == std::string::npos)
                break;

            start = end + 1;
        }

        if (static_cast<int> (out.size()) != n)
            throw SceneNetProblem ("the scene model file names the wrong number of things");

        return out;
    }
};

// A stack of feature maps, channel by channel.
struct Maps
{
    int channels = 0, height = 0, width = 0;
    std::vector<float> values;

    float* plane (int c) noexcept { return values.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (height * width); }
    const float* plane (int c) const noexcept { return values.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (height * width); }
};

} // namespace

struct SceneNet::Model
{
    std::vector<Convolution> convolutions;
    int classes = 0, attributes = 0, features = 0;
    std::vector<std::int8_t> fcWeights;      // classes x features
    std::vector<float> fcScale, fcBias;      // per class
    std::vector<float> attributeWeights;     // attributes x features
    std::vector<std::string> categoryNames, attributeNames;
    std::vector<bool> indoors;

    // scratch for look(): kept between layers so each look allocates little
    mutable std::vector<float> columns, rowWeights;

    void read (const std::uint8_t* data, std::size_t size)
    {
        Reader in { data, size };

        char magic[4];
        in.take (magic, 4);

        if (std::memcmp (magic, "DRSN", 4) != 0)
            throw SceneNetProblem ("not a scene model file");

        if (in.u32() != 1)
            throw SceneNetProblem ("a scene model file of a version this build does not read");

        const auto layers = in.count (64);
        classes = in.count (4096);
        attributes = in.count (4096);

        categoryNames = in.names (classes);
        attributeNames = in.names (attributes);

        std::vector<std::uint8_t> io (static_cast<std::size_t> (classes));
        in.take (io.data(), io.size());
        indoors.assign (io.begin(), io.end());

        for (auto& flag : io)
            if (flag > 1)
                throw SceneNetProblem ("the scene model file's indoor flags are not flags");

        int channels = 3;

        for (int i = 0; i < layers; ++i)
        {
            Convolution c;
            const auto nameLength = in.count (256);
            c.name.assign (static_cast<std::size_t> (nameLength), '\0');
            in.take (c.name.data(), c.name.size());

            c.outChannels = in.count (4096);
            c.inChannels = in.count (4096);
            c.size = in.count (15);
            c.stride = in.count (4);
            const auto pad = in.u32();

            if (pad > 7)
                throw SceneNetProblem ("the scene model file pads a convolution more than makes sense");

            c.pad = static_cast<int> (pad);

            // Each layer takes the last one's channels, except a downsample, which
            // takes the block's input like its first convolution.
            const auto downsample = c.name.find ("downsample") != std::string::npos;

            if (! downsample && c.inChannels != channels)
                throw SceneNetProblem ("the scene model file's convolutions do not fit together");

            const auto n = static_cast<std::size_t> (c.outChannels) * static_cast<std::size_t> (c.inChannels)
                         * static_cast<std::size_t> (c.size * c.size);
            in.need (n);
            c.weights.resize (n);
            in.take (c.weights.data(), n);
            c.scale = in.floats (static_cast<std::size_t> (c.outChannels));
            c.bias = in.floats (static_cast<std::size_t> (c.outChannels));

            if (! downsample)
                channels = c.outChannels;

            convolutions.push_back (std::move (c));
        }

        const auto fcRows = in.count (4096);
        features = in.count (8192);

        if (fcRows != classes || features != channels)
            throw SceneNetProblem ("the scene model file's classifier does not fit its features");

        in.need (static_cast<std::size_t> (classes) * static_cast<std::size_t> (features));
        fcWeights.resize (static_cast<std::size_t> (classes) * static_cast<std::size_t> (features));
        in.take (fcWeights.data(), fcWeights.size());
        fcScale = in.floats (static_cast<std::size_t> (classes));
        fcBias = in.floats (static_cast<std::size_t> (classes));
        attributeWeights = in.floats (static_cast<std::size_t> (attributes) * static_cast<std::size_t> (features));

        if (in.at != in.size)
            throw SceneNetProblem ("the scene model file has more in it than expected");

        // The blocks the forward pass expects: conv1, then pairs with an optional downsample.
        if (convolutions.empty() || convolutions.front().name != "conv1")
            throw SceneNetProblem ("the scene model file does not start with the first convolution");

        // And every map the forward pass makes fits what reads it: each convolution's
        // input has the channels it takes and is at least its kernel, and a block's
        // shortcut (its input, or the downsample of it) is the shape of what it adds to.
        // look() relies on this to read inside every map.
        struct Shape
        {
            int channels, height, width;
            bool operator== (const Shape& o) const noexcept { return channels == o.channels && height == o.height && width == o.width; }
        };

        const auto through = [] (const Shape& in, const Convolution& c)
        {
            if (c.inChannels != in.channels)
                throw SceneNetProblem ("the scene model file's convolutions do not fit together");

            const auto padded = std::min (in.height, in.width) + 2 * c.pad;

            if (padded < c.size)
                throw SceneNetProblem ("the scene model file's maps get smaller than its kernels");

            return Shape { c.outChannels, (in.height + 2 * c.pad - c.size) / c.stride + 1, (in.width + 2 * c.pad - c.size) / c.stride + 1 };
        };

        auto shape = through ({ 3, side, side }, convolutions.front());

        for (std::size_t i = 1; i < convolutions.size();)
        {
            if (i + 1 >= convolutions.size() || convolutions[i].name.find (".conv1") == std::string::npos
                || convolutions[i + 1].name.find (".conv2") == std::string::npos)
                throw SceneNetProblem ("the scene model file's blocks are not residual blocks");

            const auto added = through (through (shape, convolutions[i]), convolutions[i + 1]);
            i += 2;

            auto shortcut = shape;

            if (i < convolutions.size() && convolutions[i].name.find ("downsample") != std::string::npos)
                shortcut = through (shape, convolutions[i++]);

            if (! (shortcut == added))
                throw SceneNetProblem ("the scene model file's blocks do not add up");

            shape = added;
        }

        if (shape.channels != features)
            throw SceneNetProblem ("the scene model file's classifier does not fit its features");
    }

    /** out = convolution (in), with its bias, as im2col then a blocked matrix product. */
    void convolve (const Maps& in, const Convolution& c, Maps& out) const
    {
        const auto k = c.size;
        out.channels = c.outChannels;
        out.height = (in.height + 2 * c.pad - k) / c.stride + 1;
        out.width = (in.width + 2 * c.pad - k) / c.stride + 1;
        const auto n = out.height * out.width;
        const auto rows = c.inChannels * k * k;
        out.values.assign (static_cast<std::size_t> (c.outChannels) * static_cast<std::size_t> (n), 0.0f);

        // im2col: one row per (input channel, kernel y, kernel x), one column per output pixel.
        columns.resize (static_cast<std::size_t> (rows) * static_cast<std::size_t> (n));

        for (int ci = 0; ci < c.inChannels; ++ci)
        {
            const auto* src = in.plane (ci);

            for (int ky = 0; ky < k; ++ky)
            {
                for (int kx = 0; kx < k; ++kx)
                {
                    auto* dst = columns.data() + static_cast<std::size_t> ((ci * k + ky) * k + kx) * static_cast<std::size_t> (n);

                    for (int oy = 0; oy < out.height; ++oy)
                    {
                        const auto y = oy * c.stride - c.pad + ky;
                        auto* row = dst + static_cast<std::size_t> (oy * out.width);

                        if (y < 0 || y >= in.height)
                        {
                            std::fill (row, row + out.width, 0.0f);
                            continue;
                        }

                        const auto* srcRow = src + static_cast<std::size_t> (y * in.width);

                        for (int ox = 0; ox < out.width; ++ox)
                        {
                            const auto x = ox * c.stride - c.pad + kx;
                            row[ox] = (x < 0 || x >= in.width) ? 0.0f : srcRow[x];
                        }
                    }
                }
            }
        }

        // The product, eight output channels at a time over tiles of pixels, so every
        // column value read serves eight accumulations that stay in cache.
        constexpr int block = 8, tile = 512;
        rowWeights.resize (static_cast<std::size_t> (block) * static_cast<std::size_t> (rows));

        for (int oc0 = 0; oc0 < c.outChannels; oc0 += block)
        {
            const auto count = std::min (block, c.outChannels - oc0);

            std::fill (rowWeights.begin(), rowWeights.end(), 0.0f);

            for (int b = 0; b < count; ++b)
            {
                const auto* q = c.weights.data() + static_cast<std::size_t> (oc0 + b) * static_cast<std::size_t> (rows);
                const auto s = c.scale[static_cast<std::size_t> (oc0 + b)];
                auto* w = rowWeights.data() + static_cast<std::size_t> (b) * static_cast<std::size_t> (rows);

                for (int p = 0; p < rows; ++p)
                    w[p] = s * static_cast<float> (q[p]);
            }

            for (int n0 = 0; n0 < n; n0 += tile)
            {
                const auto width = std::min (tile, n - n0);

                // Accumulate in a local block: the compiler can see it never overlaps the
                // columns, so the inner loop vectorises.
                alignas (32) float acc[block][tile];

                for (int b = 0; b < block; ++b)
                    std::fill (acc[b], acc[b] + width, b < count ? c.bias[static_cast<std::size_t> (oc0 + b)] : 0.0f);

                for (int p = 0; p < rows; ++p)
                {
                    const auto* col = columns.data() + static_cast<std::size_t> (p) * static_cast<std::size_t> (n) + n0;
                    const auto* w = rowWeights.data() + p;
                    const float w0 = w[0], w1 = w[rows], w2 = w[2 * rows], w3 = w[3 * rows],
                                w4 = w[4 * rows], w5 = w[5 * rows], w6 = w[6 * rows], w7 = w[7 * rows];

                    for (int j = 0; j < width; ++j)
                    {
                        const auto v = col[j];
                        acc[0][j] += w0 * v; acc[1][j] += w1 * v; acc[2][j] += w2 * v; acc[3][j] += w3 * v;
                        acc[4][j] += w4 * v; acc[5][j] += w5 * v; acc[6][j] += w6 * v; acc[7][j] += w7 * v;
                    }
                }

                for (int b = 0; b < count; ++b)
                    std::copy (acc[b], acc[b] + width, out.plane (oc0 + b) + n0);
            }
        }
    }

    static void relu (Maps& m) noexcept
    {
        for (auto& v : m.values)
            v = std::max (v, 0.0f);
    }

    SceneReading look (const std::uint8_t* rgb) const
    {
        // The input as the network was trained: ImageNet mean and deviation per channel.
        constexpr float mean[3] { 0.485f, 0.456f, 0.406f }, deviation[3] { 0.229f, 0.224f, 0.225f };
        Maps x { 3, side, side, std::vector<float> (3 * side * side) };

        for (int c = 0; c < 3; ++c)
        {
            auto* plane = x.plane (c);

            for (int i = 0; i < side * side; ++i)
                plane[i] = (static_cast<float> (rgb[3 * i + c]) / 255.0f - mean[c]) / deviation[c];
        }

        Maps y, z, identity;
        convolve (x, convolutions[0], y);
        relu (y);
        x = std::move (y);

        for (std::size_t i = 1; i < convolutions.size();)
        {
            const auto& first = convolutions[i];
            const auto& second = convolutions[i + 1];
            const Convolution* down = i + 2 < convolutions.size() && convolutions[i + 2].name.find ("downsample") != std::string::npos
                                    ? &convolutions[i + 2] : nullptr;

            convolve (x, first, y);
            relu (y);
            convolve (y, second, z);

            if (down != nullptr)
            {
                convolve (x, *down, identity);
                const auto* id = identity.values.data();

                for (std::size_t j = 0; j < z.values.size(); ++j)
                    z.values[j] += id[j];
            }
            else
            {
                for (std::size_t j = 0; j < z.values.size(); ++j)
                    z.values[j] += x.values[j];
            }

            relu (z);
            std::swap (x, z);
            i += down != nullptr ? 3 : 2;
        }

        // Average pool to one value per channel.
        std::vector<float> feature (static_cast<std::size_t> (x.channels));
        const auto pixels = static_cast<float> (x.height * x.width);

        for (int c = 0; c < x.channels; ++c)
        {
            const auto* plane = x.plane (c);
            double sum = 0.0;

            for (int i = 0; i < x.height * x.width; ++i)
                sum += plane[i];

            feature[static_cast<std::size_t> (c)] = static_cast<float> (sum / pixels);
        }

        // Classifier and softmax.
        std::vector<float> logits (static_cast<std::size_t> (classes));

        for (int i = 0; i < classes; ++i)
        {
            const auto* w = fcWeights.data() + static_cast<std::size_t> (i) * static_cast<std::size_t> (features);
            float sum = 0.0f;

            for (int j = 0; j < features; ++j)
                sum += static_cast<float> (w[j]) * feature[static_cast<std::size_t> (j)];

            logits[static_cast<std::size_t> (i)] = fcBias[static_cast<std::size_t> (i)] + fcScale[static_cast<std::size_t> (i)] * sum;
        }

        const auto top = *std::max_element (logits.begin(), logits.end());
        double total = 0.0;

        for (auto& l : logits)
        {
            l = std::exp (l - top);
            total += l;
        }

        std::vector<int> order (static_cast<std::size_t> (classes));
        std::iota (order.begin(), order.end(), 0);
        std::partial_sort (order.begin(), order.begin() + std::min (5, classes), order.end(),
                           [&] (int a, int b) { return logits[static_cast<std::size_t> (a)] > logits[static_cast<std::size_t> (b)]; });

        SceneReading reading;

        for (int r = 0; r < std::min (5, classes); ++r)
        {
            const auto i = static_cast<std::size_t> (order[static_cast<std::size_t> (r)]);
            reading.categories.push_back ({ categoryNames[i], static_cast<float> (logits[i] / total), indoors[i] });
        }

        for (int i = 0; i < attributes; ++i)
        {
            const auto* w = attributeWeights.data() + static_cast<std::size_t> (i) * static_cast<std::size_t> (features);
            float sum = 0.0f;

            for (int j = 0; j < features; ++j)
                sum += w[j] * feature[static_cast<std::size_t> (j)];

            if (sum > 0.0f)
                reading.attributes.push_back ({ attributeNames[static_cast<std::size_t> (i)], sum });
        }

        std::stable_sort (reading.attributes.begin(), reading.attributes.end(),
                          [] (const auto& a, const auto& b) { return a.score > b.score; });
        return reading;
    }
};

SceneNet::SceneNet (const std::uint8_t* data, std::size_t size) : model (std::make_unique<Model>())
{
    model->read (data, size);
}

SceneNet::~SceneNet() = default;
SceneNet::SceneNet (SceneNet&&) noexcept = default;
SceneNet& SceneNet::operator= (SceneNet&&) noexcept = default;

SceneReading SceneNet::look (const std::uint8_t* rgb) const { return model->look (rgb); }
const std::vector<std::string>& SceneNet::categoryNames() const noexcept { return model->categoryNames; }
const std::vector<std::string>& SceneNet::attributeNames() const noexcept { return model->attributeNames; }

//==============================================================================
namespace
{

// Places365 categories whose names the room describer would not read right, or
// at all, and the words it should hear instead. The rest go by their name
// ("church/indoor" -> "church") and, failing that, a room or the open air.
constexpr const char* sceneTable[][2]
{
    { "airfield", "airfield, open air" }, { "airplane_cabin", "plane cabin" }, { "airport_terminal", "airport terminal, large, glass" },
    { "alcove", "small room" }, { "alley", "alley, brick" }, { "amphitheater", "amphitheatre outdoors, stone" },
    { "amusement_arcade", "shop, busy" }, { "amusement_park", "outdoors, busy" }, { "apartment_building/outdoor", "street" },
    { "aquarium", "aquarium, glass" }, { "aqueduct", "aqueduct, stone" }, { "arcade", "arcade covered, stone" },
    { "arch", "archway, stone" }, { "archaelogical_excavation", "outdoors, dirt" }, { "archive", "archive, books" },
    { "arena/hockey", "ice rink" }, { "arena/performance", "arena, full of people" }, { "arena/rodeo", "open air stadium, dirt" },
    { "army_base", "outdoors, concrete" }, { "art_gallery", "art gallery" }, { "art_school", "classroom" }, { "art_studio", "studio apartment" },
    { "artists_loft", "artist's loft" }, { "assembly_line", "assembly line, metal" }, { "athletic_field/outdoor", "sports field" },
    { "atrium/public", "atrium, glass, marble" }, { "attic", "attic, wood" }, { "auditorium", "auditorium large" },
    { "auto_factory", "factory, metal" }, { "auto_showroom", "showroom, glass" }, { "badlands", "desert, rock" },
    { "bakery/shop", "shop, tiles" }, { "balcony/exterior", "balcony" }, { "balcony/interior", "balcony, large room" },
    { "ball_pit", "playroom" }, { "ballroom", "ballroom, wood" }, { "bamboo_forest", "forest" }, { "bank_vault", "vault, metal" },
    { "banquet_hall", "banqueting hall" }, { "bar", "bar" }, { "barn", "barn, wood" }, { "barndoor", "barn, wood" },
    { "baseball_field", "sports field" }, { "basement", "basement, concrete" }, { "basketball_court/indoor", "basketball court, wood" },
    { "bathroom", "bathroom, tiles" }, { "bazaar/indoor", "covered market, busy" }, { "bazaar/outdoor", "street market, busy" },
    { "beach", "beach, sand" }, { "beach_house", "living room, wood" }, { "beauty_salon", "salon" }, { "bedchamber", "bedroom" },
    { "bedroom", "bedroom" }, { "beer_garden", "garden, outdoors" }, { "beer_hall", "beer hall, wood, full of people" },
    { "berth", "cabin, small" }, { "biology_laboratory", "laboratory" }, { "boardwalk", "pier, wood" }, { "boat_deck", "deck outdoors" },
    { "boathouse", "boathouse, wood" }, { "bookstore", "bookshop, books" }, { "booth/indoor", "small room, cloth" },
    { "botanical_garden", "garden" }, { "bow_window/indoor", "living room, glass" }, { "bowling_alley", "hall, wood, long" },
    { "boxing_ring", "boxing gym" }, { "bridge", "bridge" }, { "building_facade", "street" }, { "bullring", "open air stadium, sand" },
    { "burial_chamber", "burial chamber, stone" }, { "bus_interior", "bus" }, { "bus_station/indoor", "bus station" },
    { "butchers_shop", "shop, tiles" }, { "butte", "desert, rock" }, { "cabin/outdoor", "outdoors, wood" },
    { "cafeteria", "canteen, busy" }, { "campsite", "outdoors, trees" }, { "campus", "courtyard, outdoors" },
    { "canal/natural", "riverbank" }, { "canal/urban", "street, water" }, { "candy_store", "shop" }, { "canyon", "canyon, rock" },
    { "car_interior", "car interior" }, { "carrousel", "outdoors, busy" }, { "castle", "castle, stone" }, { "catacomb", "catacombs, stone" },
    { "cemetery", "outdoors, stone" }, { "chalet", "chalet, wood" }, { "chemistry_lab", "laboratory" }, { "childs_room", "bedroom, cluttered" },
    { "church/indoor", "church" }, { "church/outdoor", "courtyard, stone" }, { "classroom", "classroom" }, { "clean_room", "laboratory, empty" },
    { "cliff", "cliff" }, { "closet", "closet, cloth" }, { "clothing_store", "shop, cloth" }, { "coast", "coast" },
    { "cockpit", "cockpit" }, { "coffee_shop", "coffee shop" }, { "computer_room", "office" }, { "conference_center", "conference hall" },
    { "conference_room", "conference room" }, { "construction_site", "outdoors, concrete" }, { "corn_field", "field" },
    { "corral", "outdoors, dirt" }, { "corridor", "corridor" }, { "cottage", "cottage" }, { "courthouse", "courthouse" },
    { "courtyard", "courtyard, stone" }, { "creek", "riverbank" }, { "crevasse", "canyon, ice" }, { "crosswalk", "street" },
    { "dam", "outdoors, concrete" }, { "delicatessen", "shop, tiles" }, { "department_store", "store, large" },
    { "desert/sand", "desert, sand" }, { "desert/vegetation", "desert" }, { "desert_road", "desert, open air" },
    { "diner/outdoor", "street" }, { "dining_hall", "dining hall" }, { "dining_room", "dining room" }, { "discotheque", "nightclub, busy" },
    { "doorway/outdoor", "street" }, { "dorm_room", "dorm, cluttered" }, { "downtown", "street, glass, concrete" },
    { "dressing_room", "dressing room" }, { "driveway", "outdoors, concrete" }, { "drugstore", "shop" }, { "elevator/door", "lift, metal" },
    { "elevator_lobby", "lobby, marble" }, { "elevator_shaft", "lift shaft, concrete" }, { "embassy", "courtyard, stone" },
    { "engine_room", "engine room, metal" }, { "entrance_hall", "entrance hall" }, { "escalator/indoor", "shopping mall" },
    { "excavation", "outdoors, dirt" }, { "fabric_store", "shop, cloth" }, { "farm", "farmland" }, { "fastfood_restaurant", "restaurant, tiles" },
    { "field/cultivated", "field" }, { "field/wild", "meadow" }, { "field_road", "open country" }, { "fire_escape", "fire escape, metal" },
    { "fire_station", "garage, large, concrete" }, { "fishpond", "garden" }, { "flea_market/indoor", "covered market, cluttered" },
    { "florist_shop/indoor", "shop" }, { "food_court", "shopping mall, busy" }, { "football_field", "football pitch" },
    { "forest/broadleaf", "forest" }, { "forest_path", "forest" }, { "forest_road", "forest" }, { "formal_garden", "garden" },
    { "fountain", "plaza, stone" }, { "galley", "galley kitchen, metal" }, { "garage/indoor", "garage, concrete" },
    { "garage/outdoor", "outdoors, concrete" }, { "gas_station", "outdoors, concrete" }, { "gazebo/exterior", "gazebo" },
    { "general_store/indoor", "shop, wood" }, { "general_store/outdoor", "street" }, { "gift_shop", "shop, cluttered" },
    { "glacier", "outdoors, ice" }, { "golf_course", "golf course" }, { "greenhouse/indoor", "greenhouse, glass" },
    { "greenhouse/outdoor", "garden" }, { "grotto", "grotto, rock" }, { "gymnasium/indoor", "gymnasium, wood" },
    { "hangar/indoor", "aircraft hangar, metal" }, { "hangar/outdoor", "airfield" }, { "harbor", "harbour" },
    { "hardware_store", "shop, cluttered, metal" }, { "hayfield", "field" }, { "heliport", "helipad" }, { "highway", "open air, asphalt" },
    { "home_office", "study" }, { "home_theater", "home cinema" }, { "hospital", "courtyard, outdoors" }, { "hospital_room", "ward" },
    { "hot_spring", "outdoors, rock, water" }, { "hotel/outdoor", "street" }, { "hotel_room", "hotel room" }, { "house", "street" },
    { "hunting_lodge/outdoor", "outdoors, wood" }, { "ice_cream_parlor", "cafe, tiles" }, { "ice_floe", "outdoors, ice" },
    { "ice_shelf", "outdoors, ice" }, { "ice_skating_rink/indoor", "ice rink" }, { "ice_skating_rink/outdoor", "outdoors, ice" },
    { "iceberg", "outdoors, ice" }, { "igloo", "small room, ice" }, { "industrial_area", "outdoors, metal, concrete" },
    { "inn/outdoor", "street" }, { "islet", "coast" }, { "jacuzzi/indoor", "indoor pool, tiles, small" }, { "jail_cell", "prison cell, concrete" },
    { "japanese_garden", "garden" }, { "jewelry_shop", "boutique, glass" }, { "junkyard", "outdoors, metal, cluttered" },
    { "kasbah", "street, stone" }, { "kennel/outdoor", "outdoors, concrete" }, { "kindergarden_classroom", "classroom, cluttered" },
    { "kitchen", "kitchen" }, { "lagoon", "beach" }, { "lake/natural", "lakeside" }, { "landfill", "outdoors, dirt" },
    { "landing_deck", "deck outdoors, metal" }, { "laundromat", "shop, tiles, metal" }, { "lawn", "lawn outdoors" },
    { "lecture_room", "lecture room" }, { "legislative_chamber", "debating chamber, wood" }, { "library/indoor", "library" },
    { "library/outdoor", "street, stone" }, { "lighthouse", "coast" }, { "living_room", "living room" }, { "loading_dock", "loading dock, concrete" },
    { "lobby", "lobby" }, { "lock_chamber", "outdoors, concrete, water" }, { "locker_room", "locker room, tiles" }, { "mansion", "courtyard, stone" },
    { "manufactured_home", "living room, small" }, { "market/indoor", "indoor market, busy" }, { "market/outdoor", "street market, busy" },
    { "marsh", "field, water" }, { "martial_arts_gym", "dojo" }, { "mausoleum", "mausoleum, stone" }, { "medina", "narrow street, stone" },
    { "mezzanine", "atrium, large" }, { "moat/water", "castle, outdoors, water" }, { "mosque/outdoor", "courtyard, stone" },
    { "motel", "street" }, { "mountain", "mountains" }, { "mountain_path", "mountains" }, { "mountain_snowy", "mountains, snow" },
    { "movie_theater/indoor", "cinema" }, { "museum/indoor", "museum" }, { "museum/outdoor", "courtyard, stone" }, { "music_studio", "recording studio" },
    { "natural_history_museum", "museum, large" }, { "nursery", "nursery" }, { "nursing_home", "living room, carpet" },
    { "oast_house", "kiln, brick" }, { "ocean", "open air, water" }, { "office", "office" }, { "office_building", "street, glass" },
    { "office_cubicles", "open plan" }, { "oilrig", "outdoors, metal" }, { "operating_room", "operating theatre" },
    { "orchard", "orchard" }, { "orchestra_pit", "theatre, wood" }, { "pagoda", "courtyard, wood" }, { "palace", "palace, marble" },
    { "pantry", "pantry" }, { "park", "park" }, { "parking_garage/indoor", "parking garage, concrete" }, { "parking_garage/outdoor", "parking lot" },
    { "parking_lot", "parking lot" }, { "pasture", "pasture" }, { "patio", "patio" }, { "pavilion", "pavilion" },
    { "pet_shop", "shop" }, { "pharmacy", "shop" }, { "phone_booth", "phone box" }, { "physics_laboratory", "laboratory" },
    { "picnic_area", "park" }, { "pier", "pier" }, { "pizzeria", "restaurant, tiles" }, { "playground", "playground" },
    { "playroom", "playroom, cluttered" }, { "plaza", "plaza, stone" }, { "pond", "garden, water" }, { "porch", "porch" },
    { "promenade", "promenade" }, { "pub/indoor", "pub, wood" }, { "racecourse", "racecourse" }, { "raceway", "race track" },
    { "raft", "open air, water" }, { "railroad_track", "open air" }, { "rainforest", "rainforest" }, { "reception", "reception" },
    { "recreation_room", "family room, carpet" }, { "repair_shop", "workshop, metal" }, { "residential_neighborhood", "street" },
    { "restaurant", "restaurant" }, { "restaurant_kitchen", "restaurant kitchen, metal, tiles" }, { "restaurant_patio", "patio" },
    { "rice_paddy", "field, water" }, { "river", "riverbank" }, { "rock_arch", "canyon, rock" }, { "roof_garden", "roof terrace" },
    { "rope_bridge", "bridge, outdoors" }, { "ruin", "ruins, stone" }, { "runway", "runway" }, { "sandbox", "playground, sand" },
    { "sauna", "sauna, wood" }, { "schoolhouse", "schoolyard" }, { "science_museum", "museum" }, { "server_room", "control room, metal" },
    { "shed", "shed, wood" }, { "shoe_shop", "shop" }, { "shopfront", "street" }, { "shopping_mall/indoor", "shopping mall" },
    { "shower", "shower, tiles" }, { "ski_resort", "mountains, snow" }, { "ski_slope", "mountains, snow" }, { "sky", "open air" },
    { "skyscraper", "street, glass" }, { "slum", "narrow street, metal" }, { "snowfield", "open country, snow" }, { "soccer_field", "football pitch" },
    { "stable", "stable, wood" }, { "stadium/baseball", "open air stadium" }, { "stadium/football", "open air stadium" },
    { "stadium/soccer", "open air stadium" }, { "stage/indoor", "stage, wood" }, { "stage/outdoor", "open air theatre" },
    { "staircase", "stairwell" }, { "storage_room", "storeroom, cluttered" }, { "street", "street" },
    { "subway_station/platform", "underground station, tiles" }, { "supermarket", "supermarket" }, { "sushi_bar", "restaurant, wood" },
    { "swamp", "forest, water" }, { "swimming_hole", "outdoors, rock, water" }, { "swimming_pool/indoor", "indoor pool, tiles" },
    { "swimming_pool/outdoor", "patio, water" }, { "synagogue/outdoor", "courtyard, stone" }, { "television_room", "living room, carpet" },
    { "television_studio", "tv studio" }, { "temple/asia", "courtyard, stone" }, { "throne_room", "great hall, marble" },
    { "ticket_booth", "booth" }, { "topiary_garden", "garden" }, { "tower", "street, stone" }, { "toyshop", "shop, cluttered" },
    { "train_interior", "train" }, { "train_station/platform", "railway station" }, { "tree_farm", "orchard" }, { "tree_house", "hut, wood" },
    { "trench", "outdoors, dirt" }, { "tundra", "tundra" }, { "underwater/ocean_deep", "open air, water" }, { "utility_room", "utility room" },
    { "valley", "valley" }, { "vegetable_garden", "garden" }, { "veterinarians_office", "surgery" }, { "viaduct", "viaduct, stone" },
    { "village", "street, stone" }, { "vineyard", "field" }, { "volcano", "mountains, rock" }, { "volleyball_court/outdoor", "beach" },
    { "waiting_room", "waiting room" }, { "water_park", "outdoors, water, busy" }, { "water_tower", "outdoors" }, { "waterfall", "canyon, water" },
    { "watering_hole", "open country, water" }, { "wave", "beach, water" }, { "wet_bar", "living room, wood" }, { "wheat_field", "field" },
    { "wind_farm", "open country" }, { "windmill", "mill, wood" }, { "yard", "back yard" }, { "youth_hostel", "dormitory" },
    { "zen_garden", "garden, stone" },
};

// SUN attributes that are materials the room describer knows, as the word it knows.
constexpr const char* attributeMaterials[][2]
{
    { "carpet", "carpet" }, { "brick", "brick" }, { "tiles", "tiles" }, { "concrete", "concrete" }, { "metal", "metal" },
    { "wood", "wood" }, { "vinyl", "vinyl" }, { "cloth", "cloth" }, { "marble", "marble" }, { "glass", "glass" },
    { "rock", "stone" }, { "paper", "books" }, { "cluttered space", "cluttered" }, { "congregating", "full of people" },
};

} // namespace

std::string sceneWords (const SceneReading& reading)
{
    std::string words;

    if (! reading.categories.empty())
    {
        const auto& category = reading.categories.front();

        for (const auto& row : sceneTable)
        {
            if (category.name == row[0])
            {
                words = row[1];
                break;
            }
        }

        if (words.empty())
        {
            // By its name, without "/indoor" and with the underscores as spaces.
            auto name = category.name.substr (0, category.name.find ('/'));
            std::replace (name.begin(), name.end(), '_', ' ');

            if (describeRoom (name).knownRoom)
                words = name;
            else
                words = category.indoors ? "room" : "outdoors";
        }
    }
    else
    {
        words = "room";
    }

    int added = 0;

    for (const auto& attribute : reading.attributes)
    {
        if (attribute.score < 0.05f || added >= 3)
            break;

        for (const auto& row : attributeMaterials)
        {
            if (attribute.name == row[0] && words.find (row[1]) == std::string::npos)
            {
                words += ", ";
                words += row[1];
                ++added;
                break;
            }
        }
    }

    return words;
}

std::vector<std::uint8_t> scaleForSceneNet (const std::uint8_t* pixels, int width, int height, int channels, int rowBytes)
{
    constexpr auto side = SceneNet::side;
    std::vector<std::uint8_t> out (static_cast<std::size_t> (side * side * 3), 0);

    if (pixels == nullptr || width <= 0 || height <= 0 || channels < 3)
        return out;

    // Each output pixel averages the box of source pixels it covers; a source
    // smaller than the output repeats pixels.
    const auto span = [] (int o, int source) -> std::pair<int, int>
    {
        const auto a = static_cast<int> (static_cast<long long> (o) * source / side);
        auto b = static_cast<int> (static_cast<long long> (o + 1) * source / side);
        b = std::max (b, a + 1);
        return { a, std::min (b, source) };
    };

    for (int oy = 0; oy < side; ++oy)
    {
        const auto [y0, y1] = span (oy, height);

        for (int ox = 0; ox < side; ++ox)
        {
            const auto [x0, x1] = span (ox, width);
            std::uint32_t sum[3] {};
            std::uint32_t count = 0;

            for (int y = y0; y < y1; ++y)
            {
                const auto* row = pixels + static_cast<std::size_t> (y) * static_cast<std::size_t> (rowBytes);

                for (int x = x0; x < x1; ++x)
                {
                    const auto* p = row + static_cast<std::size_t> (x) * static_cast<std::size_t> (channels);
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    ++count;
                }
            }

            auto* o = out.data() + static_cast<std::size_t> (oy * side + ox) * 3;

            for (int c = 0; c < 3; ++c)
                o[c] = static_cast<std::uint8_t> ((sum[c] + count / 2) / std::max (count, 1u));
        }
    }

    return out;
}

//==============================================================================
namespace
{

std::uint32_t bigEndian16 (const std::uint8_t* p) noexcept { return (static_cast<std::uint32_t> (p[0]) << 8) | p[1]; }
std::uint32_t littleEndian16 (const std::uint8_t* p) noexcept { return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8); }

std::uint32_t bigEndian32 (const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t> (p[0]) << 24) | (static_cast<std::uint32_t> (p[1]) << 16)
         | (static_cast<std::uint32_t> (p[2]) << 8) | p[3];
}

/** The Orientation tag (0x0112) of IFD0 in a JPEG's Exif segment, or 1. */
int exifOrientation (const std::uint8_t* exif, std::size_t size) noexcept
{
    if (size < 14 || std::memcmp (exif, "Exif\0\0", 6) != 0)
        return 1;

    const auto* tiff = exif + 6;
    const auto length = size - 6;
    const auto little = tiff[0] == 'I' && tiff[1] == 'I';

    if (! little && ! (tiff[0] == 'M' && tiff[1] == 'M'))
        return 1;

    const auto u16 = [&] (std::size_t at) { return little ? littleEndian16 (tiff + at) : bigEndian16 (tiff + at); };
    const auto u32 = [&] (std::size_t at)
    {
        return little ? (u16 (at) | (u16 (at + 2) << 16)) : ((u16 (at) << 16) | u16 (at + 2));
    };

    if (u16 (2) != 42)
        return 1;

    const auto ifd = static_cast<std::size_t> (u32 (4));

    if (ifd > length || length - ifd < 2)
        return 1;

    const auto entries = static_cast<std::size_t> (u16 (ifd));

    for (std::size_t e = 0; e < entries; ++e)
    {
        const auto at = ifd + 2 + 12 * e;

        if (at > length || length - at < 12)
            break;

        if (u16 (at) == 0x0112 && u16 (at + 2) == 3) // a SHORT
        {
            const auto value = static_cast<int> (u16 (at + 8));
            return value >= 1 && value <= 8 ? value : 1;
        }
    }

    return 1;
}

void readJpeg (const std::uint8_t* b, std::size_t size, PictureHeader& header) noexcept
{
    std::size_t at = 2; // after the start of image

    while (at + 4 <= size)
    {
        if (b[at] != 0xFF)
            return; // not a marker where one should be: broken

        const auto marker = b[at + 1];

        if (marker == 0xFF) // fill
        {
            ++at;
            continue;
        }

        at += 2;

        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8))
            continue; // markers with nothing after them

        if (marker == 0xD9 || marker == 0xDA)
            return; // the end, or the image data, before any frame header

        const auto length = static_cast<std::size_t> (bigEndian16 (b + at));

        if (length < 2 || length > size - at)
            return;

        const auto* payload = b + at + 2;
        const auto payloadSize = length - 2;

        if (marker == 0xE1 && header.orientation == 1)
            header.orientation = exifOrientation (payload, payloadSize);

        // The frame header: SOF0 .. SOF15 but for DHT (C4), JPG (C8) and DAC (CC).
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
        {
            if (payloadSize >= 5)
            {
                header.height = bigEndian16 (payload + 1);
                header.width = bigEndian16 (payload + 3);
            }

            return;
        }

        at += length;
    }
}

void readGif (const std::uint8_t* b, std::size_t size, PictureHeader& header) noexcept
{
    if (size < 13)
        return;

    // The logical screen, then the first image's own size, which is what a decoder makes.
    auto width = littleEndian16 (b + 6), height = littleEndian16 (b + 8);
    std::size_t at = 13;

    if ((b[10] & 0x80) != 0)
        at += 3u * (2u << (b[10] & 7));

    while (at < size)
    {
        if (b[at] == 0x21) // an extension: its label, then blocks until an empty one
        {
            at += 2;

            while (at < size && b[at] != 0)
                at += 1u + b[at];

            ++at;
        }
        else if (b[at] == 0x2C) // the first image
        {
            if (size - at >= 9)
            {
                width = std::max (width, littleEndian16 (b + at + 5));
                height = std::max (height, littleEndian16 (b + at + 7));
            }

            break;
        }
        else
        {
            break;
        }
    }

    header.width = width;
    header.height = height;
}

} // namespace

PictureHeader readPictureHeader (const std::uint8_t* b, std::size_t size) noexcept
{
    PictureHeader header;

    if (b == nullptr)
        return header;

    if (size >= 24 && std::memcmp (b, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp (b + 12, "IHDR", 4) == 0)
    {
        header.format = PictureHeader::Format::png;
        header.width = bigEndian32 (b + 16);
        header.height = bigEndian32 (b + 20);
    }
    else if (size >= 4 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
    {
        header.format = PictureHeader::Format::jpeg;
        readJpeg (b, size, header);
    }
    else if (size >= 6 && (std::memcmp (b, "GIF87a", 6) == 0 || std::memcmp (b, "GIF89a", 6) == 0))
    {
        header.format = PictureHeader::Format::gif;
        readGif (b, size, header);
    }

    return header;
}

bool pictureCanBeRead (const PictureHeader& header) noexcept
{
    return header.format != PictureHeader::Format::unknown
        && header.width > 0 && header.height > 0
        && header.width <= largestPictureSide && header.height <= largestPictureSide
        && static_cast<std::uint64_t> (header.width) * header.height <= largestPicturePixels;
}

std::vector<std::uint8_t> orientPicture (const std::uint8_t* rgb, int& width, int& height, int orientation)
{
    const auto w = std::max (width, 0), h = std::max (height, 0);

    if (orientation < 1 || orientation > 8)
        orientation = 1;

    const auto turned = orientation >= 5; // 5 .. 8 swap the sides
    const auto seenWidth = turned ? h : w, seenHeight = turned ? w : h;
    std::vector<std::uint8_t> out (static_cast<std::size_t> (w) * static_cast<std::size_t> (h) * 3);

    if (rgb == nullptr)
        return out;

    for (int y = 0; y < seenHeight; ++y)
    {
        for (int x = 0; x < seenWidth; ++x)
        {
            // The stored pixel seen at (x, y).
            int sx = x, sy = y;

            switch (orientation)
            {
                case 2: sx = w - 1 - x; break;                    // mirrored left to right
                case 3: sx = w - 1 - x; sy = h - 1 - y; break;    // upside down
                case 4: sy = h - 1 - y; break;                    // mirrored top to bottom
                case 5: sx = y; sy = x; break;                    // transposed
                case 6: sx = y; sy = h - 1 - x; break;            // stored a quarter turn anticlockwise
                case 7: sx = w - 1 - y; sy = h - 1 - x; break;    // transversed
                case 8: sx = w - 1 - y; sy = x; break;            // stored a quarter turn clockwise
                default: break;
            }

            const auto* p = rgb + (static_cast<std::size_t> (sy) * static_cast<std::size_t> (w) + static_cast<std::size_t> (sx)) * 3;
            auto* o = out.data() + (static_cast<std::size_t> (y) * static_cast<std::size_t> (seenWidth) + static_cast<std::size_t> (x)) * 3;
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
        }
    }

    width = seenWidth;
    height = seenHeight;
    return out;
}

} // namespace drenched

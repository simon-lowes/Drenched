#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace drenched
{

/** Why a scene model file could not be used. */
struct SceneNetProblem : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** What the scene model saw in a photograph: the kinds of place it could be
    (Places365 categories, the likeliest first, with how sure it is of each,
    adding up to 1 over all 365) and the attributes of the scene it found
    (SUN attributes: surfaces such as carpet, brick or glass, whether it is
    enclosed, lit by daylight, and so on, each with a score where above zero
    is present). */
struct SceneReading
{
    struct Category { std::string name; float probability; bool indoors; };
    struct Attribute { std::string name; float score; };

    std::vector<Category> categories;  // the five likeliest, in order
    std::vector<Attribute> attributes; // every attribute scoring above zero, the strongest first
};

/** Looks at a photograph and says what kind of place it shows and what it is
    made of: the Places365 scene recognition network (a ResNet-18 trained by
    MIT CSAIL, used under CC BY; see NOTICE.md), run here in plain C++ so the
    plugin needs no machine-learning runtime and no network. The weights are
    read from the compact file tools/scene/convert_places365.py writes.

    One look at a 224 x 224 photograph takes about a second of one core (the
    network has no max pool, so its last maps are 14 x 14). Allocates and
    takes that long: never on the audio thread. */
class SceneNet
{
public:
    /** Reads a model file's bytes. Throws SceneNetProblem when they aren't one. */
    SceneNet (const std::uint8_t* data, std::size_t size);
    ~SceneNet();

    SceneNet (SceneNet&&) noexcept;
    SceneNet& operator= (SceneNet&&) noexcept;

    static constexpr int side = 224;

    /** Looks at a photograph: side x side pixels, three bytes each (red,
        green, blue), rows top to bottom, as a camera gives them. The caller
        has scaled it to size (the whole picture, not a crop). */
    SceneReading look (const std::uint8_t* rgb) const;

    /** The category and attribute names the model knows, in its order. */
    const std::vector<std::string>& categoryNames() const noexcept;
    const std::vector<std::string>& attributeNames() const noexcept;

private:
    struct Model;
    std::unique_ptr<Model> model;
};

/** Turns what the model saw into words describeRoom() understands: the
    kind of place, its surfaces, and how full it is; for example
    "church, stone, wood". Categories the room describer has no room for
    fall back to a room indoors or the open air. */
std::string sceneWords (const SceneReading& reading);

/** Scales a photograph of any size to the model's side, with a box filter
    (every source pixel counts), from rows of `width` pixels, `channels`
    bytes each (the first three red, green, blue). Allocates. */
std::vector<std::uint8_t> scaleForSceneNet (const std::uint8_t* pixels, int width, int height, int channels, int rowBytes);

} // namespace drenched

#include "drenched/RoomDescription.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace drenched
{

namespace
{

constexpr double speedOfSound = 343.0;

/** Which surfaces a material covers when the words don't say; crowd marks the
    people a content word puts in the room. */
enum Surface { crowd = 0, walls = 1, floor = 2, ceiling = 4, furnishing = 8 };

/** Absorption per octave band, 63 Hz .. 8 kHz, as the published tables give
    it (Sabine absorption coefficients of ordinary building surfaces). */
struct Material
{
    const char* names;      // words that name it, separated by '|'
    const char* shown;      // how the space's materials line reads
    int surfaces;           // where it goes when the words don't say
    float share;            // how much of those surfaces it covers (curtains hang on part of a wall)
    std::array<float, 8> alpha;
};

constexpr std::array<Material, 32> materials {{
    { "tile|tiles|tiled|tiling|ceramic|porcelain|glazed|enamel|enamelled",        "Tiles",        walls | floor,           1.0f, { 0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.02f, 0.02f, 0.02f } },
    { "marble|granite|terrazzo|polished stone|slate",                             "Marble",       walls | floor,           1.0f, { 0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.02f, 0.02f, 0.02f } },
    { "stone|stones|limestone|sandstone|masonry|flagstone|flagstones|cobbles|cobbled|rock|rocky", "Stone", walls, 1.0f, { 0.04f, 0.04f, 0.04f, 0.05f, 0.06f, 0.07f, 0.08f, 0.08f } },
    { "concrete|cement|breeze block|breezeblock|cinder block|rendered",           "Concrete",     walls | floor | ceiling, 1.0f, { 0.01f, 0.01f, 0.01f, 0.02f, 0.02f, 0.02f, 0.03f, 0.03f } },
    { "brick|bricks|brickwork|bare brick",                                        "Brick",        walls,                   1.0f, { 0.03f, 0.03f, 0.03f, 0.04f, 0.05f, 0.07f, 0.07f, 0.07f } },
    { "plaster|plastered|plasterboard|drywall|painted walls|gypsum|stucco",       "Plaster",      walls | ceiling,         1.0f, { 0.12f, 0.12f, 0.10f, 0.06f, 0.04f, 0.04f, 0.05f, 0.05f } },
    { "glass|glazing|windows|window|glazed walls|mirror|mirrors|mirrored",        "Glass",        walls,                   0.5f, { 0.18f, 0.18f, 0.06f, 0.04f, 0.03f, 0.02f, 0.02f, 0.02f } },
    { "wood|wooden|timber|oak|pine|panelled|panelling|paneled|paneling|boards|boarded|plywood|hardwood", "Wood", walls | floor, 1.0f, { 0.15f, 0.15f, 0.11f, 0.10f, 0.07f, 0.06f, 0.07f, 0.07f } },
    { "parquet|floorboards|wooden floor|wood floor|laminate",                     "Wooden floor", floor,                   1.0f, { 0.04f, 0.04f, 0.04f, 0.07f, 0.06f, 0.06f, 0.07f, 0.07f } },
    { "carpet|carpets|carpeted|rug|rugs",                                         "Carpet",       floor,                   1.0f, { 0.03f, 0.03f, 0.09f, 0.25f, 0.50f, 0.60f, 0.65f, 0.65f } },
    { "lino|linoleum|vinyl|vinyl floor|rubber floor",                             "Vinyl floor",  floor,                   1.0f, { 0.02f, 0.02f, 0.03f, 0.03f, 0.03f, 0.03f, 0.02f, 0.02f } },
    { "curtain|curtains|drapes|drapery|velvet|velour|hangings|tapestry|tapestries|banners", "Curtains", walls,          0.4f, { 0.14f, 0.14f, 0.35f, 0.55f, 0.72f, 0.70f, 0.65f, 0.65f } },
    { "fabric|cloth|upholstered|upholstery|padded|soft furnishings|cushions|sofa|sofas|armchairs|bedding|bed|duvet", "Soft furnishings", furnishing, 0.6f, { 0.20f, 0.20f, 0.30f, 0.45f, 0.55f, 0.60f, 0.60f, 0.60f } },
    { "foam|acoustic foam|acoustic panels|absorbers|absorber|treated|acoustic treatment|bass traps|rockwool|mineral wool|wedges", "Acoustic foam", walls | ceiling, 0.7f, { 0.10f, 0.10f, 0.25f, 0.70f, 0.95f, 0.95f, 0.95f, 0.95f } },
    { "acoustic tiles|ceiling tiles|suspended ceiling|acoustic ceiling|tiled ceiling", "Acoustic ceiling", ceiling,   1.0f, { 0.20f, 0.20f, 0.50f, 0.70f, 0.80f, 0.80f, 0.70f, 0.70f } },
    { "metal|steel|iron|corrugated|tin|aluminium|aluminum|sheet metal|metallic",   "Metal",        walls | ceiling,         1.0f, { 0.05f, 0.05f, 0.10f, 0.10f, 0.10f, 0.07f, 0.02f, 0.02f } },
    { "books|bookshelves|bookcases|shelves|shelving|library shelves",             "Bookshelves",  walls,                   0.6f, { 0.20f, 0.20f, 0.30f, 0.40f, 0.40f, 0.40f, 0.40f, 0.40f } },
    { "water|pool water|wet floor|flooded",                                       "Water",        floor,                   1.0f, { 0.01f, 0.01f, 0.01f, 0.01f, 0.02f, 0.02f, 0.03f, 0.03f } },
    { "earth|soil|dirt|mud|gravel|ground|grass|lawn|turf|moss",                    "Earth",        floor,                   1.0f, { 0.15f, 0.15f, 0.25f, 0.40f, 0.55f, 0.60f, 0.60f, 0.60f } },
    { "snow|snowy",                                                               "Snow",         floor,                   1.0f, { 0.45f, 0.45f, 0.75f, 0.90f, 0.95f, 0.95f, 0.95f, 0.95f } },
    { "trees|foliage|leaves|hedges|hedge|bushes|shrubs|vegetation|ivy|vines",     "Foliage",      walls,                   1.0f, { 0.20f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.65f, 0.70f } },
    { "sand|sandy|beach sand",                                                    "Sand",         floor,                   1.0f, { 0.15f, 0.15f, 0.35f, 0.40f, 0.50f, 0.55f, 0.80f, 0.80f } },
    { "asphalt|tarmac|paving|paved|pavement|flagged|cobblestones",                "Paving",       floor,                   1.0f, { 0.02f, 0.02f, 0.03f, 0.03f, 0.03f, 0.03f, 0.04f, 0.04f } },
    { "wallpaper|wallpapered|papered",                                            "Wallpaper",    walls,                   1.0f, { 0.05f, 0.05f, 0.05f, 0.06f, 0.07f, 0.08f, 0.08f, 0.08f } },
    { "canvas|tarpaulin|tent walls|marquee",                                      "Canvas",       walls | ceiling,         1.0f, { 0.10f, 0.10f, 0.15f, 0.25f, 0.35f, 0.40f, 0.45f, 0.45f } },
    { "leather|leather seats",                                                    "Leather",      furnishing,              0.5f, { 0.10f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.30f, 0.30f } },
    { "wooden seats|pews|wooden pews|benches|wooden benches",                     "Wooden seats", furnishing,              0.5f, { 0.08f, 0.08f, 0.10f, 0.12f, 0.14f, 0.15f, 0.15f, 0.15f } },
    { "upholstered seats|theatre seats|cinema seats|seats|seating|chairs",        "Seats",        furnishing,              0.6f, { 0.30f, 0.30f, 0.40f, 0.50f, 0.60f, 0.65f, 0.65f, 0.65f } },
    { "people|audience|crowd|crowded|packed|full of people|congregation|busy|occupied|full house|sold out", "People", furnishing, 0.6f, { 0.40f, 0.40f, 0.55f, 0.70f, 0.85f, 0.90f, 0.90f, 0.85f } },
    { "machinery|machines|equipment|racks|rack|cars|parked cars|lorries|trucks|vehicles|boxes|crates|pallets|clutter|cluttered|junk|stuff", "Clutter", furnishing, 0.5f, { 0.10f, 0.10f, 0.15f, 0.20f, 0.25f, 0.30f, 0.30f, 0.30f } },
    { "plastic|perspex|acrylic|fibreglass|fiberglass",                            "Plastic",      walls,                   1.0f, { 0.05f, 0.05f, 0.06f, 0.06f, 0.07f, 0.07f, 0.08f, 0.08f } },
    { "ice|frozen",                                                               "Ice",          walls | floor,           1.0f, { 0.01f, 0.01f, 0.01f, 0.01f, 0.02f, 0.02f, 0.02f, 0.02f } },
}};

/** Which surface a material word is said to be on: "carpeted floor",
    "stone walls", "wooden ceiling", "glass roof". */
struct SurfaceWord
{
    const char* names;
    int surface;
};

constexpr std::array<SurfaceWord, 3> surfaceWords {{
    { "floor|floors|flooring|underfoot|ground", floor },
    { "ceiling|ceilings|roof|roofed|overhead|vault|vaulted|vaulting|rafters|beams", ceiling },
    { "wall|walls|walled|sides|lined|lining|panelled|cladding|clad", walls },
}};

/** A kind of room: what its surfaces are made of when the words don't say,
    its usual size, and how the interface draws it. The shape is a shoebox
    of width x depth x height in metres. */
struct Kind
{
    const char* names;
    const char* shown;      // "a stone chapel": what the space's place line says it was taken for
    const char* uiKind;     // hall, chapel, garage, room, outdoor or underground
    float width, depth, height;
    int wallMaterial, floorMaterial, ceilingMaterial; // indices into materials; -1 = open to the sky
    float furnishing;       // how much of the floor area is covered by things that absorb, 0 .. 1
    int furnishingMaterial;
    float distance;         // where the listener usually is, 0 .. 100
};

// Material indices, by their place in the table above.
constexpr int tile = 0, marble = 1, stone = 2, concrete = 3, brick = 4, plaster = 5, glass = 6, wood = 7, woodFloor = 8,
              carpet = 9, vinyl = 10, curtains = 11, soft = 12, foam = 13, acousticCeiling = 14, metal = 15, books = 16,
              water = 17, earth = 18, snow = 19, foliage = 20, sand = 21, paving = 22, wallpaper = 23, canvas = 24,
              leather = 25, pews = 26, seats = 27, people = 28, clutter = 29, plastic = 30, ice = 31;
constexpr int open = -1;

constexpr std::array<Kind, 76> kinds {{
    // Small rooms
    { "bathroom|bathrooms|shower room|shower|showers|wet room|toilet|toilets|washroom|lavatory|loo|wc|restroom|bath", "a bathroom", "room", 2.2f, 2.6f, 2.4f, tile, tile, plaster, 0.15f, soft, 30.0f },
    { "kitchen|kitchens|galley|scullery|pantry|utility room",                 "a kitchen",           "room", 3.2f, 4.0f, 2.5f, plaster, tile, plaster, 0.3f, clutter, 35.0f },
    { "bedroom|bedrooms|bedsit|dormitory|dorm|hotel room|guest room|nursery|playroom|play room|games room|rumpus room",  "a bedroom",           "room", 3.5f, 4.2f, 2.5f, plaster, carpet, plaster, 0.6f, soft, 35.0f },
    { "living room|lounge|sitting room|front room|drawing room|parlour|parlor|den|family room|snug|flat|apartment|house|home|cottage|dining room|dinner|lounge room", "a living room", "room", 4.2f, 5.5f, 2.6f, plaster, carpet, plaster, 0.5f, soft, 40.0f },
    { "office|offices|study|meeting room|boardroom|conference room|open plan|open-plan|cubicle|cubicles|reception|waiting room|consulting room|surgery", "an office", "room", 5.0f, 7.0f, 2.8f, plaster, carpet, acousticCeiling, 0.4f, clutter, 40.0f },
    { "classroom|classrooms|school room|schoolroom|seminar room|seminar|lecture room|lecture hall|lecture theatre|lecture theater|auditorium|auditoria", "a classroom", "room", 7.0f, 9.0f, 3.2f, plaster, vinyl, acousticCeiling, 0.3f, seats, 45.0f },
    { "vocal booth|booth|isolation booth|iso booth|phone box|telephone box|cupboard|closet|wardrobe|cabinet|locker|confessional", "a vocal booth", "room", 1.4f, 1.6f, 2.2f, foam, carpet, foam, 0.0f, soft, 10.0f },
    { "studio|recording studio|live room|tracking room|rehearsal room|rehearsal space|practice room|drum room", "a studio live room", "room", 7.0f, 9.0f, 3.8f, wood, woodFloor, acousticCeiling, 0.2f, clutter, 35.0f },
    { "control room|mix room|mixing room|mastering room|mastering suite|edit suite", "a control room", "room", 5.0f, 6.5f, 3.0f, foam, carpet, foam, 0.4f, clutter, 30.0f },
    { "corridor|corridors|hallway|hallways|hall way|passage|passageway|passages|landing|cloister|cloisters|colonnade|arcade|lobby|vestibule|foyer|porch", "a corridor", "room", 2.0f, 16.0f, 2.8f, plaster, vinyl, plaster, 0.05f, clutter, 45.0f },
    { "stairwell|stairwells|staircase|stairs|stairway|fire escape|fire stairs", "a stairwell",       "room", 3.0f, 3.5f, 10.0f, concrete, concrete, concrete, 0.0f, clutter, 50.0f },
    { "attic|loft|roof space|garret|eaves",                                  "an attic",            "room", 5.0f, 8.0f, 2.2f, wood, wood, wood, 0.3f, clutter, 40.0f },
    { "cellar|cellars|basement|basements|undercroft|crypt|crypts|vault|vaults|wine cellar|boiler room|catacomb|catacombs|dungeon|bunker|bunkers|air raid shelter|shelter", "a cellar", "underground", 5.0f, 8.0f, 2.4f, stone, concrete, stone, 0.15f, clutter, 40.0f },
    { "garage|garages|lock-up|lockup|workshop|workshops|shed|sheds|outbuilding|barn|barns|stable|stables|cowshed|boathouse|boat house", "a garage", "garage", 3.5f, 6.0f, 2.6f, brick, concrete, metal, 0.3f, clutter, 40.0f },
    { "shop|shops|store|supermarket|showroom|boutique|salon|hairdresser|barber|barbers|cafe|coffee shop|restaurant|canteen|diner|bistro|bar|pub|tavern|inn|saloon|nightclub|night club|club|disco|dance floor|dancefloor", "a shop", "room", 8.0f, 12.0f, 3.5f, plaster, tile, acousticCeiling, 0.4f, clutter, 45.0f },
    { "library|libraries|reading room|archive|archives|bookshop|book shop",   "a library",           "room", 10.0f, 16.0f, 4.5f, books, carpet, plaster, 0.3f, seats, 45.0f },
    { "museum|gallery|art gallery|galleries|exhibition hall|exhibition|atrium|atria|mall|shopping centre|shopping mall|concourse|hall of mirrors", "a gallery", "hall", 14.0f, 22.0f, 7.0f, plaster, marble, plaster, 0.05f, clutter, 50.0f },
    { "hospital|ward|wards|clinic|operating theatre|operating room|morgue|mortuary|laboratory|lab", "a hospital ward", "room", 7.0f, 10.0f, 3.0f, plaster, vinyl, acousticCeiling, 0.25f, soft, 40.0f },
    { "prison|jail|cell|cells|cell block|prison cell|police station",         "a prison cell",       "room", 2.5f, 3.5f, 2.8f, concrete, concrete, concrete, 0.1f, soft, 30.0f },
    { "lift|elevator|cabin|car interior|inside a car|in a car|car|van|bus|coach|train carriage|carriage|caravan|camper|cockpit|aeroplane|airplane|plane cabin|cabin of a plane|submarine", "a vehicle", "room", 1.8f, 2.4f, 1.4f, soft, carpet, soft, 0.5f, soft, 20.0f },
    { "tent|marquee|yurt|gazebo|pavilion|big top",                            "a tent",              "room", 5.0f, 7.0f, 3.0f, canvas, earth, canvas, 0.2f, soft, 40.0f },
    { "greenhouse|conservatory|glasshouse|orangery|sunroom|sun room|winter garden", "a conservatory", "room", 4.0f, 6.0f, 3.0f, glass, tile, glass, 0.3f, foliage, 40.0f },
    { "sauna|steam room|changing room|changing rooms|locker room|dressing room|green room", "a changing room", "room", 3.5f, 5.0f, 2.6f, tile, tile, plaster, 0.2f, soft, 35.0f },
    // Big rooms
    { "concert hall|concert halls|symphony hall|philharmonic|orchestral hall|hall|halls|town hall|village hall|school hall|assembly hall|guildhall|banqueting hall|great hall|ballroom|ballrooms|function room|dining hall|refectory|mess hall|hall of residence", "a concert hall", "hall", 24.0f, 40.0f, 15.0f, plaster, woodFloor, plaster, 0.8f, seats, 55.0f },
    { "opera house|opera|theatre|theater|theatres|theaters|playhouse|music hall|amphitheatre|amphitheater|odeon|stage|proscenium|west end|broadway", "a theatre", "hall", 22.0f, 30.0f, 14.0f, plaster, woodFloor, plaster, 0.6f, seats, 55.0f },
    { "cinema|cinemas|movie theatre|movie theater|picture house|multiplex|screening room|home cinema|home theater|home theatre|imax", "a cinema", "hall", 14.0f, 24.0f, 8.0f, curtains, carpet, acousticCeiling, 0.6f, seats, 55.0f },
    { "arena|arenas|stadium|stadiums|coliseum|colosseum|indoor arena|velodrome|ice rink|rink|skating rink|bowl", "an arena", "hall", 70.0f, 110.0f, 30.0f, concrete, concrete, metal, 0.4f, seats, 70.0f },
    { "chapel|chapels|oratory|chantry|lady chapel|side chapel|chapel of rest|meeting house|prayer room|prayer hall", "a chapel", "chapel", 8.0f, 16.0f, 7.0f, stone, stone, wood, 0.2f, pews, 45.0f },
    { "church|churches|parish church|kirk|minster|priory|abbey|abbeys|monastery|convent|synagogue|temple|temples|mosque|mosques|shrine|basilica|chancel|nave|transept|choir|cloister church|country church|village church|stone church", "a church", "chapel", 14.0f, 36.0f, 14.0f, stone, stone, wood, 0.2f, pews, 50.0f },
    { "cathedral|cathedrals|duomo|dom|great church|abbey church|minster church|notre dame", "a cathedral", "chapel", 26.0f, 90.0f, 24.0f, stone, stone, stone, 0.3f, pews, 55.0f },
    { "gym|gymnasium|sports hall|sports centre|leisure centre|basketball court|squash court|badminton hall|indoor court|hangar|aircraft hangar|hanger|drill hall|exhibition centre|conference centre|convention centre|convention center", "a sports hall", "garage", 24.0f, 40.0f, 10.0f, brick, woodFloor, metal, 0.05f, clutter, 50.0f },
    { "swimming pool|pool|baths|indoor pool|leisure pool|lido|aquatics centre|aquarium|bathhouse|bath house|hammam|turkish bath|thermal baths", "a swimming pool", "garage", 20.0f, 32.0f, 8.0f, tile, water, metal, 0.0f, clutter, 50.0f },
    { "warehouse|warehouses|depot|storehouse|storeroom|stockroom|distribution centre|cold store|silo|grain store|loading bay|loading dock", "a warehouse", "garage", 30.0f, 60.0f, 9.0f, metal, concrete, metal, 0.3f, clutter, 50.0f },
    { "factory|factories|mill|mills|foundry|plant|works|machine shop|assembly line|production hall|industrial unit|industrial|shipyard|dockyard|dry dock|boiler house|engine room|turbine hall|power station|powerhouse|power plant", "a factory", "garage", 30.0f, 60.0f, 10.0f, brick, concrete, metal, 0.4f, clutter, 50.0f },
    { "car park|carpark|parking garage|multi-storey|multi storey|parking|underground car park|parking lot|parking level|garage level|parking deck", "a car park", "garage", 44.0f, 58.0f, 3.0f, concrete, concrete, concrete, 0.2f, clutter, 45.0f },
    { "station|train station|railway station|subway|underground station|metro|metro station|tube station|platform|ticket hall|airport|terminal|airport terminal|bus station|bus garage|tram depot|train shed|departure hall|arrivals hall", "a station", "garage", 30.0f, 80.0f, 12.0f, tile, paving, metal, 0.1f, people, 55.0f },
    { "tank|water tank|cistern|oil tank|empty tank|steel tank|boiler|pipe|pipes|culvert|sewer|sewers|drain|storm drain|reservoir|underground reservoir|vat|silo tank", "a tank", "underground", 6.0f, 12.0f, 5.0f, metal, metal, metal, 0.0f, clutter, 50.0f },
    { "tunnel|tunnels|railway tunnel|road tunnel|subway tunnel|underpass|passage underground|adit", "a tunnel", "underground", 6.0f, 120.0f, 5.5f, brick, paving, brick, 0.0f, clutter, 55.0f },
    { "mine|mines|mineshaft|mine shaft|colliery|pit|quarry|quarries|underground quarry|cavern|caverns|cave|caves|grotto|grottoes|cave system|pothole|sinkhole|lava tube|catacombs underground|salt mine", "a cave", "underground", 10.0f, 40.0f, 6.0f, stone, stone, stone, 0.0f, clutter, 50.0f },
    { "well|wellshaft|shaft|lift shaft|elevator shaft|chimney|ventilation shaft|borehole|silo shaft|tower|bell tower|clock tower|lighthouse|turret|spiral staircase", "a shaft", "underground", 3.0f, 3.0f, 18.0f, brick, stone, open, 0.0f, clutter, 50.0f },
    { "kiln|kilns|oven|furnace|brick kiln|lime kiln|bottle kiln|ice house|icehouse|cold store cellar|dome|domed|rotunda|cupola|planetarium|observatory|mausoleum|tomb|tombs|burial chamber|pyramid", "a domed chamber", "underground", 9.0f, 9.0f, 8.0f, brick, stone, brick, 0.0f, clutter, 45.0f },
    { "castle|castles|keep|fortress|fort|citadel|great keep|castle hall|baronial hall|banquet hall|palace|palaces|stately home|manor|manor house|mansion|chateau|villa|country house", "a castle hall", "hall", 12.0f, 24.0f, 9.0f, stone, stone, wood, 0.15f, pews, 50.0f },
    { "gymnastics hall|dojo|martial arts hall|boxing gym|fitness studio|dance studio|yoga studio|ballet studio|dance hall", "a dance studio", "room", 10.0f, 14.0f, 4.0f, glass, woodFloor, plaster, 0.05f, clutter, 45.0f },
    { "radio studio|broadcast studio|tv studio|television studio|sound stage|soundstage|film set|film studio|newsroom|podcast studio|voiceover booth|voice over booth|dubbing stage", "a sound stage", "room", 20.0f, 30.0f, 10.0f, foam, concrete, foam, 0.3f, clutter, 40.0f },
    { "schoolyard|playground|school gym|assembly room|church hall|parish hall|community centre|community hall|scout hut|village institute|legion hall|chapter house|chapterhouse", "a church hall", "hall", 12.0f, 20.0f, 6.0f, plaster, woodFloor, wood, 0.2f, seats, 50.0f },
    // Outdoors
    { "courtyard|courtyards|quad|quadrangle|cloister garth|patio|yard|back yard|backyard|walled garden|atrium open|inner court|court|piazza|plaza|town square|market square|village square|public square|marketplace", "a courtyard", "outdoor", 14.0f, 18.0f, 9.0f, brick, paving, open, 0.05f, foliage, 45.0f },
    { "street|streets|alley|alleyway|alleys|lane|lanes|high street|main street|avenue|boulevard|backstreet|back street|narrow street|side street|mews|arcade street|passage outdoors|cul de sac", "a street", "outdoor", 10.0f, 80.0f, 12.0f, brick, paving, open, 0.1f, clutter, 50.0f },
    { "canyon|canyons|gorge|gorges|ravine|ravines|chasm|cliff|cliffs|cliff face|rock face|escarpment|gully|slot canyon|crag|crags|rock wall", "a canyon", "outdoor", 25.0f, 200.0f, 40.0f, stone, earth, open, 0.0f, foliage, 60.0f },
    { "forest|forests|woods|woodland|wood|woodlands|jungle|rainforest|grove|orchard|copse|thicket|pine forest|pinewood|glade|clearing", "a forest", "outdoor", 40.0f, 60.0f, 20.0f, foliage, earth, open, 0.0f, foliage, 50.0f },
    { "field|fields|meadow|meadows|moor|moorland|heath|plain|plains|prairie|steppe|desert|tundra|open country|countryside|farmland|pasture|lawn outdoors|park|parkland|common|golf course|hillside|hill|hills|mountain|mountains|mountainside|summit|peak|fell|dale|valley|valleys", "open country", "outdoor", 200.0f, 300.0f, 60.0f, foliage, earth, open, 0.0f, foliage, 60.0f },
    { "beach|beaches|seashore|shore|shoreline|coast|coastline|seafront|harbour|harbor|marina|quay|quayside|jetty|pier|dock|docks|riverbank|river bank|lakeside|lake shore|waterfront|promenade", "a shore", "outdoor", 100.0f, 200.0f, 30.0f, sand, water, open, 0.0f, clutter, 60.0f },
    { "garden|gardens|allotment|vegetable patch|kitchen garden|rose garden|terrace|terraced garden|rooftop|roof terrace|balcony|veranda|verandah|deck outdoors|porch outdoors|pergola", "a garden", "outdoor", 12.0f, 18.0f, 6.0f, foliage, earth, open, 0.1f, foliage, 40.0f },
    { "stadium outdoors|open air stadium|football ground|football stadium|cricket ground|racecourse|race track|racetrack|athletics track|running track|sports field|pitch|football pitch|playing field|playing fields|showground|fairground|festival site|festival field|open air|open-air|outdoors|outside|outdoor|in the open|al fresco|exterior|street market|car boot sale|bandstand|open air theatre|open-air theatre|amphitheatre outdoors", "the open air", "outdoor", 90.0f, 140.0f, 25.0f, seats, earth, open, 0.3f, people, 65.0f },
    { "bridge|underneath a bridge|under a bridge|viaduct|arches|railway arches|railway arch|overpass|flyover|motorway bridge|aqueduct", "under a bridge", "outdoor", 10.0f, 30.0f, 7.0f, brick, paving, brick, 0.0f, clutter, 45.0f },
    { "ruin|ruins|ruined|roofless|abbey ruins|castle ruins|derelict|abandoned building|abandoned factory|abandoned warehouse|abandoned|disused", "a ruin", "outdoor", 14.0f, 30.0f, 10.0f, stone, earth, open, 0.1f, clutter, 50.0f },
    { "ship|ships|hold|cargo hold|ship's hold|ferry|cruise ship|liner|vessel|hull|deck below|below deck|engine room ship|barge|boat|yacht|narrowboat|container|shipping container|lorry trailer|trailer|truck trailer|skip", "a ship's hold", "garage", 12.0f, 30.0f, 6.0f, metal, metal, metal, 0.2f, clutter, 50.0f },
    { "barracks|army camp|military base|hut|nissen hut|quonset hut|portakabin|portacabin|cabin|log cabin|chalet|lodge|hunting lodge|hut mountain|bothy|summer house|summerhouse|beach hut|shepherd's hut", "a cabin", "room", 4.5f, 7.0f, 2.8f, wood, wood, wood, 0.3f, soft, 40.0f },
    { "bank|bank hall|banking hall|post office|courthouse|court room|courtroom|court house|council chamber|chamber|debating chamber|parliament|senate|civic hall|city hall|registry office|register office|town house", "a civic hall", "hall", 15.0f, 22.0f, 9.0f, plaster, marble, plaster, 0.2f, seats, 50.0f },
    { "school|schools|college|university|campus|academy|nursery school|kindergarten|canteen school|dining hall school|refectory school|assembly school", "a school hall", "hall", 12.0f, 20.0f, 6.0f, plaster, woodFloor, plaster, 0.2f, seats, 50.0f },
    { "pub cellar|beer cellar|brewery|distillery|winery|cider house|barrel room|barrel store|cask store|bonded warehouse", "a barrel store", "underground", 10.0f, 20.0f, 4.0f, stone, concrete, brick, 0.4f, clutter, 45.0f },
    { "stadium concourse|concourse|underground concourse|subway concourse|underpass concourse|foot tunnel|pedestrian tunnel|subway passage|pedestrian subway", "an underpass", "underground", 5.0f, 40.0f, 3.0f, tile, paving, concrete, 0.0f, clutter, 50.0f },
    { "spaceship|space station|starship|space ship|airlock|bridge of a ship|command deck|hangar bay|docking bay|hangar deck|sci-fi|science fiction|sci fi|futuristic", "a hangar bay", "garage", 40.0f, 60.0f, 20.0f, metal, metal, metal, 0.1f, clutter, 55.0f },
    { "hospital corridor|school corridor|hotel corridor|office corridor|service corridor|tunnel corridor|long corridor|hallway long|endless corridor", "a long corridor", "room", 2.4f, 40.0f, 3.0f, plaster, vinyl, acousticCeiling, 0.0f, clutter, 50.0f },
    { "phone booth|phone box room|cupboard under the stairs|under the stairs|crawl space|crawlspace|duct|air duct|ventilation duct|drainpipe|drain pipe|chimney flue|flue", "a duct", "room", 0.8f, 1.2f, 1.2f, metal, metal, metal, 0.0f, clutter, 10.0f },
    { "small room|little room|box room|spare room|back room|side room|anteroom|ante room|utility|cloakroom|cloak room|vestry|sacristy|robing room", "a small room", "room", 2.8f, 3.4f, 2.5f, plaster, carpet, plaster, 0.4f, clutter, 35.0f },
    // The plain room. "big room", "long room" and the like are this with a size word, so they
    // are not names of it: a name of a kind is never also read as a size.
    { "room|rooms|double room|open space|empty space|unit|space|chamber room|interior|inside|indoor|indoors", "a room", "room", 5.0f, 7.0f, 2.8f, plaster, woodFloor, plaster, 0.3f, soft, 40.0f },
    { "hangar outdoors|runway|airfield|aerodrome|tarmac apron|apron|helipad|airstrip", "an airfield", "outdoor", 200.0f, 400.0f, 60.0f, foliage, paving, open, 0.0f, clutter, 60.0f },
    { "stadium tunnel|players tunnel|players' tunnel|tunnel entrance|vomitorium|gateway|gatehouse|gate house|archway|porte cochere|carriage arch|covered passage|covered market|market hall|indoor market|arcade covered", "a covered passage", "garage", 8.0f, 24.0f, 7.0f, brick, paving, brick, 0.1f, people, 50.0f },
    { "reverb chamber|echo chamber|echo room|live chamber|chamber reverb|plate room|reverb room|acoustic chamber", "an echo chamber", "room", 3.5f, 6.0f, 3.5f, plaster, tile, plaster, 0.0f, clutter, 50.0f },
    { "washroom public|public toilets|public toilet|gents|ladies|lavatories|changing cubicle|cubicle shower", "public toilets", "room", 4.0f, 6.0f, 2.8f, tile, tile, plaster, 0.1f, clutter, 35.0f },
    { "scullery kitchen|restaurant kitchen|commercial kitchen|professional kitchen|canteen kitchen|hotel kitchen|galley kitchen", "a commercial kitchen", "room", 6.0f, 10.0f, 3.0f, tile, tile, metal, 0.3f, metal, 40.0f },
    { "cathedral crypt|crypt cathedral|undercroft cathedral|chapter crypt|church crypt|crypt chapel|charnel house|ossuary|bone house", "a crypt", "underground", 10.0f, 20.0f, 4.0f, stone, stone, stone, 0.05f, clutter, 45.0f },
    { "subway car|subway train|train|tube train|metro train|carriage train|railway carriage|railcar|tram|trolley|streetcar", "a train carriage", "room", 2.6f, 18.0f, 2.2f, plastic, vinyl, plastic, 0.5f, seats, 40.0f },
    { "classroom lecture|lecture|lectures|lecture theatre large|auditorium large|conference hall|congress hall|congress centre|plenary hall|plenary", "a lecture theatre", "hall", 14.0f, 20.0f, 7.0f, plaster, carpet, acousticCeiling, 0.6f, seats, 55.0f },
    { "studio apartment|loft apartment|penthouse|artist's loft|artists loft|artist loft|converted warehouse|warehouse apartment|loft conversion|new york loft", "a loft apartment", "room", 8.0f, 14.0f, 4.0f, brick, woodFloor, wood, 0.3f, soft, 40.0f },
    { "hotel lobby|hotel foyer|hotel atrium|grand lobby|entrance hall|reception hall|grand entrance|grand staircase|stair hall|marble hall|marble foyer", "an entrance hall", "hall", 12.0f, 16.0f, 8.0f, marble, marble, plaster, 0.1f, soft, 50.0f },
}};

/** Words that change a room's size or shape. */
struct SizeWord
{
    const char* names;
    float width, depth, height; // multipliers
};

constexpr std::array<SizeWord, 24> sizeWords {{
    { "tiny|minute|miniature|minuscule|closet sized|closet-sized|broom cupboard",          0.5f,  0.5f,  0.85f },
    { "small|little|modest|compact|cosy|cozy|poky|pokey|snug|boxy|cramped|pocket|tight",   0.7f,  0.7f,  0.9f  },
    { "smallish|fairly small|quite small|rather small|not very big|on the small side",     0.85f, 0.85f, 0.95f },
    { "medium|medium sized|medium-sized|mid sized|mid-sized|average|ordinary|normal|typical|standard|regular", 1.0f, 1.0f, 1.0f },
    { "big|large|roomy|generous|sizeable|sizable|good sized|good-sized|biggish|largish|fairly big|quite big|rather large", 1.4f, 1.4f, 1.15f },
    { "huge|enormous|massive|giant|gigantic|immense|colossal|mighty|monumental|epic",     2.0f,  2.0f,  1.5f  },
    { "vast|cavernous|cathedral sized|cathedral-sized|endless|boundless|infinite|titanic", 2.8f, 2.8f, 1.8f  },
    { "grand|great|majestic|stately|imposing|palatial|magnificent|noble",                  1.6f,  1.6f,  1.4f  },
    { "spacious|airy|open|expansive|wide open|sprawling|rambling",                        1.3f,  1.3f,  1.15f },
    { "long|elongated|lengthy|deep|extended|stretching|far end|long way",                  1.0f,  1.8f,  1.0f  },
    { "very long|extremely long|endlessly long|immensely long|incredibly long",            1.0f,  2.8f,  1.0f  },
    { "short|stubby|shallow",                                                             1.0f,  0.6f,  1.0f  },
    { "narrow|thin|slim|slender|skinny|tight passage",                                    0.6f,  1.0f,  1.0f  },
    { "very narrow|extremely narrow|really narrow|single file",                            0.4f,  1.0f,  1.0f  },
    { "wide|broad|squat|wide open space",                                                 1.5f,  1.0f,  1.0f  },
    { "square|square shaped|square-shaped|cube|cubic|boxlike|box-like",                   1.0f,  1.0f,  1.0f  }, // shape, handled below
    { "low|low ceilinged|low-ceilinged|low ceiling|low roof|squat ceiling|stooping|crouching|low slung|low-slung", 1.0f, 1.0f, 0.7f },
    { "very low|extremely low|really low",                                                1.0f,  1.0f,  0.5f  },
    { "high|tall|lofty|high ceilinged|high-ceilinged|high ceiling|high ceilings|high roof|soaring|towering|double height|double-height|vaulted high|cathedral ceiling", 1.0f, 1.0f, 1.5f },
    { "very high|very tall|extremely high|extremely tall|immensely high|really high|really tall|sky high|skyhigh", 1.0f, 1.0f, 2.2f },
    { "two storey|two-storey|two story|two-story|mezzanine|galleried|with a gallery|balconied|with balconies|upper gallery", 1.0f, 1.0f, 1.8f },
    { "three storey|three-storey|three story|three-story|triple height|triple-height",      1.0f,  1.0f,  2.6f  },
    { "cathedral like|cathedral-like|church like|church-like|like a cathedral|like a church", 1.5f, 1.8f, 1.8f },
    { "intimate|close quarters|close-quarters|confined|enclosed|tight space",               0.8f,  0.8f,  0.9f  },
}};

/** Words about how it sounds, as a factor on every band's decay and a tilt
    (positive: the top rings longer than the bottom). */
struct SoundWord
{
    const char* names;
    float length, tilt, bottom;
};

constexpr std::array<SoundWord, 14> soundWords {{
    { "reverberant|echoey|echoing|echo|echoes|echoy|ringing|resonant|live sounding|very live|lively|washy|cavernous sounding|long reverb|long tail|long decay|rings|rings on|rings out|ambient|atmospheric", 1.35f, 0.0f, 1.0f },
    { "very reverberant|extremely reverberant|hugely reverberant|incredibly reverberant|infinite reverb|endless reverb|endless tail|forever|never ending|never-ending|goes on forever", 1.9f, 0.0f, 1.0f },
    { "dead|dry|damped|dampened|deadened|muffled|absorbent|absorbing|soundproofed|sound-proofed|sound proofed|treated room|anechoic|tight sounding|short reverb|short tail|short decay|quick decay|fast decay|no echo|no reverb|little reverb|hushed|quiet", 0.6f, 0.0f, 1.0f },
    { "very dead|very dry|extremely dry|extremely dead|bone dry|completely dead|totally dead|anechoic chamber", 0.35f, 0.0f, 1.0f },
    { "bright|brilliant|sparkling|glassy|splashy|sibilant|shimmering|crisp|crystalline|airy sounding|harsh|hard sounding|clattering|clattery|hard|hard surfaces|hard surfaced|hard-surfaced|reflective|highly reflective|shiny|glossy|slick", 1.0f, 0.35f, 1.0f },
    { "dark|warm|dull|mellow|soft sounding|soft|muted|woolly|wooly|velvety|rounded|smooth|thick sounding|plush|lush", 1.0f, -0.35f, 1.0f },
    { "boomy|booming|bass heavy|bass-heavy|bassy|rumbling|rumbly|thunderous|low end|lots of bass|big bottom|heavy bottom|tubby", 1.0f, 0.0f, 1.4f },
    { "thin|tinny|no bass|bass light|bass-light|lacking bass|hollow|boxy sounding|nasal|phasey|phasy", 1.0f, 0.0f, 0.7f },
    { "cold|clinical|sterile|hospital like|hospital-like|stark|austere|bare sounding|clean", 1.0f, 0.2f, 0.9f },
    { "damp|dank|humid|moist|wet|dripping|misty|foggy|steamy", 1.0f, -0.15f, 1.0f },
    { "metallic sounding|metallic ring|ringy|ringing metal|clangy|clanging|clang|twangy|springy|zingy", 1.15f, 0.3f, 0.9f },
    { "natural|neutral|balanced|even|pleasant|nice sounding|good acoustics|good acoustic|fine acoustics|well behaved|well-behaved", 1.0f, 0.0f, 1.0f },
    { "huge sounding|epic sounding|massive sounding|stadium sized sound|arena sound|enormous sounding", 1.5f, 0.0f, 1.1f },
    { "slapback|slap back|slap echo|flutter|flutter echo|fluttery|flutter echoes|ping|pingy|pinging", 1.1f, 0.25f, 0.9f },
}};

/** Where the listener is. */
struct PlaceWord
{
    const char* names;
    float distance;
};

constexpr std::array<PlaceWord, 7> placeWords {{
    { "close|close up|close-up|near|nearby|next to|right by|up close|intimate|in your face|front row|at the front|front of|stage front|from the stage|on the stage|on stage|close mic|close-mic|close miked|close-miked|right in front", 12.0f },
    { "fairly close|quite close|not far|a few metres|a few meters|a few feet|just in front|near the front|towards the front|toward the front", 25.0f },
    { "middle|in the middle|halfway|half way|centre|center|central|mid way|midway|from the middle|stalls|the stalls|mid hall", 45.0f },
    { "far|far away|distant|faraway|far off|far-off|way back|a long way|a long way off|across the room|other side|other end|far end|far side|from a distance|at a distance|in the distance|remote", 80.0f },
    { "at the back|from the back|back row|back of the|the back|rear|at the rear|from the rear|back wall|by the back|towards the back|toward the back|balcony|from the balcony|upper circle|gallery seats|the gods|nosebleed", 85.0f },
    { "very far|extremely far|miles away|a hundred metres|a hundred meters|far far away|other end of the|opposite end", 95.0f },
    { "behind a door|next door|through the wall|through a wall|from outside|from the corridor|from another room|in the next room|down the hall|down the corridor|offstage|off stage|off-stage|in the wings|through the door", 92.0f },
}};

/** What is in the room. */
struct ContentWord
{
    const char* names;
    float furnishing;   // the floor area covered, 0 .. 1; a negative value leaves the kind's own
};

constexpr std::array<ContentWord, 5> contentWords {{
    { "empty|bare|unfurnished|stripped|stripped out|stripped-out|vacant|nothing in it|no furniture|hollow|cleared|cleared out|deserted|abandoned room|void|just walls|four walls", 0.0f },
    { "sparse|sparsely furnished|minimal|minimalist|little furniture|a few chairs|a table|lightly furnished|half empty|half-empty|nearly empty|almost empty", 0.15f },
    { "furnished|fully furnished|lived in|lived-in|homely|comfortable|comfy|with furniture|some furniture|normal furniture", 0.5f },
    { "cluttered|full|full of stuff|crammed|stuffed|packed with|busy with|overflowing|messy|chaotic|heavily furnished|lots of furniture|wall to wall|wall-to-wall|piled high|stacked", 0.8f },
    { "crowded|full of people|packed with people|busy|audience|congregation|capacity crowd|standing room|standing room only|a crowd|crowds|lots of people|many people|full house|sold out|seated audience|in concert|during a concert|during the service|at a gig|at a concert", 0.6f },
}};

// Air absorption, 4m in Eyring's formula, per metre at room temperature and
// ordinary humidity, by band (nothing to speak of below 1 kHz).
constexpr std::array<double, 8> airAbsorption { 0.0, 0.0, 0.0001, 0.0003, 0.001, 0.0025, 0.0072, 0.024 };

//==============================================================================
std::string lowered (std::string_view in)
{
    std::string out;
    out.reserve (in.size() + 2);
    out.push_back (' ');

    const auto lower = [&] (std::size_t k) { return k < in.size() ? std::tolower (static_cast<unsigned char> (in[k])) : 0; };
    const auto digit = [&] (std::size_t k) { return k < in.size() && std::isdigit (static_cast<unsigned char> (in[k])); };

    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const auto c = in[i];
        const auto u = static_cast<unsigned char> (c);
        const auto afterDigit = i > 0 && digit (i - 1);

        // A full stop belongs to a number ("3.5", ".5") or to "sq.m"; anywhere else it ends
        // a sentence, and the word before it must still be read ("a tiled bathroom.").
        const auto numberPoint = digit (i + 1) && (i == 0 || afterDigit || ! std::isalnum (static_cast<unsigned char> (in[i - 1])));
        const auto squareMetres = i >= 2 && lower (i - 2) == 's' && lower (i - 1) == 'q' && lower (i + 1) == 'm';

        // "6x4m": an x between digits is a 'by'
        if ((c == 'x' || c == 'X') && afterDigit && digit (i + 1))
            out.append (" * ");
        else if (c == '.' && (numberPoint || squareMetres))
            out.push_back ('.');
        else if (c == '\'' && afterDigit)
            out.append (" ft "); // 12' is twelve feet
        else if (c == '"' && afterDigit)
            out.append (" inches "); // 6" is six inches
        else if (std::isalnum (u) || c == '\'' || c == '-' || c == '/' || c == '&')
            out.push_back (static_cast<char> (std::tolower (u)));
        else if (c == ',' && i > 0 && i + 1 < in.size()
                 && std::isdigit (static_cast<unsigned char> (in[i - 1])) && std::isdigit (static_cast<unsigned char> (in[i + 1])))
            out.push_back (','); // 1,500 or 3,5: numberAt reads it
        else if (c == ',' || c == '.' || c == ';' || c == ':' || c == '!' || c == '?' || c == '(' || c == ')' || c == '"' || c == '\n' || c == '\t')
            out.append (" , ");
        else if (static_cast<unsigned char> (c) >= 0x80)
            out.push_back ('_'); // a letter outside ASCII: keeps the word together without matching anything
        else
            out.push_back (' ');
    }

    out.push_back (' ');

    // 'x' between numbers is a 'by'; a 'by' between numbers too, so both read the same.
    for (std::size_t i = 1; i + 1 < out.size(); ++i)
    {
        if ((out[i] == 'x' || out[i] == 'X') && out[i - 1] == ' ' && out[i + 1] == ' ')
            out[i] = '*';
    }

    // Collapse runs of spaces.
    std::string tidy;
    tidy.reserve (out.size());

    for (const auto c : out)
        if (c != ' ' || tidy.empty() || tidy.back() != ' ')
            tidy.push_back (c);

    return tidy;
}

/** Where a word or phrase (one of a '|' list) appears in the lowered text as
    whole words, or npos. Also matches its plural. */
std::size_t findWord (const std::string& text, std::string_view names, std::size_t* length = nullptr)
{
    auto best = std::string::npos;
    std::size_t bestLength = 0;

    for (std::size_t from = 0; from <= names.size();)
    {
        const auto to = names.find ('|', from);
        const auto name = names.substr (from, to == std::string_view::npos ? std::string_view::npos : to - from);
        from = to == std::string_view::npos ? names.size() + 1 : to + 1;

        if (name.empty())
            continue;

        for (const auto suffix : { "", "s", "es" })
        {
            std::string needle;
            needle.reserve (name.size() + 4);
            needle.push_back (' ');
            needle.append (name);
            needle.append (suffix);
            needle.push_back (' ');

            if (const auto at = text.find (needle); at != std::string::npos)
            {
                // The earliest mention; for a tie, the longest phrase.
                if (at < best || (at == best && needle.size() > bestLength))
                {
                    best = at;
                    bestLength = needle.size();
                }
            }
        }
    }

    if (length != nullptr)
        *length = bestLength;

    return best;
}

std::string firstName (const char* names)
{
    const std::string_view all (names);
    return std::string (all.substr (0, all.find ('|')));
}

/** Reads a number at `at` (digits, a decimal point or comma, thousands commas). */
std::optional<double> numberAt (const std::string& text, std::size_t at, std::size_t& end)
{
    std::string digits;
    auto i = at;

    while (i < text.size() && (std::isdigit (static_cast<unsigned char> (text[i])) || text[i] == '.' || text[i] == ','))
    {
        if (text[i] == ',')
        {
            // 1,500 is a thousand and a half; 1,5 is one and a half.
            const auto next3 = i + 3 < text.size() && std::isdigit (static_cast<unsigned char> (text[i + 1]))
                               && std::isdigit (static_cast<unsigned char> (text[i + 2])) && std::isdigit (static_cast<unsigned char> (text[i + 3]));

            if (! (i + 1 < text.size() && std::isdigit (static_cast<unsigned char> (text[i + 1]))))
                break;

            if (! next3)
                digits.push_back ('.');
        }
        else
        {
            digits.push_back (text[i]);
        }

        ++i;
    }

    if (digits.empty() || digits == ".")
        return std::nullopt;

    end = i;
    const auto value = std::strtod (digits.c_str(), nullptr);

    // Hundreds of digits overflow to infinity: no room is measured like that.
    if (! std::isfinite (value))
        return std::nullopt;

    return value;
}

struct Measurements
{
    std::vector<double> lengths;  // metres, in the order written: "5 by 4 by 3"
    std::optional<double> width, depth, height, area, volume, seats;

    // The stretches of the text a measurement took, its number and units ("40 square
    // metres"), which the words for kinds, sizes and materials leave alone.
    std::vector<std::pair<std::size_t, std::size_t>> spans;
};

bool startsWith (const std::string& text, std::size_t at, std::string_view word)
{
    return at <= text.size() && text.compare (at, word.size(), word) == 0;
}

/** What a unit word after a number makes it. */
enum class UnitKind { length, area, volume, seats, notLength };

struct Unit
{
    const char* word;
    UnitKind kind;
    double scale; // metres per unit, for a length
};

constexpr Unit units[] {
    { "metres", UnitKind::length, 1.0 }, { "meters", UnitKind::length, 1.0 }, { "metre", UnitKind::length, 1.0 },
    { "meter", UnitKind::length, 1.0 }, { "m", UnitKind::length, 1.0 }, { "mtrs", UnitKind::length, 1.0 }, { "mtr", UnitKind::length, 1.0 },
    { "kilometres", UnitKind::length, 1000.0 }, { "kilometers", UnitKind::length, 1000.0 }, { "km", UnitKind::length, 1000.0 },
    { "feet", UnitKind::length, 0.3048 }, { "foot", UnitKind::length, 0.3048 }, { "ft", UnitKind::length, 0.3048 },
    { "yards", UnitKind::length, 0.9144 }, { "yard", UnitKind::length, 0.9144 }, { "yds", UnitKind::length, 0.9144 }, { "yd", UnitKind::length, 0.9144 },
    { "centimetres", UnitKind::length, 0.01 }, { "centimeters", UnitKind::length, 0.01 }, { "cm", UnitKind::length, 0.01 },
    { "millimetres", UnitKind::length, 0.001 }, { "millimeters", UnitKind::length, 0.001 }, { "mm", UnitKind::length, 0.001 },
    { "inches", UnitKind::length, 0.0254 }, { "inch", UnitKind::length, 0.0254 },
    { "sq", UnitKind::area, 1.0 }, { "square", UnitKind::area, 1.0 }, { "m2", UnitKind::area, 1.0 }, { "sqm", UnitKind::area, 1.0 }, { "sq.m", UnitKind::area, 1.0 },
    { "cubic", UnitKind::volume, 1.0 }, { "cu", UnitKind::volume, 1.0 }, { "m3", UnitKind::volume, 1.0 },
    { "seats", UnitKind::seats, 1.0 }, { "seat", UnitKind::seats, 1.0 }, { "people", UnitKind::seats, 1.0 }, { "seater", UnitKind::seats, 1.0 },
    { "person", UnitKind::seats, 1.0 }, { "places", UnitKind::seats, 1.0 }, { "capacity", UnitKind::seats, 1.0 },
    // Numbers that are not sizes: a reverb time, a pre-delay, a frequency, a level, a year.
    { "seconds", UnitKind::notLength, 0.0 }, { "second", UnitKind::notLength, 0.0 }, { "secs", UnitKind::notLength, 0.0 },
    { "sec", UnitKind::notLength, 0.0 }, { "s", UnitKind::notLength, 0.0 }, { "milliseconds", UnitKind::notLength, 0.0 },
    { "millisecond", UnitKind::notLength, 0.0 }, { "msec", UnitKind::notLength, 0.0 }, { "ms", UnitKind::notLength, 0.0 },
    { "minutes", UnitKind::notLength, 0.0 }, { "minute", UnitKind::notLength, 0.0 }, { "mins", UnitKind::notLength, 0.0 },
    { "min", UnitKind::notLength, 0.0 }, { "hz", UnitKind::notLength, 0.0 }, { "khz", UnitKind::notLength, 0.0 },
    { "db", UnitKind::notLength, 0.0 }, { "percent", UnitKind::notLength, 0.0 }, { "years", UnitKind::notLength, 0.0 },
    { "year", UnitKind::notLength, 0.0 }, { "yrs", UnitKind::notLength, 0.0 }, { "century", UnitKind::notLength, 0.0 },
};

/** The unit word at `at`, if there is one, ending at a word's end. */
const Unit* unitAt (const std::string& text, std::size_t at)
{
    for (const auto& u : units)
    {
        const std::string_view word (u.word);

        if (startsWith (text, at, word) && (at + word.size() == text.size() || text[at + word.size()] == ' '))
            return &u;
    }

    return nullptr;
}

std::size_t skipSpaces (const std::string& text, std::size_t at)
{
    while (at < text.size() && text[at] == ' ')
        ++at;

    return at;
}

/** The word that ends just before `at` (the space before a number), or empty. */
std::string_view wordBefore (const std::string& text, std::size_t at)
{
    if (at < 2)
        return {};

    const auto end = at - 1;                      // the space before
    const auto start = text.rfind (' ', end - 1); // the space before that word

    if (start == std::string::npos)
        return {};

    return std::string_view (text).substr (start + 1, end - start - 1);
}

std::string metresWords (double metres)
{
    return std::to_string (static_cast<int> (std::lround (metres))) + " m";
}

/** Reads the measurements: "5 by 4 metres", "12 m long", "3 m high",
    "40 square metres", "2000 m3", "seats 800", "20 by 30 feet", "12' 6"".
    A number with no unit is a size only when it says which ("12 long"), is one
    of the sides written in a row ("5 by 4") or counts seats: "Studio 2", "built
    in 1890" and "a 2 second reverb" measure nothing. */
Measurements readMeasurements (const std::string& text, std::vector<std::string>& understood)
{
    Measurements m;

    // Sides written in a row ("20 by 30 feet") share the unit written once: the sides
    // read before it without one wait here for it.
    struct Waiting { std::size_t length, word; };
    std::vector<Waiting> waiting;
    double rowScale = 0.0; // the unit of the row so far, once one has been written

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (! std::isdigit (static_cast<unsigned char> (text[i])) || (i > 0 && text[i - 1] != ' '))
            continue;

        std::size_t end = i;
        const auto value = numberAt (text, i, end);

        if (! value.has_value())
            continue;

        const auto after = skipSpaces (text, end);
        const auto* unit = unitAt (text, after);
        auto unitEnd = unit != nullptr ? after + std::string_view (unit->word).size() : end;
        auto number = *value;
        auto scale = unit != nullptr && unit->kind == UnitKind::length ? unit->scale : 1.0;

        // 12 ft 6 inches, 12' 6": the inches belong to the feet.
        if (unit != nullptr && unit->scale == 0.3048)
        {
            const auto next = skipSpaces (text, unitEnd);
            std::size_t inchesEnd = next;

            if (next < text.size() && std::isdigit (static_cast<unsigned char> (text[next])))
            {
                if (const auto inches = numberAt (text, next, inchesEnd))
                {
                    const auto* inchUnit = unitAt (text, skipSpaces (text, inchesEnd));

                    if (inchUnit != nullptr && inchUnit->scale == 0.0254)
                    {
                        number += *inches / 12.0;
                        unitEnd = skipSpaces (text, inchesEnd) + std::string_view (inchUnit->word).size();
                    }
                }
            }
        }

        // "square metres", "cubic feet", "m square": a unit word straight after the first
        // belongs to the measurement too.
        auto spanEnd = unitEnd;

        for (int extra = 0; extra < 2; ++extra)
        {
            const auto next = skipSpaces (text, spanEnd);
            const auto* more = unitAt (text, next);

            if (more == nullptr || (more->kind != UnitKind::length && more->kind != UnitKind::area && more->kind != UnitKind::volume))
                break;

            if (unit != nullptr && (unit->kind == UnitKind::area || unit->kind == UnitKind::volume) && more->kind == UnitKind::length)
                scale = more->scale; // "20 square feet"

            spanEnd = next + std::string_view (more->word).size();
        }

        const auto rest = skipSpaces (text, unitEnd);
        const auto resume = [&] { i = rest > i ? rest - 1 : i; };

        if (unit != nullptr && unit->kind == UnitKind::notLength)
        {
            waiting.clear();
            rowScale = 0.0;
            resume();
            continue;
        }

        // What the measurement is of.
        const auto saysNext = [&] (std::string_view word)
        {
            auto k = rest;

            // skip up to two filler words ("in", "the", "from", "across")
            for (int skip = 0; skip < 3; ++skip)
            {
                if (startsWith (text, k, word) && (k + word.size() == text.size() || text[k + word.size()] == ' '))
                    return true;

                const auto space = text.find (' ', k);

                if (space == std::string::npos)
                    return false;

                const auto filler = text.substr (k, space - k);

                if (filler != "in" && filler != "the" && filler != "from" && filler != "across" && filler != "to" && filler != "of" && filler != "or" && filler != "so" && filler != "about" && filler != "roughly" && filler != "and")
                    return false;

                k = space + 1;
            }

            return false;
        };

        const auto saysBefore = [&] (std::string_view word)
        {
            // "about 20 m long", "ceiling 3 m", "seats 800": the word just before the number
            if (i < 2)
                return false;

            auto wordEnd = i - 1; // the space before the number

            for (int skip = 0; skip < 3 && wordEnd > 0; ++skip)
            {
                const auto wordStart = text.rfind (' ', wordEnd - 1);

                if (wordStart == std::string::npos)
                    return false;

                const auto found = text.substr (wordStart + 1, wordEnd - wordStart - 1);

                if (found == word)
                    return true;

                if (found != "about" && found != "roughly" && found != "around" && found != "some" && found != "of" && found != "is" && found != "for" && found != "a" && found != "an" && found != "approximately" && found != "nearly" && found != "almost" && found != "over" && found != "maybe" && found != "perhaps")
                    return false;

                wordEnd = wordStart;
            }

            return false;
        };

        const auto isSeats = (unit != nullptr && unit->kind == UnitKind::seats)
                             || saysBefore ("seats") || saysBefore ("seating") || saysBefore ("holds") || saysBefore ("capacity") || saysNext ("seats") || saysNext ("people") || saysNext ("seater");

        if (isSeats)
        {
            if (number > 0.0)
            {
                m.seats = number;
                understood.push_back (std::to_string (static_cast<int> (std::lround (number))) + " seats");
            }

            resume();
            continue;
        }

        const auto before = wordBefore (text, i);
        const auto inRow = before == "by" || before == "*";
        const auto rowGoesOn = startsWith (text, rest, "by ") || startsWith (text, rest, "* ");

        if (unit == nullptr && inRow && rowScale > 0.0)
            scale = rowScale; // "20 ft by 30": feet too

        const auto metres = number * scale;
        const auto measured = metres > 0.0 && std::isfinite (metres);
        const auto unitIsLength = unit != nullptr && unit->kind == UnitKind::length;
        bool tookLength = false;

        const auto area = (unit != nullptr && unit->kind == UnitKind::area) || saysNext ("square") || saysNext ("sq") || saysBefore ("area");
        const auto volume = (unit != nullptr && unit->kind == UnitKind::volume) || saysNext ("cubic") || saysNext ("cu") || saysBefore ("volume");

        if (! measured)
        {
            // nothing measures nothing: "0 m long" leaves the room's own length
        }
        else if (area)
        {
            m.area = metres * scale; // "20 square feet": the feet squared
            understood.push_back (std::to_string (static_cast<int> (std::lround (*m.area))) + " square metres");
            m.spans.push_back ({ i, spanEnd });
        }
        else if (volume)
        {
            m.volume = metres * scale * scale;
            understood.push_back (std::to_string (static_cast<int> (std::lround (*m.volume))) + " cubic metres");
            m.spans.push_back ({ i, spanEnd });
        }
        else if (saysNext ("high") || saysNext ("tall") || saysNext ("ceiling") || saysNext ("ceilings") || saysNext ("headroom") || saysBefore ("ceiling") || saysBefore ("height") || saysBefore ("high") || saysBefore ("tall"))
        {
            m.height = metres;
            understood.push_back ("height " + std::to_string (static_cast<int> (std::lround (metres * 10.0))) + "0 cm");
            m.spans.push_back ({ i, spanEnd });
            tookLength = true;
        }
        else if (saysNext ("long") || saysNext ("deep") || saysNext ("length") || saysBefore ("length") || saysBefore ("long") || saysBefore ("deep") || saysNext ("away"))
        {
            m.depth = metres;
            understood.push_back ("length " + metresWords (metres));
            m.spans.push_back ({ i, spanEnd });
            tookLength = true;
        }
        else if (saysNext ("wide") || saysNext ("across") || saysNext ("width") || saysBefore ("width") || saysBefore ("wide") || saysBefore ("across"))
        {
            m.width = metres;
            understood.push_back ("width " + metresWords (metres));
            m.spans.push_back ({ i, spanEnd });
            tookLength = true;
        }
        else if (unitIsLength || inRow || rowGoesOn)
        {
            m.lengths.push_back (metres);
            understood.push_back (metresWords (metres));
            m.spans.push_back ({ i, spanEnd });
            tookLength = true;

            if (! unitIsLength && rowScale == 0.0)
                waiting.push_back ({ m.lengths.size() - 1, understood.size() - 1 });
        }

        // The unit, once written in a row, is every side's in it.
        if (tookLength && unitIsLength)
        {
            for (const auto& w : waiting)
            {
                m.lengths[w.length] *= unit->scale;
                understood[w.word] = metresWords (m.lengths[w.length]);
            }

            waiting.clear();
        }

        if (tookLength && rowGoesOn)
        {
            if (unitIsLength)
                rowScale = unit->scale;
        }
        else
        {
            waiting.clear();
            rowScale = 0.0;
        }

        resume();
    }

    // Lengths written in a row ("5 by 4 by 3", "5 x 4") are the sides: the two
    // longest across the floor, the third the height.
    if (! m.lengths.empty())
    {
        auto sides = m.lengths;

        if (sides.size() > 3)
            sides.resize (3);

        if (sides.size() == 3)
        {
            // The height is the odd one out: usually the smallest, unless the room is a
            // shaft; written last by habit.
            auto h = sides[2];
            auto a = sides[0], b = sides[1];

            if (! m.height) m.height = h;
            if (! m.depth)  m.depth = std::max (a, b);
            if (! m.width)  m.width = std::min (a, b);
        }
        else if (sides.size() == 2)
        {
            if (m.depth && ! m.width)       m.width = std::min (sides[0], sides[1]);
            else if (m.width && ! m.depth)  m.depth = std::max (sides[0], sides[1]);
            else
            {
                if (! m.depth) m.depth = std::max (sides[0], sides[1]);
                if (! m.width) m.width = std::min (sides[0], sides[1]);
            }
        }
        else
        {
            // One length on its own is the room's long side.
            if (! m.depth) m.depth = sides[0];
        }
    }

    return m;
}

/** Blanks [from, to) of the words, keeping their places, so nothing more is read there. */
void blank (std::string& words, std::size_t from, std::size_t to)
{
    for (auto k = from; k < std::min (to, words.size()); ++k)
        words[k] = ' ';
}

/** Which entries of a table of phrases the words use, each once: the longest phrase
    first, then the next longest in what is left, so "very long" is not also "long" and
    "quite small" not also "small". Each found phrase is blanked from the words. Gives,
    per entry, where it was found (npos if not) and how long the match was. */
template <typename Entry, std::size_t N>
std::array<std::pair<std::size_t, std::size_t>, N> takePhrases (std::string& words, const std::array<Entry, N>& table)
{
    std::array<std::pair<std::size_t, std::size_t>, N> found;
    found.fill ({ std::string::npos, 0 });

    for (;;)
    {
        auto best = N;
        auto bestAt = std::string::npos;
        std::size_t bestLength = 0;

        for (std::size_t e = 0; e < N; ++e)
        {
            if (found[e].first != std::string::npos)
                continue;

            std::size_t length = 0;
            const auto at = findWord (words, table[e].names, &length);

            if (at != std::string::npos && (length > bestLength || (length == bestLength && at < bestAt)))
            {
                best = e;
                bestAt = at;
                bestLength = length;
            }
        }

        if (best == N)
            return found;

        found[best] = { bestAt, bestLength };
        blank (words, bestAt + 1, bestAt + bestLength - 1); // keeps the spaces either side
    }
}

std::string tidyName (std::string_view words)
{
    std::string out;
    bool space = true;

    for (const auto c : words)
    {
        const auto u = static_cast<unsigned char> (c);

        if (std::isspace (u) || c == '\n' || c == '\t')
        {
            if (! space)
                out.push_back (' ');

            space = true;
        }
        else
        {
            out.push_back (c);
            space = false;
        }
    }

    while (! out.empty() && (out.back() == ' ' || out.back() == '.' || out.back() == ','))
        out.pop_back();

    if (! out.empty())
        out[0] = static_cast<char> (std::toupper (static_cast<unsigned char> (out[0])));

    // Cut a long description at a word, with room for an ellipsis.
    constexpr std::size_t longest = 48;

    if (out.size() > longest)
    {
        auto cut = out.rfind (' ', longest - 3);

        if (cut == std::string::npos || cut < 16)
            cut = longest - 3;

        // Never inside a character of more than one byte (UTF-8 continuation bytes are
        // 10xxxxxx): back up to where the character starts.
        while (cut > 0 && (static_cast<unsigned char> (out[cut]) & 0xC0) == 0x80)
            --cut;

        out.resize (cut);
        out.append ("...");
    }

    return out;
}

std::string joinNames (const std::vector<std::string>& names)
{
    std::string out;

    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
            out.append (i + 1 == names.size() ? " and " : ", ");

        out.append (names[i]);
    }

    return out;
}

} // namespace

//==============================================================================
DescribedRoom describeRoom (std::string_view words)
{
    DescribedRoom room;
    const auto text = lowered (words);
    room.name = tidyName (words);

    // The measurements first: the words they took ("40 square metres", "300 cubic
    // metres") are not read again as a kind ("square"), a size or a material.
    std::vector<std::string> measuredWords;
    const auto m = readMeasurements (text, measuredWords);
    auto plain = text;

    for (const auto& [from, to] : m.spans)
        blank (plain, from, to);

    // The kind of room: the first one named. The longest name wins a tie at the same
    // place ("concert hall" over "hall"), so the tables can list both.
    const Kind* kind = nullptr;
    auto kindAt = std::string::npos;
    std::size_t kindLength = 0;

    for (const auto& k : kinds)
    {
        std::size_t length = 0;
        const auto at = findWord (plain, k.names, &length);

        if (at != std::string::npos && (at < kindAt || (at == kindAt && length > kindLength)))
        {
            kind = &k;
            kindAt = at;
            kindLength = length;
        }
    }

    // The generic room, when nothing was named.
    if (kind == nullptr)
    {
        for (const auto& k : kinds)
            if (std::string_view (k.names).rfind ("room|", 0) == 0)
                kind = &k;

        room.knownRoom = false;
    }
    else
    {
        room.knownRoom = true;
        room.understood.push_back (text.substr (kindAt + 1, kindLength - 2));
    }

    room.kind = kind->uiKind;
    room.place = kind->shown;

    double W = kind->width, D = kind->depth, H = kind->height;
    auto furnishing = static_cast<double> (kind->furnishing);
    auto furnishingMaterial = kind->furnishingMaterial;
    room.distancePercent = kind->distance;

    // Sizes, sounds and where the listener is are not read inside the kind's own name: a
    // "long corridor" is already long, an "echo chamber" already echoes.
    auto beyondKind = plain;

    if (kindAt != std::string::npos)
        blank (beyondKind, kindAt + 1, kindAt + kindLength - 1);

    // Size words, all of them: "long narrow" is both; "very long" is not also "long".
    bool square = false;
    auto sizeText = beyondKind;
    const auto sizesFound = takePhrases (sizeText, sizeWords);

    for (std::size_t e = 0; e < sizeWords.size(); ++e)
    {
        const auto& s = sizeWords[e];
        const auto [at, length] = sizesFound[e];

        if (at == std::string::npos)
            continue;

        room.understood.push_back (text.substr (at + 1, length - 2));

        if (std::string_view (s.names).rfind ("square|", 0) == 0)
            square = true;
        else
        {
            W *= s.width;
            D *= s.depth;
            H *= s.height;
        }
    }

    if (square)
        W = D = std::sqrt (W * D);

    // Measurements override the words. Each is finite and above zero (readMeasurements
    // drops the rest), and the sides are kept sensible before an area, a number of seats
    // or a volume scales them, so nothing here can divide by zero or make a NaN.
    room.understood.insert (room.understood.end(), measuredWords.begin(), measuredWords.end());

    if (m.width)  W = *m.width;
    if (m.depth)  D = *m.depth;
    if (m.height) H = *m.height;

    const auto keepSensible = [&]
    {
        W = std::clamp (W, 0.6, 400.0);
        D = std::clamp (D, 0.6, 400.0);
        H = std::clamp (H, 0.6, 120.0);
    };

    keepSensible();

    if (m.area && *m.area > 0.0)
    {
        const auto k = std::sqrt (*m.area / (W * D));
        W *= k;
        D *= k;
    }

    if (m.seats && *m.seats > 0.0)
    {
        // About 8 cubic metres a seat in a hall, a church or a theatre, over a floor of
        // about 0.7 square metres a seat; the proportions stay the kind's.
        const auto volume = std::max (20.0, *m.seats * 8.0);
        const auto k = std::cbrt (volume / (W * D * H));
        W *= k;
        D *= k;
        H *= k;
        furnishing = std::max (furnishing, 0.6);
        furnishingMaterial = seats;
    }

    if (m.volume && *m.volume > 0.0)
    {
        const auto k = std::cbrt (*m.volume / (W * D * H));
        W *= k;
        D *= k;
        H *= k;
    }

    if (! std::isfinite (W) || ! std::isfinite (D) || ! std::isfinite (H))
    {
        W = kind->width;
        D = kind->depth;
        H = kind->height;
    }

    keepSensible();

    // Materials: each one named replaces what the kind had on the surfaces it is said
    // to be on, or on the surfaces it usually covers.
    struct Chosen { int material; int surfaces; float share; };
    std::vector<Chosen> chosen;
    std::vector<std::string> shownMaterials;

    for (std::size_t mi = 0; mi < materials.size(); ++mi)
    {
        const auto& mat = materials[mi];
        std::size_t length = 0;
        const auto at = findWord (plain, mat.names, &length);

        if (at == std::string::npos)
            continue;

        // Don't read a kind's own name as a material ("stone chapel" names stone; "car park"
        // does not name cars, "swimming pool" does not name water).
        if (kindAt != std::string::npos && at >= kindAt && at < kindAt + kindLength - 1)
            continue;

        room.understood.push_back (text.substr (at + 1, length - 2));

        // A surface word within a few words either side: "carpeted floor", "walls of stone".
        int surfaces = 0;
        const auto before = at >= 24 ? at - 24 : 0;
        const auto window = plain.substr (before, std::min (plain.size(), at + length + 18) - before);

        for (const auto& sw : surfaceWords)
            if (findWord (window, sw.names) != std::string::npos)
                surfaces |= sw.surface;

        if (surfaces == 0)
            surfaces = mat.surfaces;

        chosen.push_back ({ static_cast<int> (mi), surfaces, mat.share });

        if (std::find (shownMaterials.begin(), shownMaterials.end(), mat.shown) == shownMaterials.end())
            shownMaterials.push_back (mat.shown);
    }

    // What is in it.
    for (const auto& c : contentWords)
    {
        std::size_t length = 0;
        const auto at = findWord (plain, c.names, &length);

        if (at == std::string::npos)
            continue;

        room.understood.push_back (text.substr (at + 1, length - 2));

        if (std::string_view (c.names).rfind ("crowded|", 0) == 0)
        {
            chosen.push_back ({ people, crowd, 0.0f });
        }
        else
        {
            furnishing = c.furnishing;
        }
    }

    // Surfaces in square metres.
    const auto floorArea = W * D;
    const auto wallArea = 2.0 * (W + D) * H;
    const auto ceilingArea = floorArea;
    const auto openCeiling = kind->ceilingMaterial == open;

    // Each surface's absorption: the kind's material, replaced in part or whole by the
    // materials named for it. Several named for one surface share it.
    std::array<double, 8> absorbing {}; // Sabine absorption (square metres) per band
    double surface = floorArea + wallArea + ceilingArea;

    const auto addSurface = [&] (int surfaceBit, double area, int defaultMaterial)
    {
        double covered = 0.0;
        std::array<double, 8> sum {};

        for (const auto& c : chosen)
        {
            if ((c.surfaces & surfaceBit) == 0)
                continue;

            const auto& alpha = materials[static_cast<std::size_t> (c.material)].alpha;

            for (std::size_t b = 0; b < 8; ++b)
                sum[b] += c.share * alpha[b];

            covered += c.share;
        }

        if (covered > 1.0)
        {
            for (auto& s : sum)
                s /= covered;

            covered = 1.0;
        }

        for (std::size_t b = 0; b < 8; ++b)
        {
            const auto rest = defaultMaterial == open ? 1.0 : static_cast<double> (materials[static_cast<std::size_t> (defaultMaterial)].alpha[b]);
            absorbing[b] += area * (sum[b] + (1.0 - covered) * rest);
        }
    };

    addSurface (walls, wallArea, kind->wallMaterial);
    addSurface (floor, floorArea, kind->floorMaterial);
    addSurface (ceiling, ceilingArea, kind->ceilingMaterial);

    // Things in the room: an area of absorbing stuff on top of the floor.
    {
        double peopleArea = 0.0;

        for (const auto& c : chosen)
            if (c.material == people && c.surfaces == crowd)
                peopleArea = floorArea * 0.6;

        const auto furnishedArea = floorArea * std::clamp (furnishing, 0.0, 1.0);
        const auto& furnishAlpha = materials[static_cast<std::size_t> (furnishingMaterial)].alpha;

        for (std::size_t b = 0; b < 8; ++b)
            absorbing[b] += furnishedArea * furnishAlpha[b] + peopleArea * materials[people].alpha[b];

        surface += furnishedArea + peopleArea;

        // Materials named as furnishing (sofas, pews, seats) cover a share of the floor too.
        for (const auto& c : chosen)
        {
            if ((c.surfaces & Surface::furnishing) == 0 || c.surfaces == crowd)
                continue;

            const auto area = floorArea * c.share;
            const auto& alpha = materials[static_cast<std::size_t> (c.material)].alpha;

            for (std::size_t b = 0; b < 8; ++b)
                absorbing[b] += area * alpha[b];

            surface += area;
        }
    }

    // How it sounds: a factor on the decay, a tilt and a bottom.
    double length = 1.0, tilt = 0.0, bottom = 1.0;

    auto soundText = beyondKind;
    const auto soundsFound = takePhrases (soundText, soundWords);

    for (std::size_t e = 0; e < soundWords.size(); ++e)
    {
        const auto& s = soundWords[e];
        const auto [at, len] = soundsFound[e];

        if (at == std::string::npos)
            continue;

        room.understood.push_back (text.substr (at + 1, len - 2));
        length *= s.length;
        tilt += s.tilt;
        bottom *= s.bottom;
    }

    auto placeText = beyondKind;
    const auto placesFound = takePhrases (placeText, placeWords);

    for (std::size_t e = 0; e < placeWords.size(); ++e)
    {
        const auto& p = placeWords[e];
        const auto [at, len] = placesFound[e];

        if (at == std::string::npos)
            continue;

        room.understood.push_back (text.substr (at + 1, len - 2));
        room.distancePercent = p.distance;
    }

    // Eyring: T = 0.161 V / (-S ln (1 - a) + 4 m V), with the sky as a perfect absorber.
    const auto volume = W * D * H;

    for (std::size_t b = 0; b < 8; ++b)
    {
        const auto alpha = std::clamp (absorbing[b] / surface, 0.01, 0.99);
        auto t = 0.161 * volume / (-surface * std::log (1.0 - alpha) + 4.0 * airAbsorption[b] * volume);

        // Outdoors there is no reverberant field to speak of beyond the first few
        // reflections; what Eyring gives is already short from the open sky.
        t *= length;

        // Tilt: the top bands longer or shorter than the bottom ones, about the middle.
        const auto position = (static_cast<double> (b) - 3.5) / 3.5; // -1 at 63 Hz, +1 at 8 kHz
        t *= std::exp (tilt * position);

        if (b < 3)
            t *= std::pow (bottom, (3.0 - static_cast<double> (b)) / 3.0);

        room.decaySeconds[b] = static_cast<float> (std::clamp (t, 0.1, 20.0));
    }

    // Pre-delay from the mean free path, about half of it; distance as the kind
    // or the words put the listener. Early/late leans late in big rooms.
    const auto meanFreePath = 4.0 * volume / (floorArea + wallArea + ceilingArea);
    room.predelayMs = static_cast<float> (std::clamp (std::round (0.5 * meanFreePath / speedOfSound * 1000.0), 0.0, 250.0));
    room.earlyLatePercent = static_cast<float> (std::clamp (std::round (45.0 + 5.0 * std::log10 (std::max (1.0, volume / 10.0))), 35.0, 70.0));

    if (openCeiling)
        room.earlyLatePercent = std::min (room.earlyLatePercent, 50.0f);

    room.width = static_cast<float> (W);
    room.depth = static_cast<float> (D);
    room.height = static_cast<float> (H);

    if (shownMaterials.empty())
    {
        std::vector<std::string> own;

        for (const auto id : { kind->wallMaterial, kind->floorMaterial })
            if (id != open && std::find (own.begin(), own.end(), materials[static_cast<std::size_t> (id)].shown) == own.end())
                own.push_back (materials[static_cast<std::size_t> (id)].shown);

        if (openCeiling)
            own.push_back ("open sky");

        room.materials = joinNames (own);
    }
    else
    {
        room.materials = joinNames (shownMaterials);
    }

    if (room.name.empty())
        room.name = "Described room";

    return room;
}

std::vector<std::string> describableRooms()
{
    std::vector<std::string> out;

    for (const auto& k : kinds)
        out.push_back (firstName (k.names));

    return out;
}

std::vector<std::string> describableMaterials()
{
    std::vector<std::string> out;

    for (const auto& m : materials)
        out.push_back (firstName (m.names));

    return out;
}

} // namespace drenched

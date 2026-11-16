#include "horizon/Types.hpp"
#include <stdexcept>
#include <utility>

namespace horizon {
double distance(Vec2 a, Vec2 b) { return std::hypot(a.x-b.x, a.y-b.y); }
bool overlaps(const Aabb& a, const Aabb& b) {
    return a.position.x < b.position.x+b.width && a.position.x+a.width > b.position.x &&
           a.position.y < b.position.y+b.height && a.position.y+a.height > b.position.y;
}
const TileDefinition& tile_definition(TileType type) {
    static const TileDefinition definitions[] = {
        {"Vacuum", false, false, false, 0, ' ', 0x090e20},
        {"Loose regolith", true, true, true, 0.45, '.', 0x75645c},
        {"Hydrated phyllosilicates", true, true, false, 1.3, 'H', 0x328b8d},
        {"Iron/nickel ore", true, true, false, 1.8, 'O', 0xb4784d},
        {"Water ice", true, true, false, 0.8, 'I', 0x99dce8},
        {"Salvaged alloy", true, true, false, 0.65, 'S', 0x8a9aab},
        {"NASA data core", true, true, false, 2.0, '*', 0x81ffb1},
        {"Asteroid bedrock", true, false, false, 0, '#', 0x393747},
        {"Rocket scaffolding", false, false, false, 0, '|', 0xb7a85d},
        {"Rock fragments",true,true,false,1.1,'B',0x96816a},
        {"Silicate minerals",true,true,false,1.2,'S',0xbca58d},
        {"Water ice barrier",true,true,false,0.9,'I',0x99dce8}
    };
    const auto index = static_cast<unsigned>(type);
    if (index >= sizeof(definitions)/sizeof(definitions[0])) throw std::out_of_range("tile type");
    return definitions[index];
}
const char* item_name(Item item) {
    static const char* names[] = {"Regolith", "Hydrated minerals", "Iron/nickel ore", "Water ice",
        "Purified water", "Scrap alloys", "Hydrogen gas", "Oxygen gas", "Liquid hydrogen",
        "Liquid oxygen", "Refined fuel kit", "Structural plates", "Polyethylene", "NASA data core", "Slag", "Food and water supplement", "First-aid kit", "Silicate sample", "Water ice sample", "Rock fragments", "USB drive", "Mars Exploration data chip"};
    const auto index = static_cast<std::size_t>(item);
    if (index >= item_count) throw std::out_of_range("item");
    return names[index];
}
bool measured_in_grams(Item item) {
    return item != Item::USBDrive && item != Item::MarsDataChip && item != Item::SilicateSample && item != Item::IceSample && item != Item::RockSample && item != Item::FoodWaterSupply && item != Item::FirstAidKit && item != Item::DataCore && item != Item::StructuralPlates && item != Item::RefinedFuel;
}
ActionResult ActionResult::success(std::string message) { return {true, std::move(message)}; }
ActionResult ActionResult::failure(std::string message) { return {false, std::move(message)}; }
}

#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace horizon {
struct Vec2 { double x = 0.0, y = 0.0; };
struct GridPos { int x = 0, y = 0; };
struct Aabb { Vec2 position; double width = 0.7, height = 0.85; };
double distance(Vec2 a, Vec2 b);
bool overlaps(const Aabb& a, const Aabb& b);
enum class TileType { VacuumAir, Regolith, HydratedMinerals, IronNickelOre,
                      WaterIce, ScrapAlloy, NASADataCore, Bedrock, Scaffold, SupplyBarrier, SilicateBarrier, IceBarrier };
struct TileDefinition {
    const char* name; bool solid; bool mineable; bool loose;
    double hardness; char glyph; std::uint32_t color;
};
const TileDefinition& tile_definition(TileType type);
enum class Item { Regolith, HydratedMinerals, IronNickelOre, WaterIce, Water,
                  ScrapAlloys, HydrogenGas, OxygenGas, LiquidHydrogen, LiquidOxygen,
                  RefinedFuel, StructuralPlates, Polyethylene, DataCore, Slag, FoodWaterSupply, FirstAidKit, SilicateSample, IceSample, RockSample, USBDrive, MarsDataChip, Count };
constexpr std::size_t item_count = static_cast<std::size_t>(Item::Count);
const char* item_name(Item item);
bool measured_in_grams(Item item);
enum class Upgrade { SolarThermalDrill, DustRepellentTreads, RadiationShielding };
struct ActionResult {
    bool ok; std::string message;
    static ActionResult success(std::string message);
    static ActionResult failure(std::string message);
};
} // namespace horizon

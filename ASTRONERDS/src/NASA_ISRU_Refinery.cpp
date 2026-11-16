#include "horizon/NASA_ISRU_Refinery.hpp"
#include "horizon/BaseCamp.hpp"
#include <cmath>
#include <stdexcept>

namespace horizon {
namespace {
ActionResult away() { return ActionResult::failure("Return to the base crafting bench (within 2.5 tiles)."); }
ActionResult crafted(bool ok, const char* success, const char* missing) {
    return ok ? ActionResult::success(success) : ActionResult::failure(missing);
}
}
NASA_ISRU_Refinery::NASA_ISRU_Refinery(Vec2 station, ISRUConfig config) : station_(station), config_(config) {
    if (!std::isfinite(station.x) || !std::isfinite(station.y) ||
        !std::isfinite(config.hydrated_water_mass_fraction) || config.hydrated_water_mass_fraction <= 0 || config.hydrated_water_mass_fraction > 1 ||
        !std::isfinite(config.recovery_efficiency) || config.recovery_efficiency <= 0 || config.recovery_efficiency > 1 ||
        !std::isfinite(config.extraction_temperature_kelvin) || config.extraction_temperature_kelvin < 273.15)
        throw std::invalid_argument("invalid refinery configuration");
}
void NASA_ISRU_Refinery::set_secondary_bench(Vec2 bench, bool require_base_interior) {
    if (!std::isfinite(bench.x) || !std::isfinite(bench.y)) throw std::invalid_argument("invalid secondary bench position");
    secondary_bench_ = bench;
    secondary_requires_base_ = require_base_interior;
}
bool NASA_ISRU_Refinery::can_operate(const Unit7Entity& unit) const {
    return ((!indoor_only_ || BaseCamp::inside(unit.center())) && unit.open_crafting_bench(station_)) ||
        (secondary_bench_ && (!secondary_requires_base_ || BaseCamp::inside(unit.center())) && unit.open_crafting_bench(*secondary_bench_));
}
Vec2 NASA_ISRU_Refinery::operating_position(const Unit7Entity& unit) const {
    return secondary_bench_ && (!secondary_requires_base_ || BaseCamp::inside(unit.center())) && unit.open_crafting_bench(*secondary_bench_) &&
        distance(unit.center(),*secondary_bench_) < distance(unit.center(),station_) ? *secondary_bench_ : station_;
}
ActionResult NASA_ISRU_Refinery::thermal_extraction(Unit7Entity& unit) const {
    if (!can_operate(unit)) return away();
    const auto water = static_cast<std::int64_t>(std::llround(20000*config_.hydrated_water_mass_fraction*config_.recovery_efficiency));
    if (water <= 0) return ActionResult::failure("Feedstock yield is below the one-gram resolution.");
    // Heating releases bound water; capture/purification are bundled into recovery.
    // NASA describes the process chain; 10 wt% and 90% recovery are explicit design inputs.
    return crafted(unit.inventory().exchange({{Item::HydratedMinerals,20000}},
        {{Item::Water,water},{Item::Slag,20000-water}}),
        "Thermal extraction complete: purified water and dry residue recovered.", "Need 20000 g hydrated minerals.");
}
ActionResult NASA_ISRU_Refinery::melt_ice(Unit7Entity& unit) const {
    if (!can_operate(unit)) return away();
    return crafted(unit.inventory().exchange({{Item::WaterIce,1800}},{{Item::Water,1800}}),
        "Ice melted and purified: 1800 g water.","Need 1800 g water ice.");
}
ActionResult NASA_ISRU_Refinery::electrolysis(Unit7Entity& unit) const {
    if (!can_operate(unit)) return away();
    // 2 H2O -> 2 H2 + O2: 2:1 MOLAR ratio, approximately 1:8 by MASS.
    // Whole-number atomic masses make 1800 g -> 200 g + 1600 g exactly conserved.
    // Real atomic weights give ~11.19% H2; this game uses 11.11% to keep integer batches.
    return crafted(unit.inventory().exchange({{Item::Water,1800}},{{Item::HydrogenGas,200},{Item::OxygenGas,1600}}),
        "Electrolysis: 200 g H2 gas + 1600 g O2 gas. Cryogenic cooling is still required.","Need 1800 g purified water.");
}
ActionResult NASA_ISRU_Refinery::liquefy_propellant(Unit7Entity& unit) const {
    if (!can_operate(unit)) return away();
    // The base includes a powered cryocooler: electrolysis alone does not make liquids.
    // Power, drying, cooling losses, boil-off and process time are abstracted.
    return crafted(unit.inventory().exchange({{Item::HydrogenGas,200},{Item::OxygenGas,1600}},
        {{Item::LiquidHydrogen,200},{Item::LiquidOxygen,1600}}),
        "Cryocooler: 200 g LH2 + 1600 g LOX stored.","Need 200 g H2 gas and 1600 g O2 gas.");
}
ActionResult NASA_ISRU_Refinery::package_refined_fuel(Unit7Entity& unit) const {
    if (!can_operate(unit)) return away();
    return crafted(unit.inventory().exchange({{Item::LiquidHydrogen,200},{Item::LiquidOxygen,1600}},{{Item::RefinedFuel,1}}),
        "One sealed 1.8 kg propellant kit prepared.","Need 200 g LH2 and 1600 g LOX.");
}
ActionResult NASA_ISRU_Refinery::alloy_smelting(Unit7Entity& unit) const {
    if (!can_operate(unit)) return away();
    // Ore sorting/reduction, regolith flux and forming are bundled; arbitrary soil
    // does not become pure metal by simply melting it. Each plate represents 400 g.
    return crafted(unit.inventory().exchange({{Item::IronNickelOre,1000},{Item::Regolith,1000}},
        {{Item::StructuralPlates,2},{Item::Slag,1200}}),
        "Two 400 g structural plates formed; 1200 g slag retained.","Need 1000 g ore and 1000 g regolith.");
}
ActionResult NASA_ISRU_Refinery::craft_upgrade(Unit7Entity& unit, Upgrade upgrade) const {
    if (!can_operate(unit)) return away();
    if (unit.has_upgrade(upgrade)) return ActionResult::failure("Upgrade is already installed.");
    const Item extra = upgrade == Upgrade::RadiationShielding ? Item::Polyethylene : Item::ScrapAlloys;
    if (!unit.inventory().exchange({{Item::StructuralPlates,2},{extra,1000}},{}))
        return ActionResult::failure(std::string("Need 2 structural plates and 1000 g ")+item_name(extra)+".");
    unit.install_upgrade(upgrade);
    return ActionResult::success("Upgrade installed.");
}
}

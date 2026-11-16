#pragma once
#include "horizon/Unit7Entity.hpp"
#include <optional>

namespace horizon {
struct ISRUConfig {
    // Selected fictional feedstock, NOT a measured Phobos bulk water abundance.
    double hydrated_water_mass_fraction = 0.10;
    double recovery_efficiency = 0.90;
    double extraction_temperature_kelvin = 873.15;
};
class NASA_ISRU_Refinery {
public:
    explicit NASA_ISRU_Refinery(Vec2 station, ISRUConfig config = {});
    Vec2 position() const { return station_; }
    const ISRUConfig& config() const { return config_; }
    // The secondary bench is an indoor habitat station; its use requires entry.
    void set_secondary_bench(Vec2 bench, bool require_base_interior = true);
    void clear_secondary_bench() { secondary_bench_.reset(); secondary_requires_base_ = true; }
    void set_indoor_only(bool value = true) { indoor_only_ = value; }
    bool indoor_only() const { return indoor_only_; }
    const std::optional<Vec2>& secondary_bench() const { return secondary_bench_; }
    bool can_operate(const Unit7Entity& unit) const;
    Vec2 operating_position(const Unit7Entity& unit) const;
    ActionResult thermal_extraction(Unit7Entity& unit) const;
    ActionResult melt_ice(Unit7Entity& unit) const;
    ActionResult electrolysis(Unit7Entity& unit) const;
    ActionResult liquefy_propellant(Unit7Entity& unit) const;
    ActionResult package_refined_fuel(Unit7Entity& unit) const;
    ActionResult alloy_smelting(Unit7Entity& unit) const;
    ActionResult craft_upgrade(Unit7Entity& unit, Upgrade upgrade) const;
private:
    Vec2 station_;
    std::optional<Vec2> secondary_bench_;
    bool secondary_requires_base_ = true;
    bool indoor_only_ = false;
    ISRUConfig config_;
};
}

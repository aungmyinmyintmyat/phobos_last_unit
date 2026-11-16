#include "horizon/Unit7Entity.hpp"
#include "horizon/PlanetTerrain.hpp"
#include <algorithm>
#include <stdexcept>

namespace horizon {
namespace {
std::size_t upgrade_index(Upgrade upgrade) {
    const auto index = static_cast<std::size_t>(upgrade);
    if (index >= 3) throw std::out_of_range("upgrade");
    return index;
}
constexpr double idle_battery_per_second = 0.003;
constexpr double movement_battery_per_unit = 0.04;
constexpr double jump_battery_cost = 0.35;
constexpr double mining_battery_per_second = 0.10;
constexpr double placement_battery_cost = 0.12;
}
Unit7Entity::Unit7Entity(Vec2 spawn) : position_(spawn) {
    if (!std::isfinite(spawn.x) || !std::isfinite(spawn.y) ||
        std::abs(spawn.x) > 4096 || std::abs(spawn.y) > 4096)
        throw std::invalid_argument("invalid robot spawn");
}
bool Unit7Entity::spend_battery(double amount) {
    if (!alive() || !std::isfinite(amount) || amount < 0 || amount > battery_percent_) return false;
    drain_battery(amount);
    return true;
}
void Unit7Entity::transfer_to(Vec2 center) {
    if(!std::isfinite(center.x) || !std::isfinite(center.y)) return;
    stop_actuators(); position_={center.x-0.35,center.y-0.425};
    elevation_initialized_=false; grounded_=false; elevation_=ground_elevation_=0;
}
void Unit7Entity::drain_battery(double amount) {
    if (!std::isfinite(amount) || amount < 0) return;
    battery_percent_ = std::max(0.0,battery_percent_-amount);
    if (!alive()) stop_actuators();
}
void Unit7Entity::recharge_battery(double amount) {
    // A depleted unit has already shut down; only a new mission can restart it.
    if (!alive() || !std::isfinite(amount) || amount < 0) return;
    battery_percent_ += std::min(amount,100.0-battery_percent_);
}
void Unit7Entity::apply_damage(double amount) {
    if (!alive() || !std::isfinite(amount) || amount <= 0) return;
    integrity_ = std::max(0.0,integrity_-amount);
    if (!alive()) stop_actuators();
}
void Unit7Entity::update_wreck(double dt,const WorldGrid& world) {
    if (alive() || fell_into_pit_ || !world.planar() || !std::isfinite(dt) || dt <= 0) return;
    dt = std::min(dt,0.25);
    while (dt > 1e-9) {
        const double step = std::min(dt,1.0/120);
        ground_elevation_ = support_height(position_,world);
        if (elevation_ > ground_elevation_+0.0001) {
            grounded_ = false;
            elevation_ += vertical_velocity_*step-0.5*jump_gravity*step*step;
            vertical_velocity_ -= jump_gravity*step;
        }
        if (elevation_ <= ground_elevation_) {
            elevation_ = ground_elevation_; vertical_velocity_ = 0; grounded_ = true;
        }
        dt -= step;
    }
}
void Unit7Entity::move_left() { movement_ = alive() ? -1 : 0; }
void Unit7Entity::move_right() { movement_ = alive() ? 1 : 0; }
void Unit7Entity::stop_moving() { movement_ = 0; }
void Unit7Entity::jump() { if (alive()) jump_requested_ = true; }
void Unit7Entity::set_climbing(bool enabled) { climbing_ = alive() && enabled; }
void Unit7Entity::move_on_plane(Vec2 direction, bool request_jump) {
    if (!alive() || !std::isfinite(direction.x) || !std::isfinite(direction.y)) return;
    const double length = std::hypot(direction.x,direction.y);
    planar_input_ = length > 0 ? Vec2{direction.x/length,direction.y/length} : Vec2{};
    if (request_jump) jump_requested_ = true;
}
bool Unit7Entity::near(Vec2 point, double reach) const {
    return alive() && std::isfinite(reach) && reach >= 0 && distance(center(),point) <= reach;
}
bool Unit7Entity::open_crafting_bench(Vec2 bench) const { return near(bench); }
bool Unit7Entity::has_upgrade(Upgrade upgrade) const { return upgrades_[upgrade_index(upgrade)]; }
void Unit7Entity::install_upgrade(Upgrade upgrade) { upgrades_[upgrade_index(upgrade)] = true; }
bool Unit7Entity::collides(Vec2 position, const WorldGrid& world) const {
    constexpr double epsilon = 0.000001;
    const int left = int(std::floor(position.x+epsilon));
    const int right = int(std::floor(position.x+0.7-epsilon));
    const int top = int(std::floor(position.y+epsilon));
    const int bottom = int(std::floor(position.y+0.85-epsilon));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (!world.planar()) { if (world.solid(x,y)) return true; continue; }
            if (!world.in_bounds(x,y)) return true;
            if (world.mission4_cliff_cell(x,y)) return true;
            const double floor = world.continuous_ground() && !world.solid(x,y) ? PlanetTerrain::surface_height(world,{position.x+0.35,position.y+0.425}) : world.elevation(x,y);
            // Only grounded walking can auto-step a small stair. An airborne body
            // must physically clear the side of the raised surface or obstacle.
            const double step = grounded_ ? 0.65 : 0.00001;
            if (floor > elevation_+step) return true;
            if (world.solid(x,y) && elevation_ < floor+world.obstacle_height(x,y)-0.00001)
                return true;
        }
    }
    return false;
}
double Unit7Entity::support_height(Vec2 position, const WorldGrid& world) const {
    double support = world.continuous_ground() ? PlanetTerrain::surface_height(world,{position.x+0.35,position.y+0.425}) : -1000;
    for (int y = int(std::floor(position.y+0.00001)); y <= int(std::floor(position.y+0.85-0.00001)); ++y)
        for (int x = int(std::floor(position.x+0.00001)); x <= int(std::floor(position.x+0.7-0.00001)); ++x) {
            if (!world.in_bounds(x,y) || world.pit(x,y)) continue;
            if (!world.continuous_ground() || world.solid(x,y))
                support = std::max(support,world.elevation(x,y)+world.obstacle_height(x,y));
        }
    return support;
}
bool Unit7Entity::can_climb(const WorldGrid& world) const {
    for (int y = int(std::floor(position_.y)); y <= int(std::floor(position_.y+0.85)); ++y)
        for (int x = int(std::floor(position_.x)); x <= int(std::floor(position_.x+0.7)); ++x)
            if (world.get_tile_type(x,y) == TileType::Scaffold) return true;
    return collides({position_.x-0.06,position_.y},world) || collides({position_.x+0.06,position_.y},world);
}
void Unit7Entity::update(double dt, const WorldGrid& world) {
    if (!alive() || !std::isfinite(dt) || dt <= 0 || dt > 0.25) return;
    // Small bounded substeps plus per-axis collision keep a fast fall from tunneling.
    while (dt > 1e-9 && alive()) {
        const double step = std::min(dt,1.0/120.0);
        impact_cooldown_ = std::max(0.0,impact_cooldown_-step);
        const Vec2 before = position_;
        const double initial_support = world.planar() && !elevation_initialized_ ? support_height(position_,world) : elevation_;
        const double before_elevation = initial_support < -999 ? 0 : initial_support;
        if (world.planar()) planar_step(step,world); else physics_step(step,world);
        // Count travel after collision resolution, including airborne movement.
        // Initializing the ground reference is not an actual vertical movement.
        const double vertical_travel = world.planar() ? elevation_-before_elevation : 0.0;
        const double travel = std::hypot(distance(position_,before),vertical_travel);
        drain_battery(idle_battery_per_second*step+movement_battery_per_unit*travel);
        dt -= step;
    }
}
void Unit7Entity::planar_step(double dt, const WorldGrid& world) {
    if (!elevation_initialized_) {
        const double support = support_height(position_,world);
        elevation_ = ground_elevation_ = support > -999 ? support : 0;
        fall_origin_elevation_ = support > -999 ? world.elevation(int(center().x),int(center().y)) : 0;
        grounded_ = support > -999; elevation_initialized_ = true;
    }
    if (jump_requested_ && grounded_ && spend_battery(jump_battery_cost)) {
        if (!alive()) return;
        vertical_velocity_ = jump_impulse; grounded_ = false;
    }
    jump_requested_ = false; // Airborne presses never queue an automatic second jump.
    const double speed = has_upgrade(Upgrade::DustRepellentTreads) ? 4.8 : 3.7;
    const bool driving = std::hypot(planar_input_.x,planar_input_.y) > 0;
    const double forward_speed = velocity_.x*planar_input_.x+velocity_.y*planar_input_.y;
    // High ground traction gives a short, predictable stop and firm direction
    // changes. Air control stays separate so jumping never adds a ground dash.
    const double acceleration = grounded_ ? (!driving ? 68.0 : forward_speed < -0.1 ? 60.0 : 24.0) : 18.0;
    const Vec2 change{planar_input_.x*speed-velocity_.x,planar_input_.y*speed-velocity_.y};
    const double length = std::hypot(change.x,change.y);
    const double fraction = length > 0 ? std::min(1.0,acceleration*dt/length) : 0;
    velocity_.x += change.x*fraction; velocity_.y += change.y*fraction;
    for (bool horizontal : {true,false}) {
        const double amount = (horizontal ? velocity_.x : velocity_.y)*dt;
        Vec2 candidate = position_;
        (horizontal ? candidate.x : candidate.y) += amount;
        if (!collides(candidate,world)) position_ = candidate;
        else {
            const double impact = std::abs(horizontal ? velocity_.x : velocity_.y);
            (horizontal ? velocity_.x : velocity_.y) = 0;
            if (impact > 3.1 && impact_cooldown_ <= 0) {
                apply_damage(std::min(10.0,(impact-3.1)*3.0));
                impact_cooldown_ = 0.8;
            }
        }
    }
    const double support = support_height(position_,world);
    const bool over_pit = support < -999;
    if (grounded_ && !world.pit(int(center().x),int(center().y)))
        fall_origin_elevation_ = world.elevation(int(center().x),int(center().y));
    ground_elevation_ = over_pit ? fall_origin_elevation_ : support;
    // Follow shallow downhill terrain instead of briefly falling at every cell
    // boundary. Taller ledges and removed resource blocks still trigger gravity.
    if (!over_pit && grounded_ && ground_elevation_ >= elevation_-0.36) {
        elevation_ = ground_elevation_; vertical_velocity_ = 0;
    } else {
        grounded_ = false;
        // Exact constant-acceleration integration, independent of rendering rate.
        // z and vz are separate from the map's x/y movement; jumping adds no dash.
        elevation_ += vertical_velocity_*dt-0.5*jump_gravity*dt*dt;
        vertical_velocity_ -= jump_gravity*dt;
        if (over_pit && vertical_velocity_ < 0 && elevation_ < fall_origin_elevation_-1.2) {
            fell_into_pit_ = true;
            shutdown();
            return;
        }
        if (!over_pit && vertical_velocity_ <= 0 && elevation_ <= ground_elevation_) {
            const double impact = -vertical_velocity_;
            elevation_ = ground_elevation_; vertical_velocity_ = 0; grounded_ = true;
            if (impact > safe_fall_speed)
                apply_damage(std::min(100.0,(impact*impact-safe_fall_speed*safe_fall_speed)*0.55));
        }
    }
}
void Unit7Entity::physics_step(double dt, const WorldGrid& world) {
    grounded_ = collides({position_.x,position_.y+0.02},world);
    const bool dusty = world.get_tile_type(int(std::floor(center().x)),int(std::floor(position_.y+0.88))) == TileType::Regolith;
    const double max_speed = dusty && !has_upgrade(Upgrade::DustRepellentTreads) ? 2.5 : 4.5;
    const double target = movement_*max_speed;
    const double acceleration = (movement_ == 0 ? 35.0 : 24.0)*dt;
    velocity_.x += std::clamp(target-velocity_.x,-acceleration,acceleration);
    if (jump_requested_ && grounded_ && spend_battery(jump_battery_cost)) {
        if (!alive()) return;
        velocity_.y = -8.2;
    }
    jump_requested_ = false;
    // Gravity/jump strengths are intentionally tuned for readable platforming, not
    // Phobos' actual tiny surface acceleration. See docs/SCIENCE.md.
    velocity_.y = std::min(12.0,velocity_.y+18.0*dt);
    if (climbing_ && can_climb(world)) velocity_.y = -3.0;
    const auto move_axis = [&](bool horizontal) {
        const double amount = (horizontal ? velocity_.x : velocity_.y)*dt;
        Vec2 candidate = position_;
        (horizontal ? candidate.x : candidate.y) += amount;
        if (!collides(candidate,world)) { position_ = candidate; return; }
        // Binary-search the last free position instead of rounding the body into a tile.
        double low = 0, high = 1;
        for (int i = 0; i < 18; ++i) {
            const double fraction = (low+high)/2;
            candidate = position_;
            (horizontal ? candidate.x : candidate.y) += amount*fraction;
            if (collides(candidate,world)) high = fraction; else low = fraction;
        }
        (horizontal ? position_.x : position_.y) += amount*low;
        (horizontal ? velocity_.x : velocity_.y) = 0;
    };
    move_axis(true);
    move_axis(false);
    grounded_ = collides({position_.x,position_.y+0.02},world);
}
ActionResult Unit7Entity::use_mining_laser(int x, int y, WorldGrid& world, double dt) {
    if (!alive()) return ActionResult::failure("Unit-01 is offline.");
    if (!std::isfinite(dt) || dt <= 0 || dt > 0.1) return ActionResult::failure("Invalid laser time step.");
    if (!world.in_bounds(x,y) || !near({x+0.5,y+0.5},3.1)) return ActionResult::failure("Target is out of reach (3.1 tiles).");
    if (!world.line_of_sight(center(),{x,y})) return ActionResult::failure("Another block obstructs the laser.");
    const auto type = world.get_tile_type(x,y);
    if (!tile_definition(type).mineable) return ActionResult::failure("This tile cannot be mined.");
    const double battery_cost = mining_battery_per_second*dt;
    if (battery_percent_ < battery_cost) return ActionResult::failure("Battery too low to power the mining laser.");
    const double rate = has_upgrade(Upgrade::SolarThermalDrill) ? 3.0 : 1.0;
    if (!world.damage_tile(x,y,dt*rate)) {
        spend_battery(battery_cost);
        return ActionResult::success("Laser cutting...");
    }
    const auto drop = world.break_tile(x,y);
    if (!drop && (type == TileType::SupplyBarrier || type == TileType::SilicateBarrier || type == TileType::IceBarrier)) { spend_battery(battery_cost); return ActionResult::success("Barrier cleared."); }
    if (!drop) return ActionResult::failure("No material recovered.");
    if (!inventory_.add(drop->item,drop->quantity)) {
        world.place_tile(x,y,type);
        return ActionResult::failure("Inventory quantity limit reached.");
    }
    spend_battery(battery_cost);
    return ActionResult::success(std::string("Recovered ")+item_name(drop->item)+".");
}
ActionResult Unit7Entity::place_tile(int x, int y, WorldGrid& world) {
    if (!alive()) return ActionResult::failure("Unit-01 is offline.");
    if (!world.in_bounds(x,y) || !near({x+0.5,y+0.5},3.1)) return ActionResult::failure("Placement is out of reach.");
    if (overlaps(body(),{{double(x),double(y)},1,1})) return ActionResult::failure("Unit-01 occupies this tile.");
    if (!world.line_of_sight(center(),{x,y})) return ActionResult::failure("Placement is obstructed.");
    if (world.get_tile_type(x,y) != TileType::VacuumAir) return ActionResult::failure("Tile is occupied.");
    if (battery_percent_ < placement_battery_cost) return ActionResult::failure("Battery too low to place a block.");
    if (!inventory_.consume(Item::Regolith,1000)) return ActionResult::failure("Need 1000 g regolith.");
    if (!world.place_tile(x,y)) {
        inventory_.add(Item::Regolith,1000);
        return ActionResult::failure("Cannot place here.");
    }
    spend_battery(placement_battery_cost);
    return ActionResult::success(world.planar() ? "Regolith block placed." : "Regolith placed; unsupported dust will fall.");
}
void Unit7Entity::apply_environment(double dose_rate_msv_hr, double temperature_kelvin, double dt) {
    if (!alive() || !std::isfinite(dt) || dt <= 0 || !std::isfinite(dose_rate_msv_hr) ||
        dose_rate_msv_hr < 0 || !std::isfinite(temperature_kelvin) || temperature_kelvin < 0) return;
    const double shield = has_upgrade(Upgrade::RadiationShielding) ? 0.45 : 1.0;
    const double dose = dose_rate_msv_hr*shield*dt/3600.0;
    accumulated_dose_msv_ += dose;
    // mSv is a narrative dosimeter reading, NOT an electronics damage model. These
    // fictional coefficients turn exposure and heat into a permanent gameplay cost.
    const double heat_damage = std::max(0.0,temperature_kelvin-345.0)*0.004*dt;
    integrity_ = std::max(0.0,integrity_-dose*0.9-heat_damage);
    if (!alive()) shutdown();
}
void Unit7Entity::shutdown() {
    integrity_ = 0;
    stop_actuators();
}
void Unit7Entity::stop_actuators() {
    velocity_ = {}; movement_ = 0; jump_requested_ = false; climbing_ = false;
    planar_input_ = {}; vertical_velocity_ = 0;
}
}

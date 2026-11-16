#pragma once
#include "horizon/WorldGrid.hpp"
#include <algorithm>
#include <array>

namespace horizon {
class NASA_ISRU_Refinery;
class Unit7Entity {
public:
    explicit Unit7Entity(Vec2 spawn);
    void transfer_to(Vec2 center);
    Vec2 position() const { return position_; }
    Vec2 center() const { return {position_.x+0.35,position_.y+0.425}; }
    Vec2 velocity() const { return velocity_; }
    Aabb body() const { return {position_,0.7,0.85}; }
    bool grounded() const { return grounded_; }
    double integrity() const { return integrity_; }
    double battery_percent() const { return battery_percent_; }
    double accumulated_dose_msv() const { return accumulated_dose_msv_; }
    bool alive() const { return integrity_ > 0 && battery_percent_ > 0; }
    bool fell_into_pit() const { return fell_into_pit_; }
    double fall_origin_elevation() const { return fall_origin_elevation_; }
    bool spend_battery(double amount);
    void drain_battery(double amount);
    void recharge_battery(double amount);
    Inventory& inventory() { return inventory_; }
    const Inventory& inventory() const { return inventory_; }
    void move_left();
    void move_right();
    void stop_moving();
    void jump();
    void set_climbing(bool enabled);
    void move_on_plane(Vec2 direction, bool request_jump);
    static constexpr double jump_gravity = 9.81;
    static constexpr double jump_impulse = 5.2;
    double elevation() const { return elevation_; }
    double ground_elevation() const { return ground_elevation_; }
    double jump_height() const { return std::max(0.0,elevation_-ground_elevation_); }
    double vertical_velocity() const { return vertical_velocity_; }
    void update(double dt, const WorldGrid& world);
    void update_wreck(double dt, const WorldGrid& world);
    void apply_damage(double amount);
    static constexpr double safe_fall_speed = 7.5;
    ActionResult use_mining_laser(int x, int y, WorldGrid& world, double dt = 1.0/60.0);
    ActionResult place_tile(int x, int y, WorldGrid& world);
    bool open_crafting_bench(Vec2 bench) const;
    bool has_upgrade(Upgrade upgrade) const;
    bool near(Vec2 point, double reach = 2.5) const;
    void apply_environment(double dose_rate_msv_hr, double temperature_kelvin, double dt);
    void shutdown();
private:
    friend class NASA_ISRU_Refinery;
    void install_upgrade(Upgrade upgrade);
    bool collides(Vec2 position, const WorldGrid& world) const;
    bool can_climb(const WorldGrid& world) const;
    void physics_step(double dt, const WorldGrid& world);
    void planar_step(double dt, const WorldGrid& world);
    double support_height(Vec2 position, const WorldGrid& world) const;
    void stop_actuators();
    Vec2 position_, velocity_{};
    int movement_ = 0;
    bool grounded_ = false, jump_requested_ = false, climbing_ = false;
    double integrity_ = 100, battery_percent_ = 100, accumulated_dose_msv_ = 0;
    Inventory inventory_;
    std::array<bool,3> upgrades_{};
    Vec2 planar_input_{};
    double elevation_ = 0, ground_elevation_ = 0, vertical_velocity_ = 0;
    bool elevation_initialized_ = false;
    double impact_cooldown_ = 0;
    double fall_origin_elevation_ = 0;
    bool fell_into_pit_ = false;
};
}

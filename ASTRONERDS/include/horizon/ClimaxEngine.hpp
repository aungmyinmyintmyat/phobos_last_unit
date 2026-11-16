#pragma once
#include "horizon/RocketRepairSystem.hpp"

namespace horizon {
struct RadiationSample { double dose_rate_msv_hr = 0, temperature_kelvin = 250; };
class RadiationStorm {
public:
    void start();
    void update(double dt);
    bool active() const { return active_; }
    double elapsed() const { return elapsed_; }
    double front_x(int world_width) const;
    RadiationSample sample(Vec2 point, const WorldGrid& world) const;
private:
    bool active_ = false;
    double elapsed_ = 0;
};
enum class ClimaxState { Exploring, SolarParticleEvent, Launching, Complete, Failed };
const char* state_name(ClimaxState state);
class ClimaxEngine {
public:
    ClimaxState state() const { return state_; }
    const RadiationStorm& storm() const { return storm_; }
    double fade_alpha() const;
    double launch_elapsed() const { return launch_elapsed_; }
    void update(double dt, Unit7Entity& unit, const WorldGrid& world, const RocketRepairSystem& rocket);
    ActionResult activate_launch_valve(Unit7Entity& unit, RocketRepairSystem& rocket);
    const char* outcome_text() const;
private:
    ClimaxState state_ = ClimaxState::Exploring;
    RadiationStorm storm_;
    double launch_elapsed_ = 0;
};
}

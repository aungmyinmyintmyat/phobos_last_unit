#include "horizon/ClimaxEngine.hpp"
#include <algorithm>

namespace horizon {
void RadiationStorm::start() { if (!active_) { active_ = true; elapsed_ = 0; } }
void RadiationStorm::update(double dt) { if (active_ && std::isfinite(dt) && dt > 0) elapsed_ += dt; }
double RadiationStorm::front_x(int world_width) const {
    return active_ ? std::min(double(world_width),elapsed_*world_width/4.0) : 0;
}
RadiationSample RadiationStorm::sample(Vec2 point, const WorldGrid& world) const {
    if (!active_ || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x < 0 || point.x >= world.width() || point.y < 0 || point.y >= world.height() ||
        point.x > front_x(world.width())) return {};
    // Count actual overhead solid cover: a deep open shaft gives no protection.
    int cover = 0;
    const int x = int(std::floor(point.x));
    for (int y = 0; !world.planar() && y < int(std::floor(point.y)) && y < world.height(); ++y)
        if (world.solid(x,y)) ++cover;
    const double attenuation = std::exp(-0.45*cover);
    const double flux = 3500.0+8500.0*std::min(1.0,elapsed_/45.0);
    const double heat = 365.0+70.0*std::pow(std::max(0.0,std::sin(elapsed_*1.8)),4);
    return {flux*attenuation,250.0+(heat-250.0)*attenuation};
}
const char* state_name(ClimaxState state) {
    switch (state) {
        case ClimaxState::Exploring: return "EXPLORATION";
        case ClimaxState::SolarParticleEvent: return "SPE / MANUAL LAUNCH REQUIRED";
        case ClimaxState::Launching: return "EARTH RETURN / UNIT-01 OFFLINE";
        case ClimaxState::Complete: return "MISSION COMPLETE";
        case ClimaxState::Failed: return "SIGNAL LOST / MISSION FAILED";
    }
    return "UNKNOWN";
}
void ClimaxEngine::update(double dt, Unit7Entity& unit, const WorldGrid& world, const RocketRepairSystem& rocket) {
    if (!std::isfinite(dt) || dt <= 0) return;
    if (state_ == ClimaxState::Complete || state_ == ClimaxState::Failed) return;
    if (state_ == ClimaxState::Exploring && !unit.alive()) { state_ = ClimaxState::Failed; return; }
    if (state_ == ClimaxState::Exploring && rocket.ready()) {
        storm_.start(); state_ = ClimaxState::SolarParticleEvent;
    }
    if (state_ == ClimaxState::SolarParticleEvent) {
        storm_.update(dt);
        auto sample = storm_.sample(unit.center(),world);
        unit.apply_environment(sample.dose_rate_msv_hr,sample.temperature_kelvin,dt);
        if (!unit.alive()) state_ = ClimaxState::Failed;
    } else if (state_ == ClimaxState::Launching) {
        launch_elapsed_ += dt;
        if (launch_elapsed_ >= 5.0) state_ = ClimaxState::Complete;
    }
}
ActionResult ClimaxEngine::activate_launch_valve(Unit7Entity& unit, RocketRepairSystem& rocket) {
    if (state_ != ClimaxState::SolarParticleEvent) return ActionResult::failure("The manual SPE launch sequence is not active.");
    if (!unit.alive()) return ActionResult::failure("Unit-01 shut down before reaching the valve.");
    auto result = rocket.activate_launch_valve(unit);
    if (result.ok) {
        unit.shutdown(); // Sacrifice is permanent, even with shielding installed.
        state_ = ClimaxState::Launching;
        launch_elapsed_ = 0;
    }
    return result;
}
double ClimaxEngine::fade_alpha() const {
    if (state_ == ClimaxState::Complete) return 1;
    return state_ == ClimaxState::Launching ? std::clamp((launch_elapsed_-2)/3.0,0.0,1.0) : 0;
}
const char* ClimaxEngine::outcome_text() const {
    if (state_ == ClimaxState::Complete) return "Mission Complete. Earth Saved.";
    if (state_ == ClimaxState::Failed) return "Unit-01 offline. The rescue rocket never left the surface.";
    return "";
}
}

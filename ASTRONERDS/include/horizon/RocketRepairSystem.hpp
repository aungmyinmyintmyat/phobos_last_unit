#pragma once
#include "horizon/Unit7Entity.hpp"

namespace horizon {
class ClimaxEngine;
class RocketRepairSystem {
public:
    RocketRepairSystem(Vec2 ground_socket, Vec2 launch_valve);
    Vec2 ground_socket() const { return ground_socket_; }
    Vec2 launch_valve() const { return launch_valve_; }
    bool hull_welded() const { return hull_welded_; }
    bool fuel_lines_connected() const { return fuel_lines_connected_; }
    bool data_core_socketed() const { return data_core_socketed_; }
    bool calibrated() const { return calibrated_; }
    bool ready() const;
    bool launched() const { return launched_; }
    ActionResult weld_hull(Unit7Entity& unit);
    ActionResult hookup_fuel_lines(Unit7Entity& unit);
    ActionResult socket_data_core(Unit7Entity& unit);
    ActionResult calibrate_launch_control_valve(Unit7Entity& unit);
private:
    friend class ClimaxEngine;
    // Only the endgame state machine may commit the irreversible launch.
    ActionResult activate_launch_valve(Unit7Entity& unit);
    Vec2 ground_socket_, launch_valve_;
    bool hull_welded_ = false, fuel_lines_connected_ = false;
    bool data_core_socketed_ = false, calibrated_ = false, launched_ = false;
};
}

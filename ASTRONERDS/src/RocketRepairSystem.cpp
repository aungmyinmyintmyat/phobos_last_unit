#include "horizon/RocketRepairSystem.hpp"
#include <stdexcept>

namespace horizon {
RocketRepairSystem::RocketRepairSystem(Vec2 ground_socket, Vec2 launch_valve)
    : ground_socket_(ground_socket), launch_valve_(launch_valve) {
    if (!std::isfinite(ground_socket.x) || !std::isfinite(ground_socket.y) ||
        !std::isfinite(launch_valve.x) || !std::isfinite(launch_valve.y))
        throw std::invalid_argument("invalid rocket sockets");
}
bool RocketRepairSystem::ready() const { return hull_welded_ && fuel_lines_connected_ && data_core_socketed_ && calibrated_; }
ActionResult RocketRepairSystem::weld_hull(Unit7Entity& unit) {
    if (!unit.near(ground_socket_)) return ActionResult::failure("Stand beside the rocket ground socket.");
    if (hull_welded_) return ActionResult::failure("Hull is already welded.");
    if (!unit.inventory().consume(Item::StructuralPlates,4)) return ActionResult::failure("Hull needs 4 structural plates.");
    hull_welded_ = true;
    return ActionResult::success("Hull welded.");
}
ActionResult RocketRepairSystem::hookup_fuel_lines(Unit7Entity& unit) {
    if (!unit.near(ground_socket_)) return ActionResult::failure("Stand beside the rocket ground socket.");
    if (!hull_welded_) return ActionResult::failure("Weld the hull first.");
    if (fuel_lines_connected_) return ActionResult::failure("Fuel lines are already connected.");
    if (!unit.inventory().exchange({{Item::StructuralPlates,2},{Item::RefinedFuel,2}},{}))
        return ActionResult::failure("Fuel hookup needs 2 plates and 2 refined fuel kits.");
    fuel_lines_connected_ = true;
    return ActionResult::success("Cryogenic lines connected; LH2/LOX transferred.");
}
ActionResult RocketRepairSystem::socket_data_core(Unit7Entity& unit) {
    if (!unit.near(ground_socket_)) return ActionResult::failure("Stand beside the rocket ground socket.");
    if (!hull_welded_) return ActionResult::failure("Weld the hull first.");
    if (data_core_socketed_) return ActionResult::failure("Data core already installed.");
    if (!unit.inventory().consume(Item::DataCore,1)) return ActionResult::failure("Recover the NASA data core.");
    data_core_socketed_ = true;
    return ActionResult::success("NASA data core secured for Earth return.");
}
ActionResult RocketRepairSystem::calibrate_launch_control_valve(Unit7Entity& unit) {
    if (!unit.near(ground_socket_)) return ActionResult::failure("Use the rocket's ground calibration terminal.");
    if (calibrated_) return ActionResult::failure("Launch valve already calibrated.");
    if (!hull_welded_ || !fuel_lines_connected_ || !data_core_socketed_)
        return ActionResult::failure("Complete the hull, fuel lines and data socket before calibration.");
    calibrated_ = true;
    return ActionResult::success("Valve calibrated. SPE warning: reach the raised valve and manually launch!");
}
ActionResult RocketRepairSystem::activate_launch_valve(Unit7Entity& unit) {
    if (launched_) return ActionResult::failure("Rocket already launched.");
    if (!ready()) return ActionResult::failure("Rocket is not flight-ready.");
    if (!unit.near(launch_valve_,1.5)) return ActionResult::failure("Climb to the manual valve at the scaffold summit.");
    launched_ = true;
    return ActionResult::success("Manual launch committed. Unit-01 hardware meltdown.");
}
}

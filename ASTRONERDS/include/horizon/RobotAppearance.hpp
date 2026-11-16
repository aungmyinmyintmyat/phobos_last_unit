#pragma once
#include <cstdint>

namespace horizon {
enum class RobotModel { Mark1, Mark2 };

// Presentation-only choices. Mark 1 keeps its original palette and geometry;
// these editable materials belong to Mark 2 and never affect the simulation.
struct RobotAppearance {
    RobotModel model = RobotModel::Mark2;
    std::uint32_t armor = 0xc6eeeb;
    std::uint32_t trim = 0xf8b95b;
    std::uint32_t optics = 0x46baff;
};
inline const char* robot_model_name(RobotModel model) { return model == RobotModel::Mark1 ? "MARK 1" : "MARK 2"; }
}

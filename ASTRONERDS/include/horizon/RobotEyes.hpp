#pragma once
#include "horizon/RobotAppearance.hpp"
#include "horizon/Types.hpp"
#include <array>

namespace horizon {
class Game;
struct EyeEmitter {
    Vec2 position, forward;
    double elevation = 0, pitch = 0, intensity = 0;
};
struct RobotEyePose {
    Vec2 center;
    double elevation = 0, heading = 0, pitch = 0, eye_height = 0.91;
    std::array<EyeEmitter,2> emitters;
    Vec2 point(double across, double forward, double height, double& z) const;
};
// The optical geometry is shared by the robot mesh and its two light sources.
class RobotEyes {
public:
    static RobotEyePose pose(const Game& game, RobotModel model = RobotModel::Mark2);
    static double irradiance(const EyeEmitter& eye, Vec2 point, double elevation);
};
}

#pragma once
#include "horizon/Types.hpp"
#include "horizon/RobotAppearance.hpp"
#include <vector>

namespace horizon {
class Game;
class PlanetProjection;
struct LightingView {
    double camera_x = 0, camera_y = 0;
    bool headlamp = true;
    const PlanetProjection* planet = nullptr;
    RobotAppearance robot{};
    bool ship_damage_style = true;
};
class SceneLighting {
public:
    // Applied before HUD drawing; picking IDs and game state remain unchanged.
    static bool apply(std::vector<std::uint32_t>& pixels, const std::vector<GridPos>& ids,
                      const Game& game, const LightingView& view);
};
}

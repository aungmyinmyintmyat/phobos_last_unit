#pragma once
#include "horizon/Types.hpp"
#include <vector>
#include <array>

namespace horizon {
class Game;
class PlanetProjection;
struct ShadowCaster {
    Vec2 position;
    double ground_height = 0, height = 1, radius_x = 0.4, radius_y = 0.4;
    double lift = 0, strength = 0.3;
    double heading = 0;
    Vec2 direction{0.52,-0.08};
};
class SceneShadows {
public:
    // A shared sun direction for every ground shadow, in world units per height.
    static constexpr Vec2 sun_shadow{0.52,-0.08};
    static double opacity_at(Vec2 point, double receiver_height, const ShadowCaster& caster);
    static std::array<ShadowCaster,4> robot_casters(const Game& game);
    // Ground-only pass: call after terrain and before opaque objects and lights.
    static bool apply(std::vector<std::uint32_t>& pixels, const std::vector<GridPos>& ids,
                      const Game& game, const PlanetProjection& projection);
};
}

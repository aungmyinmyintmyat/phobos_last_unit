#pragma once
#include "horizon/Types.hpp"
#include <vector>

namespace horizon {
class Game;
class WorldGrid;
struct RobotAppearance;

// A north-up, presentation-only overview. Coordinates remain in world space;
// changing the expedition camera never changes the minimap or its markers.
class Minimap {
public:
    static constexpr int left = 528, top = 228, width = 104, height = 84;
    static bool contains(int x, int y) {
        return x >= left && x < left+width && y >= top && y < top+height;
    }
    Vec2 location(Vec2 point, const WorldGrid& world) const;
    void render(std::vector<std::uint32_t>& pixels, const Game& game, const RobotAppearance& appearance);
};
}

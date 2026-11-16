#pragma once
#include "horizon/Types.hpp"
#include "horizon/PlanetProjection.hpp"
#include "horizon/RobotAppearance.hpp"
#include <array>
#include <vector>

namespace horizon {
class Game;
class WorldGrid;
class IsometricScene {
public:
    IsometricScene() { projection_.curved = false; }
    void toggle_camera() { projection_.curved = !projection_.curved; last_time_ = -1; }
    void toggle_ship_damage_style() { ship_damage_style_ = !ship_damage_style_; }
    bool ship_damage_style() const { return ship_damage_style_; }
    bool curved_camera() const { return projection_.curved; }
    void render(std::vector<std::uint32_t>& pixels, const Game& game, bool headlamp = true,
                const RobotAppearance& robot = {});
    Vec2 project(Vec2 point, const WorldGrid& world) const;
    Vec2 item_position(GridPos cell, const WorldGrid& world) const;
    std::array<Vec2,4> floor_corners(GridPos cell, const WorldGrid& world) const;
    GridPos pick(int x, int y) const;
private:
    void render_ship(std::vector<std::uint32_t>& pixels,const Game& game,bool headlamp,const RobotAppearance& robot);
    const PlanetProjection& active_projection() const { return ship_scene_ ? ship_projection_ : projection_; }
    PlanetProjection projection_;
    PlanetProjection ship_projection_;
    double ship_time_ = -1;
    bool ship_scene_ = false;
    bool ship_damage_style_ = true;
    double last_time_ = -1;
    std::vector<GridPos> picking_;
    std::vector<std::uint32_t> ground_colors_;
    int terrain_width_ = 0, terrain_height_ = 0;
    std::uint32_t terrain_seed_ = 0;
};
}

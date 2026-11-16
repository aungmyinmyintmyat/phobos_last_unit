#pragma once
#include "horizon/Inventory.hpp"
#include <optional>
#include <vector>

namespace horizon {
struct Tile { TileType type = TileType::VacuumAir; double mining_work = 0.0; };
enum class ShipDoor;
class WorldGrid {
public:
    WorldGrid(int width, int height);
    static WorldGrid generate(int width = 160, int height = 72, std::uint32_t seed = 7);
    static WorldGrid generate_isometric(int width = 144, int height = 64, std::uint32_t seed = 7);
    void setup_mission1();
    void setup_mission4();
    bool mission4_cliff_cell(int x,int y) const;
    static WorldGrid spaceship_interior();
    void set_ship_storage_door(bool open);
    void set_ship_room_door(ShipDoor door,bool open);
    void remove_ship_food(int food_index);
    void release_ship_carrier();
    bool continuous_ground() const { return continuous_ground_; }
    bool planar() const { return planar_; }
    std::uint32_t seed() const { return seed_; }
    double elevation(int x, int y) const;
    static double obstacle_height(TileType type);
    double obstacle_height(int x,int y) const;
    int width() const { return width_; }
    int height() const { return height_; }
    int surface_y() const { return surface_y_; }
    bool in_bounds(int x, int y) const;
    TileType get_tile_type(int x, int y) const;
    bool solid(int x, int y) const;
    bool pit(int x, int y) const;
    // Low-level world mutation. Player reach, tools and item costs live in Unit7Entity.
    std::optional<ItemStack> break_tile(int x, int y);
    bool damage_tile(int x, int y, double work);
    double mining_progress(int x, int y) const;
    bool place_tile(int x, int y, TileType type = TileType::Regolith);
    void apply_gravity_to_loose_regolith(std::optional<Aabb> protected_body = std::nullopt);
    bool line_of_sight(Vec2 from, GridPos target) const;
    GridPos data_core_position() const { return core_position_; }
private:
    std::size_t index(int x, int y) const;
    int width_, height_, surface_y_;
    GridPos core_position_{};
    std::vector<Tile> tiles_;
    bool planar_ = false, continuous_ground_ = false, mission4_cliff_ = false;
    bool ship_interior_ = false;
    std::uint32_t seed_ = 7;
    std::vector<double> elevations_;
    std::vector<double> obstacle_heights_;
    std::vector<std::uint8_t> pits_;
};
}

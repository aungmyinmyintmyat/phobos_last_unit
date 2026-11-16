#include "horizon/WorldGrid.hpp"
#include "horizon/PlanetTerrain.hpp"
#include "horizon/BaseCamp.hpp"
#include "horizon/Mission1.hpp"
#include "horizon/ShipInterior.hpp"
#include <algorithm>
#include <limits>
#include <random>
#include <stdexcept>

namespace horizon {
WorldGrid::WorldGrid(int width, int height) : width_(width), height_(height), surface_y_(height/3) {
    if (width < 4 || height < 4 || width > 4096 || height > 4096)
        throw std::invalid_argument("world dimensions must be 4..4096");
    tiles_.resize(static_cast<std::size_t>(width)*static_cast<std::size_t>(height));
}
WorldGrid WorldGrid::spaceship_interior() {
    WorldGrid world(ShipInterior::width,ShipInterior::height);
    world.planar_=world.continuous_ground_=true;
    world.ship_interior_=true;
    world.elevations_.resize(world.tiles_.size(),0);
    // Hull bulkheads are collision barriers rather than platforms. Their visual
    // height remains realistic, while this height prevents clearing or landing
    // on them during a jump.
    world.obstacle_heights_.resize(world.tiles_.size(),ShipInterior::wall_collision_height);
    for(int y=0;y<world.height_;++y) for(int x=0;x<world.width_;++x)
        world.tiles_[world.index(x,y)]={ShipInterior::blocked(x,y) ? TileType::Bedrock : TileType::VacuumAir,0};
    for(const auto& prop:ShipInterior::furniture)
        for(int y=prop.bounds.y;y<prop.bounds.y+prop.bounds.h;++y)
            for(int x=prop.bounds.x;x<prop.bounds.x+prop.bounds.w;++x)
                world.obstacle_heights_[world.index(x,y)]=ShipInterior::fixture_height(prop.kind);
    world.set_ship_storage_door(false);
    return world;
}
void WorldGrid::set_ship_storage_door(bool open) {
    const auto door=ShipInterior::storage_door;
    for(int y=door.y;y<door.y+door.h;++y) for(int x=door.x;x<door.x+door.w;++x) {
        if(!in_bounds(x,y)) continue;
        tiles_[index(x,y)]={open ? TileType::VacuumAir : TileType::Bedrock,0};
        if(!obstacle_heights_.empty()) obstacle_heights_[index(x,y)]=ShipInterior::wall_collision_height;
    }
}
void WorldGrid::set_ship_room_door(ShipDoor id,bool open) {
    if(!ship_interior_) return;
    const auto door=ShipInterior::doors[static_cast<int>(id)].bounds;
    for(int y=door.y;y<door.y+door.h;++y) for(int x=door.x;x<door.x+door.w;++x) {
        tiles_[index(x,y)]={open ? TileType::VacuumAir : TileType::Bedrock,0};
        obstacle_heights_[index(x,y)]=open ? 0 : ShipInterior::wall_collision_height;
    }
}
void WorldGrid::release_ship_carrier() {
    if(!ship_interior_) return;
    for(const auto& prop:ShipInterior::furniture) {
        if(prop.kind!=ShipFixture::Rover && prop.kind!=ShipFixture::RoverArm) continue;
        for(int y=prop.bounds.y;y<prop.bounds.y+prop.bounds.h;++y)
            for(int x=prop.bounds.x;x<prop.bounds.x+prop.bounds.w;++x) {
                tiles_[index(x,y)]={};obstacle_heights_[index(x,y)]=0;
            }
    }
}
void WorldGrid::remove_ship_food(int food_index) {
    if(!ship_interior_ || food_index<0 || food_index>=int(ShipInterior::food_supplies.size())) return;
    const auto p=ShipInterior::food_supplies[food_index];const auto cell=index(int(p.x),int(p.y));
    tiles_[cell]={};obstacle_heights_[cell]=0;
}
double WorldGrid::obstacle_height(int x,int y) const {
    if(!solid(x,y)) return 0;
    if(planar_ && in_bounds(x,y) &&
       (BaseCamp::wall(x,y) || (BaseCamp::doorway(x,y) && get_tile_type(x,y)==TileType::Bedrock))) return 100.0;
    return in_bounds(x,y) && !obstacle_heights_.empty() ? obstacle_heights_[index(x,y)] : obstacle_height(get_tile_type(x,y));
}
bool WorldGrid::mission4_cliff_cell(int x,int y) const {
    return mission4_cliff_ && in_bounds(x,y) && y>=BaseCamp::mission4_cliff_north &&
        y<=BaseCamp::mission4_cliff_south &&
        (x<BaseCamp::mission4_cliff_gap_left || x>BaseCamp::mission4_cliff_gap_right);
}
std::size_t WorldGrid::index(int x, int y) const {
    return static_cast<std::size_t>(y)*static_cast<std::size_t>(width_)+static_cast<std::size_t>(x);
}
bool WorldGrid::in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
TileType WorldGrid::get_tile_type(int x, int y) const {
    // A solid sentinel makes the world edge a reliable collision boundary.
    return in_bounds(x,y) ? tiles_[index(x,y)].type : TileType::Bedrock;
}
bool WorldGrid::solid(int x, int y) const { return tile_definition(get_tile_type(x,y)).solid; }
bool WorldGrid::pit(int x,int y) const {
    return planar_ && in_bounds(x,y) && !pits_.empty() && pits_[index(x,y)] != 0;
}
double WorldGrid::elevation(int x, int y) const {
    return planar_ && in_bounds(x,y) ? elevations_[index(x,y)] : 0;
}
double WorldGrid::obstacle_height(TileType type) {
    switch (type) {
        case TileType::VacuumAir: case TileType::Scaffold: return 0;
        case TileType::SupplyBarrier: case TileType::SilicateBarrier: case TileType::IceBarrier: return Mission1::barrier_height;
        case TileType::Regolith: return 0.62;
        case TileType::HydratedMinerals: return 1.2;
        case TileType::WaterIce: return 0.65;
        case TileType::NASADataCore: return 0.7;
        case TileType::Bedrock: return 1.87;
        default: return 0.92;
    }
}
WorldGrid WorldGrid::generate_isometric(int width, int height, std::uint32_t seed) {
    if (width < 40 || height < 40 || width > 256 || height > 128) throw std::invalid_argument("isometric worlds require width 40..256 and height 40..128");
    WorldGrid world(width,height);
    world.planar_ = true;
    world.seed_ = seed;
    world.surface_y_ = 0;
    world.elevations_.resize(std::size_t(width)*height);
    world.pits_.resize(std::size_t(width)*height,0);
    std::mt19937 rng(seed);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        TileType type = TileType::VacuumAir;
        const auto roll = rng()%200;
        if (roll < 3) type = TileType::IronNickelOre;
        else if (roll < 6) type = TileType::HydratedMinerals;
        else if (roll < 9) type = TileType::Regolith;
        else if (roll == 9) type = TileType::WaterIce;
        // Seeded boulder fields add jumpable rubble and larger route obstacles.
        if (x > 19 || y > 20) {
            const double rocks = PlanetTerrain::noise(x*0.24,y*0.24,seed+211);
            if (roll >= 10 && roll < 19 && rocks > 0.61)
                type = roll < 15 ? TileType::Bedrock : TileType::Regolith;
        }
        if (x == 0 || y == 0 || x == width-1 || y == height-1) type = TileType::Bedrock;
        if (x >= 2 && x <= 12 && y >= 2 && y <= 12) type = TileType::VacuumAir;
        // Wide hub roads, leading to the quarry and the raised launch terrace.
        if ((std::abs(y-10) <= 1 && x > 1 && x < width-4) ||
            (std::abs(x-(width-16)) <= 1 && y > 8 && y < height-4)) type = TileType::VacuumAir;
        const bool core_spur = x >= width-17 && x <= width-10 && std::abs(y-(height-12)) <= 1;
        if (core_spur) type = TileType::VacuumAir;
        world.tiles_[world.index(x,y)] = {type,0};
        double elevation = PlanetTerrain::relief(x+0.5,y+0.5,width,height,seed);
        // Settled hub ground and graded trails preserve safe, readable routes.
        const double road = std::min(std::abs(y-10.0),std::abs(x-(width-16.0)));
        elevation *= std::clamp((road-1.0)/2.5,0.0,1.0);
        if (x >= width-18 && x <= width-9) {
            const double spur_blend = std::clamp((std::abs(y-(height-12.0))-1.0)/3.5,0.0,1.0);
            elevation *= spur_blend;
        }
        if (x >= 2 && x <= 12 && y >= 2 && y <= 12) elevation = 0;
        // The crashed hull rests on the floor of an excavated impact basin.
        // Its southern access channel grades back up to the original hub road.
        if (x > 0 && y > 0 && x < 17 && y < 13)
            elevation += PlanetTerrain::crash_relief(x+0.5,y+0.5);
        if (BaseCamp::footprint(x,y)) elevation = BaseCamp::floor_elevation;
        if (y >= 9 && y <= 11 && x >= 8 && x <= 12) elevation = 0;
        if (x >= 6 && x <= 7 && y >= 9 && y <= 11)
            elevation = BaseCamp::floor_elevation*std::clamp((11.0-y)/3.0,0.0,1.0);
        if (x >= width-14 && y >= 8 && y <= 20)
            elevation = std::min(3.5,(x-(width-14))*0.5);
        if (x >= 22 && y >= 24) elevation -= std::min(1.5,(y-24)*0.25);
        world.elevations_[world.index(x,y)] = elevation;
    }
    for (int y = BaseCamp::north; y <= BaseCamp::south; ++y)
        for (int x = BaseCamp::left; x <= BaseCamp::right; ++x)
            world.tiles_[world.index(x,y)] = {BaseCamp::structure(x,y) ? TileType::Bedrock : TileType::VacuumAir,0};
    for (int y : {8,12}) for (int x : {13,15,17}) world.tiles_[world.index(x,y)] = {TileType::IronNickelOre,0};
    for (int y : {14,16}) for (int x : {8,10,12}) world.tiles_[world.index(x,y)] = {TileType::Regolith,0};
    for (int x : {15,17}) world.tiles_[world.index(x,17)] = {TileType::HydratedMinerals,0};
    world.core_position_ = {width-12,height-12};
    world.tiles_[world.index(world.core_position_.x,world.core_position_.y)] = {TileType::NASADataCore,0};
    for (int x = width-14; x <= width-8; ++x)
        world.tiles_[world.index(x,14)] = {TileType::Scaffold,0};
    for (int y = 11; y <= 15; ++y) for (int x = width-15; x <= width-13; ++x)
        world.tiles_[world.index(x,y)] = {};
    world.tiles_[world.index(8,10)] = {TileType::Bedrock,0};
    world.tiles_[world.index(8,9)] = {TileType::Bedrock,0};
    world.tiles_[world.index(width-14,12)] = {TileType::Bedrock,0};
    world.tiles_[world.index(width-14,11)] = {TileType::Bedrock,0};
    return world;
}
void WorldGrid::setup_mission1() {
    continuous_ground_ = true;
    if (!planar_) return;
    for (int y=1;y<height_-1;++y) for(int x=1;x<width_-1;++x)
        if (!BaseCamp::structure(x,y) && !BaseCamp::doorway(x,y)) tiles_[index(x,y)] = {};
    for (int x=BaseCamp::left;x<=BaseCamp::right;++x)
        if (BaseCamp::doorway(x,BaseCamp::south)) tiles_[index(x,BaseCamp::south)] = {TileType::Bedrock,0};
    // Campaign setup clears the expedition's deposits. Restore sparse mineral
    // pockets with seeded spacing, leaving roads and mission landmarks open.
    std::mt19937 minerals(seed_^0x4d415031u);
    constexpr TileType types[]{TileType::IronNickelOre,TileType::HydratedMinerals,TileType::WaterIce};
    for(int gy=4;gy<height_-4;gy+=7) for(int gx=16;gx<width_-4;gx+=7) {
        const int x=gx+int(minerals()%3),y=gy+int(minerals()%3);
        const auto type=types[minerals()%3];
        if(minerals()%4==0 || !in_bounds(x,y) || x>=width_-3 || y>=height_-3) continue;
        if(std::abs(y-10)<=2 || std::abs(x-(width_-16))<=2 ||
           (y>=52 && y<=55) || (x>=108 && y>=44) || pit(x,y)) continue;
        const Vec2 p{x+.5,y+.5};bool protected_area=false;
        for(const auto supply:Mission1::supplements) if(distance(p,supply)<4) protected_area=true;
        if(distance(p,BaseCamp::center)<9 || distance(p,BaseCamp::data_bank)<5 ||
           distance(p,BaseCamp::damaged_shuttle)<6 || protected_area) continue;
        bool clear=true;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(solid(x+dx,y+dy) || pit(x+dx,y+dy) || std::abs(elevation(x+dx,y+dy)-elevation(x,y))>.7) clear=false;
        if(clear) tiles_[index(x,y)]={type,0};
    }
    for (int supply=2;supply<5;++supply) {
        const GridPos center{int(Mission1::supplements[supply].x),int(Mission1::supplements[supply].y)};
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if ((dx || dy) && in_bounds(center.x+dx,center.y+dy))
                tiles_[index(center.x+dx,center.y+dy)]={supply==2 ? TileType::SilicateBarrier : supply==3 ? TileType::IceBarrier : TileType::SupplyBarrier,0};
    }
}
void WorldGrid::setup_mission4() {
    if (!planar_ || width_ < 124 || height_ < 62) return;
    mission4_cliff_=true;
    for (int y=BaseCamp::mission4_cliff_north;y<=BaseCamp::mission4_cliff_south;++y)
        for (int x=1;x<width_-1;++x)
            if (mission4_cliff_cell(x,y)) tiles_[index(x,y)]={TileType::Bedrock,0};
}
WorldGrid WorldGrid::generate(int width, int height, std::uint32_t seed) {
    if (width < 64 || height < 48) throw std::invalid_argument("generated worlds require at least 64x48 tiles");
    WorldGrid world(width,height);
    world.surface_y_ = height/2;
    std::mt19937 rng(seed);
    // Terraria-style row-major generation: shallow dust, deeper mineral-bearing rock,
    // and pockets of ore/ice. Percentages are design assumptions, not Phobos measurements.
    for (int y = world.surface_y_; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto type = TileType::Regolith;
            const auto roll = rng()%100;
            if (y > world.surface_y_+2) {
                type = roll < 42 ? TileType::HydratedMinerals : TileType::IronNickelOre;
                if (roll > 90) type = TileType::WaterIce;
            }
            if (x == 0 || x == width-1 || y == height-1) type = TileType::Bedrock;
            world.tiles_[world.index(x,y)] = {type,0};
        }
    }
    // Guaranteed starter seams make progression independent of procedural luck.
    for (int x = 18; x < 36; ++x) {
        world.tiles_[world.index(x,world.surface_y_)] =
            {x%2 ? TileType::HydratedMinerals : TileType::IronNickelOre,0};
    }
    world.core_position_ = {width/2, world.surface_y_+10};
    world.tiles_[world.index(world.core_position_.x,world.core_position_.y)] = {TileType::NASADataCore,0};
    // Open ladder tiles are climbable and do not block the robot or mining rays.
    for (int y = world.surface_y_-18; y < world.surface_y_; ++y)
        world.tiles_[world.index(width-22,y)] = {TileType::Scaffold,0};
    return world;
}
std::optional<ItemStack> WorldGrid::break_tile(int x, int y) {
    if (!in_bounds(x,y)) return std::nullopt;
    auto& tile = tiles_[index(x,y)];
    if (!tile_definition(tile.type).mineable) return std::nullopt;
    if (tile.type == TileType::SupplyBarrier || tile.type == TileType::SilicateBarrier || tile.type == TileType::IceBarrier) { tile = {}; return std::nullopt; }
    ItemStack drop{Item::Regolith,1000};
    switch (tile.type) {
        case TileType::HydratedMinerals: drop = {Item::HydratedMinerals,20000}; break;
        case TileType::IronNickelOre: drop = {Item::IronNickelOre,1000}; break;
        case TileType::WaterIce: drop = {Item::WaterIce,1800}; break;
        case TileType::ScrapAlloy: drop = {Item::ScrapAlloys,1000}; break;
        case TileType::NASADataCore: drop = {Item::DataCore,1}; break;
        default: break;
    }
    tile = {};
    return drop;
}
bool WorldGrid::damage_tile(int x, int y, double work) {
    if (!in_bounds(x,y) || !std::isfinite(work) || work <= 0) return false;
    auto& tile = tiles_[index(x,y)];
    const auto& definition = tile_definition(tile.type);
    if (!definition.mineable) return false;
    tile.mining_work = std::min(definition.hardness, tile.mining_work+work);
    return tile.mining_work >= definition.hardness;
}
bool WorldGrid::place_tile(int x, int y, TileType type) {
    if (!in_bounds(x,y) || get_tile_type(x,y) != TileType::VacuumAir) return false;
    // Keep the aircraft cabin and entry ramp clear when placing loose regolith.
    if (planar_ && (BaseCamp::footprint(x,y) || pit(x,y))) return false;
    tile_definition(type); // Validate public enum arguments.
    if (type == TileType::VacuumAir) return false;
    tiles_[index(x,y)] = {type,0};
    return true;
}
double WorldGrid::mining_progress(int x, int y) const {
    if (!in_bounds(x,y)) return 0;
    const auto& tile = tiles_[index(x,y)];
    const double hardness = tile_definition(tile.type).hardness;
    return hardness > 0 ? std::clamp(tile.mining_work/hardness,0.0,1.0) : 0;
}
void WorldGrid::apply_gravity_to_loose_regolith(std::optional<Aabb> protected_body) {
    if (planar_) return; // In an overhead world, map south is not physical down.
    // Bottom-to-top traversal moves each grain at most one cell this tick. A top-down
    // loop would let the same grain fall an entire column in one update.
    for (int y = height_-2; y >= 0; --y) {
        for (int x = 0; x < width_; ++x) {
            if (!tile_definition(get_tile_type(x,y)).loose || get_tile_type(x,y+1) != TileType::VacuumAir) continue;
            const Aabb destination{{double(x),double(y+1)},1,1};
            if (protected_body && overlaps(destination,*protected_body)) continue;
            tiles_[index(x,y+1)] = tiles_[index(x,y)];
            tiles_[index(x,y)] = {};
        }
    }
}
bool WorldGrid::line_of_sight(Vec2 from, GridPos target) const {
    if (!std::isfinite(from.x) || !std::isfinite(from.y) || from.x < 0 || from.y < 0 ||
        from.x >= width_ || from.y >= height_ || !in_bounds(target.x,target.y)) return false;
    // Grid DDA visits every crossed cell, avoiding the tunneling of sparse ray samples.
    const Vec2 delta{target.x+0.5-from.x,target.y+0.5-from.y};
    int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    const int sx = delta.x > 0 ? 1 : -1, sy = delta.y > 0 ? 1 : -1;
    const double inf = std::numeric_limits<double>::infinity();
    const double tx = delta.x == 0 ? inf : std::abs(1.0/delta.x);
    const double ty = delta.y == 0 ? inf : std::abs(1.0/delta.y);
    double next_x = delta.x == 0 ? inf : (delta.x > 0 ? x+1-from.x : from.x-x)*tx;
    double next_y = delta.y == 0 ? inf : (delta.y > 0 ? y+1-from.y : from.y-y)*ty;
    for (int steps = 0; steps < width_+height_+2; ++steps) {
        if (x == target.x && y == target.y) return true;
        if (solid(x,y)) return false;
        if (next_x < next_y) { x += sx; next_x += tx; }
        else if (next_y < next_x) { y += sy; next_y += ty; }
        else {
            // A ray cannot mine diagonally through the shared corner of two solid blocks.
            if (solid(x+sx,y) && solid(x,y+sy)) return false;
            x += sx; y += sy; next_x += tx; next_y += ty;
        }
    }
    return false;
}
}

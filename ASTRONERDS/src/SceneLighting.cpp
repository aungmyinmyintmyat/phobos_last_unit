#include "horizon/SceneLighting.hpp"
#include "horizon/Game.hpp"
#include "horizon/PlanetProjection.hpp"
#include "horizon/PlanetTerrain.hpp"
#include "horizon/RobotEyes.hpp"
#include "horizon/ShipInterior.hpp"
#include <algorithm>

namespace horizon {
namespace {
bool visible(const WorldGrid& world, Vec2 from, double from_z, Vec2 to, double to_z) {
    const double length = distance(from,to); const int steps = std::max(1,int(length*8));
    const GridPos start{int(from.x),int(from.y)}, end{int(to.x),int(to.y)};
    for (int i = 1; i < steps; ++i) {
        const double t = double(i)/steps;
        const int x = int(std::floor(from.x+(to.x-from.x)*t)), y = int(std::floor(from.y+(to.y-from.y)*t));
        if ((x == start.x && y == start.y) || (x == end.x && y == end.y)) continue;
        const double top = world.elevation(x,y)+WorldGrid::obstacle_height(world.get_tile_type(x,y));
        if (top > from_z+(to_z-from_z)*t+0.02) return false;
    }
    return true;
}
double pool(Vec2 point, Vec2 source, double radius) {
    const double t = std::max(0.0,1-distance(point,source)/radius);
    return t*t;
}
double eye_visibility(const WorldGrid& world,const EyeEmitter& eye,Vec2 point,double elevation) {
    // The physical separation of the two emitters already softens shadows.
    // Three nearby receiver samples add a small penumbra at rock edges instead
    // of switching a complete tile between fully lit and fully shadowed.
    const Vec2 side{eye.forward.y*0.14,-eye.forward.x*0.14};
    return (visible(world,eye.position,eye.elevation,point,elevation) ? 0.5 : 0.0)+
        (visible(world,eye.position,eye.elevation,{point.x-side.x,point.y-side.y},elevation) ? 0.25 : 0.0)+
        (visible(world,eye.position,eye.elevation,{point.x+side.x,point.y+side.y},elevation) ? 0.25 : 0.0);
}
}
bool SceneLighting::apply(std::vector<std::uint32_t>& pixels, const std::vector<GridPos>& ids,
                          const Game& game, const LightingView& view) {
    constexpr int width = 640, height = 360, top = 0, bottom = 360;
    if (pixels.size() != width*height || ids.size() != pixels.size() ||
        !std::isfinite(view.camera_x) || !std::isfinite(view.camera_y)) return false;
    const auto& world = game.world(); const auto robot = game.unit().center(), base = game.refinery().position();
    const auto core_cell = world.data_core_position(); const Vec2 core{core_cell.x+0.5,core_cell.y+0.5};
    const bool core_present = world.get_tile_type(core_cell.x,core_cell.y) == TileType::NASADataCore;
    const bool lamp = view.headlamp && game.unit().alive();
    const auto eyes = RobotEyes::pose(game,view.robot.model);
    const auto eye_color = view.robot.model == RobotModel::Mark1 ? 0x49baffu : view.robot.optics;
    const bool camp_cutaway = game.inside_base();
    //const double daylight = game.survival_enabled() ? game.phobos().sunlight() : 1;
    const double daylight = (game.survival_enabled() && !game.in_spaceship())
    ? game.phobos().sunlight()
    : 1;
    const double day_level = std::sqrt(daylight);
    const double ambient_red = 0.12+0.82*day_level, ambient_green = 0.14+0.81*day_level, ambient_blue = 0.22+0.72*day_level;
    struct Cell {
        std::array<double,2> eyes{{0,0}};
        bool base = false, core = false, curved = false;
        Vec2 screen{};
        double xx = 0, xy = 0, yx = 0, yy = 0, elevation = 0;
    };
    std::vector<Cell> cells(std::size_t(world.width())*world.height());
    for (int y = 0; y < world.height(); ++y) for (int x = 0; x < world.width(); ++x) {
        const Vec2 p{x+0.5,y+0.5}; const double z = world.elevation(x,y)+WorldGrid::obstacle_height(world.get_tile_type(x,y))*0.45+0.1;
        auto& c = cells[y*world.width()+x];
        c.elevation = z;
        if (view.planet) {
            const double h = world.elevation(x,y);
            c.screen = view.planet->project(p,h).screen;
            const auto a = view.planet->project({p.x+0.01,p.y},h).screen;
            const auto b = view.planet->project({p.x,p.y+0.01},h).screen;
            const double ax = (a.x-c.screen.x)*100, ay = (a.y-c.screen.y)*100;
            const double bx = (b.x-c.screen.x)*100, by = (b.y-c.screen.y)*100;
            const double determinant = ax*by-ay*bx;
            if (std::abs(determinant) > 0.001) {
                c.curved = true; c.xx = by/determinant; c.xy = -bx/determinant;
                c.yx = -ay/determinant; c.yy = ax/determinant;
            }
        }
        if (lamp && distance(p,robot) < 10) for (int eye = 0; eye < 2; ++eye) {
            const auto& source = eyes.emitters[eye];
            c.eyes[eye] = source.intensity > 0 ? eye_visibility(world,source,p,z) : 0;
        }
        if (distance(p,base) < 5)
            c.base = visible(world,base,world.elevation(int(base.x),int(base.y))+1.35,p,z);
        if (core_present && distance(p,core) < 3) c.core = visible(world,core,world.elevation(core_cell.x,core_cell.y)+0.7,p,z);
    }
    for (int y = top; y < bottom; ++y) for (int x = 0; x < width; ++x) {
        if ((pixels[y*width+x]&0xffffff) == 0) continue; // Pit interiors remain absolute black.
        const auto id = ids[y*width+x];
        if (!world.in_bounds(id.x,id.y)) {
            // Space remains black and stars stay visible: Phobos has no sky
            // atmosphere. Only uncovered planet/background ground follows night.
            if (view.planet && !view.planet->unproject({x+0.5,y+0.5})) continue;
            const auto original = pixels[y*width+x];
            pixels[y*width+x] = 0xff000000|
                (unsigned(((original>>16)&255)*ambient_red)<<16)|
                (unsigned(((original>>8)&255)*ambient_green)<<8)|unsigned((original&255)*ambient_blue);
            continue;
        }
        const double a = (x+view.camera_x)/22.0, b = (y+view.camera_y+world.elevation(id.x,id.y)*14)/11.0;
        Vec2 point{std::clamp((a+b)/2,id.x-0.1,id.x+1.1),std::clamp((b-a)/2,id.y-0.1,id.y+1.1)};
        if (view.planet) {
            const auto& c = cells[id.y*world.width()+id.x];
            const double sx = x+0.5-c.screen.x, sy = y+0.5-c.screen.y;
            point = c.curved ? Vec2{std::clamp(id.x+0.5+c.xx*sx+c.xy*sy,id.x-0.1,id.x+1.1),
                                   std::clamp(id.y+0.5+c.yx*sx+c.yy*sy,id.y-0.1,id.y+1.1)} : Vec2{id.x+0.5,id.y+0.5};
        }
        const auto visibility = [&](int source) {
            const int ix = int(std::floor(point.x-0.5)), iy = int(std::floor(point.y-0.5));
            const double fx = point.x-0.5-ix, fy = point.y-0.5-iy;
            const auto sample = [&](int sx,int sy) {
                if (!world.in_bounds(sx,sy)) return 0.0;
                const auto& cell = cells[sy*world.width()+sx];
                return source < 2 ? cell.eyes[source] : double(source == 2 ? cell.base : cell.core);
            };
            return sample(ix,iy)*(1-fx)*(1-fy)+sample(ix+1,iy)*fx*(1-fy)+
                   sample(ix,iy+1)*(1-fx)*fy+sample(ix+1,iy+1)*fx*fy;
        };
        double red = ambient_red, green = ambient_green, blue = ambient_blue;
        if(game.in_spaceship()) {
            red=.58;green=.60;blue=.58;
            if(ShipInterior::forward_power_outage(id.x,id.y)) {
                // Faint cool fill preserves silhouettes in the unpowered cabin.
                red=.07;green=.10;blue=.16;
            }
            if(ShipInterior::room_at({id.x+.5,id.y+.5})==1) {
                // Low room fill with two yellow task lamps over the benches.
                const double warm=pool(point,{2.0,16.0},6.0)+pool(point,{2.0,23.5},6.0);
                red=.34+warm*.72;green=.30+warm*.51;blue=.21+warm*.15;
            }
            if(view.ship_damage_style && ShipInterior::room_at({id.x+.5,id.y+.5})==2) {
                const double warm=pool(point,{25.0,15.2},6.0)+pool(point,{29.0,25.4},6.0);
                red=.46+warm*.38;green=.39+warm*.26;blue=.25+warm*.10;
            }
        }
        if (BaseCamp::inside({id.x+0.5,id.y+0.5}) || (camp_cutaway && BaseCamp::footprint(id.x,id.y))) {
            red = 0.94; green = 0.94; blue = 0.92;
        }
        double rim = 0;
        if (view.planet && view.planet->curved) {
            const double nx = (x-PlanetProjection::center_x)/PlanetProjection::radius;
            const double ny = (y-PlanetProjection::center_y())/PlanetProjection::radius;
            const double nz = std::sqrt(std::max(0.0,1-nx*nx-ny*ny));
            const double sun = std::clamp(0.6+0.4*std::max(0.0,-nx*0.4-ny*0.6+nz*0.65),0.6,1.0);
            red *= sun; green *= sun; blue *= sun;
            rim = std::pow(1-nz,4)*0.22;
        }
        const double robot_dx = point.x-robot.x, robot_dy = point.y-robot.y;
        if (lamp && robot_dx*robot_dx+robot_dy*robot_dy < 100) {
            const bool ground_receiver = !world.solid(id.x,id.y);
            const double z = ground_receiver ? PlanetTerrain::surface_height(world,point)+0.04 :
                cells[id.y*world.width()+id.x].elevation;
            double strength = 0;
            for (int eye = 0; eye < 2; ++eye) {
                const auto& source = eyes.emitters[eye];
                const double distance3 = std::hypot(distance(point,source.position),source.elevation-z);
                // A shallow beam deposits less light on the ground. Keep a
                // modest diffuse floor for unresolved small rock facets.
                const double incidence = ground_receiver && distance3 > 0.001 ?
                    0.65+0.35*std::clamp((source.elevation-z)/distance3,0.0,1.0) : 1;
                strength += RobotEyes::irradiance(source,point,z)*visibility(eye)*incidence;
            }
            // Blue optics illuminate with a cool white core. Mixing a neutral
            // component preserves dusty material colors and avoids cyan clipping
            // where the two beams overlap, while the falloff remains visibly blue.
            red += strength*(0.06+((eye_color>>16)&255)/255.0*0.22);
            green += strength*(0.12+((eye_color>>8)&255)/255.0*0.65);
            blue += strength*(0.20+(eye_color&255)/255.0*0.95);
        }
        if (distance(point,base) < 5) {
            const double strength = pool(point,base,5)*visibility(2);
            red += strength*0.53; green += strength*0.33; blue += strength*0.14;
        }
        if (core_present && distance(point,core) < 3) {
            const double strength = pool(point,core,3)*visibility(3);
            red += strength*0.08; green += strength*0.6; blue += strength*0.4;
        }
        if (game.presentation().mining_target) {
            const auto target = *game.presentation().mining_target;
            const double strength = pool(point,{target.x+0.5,target.y+0.5},2)*(0.75+0.2*std::sin(game.presentation().clock*37));
            red += strength*0.65; green += strength*0.5; blue += strength*0.22;
        }
        const auto original = pixels[y*width+x];
        const auto channel = [&](int shift, double gain) {
            const double lit = std::clamp(((original>>shift)&255)*gain,0.0,255.0);
            return unsigned(lit*(1-rim)+((0x82969cu>>shift)&255)*rim);
        };
        pixels[y*width+x] = 0xff000000|(channel(16,red)<<16)|(channel(8,green)<<8)|channel(0,blue);
    }
    return true;
}
}

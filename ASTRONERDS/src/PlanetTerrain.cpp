#include "horizon/PlanetTerrain.hpp"
#include "horizon/WorldGrid.hpp"
#include "horizon/BaseCamp.hpp"
#include <algorithm>
#include <array>

namespace horizon {
double PlanetTerrain::crash_relief(double x,double y) {
    const double r = std::hypot((x-7.7)/7.3,(y-5.1)/4.8);
    const double t = std::clamp((r-0.66)/0.42,0.0,1.0);
    return BaseCamp::floor_elevation*(1-t*t*(3-2*t))+
        0.64*std::exp(-std::pow((r-1.13)/0.19,2));
}
double PlanetTerrain::surface_height(const WorldGrid& world, Vec2 point) {
    const int x = int(std::floor(point.x)), y = int(std::floor(point.y));
    const auto corner = [&](int cx,int cy) {
        double sum = 0; int count = 0;
        for (int dy : {-1,0}) for (int dx : {-1,0})
            if (world.in_bounds(cx+dx,cy+dy)) { sum += world.elevation(cx+dx,cy+dy); ++count; }
        return count ? sum/count : 0;
    };
    const double fx = point.x-x, fy = point.y-y;
    return corner(x,y)*(1-fx)*(1-fy)+corner(x+1,y)*fx*(1-fy)+
        corner(x,y+1)*(1-fx)*fy+corner(x+1,y+1)*fx*fy;
}
namespace {
double smooth(double a,double b,double value) {
    const double t = std::clamp((value-a)/(b-a),0.0,1.0);
    return t*t*(3-2*t);
}
// Elongated fault gullies have steep side walls and long graded exits at both
// ends. They are actual collision elevations, shared by both camera modes.
double fault_depth(double x,double y,int width,int height,std::uint32_t seed) {
    struct Fault { double x,y,length,half_width,depth,angle; };
    const std::array<Fault,3> faults{{
        {width*0.44,height*0.34,width*0.17,2.5,7.4,0.35},
        {width*0.30,height*0.68,height*0.19,2.8,8.6,1.18},
        {width*0.78,height*0.74,width*0.15,2.3,7.8,-0.38}
    }};
    double depth = 0;
    for (const auto& f : faults) {
        const double dx = x-f.x, dy = y-f.y;
        const double along = dx*std::cos(f.angle)+dy*std::sin(f.angle);
        const double bend = 0.45*std::sin(along*0.65)+0.16*std::sin(along*1.43+f.angle);
        const double across = -dx*std::sin(f.angle)+dy*std::cos(f.angle)+bend;
        const double width_variation = 0.94+0.15*PlanetTerrain::noise(along*0.34,f.x,seed+301);
        const double bank = std::abs(across)/(f.half_width*width_variation);
        const double ends = 1-smooth(f.length*0.24,f.length,std::abs(along));
        // An inset rock shelf breaks the slope above a narrow, deeper floor.
        // Width changes and two meander scales avoid uniform trench walls.
        const double walls = 0.82*(1-smooth(0.30,1.0,bank))+0.18*(1-smooth(0.48,0.68,bank));
        depth = std::max(depth,f.depth*ends*walls);
    }
    // Preserve the starter resource pocket as well as the camp approach.
    return depth*smooth(17,23,std::max(x,y));
}
double lattice(int x, int y, std::uint32_t seed) {
    auto n = std::uint32_t(x)*374761393u+std::uint32_t(y)*668265263u+seed*1442695041u;
    n = (n^(n>>13))*1274126177u; n ^= n>>16;
    return (n & 0xffffff)/16777215.0;
}
std::uint32_t blend(std::uint32_t a, std::uint32_t b, double t) {
    t = std::clamp(t,0.0,1.0); std::uint32_t out = 0;
    for (int shift : {0,8,16}) out |= unsigned(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t)<<shift;
    return out;
}
}
double PlanetTerrain::noise(double x, double y, std::uint32_t seed) {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1e6 || std::abs(y) > 1e6) return 0.5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x-ix, fy = y-iy, u = fx*fx*(3-2*fx), v = fy*fy*(3-2*fy);
    const double a = lattice(ix,iy,seed)*(1-u)+lattice(ix+1,iy,seed)*u;
    const double b = lattice(ix,iy+1,seed)*(1-u)+lattice(ix+1,iy+1,seed)*u;
    return a*(1-v)+b*v;
}
double PlanetTerrain::chasm_depth(double x,double y,int width,int height,std::uint32_t seed) {
    return fault_depth(x,y,width,height,seed);
}
double PlanetTerrain::relief(double x, double y, int width, int height, std::uint32_t seed) {
    const double field = smooth(17,24,std::max(x,y));
    double h = (noise(x*0.12,y*0.12,seed)-0.5)*0.45;
    struct Mountain { double x,y,rx,ry,height; };
    const std::array<Mountain,4> mountains{{
        {width*0.40,height*0.34,8.5,7.5,4.8},
        {width*0.27,height*0.68,9.0,10.0,5.8},
        {width*0.76,height*0.70,9.0,9.0,5.2},
        {width*0.58,height*0.90,10.0,7.0,4.0}
    }};
    for (const auto& mountain : mountains) {
        const double dx = (x-mountain.x)/mountain.rx, dy = (y-mountain.y)/mountain.ry;
        h += field*mountain.height*std::exp(-(dx*dx+dy*dy)*0.65);
    }
    h += field*0.35*std::sin(x*0.16+y*0.04)*std::sin(y*0.12);
    return h;
}
std::uint32_t PlanetTerrain::material(double x, double y, std::uint32_t seed, int width, int height) {
    const double broad = noise(x*0.15,y*0.15,seed+63), detail = noise(x*1.9,y*1.9,seed+103);
    // Phobos is predominantly dark, nearly colorless carbonaceous rock under a
    // blanket of impact-ground regolith; subtle warm light comes from Mars.
    auto color = blend(0x484744,0x756b60,broad);
    color = blend(color,0x958779,detail*0.13);
    const double veins = noise(x*0.22+y*0.035,y*0.5,seed+81);
    if (veins > 0.60) color = blend(color,0x383a3b,(veins-0.60)*1.25);
    color = blend(color,0x69625a,noise(x*0.08,y*0.08,seed+317)*0.20);
    color = blend(color,0x817d76,std::clamp(relief(x,y,width,height,seed)/6.0,0.0,1.0)*0.22);
    const double impact = std::hypot((x-7.7)/7.3,(y-5.1)/4.8);
    if (impact < 1.45) {
        color = blend(color,0x343333,std::max(0.0,1-impact)*0.50);
        color = blend(color,0x81766a,std::exp(-std::pow((impact-1.13)/0.20,2))*0.30);
    }
    return color;
}
}

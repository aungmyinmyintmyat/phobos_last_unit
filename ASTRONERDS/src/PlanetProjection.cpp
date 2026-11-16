#include "horizon/PlanetProjection.hpp"
#include <algorithm>

namespace horizon {
namespace { constexpr double root_two = 1.4142135623730951, half_pi = 1.5707963267948966; }
double PlanetProjection::center_y() { return 200-radius*std::sin(reference_latitude); }
double PlanetProjection::world_radius() { return radius/(22*root_two); }
double PlanetProjection::horizon_y(double x) {
    const double dx = x-center_x;
    return std::abs(dx) <= radius ? center_y()-std::sqrt(std::max(0.0,radius*radius-dx*dx)) : center_y();
}
PlanetVertex PlanetProjection::project(Vec2 p, double altitude) const {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(altitude) ||
        !std::isfinite(anchor.x) || !std::isfinite(anchor.y) || radius+altitude*height_scale <= 0) return {};
    if (!curved) return {{center_x+22*((p.x-anchor.x)-(p.y-anchor.y)),
        200+11*((p.x-anchor.x)+(p.y-anchor.y))-(altitude-anchor_altitude)*height_scale},1,true};
    const double u = ((p.x-anchor.x)-(p.y-anchor.y))/root_two;
    const double v = ((p.x-anchor.x)+(p.y-anchor.y))/root_two;
    const double longitude = u/(world_radius()*std::cos(reference_latitude));
    const double latitude = reference_latitude+v/world_radius();
    const double nx = std::cos(latitude)*std::sin(longitude), ny = std::sin(latitude);
    const double depth = std::cos(latitude)*std::cos(longitude), r = radius+altitude*height_scale;
    return {{center_x+r*nx,center_y()+r*ny},depth,
        std::abs(latitude) < half_pi && std::abs(longitude) < half_pi && depth > 0.001};
}
std::optional<Vec2> PlanetProjection::unproject(Vec2 p, double altitude) const {
    const double r = radius+altitude*height_scale;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(r) || r <= 0 ||
        !std::isfinite(anchor.x) || !std::isfinite(anchor.y)) return std::nullopt;
    if (!curved) {
        const double u = (p.x-center_x)/22, v = (p.y-200+(altitude-anchor_altitude)*height_scale)/11;
        return Vec2{anchor.x+(u+v)/2,anchor.y+(v-u)/2};
    }
    const double nx = (p.x-center_x)/r, ny = (p.y-center_y())/r;
    if (nx*nx+ny*ny >= 1) return std::nullopt;
    const double latitude = std::asin(std::clamp(ny,-1.0,1.0));
    const double longitude = std::asin(std::clamp(nx/std::cos(latitude),-1.0,1.0));
    const double u = longitude*world_radius()*std::cos(reference_latitude);
    const double v = (latitude-reference_latitude)*world_radius();
    return Vec2{anchor.x+(u+v)/root_two,anchor.y+(v-u)/root_two};
}
Vec2 PlanetProjection::direction_marker(Vec2 p) const {
    const double dx = (p.x-anchor.x)-(p.y-anchor.y), dy = (p.x-anchor.x)+(p.y-anchor.y);
    if (std::abs(dx) > std::abs(dy)) return {dx > 0 ? 680.0 : -40.0,180};
    return {320,dy > 0 ? 380.0 : -40.0};
}
}

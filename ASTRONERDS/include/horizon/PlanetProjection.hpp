#pragma once
#include "horizon/Types.hpp"
#include <optional>

namespace horizon {
struct PlanetVertex { Vec2 screen; double depth = 0; bool visible = false; };
// A local longitude/latitude chart rolls over a rendered sphere as the camera
// follows Unit-01. Logical mining cells and jump heights keep their existing units.
class PlanetProjection {
public:
    static constexpr double radius = 420;
    static constexpr double center_x = 320;
    static constexpr double reference_latitude = -0.75;
    static constexpr double height_scale = 20;
    Vec2 anchor{10.5,10.5};
    bool curved = true;
    double anchor_altitude = 0;
    static double center_y();
    static double world_radius();
    static double horizon_y(double screen_x);
    PlanetVertex project(Vec2 world, double altitude = 0) const;
    std::optional<Vec2> unproject(Vec2 screen, double altitude = 0) const;
    Vec2 direction_marker(Vec2 world) const;
};
}

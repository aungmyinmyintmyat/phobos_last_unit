#pragma once
#include <cstdint>
#include <vector>

namespace horizon {
struct BackgroundView {
    int width = 640, height = 360, top = 32, bottom = 316;
    double camera_x = 0, camera_y = 0, surface_y = 180, time = 0;
    bool terrain = true;
};
// CPU background rendering: analytic sphere lighting, rotating perspective meshes,
// and parallax terrain. It has no access to world collisions or gameplay state.
class SpaceBackground {
public:
    static bool draw(std::vector<std::uint32_t>& pixels, const BackgroundView& view);
};
}

#pragma once
#include "horizon/BaseCamp.hpp"
#include <algorithm>

namespace horizon {
// Shared by carrier navigation, the boarding animation and the rendered hull.
struct RescueShip {
    static constexpr double carrier_clearance=1.2;
    static constexpr Vec2 carrier_stop{115.4,60.0};
    static constexpr Vec2 ramp_entry{116.5,60.0};
    static constexpr Vec2 door{117.075,60.0};
    static constexpr Vec2 passenger_seat{118.1,60.0};
    static constexpr double passenger_hip_height=.76, roof_height=1.92;
    static bool blocks_carrier(Vec2 p) {
        // Include the wings as well as the fuselage. A circular clearance bound
        // keeps the entire rover outside at every steering angle.
        const auto ship=BaseCamp::damaged_shuttle;
        const double x=std::clamp(p.x,ship.x-2.2,ship.x+1.925);
        const double y=std::clamp(p.y,ship.y-1.65,ship.y+1.65);
        return std::hypot(p.x-x,p.y-y)<carrier_clearance;
    }
};
}

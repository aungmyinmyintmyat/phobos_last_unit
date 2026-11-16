#pragma once
#include "horizon/Types.hpp"
#include <array>

namespace horizon {
enum class CampFixture { None, Workbench, Console, Bunk, Locker, LifeSupport };
// The landed rescue aircraft is the safe hub. This shared cabin plan drives
// its hull collision, cutaway interior and map icon.
struct BaseCamp {
    static constexpr double floor_elevation = -0.9;
    static constexpr int left = 4, right = 11, north = 3, south = 8;
    static constexpr Vec2 center{8,6}, entrance{7,8.5}, approach{7,9.7};
    static constexpr Vec2 charger{8.5,6.5};
    static constexpr Vec2 data_bank{112.0,48.0};
    static constexpr Vec2 damaged_shuttle{119.0,60.0};
    static constexpr int mission4_cliff_north=53, mission4_cliff_south=54;
    static constexpr int mission4_cliff_gap_left=114, mission4_cliff_gap_right=118;
    static constexpr Vec2 craft_bench{5.5,4.5};
    static constexpr Vec2 observed_table = craft_bench;
    static constexpr std::array<Vec2,2> solar{{{2.45,5.2},{2.45,7.0}}};
    static bool footprint(int x,int y) { return x >= left && x <= right && y >= north && y <= south; }
    static bool doorway(int x,int y) { return y == south && (x == 6 || x == 7); }
    static bool wall(int x,int y) {
        return footprint(x,y) && (x == left || x == right || y == north || y == south) && !doorway(x,y);
    }
    static CampFixture fixture(int x,int y) {
        if (x == int(craft_bench.x) && y == int(craft_bench.y)) return CampFixture::Workbench;
        if (x == 8 && y == 4) return CampFixture::Console;
        if (x == 10 && y == 5) return CampFixture::Bunk;
        if (x == 10 && y == 7) return CampFixture::Locker;
        if (x == 5 && y == 6) return CampFixture::LifeSupport;
        return CampFixture::None;
    }
    static bool structure(int x,int y) { return wall(x,y) || fixture(x,y) != CampFixture::None; }
    static bool inside(Vec2 point) {
        return (point.x >= left+1 && point.x < right && point.y >= north+1 && point.y < south) ||
               (point.x >= 6 && point.x < 8 && point.y >= south && point.y < south+1);
    }
};
}

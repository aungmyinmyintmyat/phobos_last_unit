#pragma once
#include "horizon/Types.hpp"

namespace horizon {
class WorldGrid;
class PlanetTerrain {
public:
    static double surface_height(const WorldGrid& world, Vec2 point);
    static double crash_relief(double x, double y);
    static double noise(double x, double y, std::uint32_t seed);
    static double relief(double x, double y, int width, int height, std::uint32_t seed);
    static double chasm_depth(double x, double y, int width, int height, std::uint32_t seed);
    static std::uint32_t material(double x, double y, std::uint32_t seed, int width = 44, int height = 44);
};
}

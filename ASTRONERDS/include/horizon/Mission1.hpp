#pragma once
#include "horizon/Types.hpp"
#include <array>
namespace horizon {
// Reusable definition of Mission 1, Act 1. Entries 2..4 have laser-only enclosures.
struct Mission1 {
    static constexpr const char* name = "Mission 1";
    static constexpr int required_boxes = 5;
    static constexpr double barrier_height = 2.1;
    static constexpr double observation_seconds = 6.0;
    static constexpr std::array<std::array<const char*,3>,3> sample_facts{{
        {{"SILICATES CONTAIN SILICON AND OXYGEN.",
          "THEY FORM MANY COMMON ROCK MINERALS.",
          "THIS SAMPLE HAS PALE, GRAINY FACETS."}},
        {{"WATER ICE IS THE SOLID FORM OF H2O.",
          "IT CAN MELT INTO LIQUID WATER.",
          "THIS SAMPLE HAS FROST AND CRACKS."}},
        {{"FRAGMENTS ARE PIECES OF LARGER ROCKS.",
          "ROCKS CAN CONTAIN MULTIPLE MINERALS.",
          "THIS SAMPLE HAS ROUGH, BROKEN EDGES."}}
    }};
    // The first two first-aid kits sit inside the rock and ice enclosures.
    static constexpr std::array<Vec2,5> supplements{{{18.5,20.5},{30.5,10.5},{25.5,14.5},{30.5,10.5},{18.5,20.5}}};
    static constexpr Vec2 astronaut{10.5,5.5};
};
}

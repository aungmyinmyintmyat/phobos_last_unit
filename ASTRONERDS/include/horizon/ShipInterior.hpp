#pragma once
#include "horizon/Types.hpp"
#include <array>
namespace horizon {
struct ShipRect {
    int x,y,w,h;
    bool contains(int px,int py) const { return px>=x && px<x+w && py>=y && py<y+h; }
};
struct ShipRoom { ShipRect bounds; const char* name; std::uint32_t light; };
enum class ShipDoor { Cargo, Monitor };
struct ShipDoorPlan { ShipRect bounds; Vec2 center; const char* name; };
enum class ShipFixture { Bunk, MessTable, Sofa, Galley, Locker, Crate, Rover, RoverArm, Monitor, ObservedTable, CraftTable, Reactor, Tank, PCDesk, Seat, FoodSupply, CarrierWheelTable, CarrierArmTable, StorageChipTable, CollapsedBulkhead };
struct ShipFurniture { ShipRect bounds; ShipFixture kind; };
struct ShipInterior {
    static constexpr int width=32, height=57;
    static constexpr Vec2 entry{16.0,53.0}, exit{16.0,55.5};
    static constexpr ShipRect forward_wreckage{13,8,6,2};
    // The sealed forward cabin and its surrounding hull have lost power.
    static bool forward_power_outage(int x,int y) {
        return x>=12 && x<20 && y>=1 && y<forward_wreckage.y;
    }
    static constexpr Vec2 astronaut{10.5,17.5}, table{3.5,17.5}, craft_table{3.5,24.0}, charger{22.5,37.5};
    static constexpr std::array<Vec2,3> televisions{{{21.5,32.02},{25,32.02},{28.5,32.02}}};
    static constexpr double cargo_rover_scale=.5;
    static constexpr Vec2 carrier_robot{29,21}, carrier_wheel{26,17.5}, carrier_arm{26,23.5}, carrier_chip{3,18.5};
    static constexpr double carrier_repair_seconds=6.0;
    static constexpr Vec2 records_pc{26.5,32.5};
    static constexpr std::array<Vec2,3> rescue_materials{{{3,18.5},{3.5,23.5},{5.5,24.5}}};
    static constexpr std::array<const char*,3> rescue_material_names{{"HULL PLATES","WIRING KIT","SEALANT"}};
    static constexpr Vec2 rescue_unit_bedside{11.5,17.5}, rescue_carrier_bedside{12.5,18.5};
    static constexpr std::array<Vec2,2> food_supplies{{{1.5,19.5},{24.5,47.5}}};
    static constexpr ShipRect storage_door{7,14,1,3};
    static constexpr Vec2 storage_door_center{7.5,15.5};
    static constexpr ShipRect cargo_door{24,20,1,3};
    static constexpr Vec2 cargo_door_center{24.5,21.5};
    static constexpr ShipRect monitor_door{19,35,1,3};
    static constexpr Vec2 monitor_door_center{19.5,36.5};
    static constexpr std::array<ShipDoorPlan,2> doors{{
        {cargo_door,cargo_door_center,"CARGO HANGAR"},
        {monitor_door,monitor_door_center,"MONITOR ROOM"}
    }};
    static bool cargo_bulkhead(int x,int y) { return x==24 && y>=14 && y<26 && !cargo_door.contains(x,y); }
    static bool monitor_bulkhead(int x,int y) { return x==19 && y>=32 && y<40 && !monitor_door.contains(x,y); }
    static constexpr std::array<ShipRoom,5> rooms{{
        {{8,10,17,16},"CREW ROOM",0xf0cd82},
        {{1,14,6,12},"STORAGE ROOM",0xf4c96b},
        {{24,14,7,12},"CARGO HANGAR",0xe5b95f},
        {{19,32,12,8},"MONITOR ROOM",0x56c8ff},
        {{7,41,18,11},"ENGINE ROOM",0x59ddf0}
    }};
    static constexpr std::array<ShipRect,8> corridors{{
        {13,2,6,8}, {13,9,6,3}, // Bridge and forward passage.
        {6,14,4,3}, {23,20,3,3}, // Side-room pressure doors.
        {13,25,6,17}, {17,35,3,3}, // Main spine and monitor junction.
        {1,35,13,4}, {13,51,6,6} // Service passage and rear boarding ramp.
    }};
    // Half-width deck with compact fixtures and clear access around each station.
    static constexpr std::array<ShipFurniture,30> furniture{{
        {{8,16,3,3},ShipFixture::Bunk}, {{8,20,3,3},ShipFixture::Bunk},
        {{8,24,3,2},ShipFixture::Bunk}, {{14,19,4,4},ShipFixture::MessTable},
        {{10,11,12,2},ShipFixture::Galley},
        {{2,15,3,2},ShipFixture::ObservedTable}, {{5,19,1,4},ShipFixture::Locker},
        {{2,23,3,2},ShipFixture::CraftTable}, {{1,19,1,1},ShipFixture::FoodSupply},
        {{5,24,1,2},ShipFixture::Crate},
        {{28,20,2,2},ShipFixture::Rover}, {{27,20,2,1},ShipFixture::RoverArm},
        {{25,15,2,2},ShipFixture::Crate},
        {{28,15,2,2},ShipFixture::Crate}, {{28,24,2,2},ShipFixture::Crate},
        {{25,32,3,1},ShipFixture::PCDesk}, {{26,34,1,1},ShipFixture::Seat},
        {{28,36,2,3},ShipFixture::Locker}, {{14,44,4,5},ShipFixture::Reactor},
        {{9,44,2,5},ShipFixture::Tank}, {{21,44,2,5},ShipFixture::Tank},
        {{8,50,4,1},ShipFixture::Monitor}, {{20,50,4,1},ShipFixture::Monitor},
        {{24,47,1,1},ShipFixture::FoodSupply},
        {{14,3,4,2},ShipFixture::Monitor}, {{8,42,3,1},ShipFixture::Galley},
        {{25,17,2,1},ShipFixture::CarrierWheelTable}, {{25,23,2,1},ShipFixture::CarrierArmTable},
        {{2,18,2,1},ShipFixture::StorageChipTable},
        {forward_wreckage,ShipFixture::CollapsedBulkhead}
    }};
    static bool floor(int x,int y) {
        if(cargo_bulkhead(x,y) || monitor_bulkhead(x,y)) return false;
        for(const auto& room:rooms) if(room.bounds.contains(x,y)) return true;
        for(const auto& corridor:corridors) if(corridor.contains(x,y)) return true;
        return false;
    }
    static constexpr double wall_height=1.87, cutaway_height=.42;
    static constexpr double wall_collision_height=100.0;
    static double visible_wall_height(int x,int y) {
        // The display bulkhead keeps the hanging TVs above the PC monitor.
        if(y==31 && x>=19 && x<31) return 3.0;
        // North/west bulkheads remain tall; camera-facing edges stay cut away.
        return floor(x+1,y) || floor(x,y+1) ? wall_height : cutaway_height;
    }
    static bool blocked(int x,int y) {
        if(!floor(x,y)) return true;
        for(const auto& door:doors) if(door.bounds.contains(x,y)) return true;
        for(const auto& prop:furniture) if(prop.bounds.contains(x,y)) return true;
        return false;
    }
    static double fixture_height(ShipFixture kind) {
        switch(kind) {
            case ShipFixture::Bunk:
                return .45;

            case ShipFixture::MessTable:
            case ShipFixture::Sofa:
                return .8;

            case ShipFixture::Galley:
            case ShipFixture::ObservedTable:
            case ShipFixture::CraftTable:
            case ShipFixture::Seat:
                return .9;

            case ShipFixture::PCDesk:
            case ShipFixture::Crate:
            case ShipFixture::FoodSupply:
            case ShipFixture::CarrierWheelTable:
            case ShipFixture::CarrierArmTable:
            case ShipFixture::StorageChipTable:
                return .85;

            case ShipFixture::Rover:
                return 1.15*cargo_rover_scale;

            case ShipFixture::RoverArm:
                return 1.55;

            case ShipFixture::Monitor:
                return 1.1;

            case ShipFixture::Locker:
                return 1.5;

            case ShipFixture::Reactor:
                return 1.75;

            case ShipFixture::Tank:
                return 1.6;

            case ShipFixture::CollapsedBulkhead:
                return wall_collision_height;
        }
        return 1.87;
    }
    static int room_at(Vec2 p) {
        for(int i=0;i<int(rooms.size());++i) if(rooms[i].bounds.contains(int(p.x),int(p.y))) return i;
        return -1;
    }
};
}

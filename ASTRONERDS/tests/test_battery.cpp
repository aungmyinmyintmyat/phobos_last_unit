#include "horizon/Unit7Entity.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace horizon;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance = 1e-7) {
    if (std::abs(actual-expected) > tolerance)
        throw std::runtime_error("battery or movement value differs: "+std::to_string(actual)+" / "+std::to_string(expected));
}
WorldGrid floor_world() {
    WorldGrid world(64,72);
    for (int x = 0; x < world.width(); ++x) world.place_tile(x,24,TileType::Bedrock);
    return world;
}
void advance(Unit7Entity& unit, const WorldGrid& world, int frames, double dt) {
    for (int i = 0; i < frames; ++i) unit.update(dt,world);
}
}

int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"battery transactions, finite inputs and recharge cap",[] {
            Unit7Entity unit({10,23.15});
            close(unit.battery_percent(),100);
            require(unit.spend_battery(12.5),"funded spend rejected");
            close(unit.battery_percent(),87.5);
            for (const double amount : {-1.0,88.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
                require(!unit.spend_battery(amount),"invalid spend accepted");
                close(unit.battery_percent(),87.5);
            }
            for (const double amount : {-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
                unit.recharge_battery(amount); unit.drain_battery(amount);
                close(unit.battery_percent(),87.5);
            }
            unit.recharge_battery(std::numeric_limits<double>::max());
            close(unit.battery_percent(),100);
            require(unit.spend_battery(0),"zero spend rejected");
            close(unit.integrity(),100);
        }},
        {"idle drain is timed and travel costs actual distance",[] {
            auto world = floor_world();
            Unit7Entity idle({10,23.15}), moving({10,23.15});
            moving.move_right();
            const auto start = moving.position();
            advance(idle,world,120,1.0/60); advance(moving,world,120,1.0/60);
            close(100-idle.battery_percent(),0.003*2,1e-6);
            close(100-moving.battery_percent(),0.003*2+0.04*distance(start,moving.position()),1e-6);
            require(moving.battery_percent() < idle.battery_percent()-0.3,"moving is not more expensive than idling");
            const double charge = idle.battery_percent();
            idle.update(-1,world); idle.update(1,world); idle.update(std::numeric_limits<double>::quiet_NaN(),world);
            close(idle.battery_percent(),charge);
        }},
        {"blocked movement adds only idle drain",[] {
            auto world = floor_world();
            for (int y = 20; y < 24; ++y) world.place_tile(12,y,TileType::Bedrock);
            Unit7Entity unit({11.3,23.15}); unit.move_right();
            advance(unit,world,120,1.0/60);
            close(unit.position().x,11.3,2e-6);
            close(100-unit.battery_percent(),0.003*2,1e-6);
            auto planar = WorldGrid::generate_isometric();
            require(planar.place_tile(21,10,TileType::Bedrock),"blocked planar fixture failed");
            Unit7Entity overhead({20.3,10.075}); overhead.move_on_plane({1,0},false);
            advance(overhead,planar,120,1.0/60);
            close(overhead.position().x,20.3);
            close(100-overhead.battery_percent(),0.003*2);
        }},
        {"successful mining and placement use energy",[] {
            auto world = floor_world(); Unit7Entity unit({10,23.15});
            require(world.place_tile(12,23,TileType::Regolith),"mining fixture failed");
            for (int i = 0; i < 5; ++i) require(unit.use_mining_laser(12,23,world,0.1).ok,"valid laser failed");
            require(unit.inventory().count(Item::Regolith) == 1000,"mine failed to recover material");
            close(unit.battery_percent(),99.95);
            require(unit.place_tile(12,23,world).ok,"valid placement failed");
            close(unit.battery_percent(),99.83);
            require(unit.inventory().count(Item::Regolith) == 0,"placement did not consume material");
            require(100-unit.battery_percent() > 0.003*0.5,"tasks do not cost more than idle time");
        }},
        {"rejected actions preserve charge, inventory and world",[] {
            auto world = floor_world(); Unit7Entity unit({10,23.15});
            unit.inventory().add(Item::Regolith,1000);
            require(!unit.use_mining_laser(12,23,world).ok,"vacuum mining accepted");
            require(!unit.use_mining_laser(10,24,world).ok,"bedrock mining accepted");
            require(!unit.use_mining_laser(40,23,world).ok,"remote mining accepted");
            require(!unit.use_mining_laser(12,23,world,std::numeric_limits<double>::quiet_NaN()).ok,"invalid mining time accepted");
            require(!unit.place_tile(10,23,world).ok,"self placement accepted");
            require(!unit.place_tile(40,23,world).ok,"remote placement accepted");
            close(unit.battery_percent(),100);
            require(unit.inventory().count(Item::Regolith) == 1000,"rejected placement spent material");
            unit.drain_battery(99.995);
            require(world.place_tile(12,23,TileType::IronNickelOre),"low charge fixture failed");
            const double charge = unit.battery_percent();
            require(!unit.use_mining_laser(12,23,world,0.1).ok,"underpowered laser accepted");
            require(!unit.place_tile(11,22,world).ok,"underpowered placement accepted");
            close(world.mining_progress(12,23),0); close(unit.battery_percent(),charge);
            require(world.get_tile_type(11,22) == TileType::VacuumAir,"underpowered placement changed world");
            require(unit.inventory().count(Item::Regolith) == 1000,"underpowered placement consumed material");
        }},
        {"placement protection and mining overflow do not charge",[] {
            auto world = WorldGrid::generate_isometric(); Unit7Entity indoor({6.15,5.075});
            indoor.inventory().add(Item::Regolith,1000);
            require(!indoor.place_tile(7,6,world).ok,"protected habitat placement accepted");
            close(indoor.battery_percent(),100);
            require(indoor.inventory().count(Item::Regolith) == 1000,"protected placement lost material");
            auto side = floor_world(); Unit7Entity full({10,23.15});
            require(side.place_tile(12,23,TileType::IronNickelOre),"overflow fixture failed");
            side.damage_tile(12,23,2);
            full.inventory().add(Item::IronNickelOre,std::numeric_limits<std::int64_t>::max());
            require(!full.use_mining_laser(12,23,side,0.1).ok,"overflow mining accepted");
            close(full.battery_percent(),100);
            require(side.get_tile_type(12,23) == TileType::IronNickelOre,"overflow removed resource");
            require(full.inventory().count(Item::IronNickelOre) == std::numeric_limits<std::int64_t>::max(),"overflow changed inventory");
        }},
        {"successful jumps cost energy and airborne retries are free",[] {
            for (const bool planar : {false,true}) {
                auto world = planar ? WorldGrid::generate_isometric() : floor_world();
                Unit7Entity unit(planar ? Vec2{20.15,10.075} : Vec2{10,23.15});
                unit.update(1.0/120,world);
                const double charge = unit.battery_percent(), elevation = unit.elevation();
                const auto position = unit.position();
                unit.jump(); unit.update(1.0/120,world);
                const double travel = planar ? std::hypot(distance(position,unit.position()),unit.elevation()-elevation)
                                             : distance(position,unit.position());
                close(charge-unit.battery_percent(),0.35+0.003/120+0.04*travel);
                require(!unit.grounded(),"successful jump did not leave ground");
                Unit7Entity control = unit;
                unit.jump(); unit.update(1.0/120,world); control.update(1.0/120,world);
                close(unit.battery_percent(),control.battery_percent());
                close(unit.vertical_velocity(),control.vertical_velocity());
                close(unit.velocity().y,control.velocity().y);
                Unit7Entity low(planar ? Vec2{20.15,10.075} : Vec2{10,23.15});
                low.update(1.0/120,world); low.drain_battery(99.9);
                const double low_charge = low.battery_percent();
                low.jump(); low.update(1.0/120,world);
                require(low.grounded(),"underpowered jump took off");
                close(low_charge-low.battery_percent(),0.003/120,1e-7);
            }
        }},
        {"empty battery stops movement and cannot be revived",[] {
            auto world = WorldGrid::generate_isometric(); Unit7Entity unit({20.15,10.075});
            unit.move_on_plane({1,0},true); unit.update(0.1,world);
            require(unit.vertical_velocity() > 0 && unit.velocity().x > 0,"shutdown fixture is not moving");
            unit.drain_battery(1000);
            close(unit.battery_percent(),0); close(unit.integrity(),100);
            require(!unit.alive(),"empty unit remains alive");
            close(unit.velocity().x,0); close(unit.velocity().y,0); close(unit.vertical_velocity(),0);
            const auto position = unit.position(); const double elevation = unit.elevation();
            unit.move_right(); unit.move_on_plane({1,0},true); unit.jump(); unit.set_climbing(true);
            unit.recharge_battery(100); unit.update(0.25,world);
            close(unit.battery_percent(),0); close(unit.position().x,position.x); close(unit.position().y,position.y);
            close(unit.elevation(),elevation); require(!unit.spend_battery(0),"dead unit accepted spend");
            Unit7Entity sacrificed({10,23.15}); sacrificed.spend_battery(10); sacrificed.shutdown();
            sacrificed.recharge_battery(100); close(sacrificed.battery_percent(),90); close(sacrificed.integrity(),0);
        }},
        {"exhaustion stops the bounded movement substeps",[] {
            auto world = WorldGrid::generate_isometric(); Unit7Entity unit({20.15,10.075});
            unit.drain_battery(99.999); const auto start = unit.position();
            unit.move_on_plane({1,0},false); unit.update(0.25,world);
            require(!unit.alive(),"low charge did not run out");
            require(distance(unit.position(),start) < 0.06,"movement continued after depletion");
            close(unit.velocity().x,0); close(unit.vertical_velocity(),0);
        }},
        {"battery drain and movement agree across rendering rates",[] {
            for (const bool planar : {false,true}) {
                auto world = planar ? WorldGrid::generate_isometric() : floor_world();
                Unit7Entity slow(planar ? Vec2{20.15,10.075} : Vec2{10,23.15}), fast = slow;
                if (planar) { slow.move_on_plane({1,0},true); fast.move_on_plane({1,0},true); }
                else { slow.move_right(); fast.move_right(); slow.jump(); fast.jump(); }
                advance(slow,world,60,1.0/30); advance(fast,world,240,1.0/120);
                close(slow.battery_percent(),fast.battery_percent());
                close(slow.position().x,fast.position().x); close(slow.position().y,fast.position().y);
                close(slow.elevation(),fast.elevation());
            }
        }}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size()-failed << '/' << tests.size() << " battery tests passed\n";
    return failed ? 1 : 0;
}

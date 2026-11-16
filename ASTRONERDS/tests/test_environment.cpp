#include "horizon/PhobosClock.hpp"
#include "horizon/DangerSystem.hpp"
#include "horizon/BaseCamp.hpp"
#include "horizon/WorldGrid.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace horizon;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void close(double a, double b, double epsilon = 1e-7) { require(std::abs(a-b) < epsilon,"values differ"); }
DangerConfig fast_config(int count = 1) { return {0.01,0.01,0.01,30,30,count}; }
void check_creature(const Creature& creature, const WorldGrid& world) {
    require(!BaseCamp::inside(creature.position),"creature entered shelter");
    for (double dx : {-0.23,0.23}) for (double dy : {-0.23,0.23}) {
        const Vec2 point{creature.position.x+dx,creature.position.y+dy};
        const int x = int(std::floor(point.x)), y = int(std::floor(point.y));
        require(world.in_bounds(x,y) && !world.solid(x,y),"creature crossed solid world");
        require(!BaseCamp::inside(point) && !BaseCamp::wall(x,y),"creature crossed habitat boundary");
    }
}
}
int main() {
    std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"Phobos mean solar period and local lighting",[] {
            require(PhobosClock::solar_day_seconds > 27566 && PhobosClock::solar_day_seconds < 27567,"incorrect solar period");
            require(PhobosClock::gameplay_day_seconds > 459 && PhobosClock::gameplay_day_seconds < 460,"gameplay cycle is not 7m39s");
            PhobosClock midnight(0), sunrise(0.25), noon(0.5), sunset(0.75);
            require(midnight.local_time() == "00:00:00" && midnight.night(),"midnight incorrect");
            require(sunrise.local_time() == "06:00:00" && !sunrise.night(),"sunrise incorrect");
            require(noon.local_time() == "12:00:00" && !noon.night(),"noon incorrect");
            require(sunset.local_time() == "18:00:00" && sunset.night(),"sunset incorrect");
            close(midnight.sunlight(),0); close(sunrise.sunlight(),0);
            close(noon.sunlight(),1); close(sunset.sunlight(),0);
            close(noon.seconds_to_transition(),PhobosClock::gameplay_day_seconds*0.25);
        }},
        {"clock compresses the solar day into minutes and counts local days",[] {
            PhobosClock clock(0.73);
            require(clock.day_number() == 1,"day must start at one");
            const double before = clock.phase();
            clock.advance(60);
            close(clock.phase()-before,60/PhobosClock::gameplay_day_seconds);
            clock.advance(PhobosClock::gameplay_day_seconds*2);
            require(clock.day_number() == 3,"day count incorrect");
            close(clock.phase(),before+60/PhobosClock::gameplay_day_seconds);
            PhobosClock near_sunset(0.74);
            near_sunset.advance(PhobosClock::gameplay_day_seconds*0.011);
            require(near_sunset.night(),"sunset did not change night state");
        }},
        {"clock rejects invalid elapsed time and normalizes initial phase",[] {
            PhobosClock clock(-0.25); close(clock.phase(),0.75);
            clock.advance(-10); clock.advance(std::numeric_limits<double>::infinity());
            clock.advance(std::numeric_limits<double>::quiet_NaN());
            close(clock.phase(),0.75); require(clock.day_number() == 1,"invalid dt changed clock");
            PhobosClock fallback(std::numeric_limits<double>::quiet_NaN());
            close(fallback.phase(),PhobosClock::default_starting_phase);
            require(!fallback.night() && fallback.sunlight()>0.99,"invalid initial phase does not fall back to daytime");
            PhobosClock almost_midnight(-1e-30);
            require(almost_midnight.phase() >= 0 && almost_midnight.phase() < 1,"negative tiny phase escaped cycle");
            clock.advance(std::numeric_limits<double>::max());
            require(clock.phase() >= 0 && clock.phase() < 1,"large finite dt broke clock");
            require(clock.day_number() == std::numeric_limits<std::uint64_t>::max(),"large day count did not saturate");
        }},
        {"every danger period has warning and clearing transitions",[] {
            WorldGrid world(40,40); DangerSystem danger(7,{0.2,0.2,0.3,0.4,0.4,0});
            const Vec2 robot{20.5,20.5};
            auto tick = danger.update(0.2,world,robot,false);
            require(danger.warning() && tick.warning_started && tick.beep && !tick.active_started,"missing advance warning");
            close(danger.seconds_remaining(),0.3);
            tick = danger.update(0.15,world,robot,false);
            require(danger.warning() && !tick.active_started,"warning ended early");
            tick = danger.update(0.15,world,robot,false);
            require(danger.active() && tick.active_started && tick.beep,"danger did not start");
            tick = danger.update(0.4,world,robot,false);
            require(danger.phase() == DangerPhase::Calm && tick.cleared,"danger did not clear");
            close(danger.seconds_remaining(),0.2);
            tick = danger.update(1.8,world,robot,false);
            require(tick.warning_started && tick.active_started && tick.cleared,"large dt skipped transition notifications");
            require(danger.phase() == DangerPhase::Calm,"catch-up cycle incorrect");
        }},
        {"warning pulse countdown intensifies for last ten seconds",[] {
            WorldGrid world(40,40); DangerSystem danger(7,{0.01,0.01,20,2,2,0});
            const Vec2 robot{20.5,20.5};
            require(danger.update(0.01,world,robot,false).beep,"missing warning start pulse");
            require(!danger.update(1,world,robot,false).beep,"early pulse before five seconds");
            require(danger.update(4,world,robot,false).beep,"missing five second pulse");
            require(danger.update(5,world,robot,false).beep,"missing last ten second pulse");
            require(!danger.update(0.5,world,robot,false).beep,"early final countdown pulse");
            require(danger.update(0.5,world,robot,false).beep,"missing one second pulse");
        }},
        {"seeded random periods repeat reproducibly and vary by cycle",[] {
            WorldGrid world(40,40); const DangerConfig config{0.2,0.5,0.1,0.2,0.3,0};
            DangerSystem first(91,config), same(91,config), other(92,config);
            require(std::abs(first.seconds_remaining()-other.seconds_remaining()) > 1e-6,"seed has no effect");
            const double initial = first.seconds_remaining();
            bool new_interval = false;
            for (int i = 0; i < 60; ++i) {
                const auto a = first.update(0.1,world,{20.5,20.5},false);
                const auto b = same.update(0.1,world,{20.5,20.5},false);
                require(first.phase() == same.phase() && a.cleared == b.cleared,"seeded phases differ");
                close(first.seconds_remaining(),same.seconds_remaining());
                if (a.cleared && std::abs(first.seconds_remaining()-initial) > 1e-4) new_interval = true;
            }
            require(new_interval,"calm intervals did not rerandomize");
        }},
        {"creatures spawn at fair distance and pursue at bounded speed",[] {
            WorldGrid world(40,40); DangerSystem danger(7,fast_config(3)); const Vec2 robot{20.5,20.5};
            auto tick = danger.update(0.02,world,robot,false);
            require(tick.active_started && tick.battery_damage == 0,"spawn inflicted immediate damage");
            require(danger.creatures().size() == 3,"missing creatures on open ground");
            for (const auto& creature : danger.creatures()) {
                check_creature(creature,world);
                require(distance(creature.position,robot) >= 5 && distance(creature.position,robot) <= 9,"unfair spawn distance");
            }
            const auto before = danger.creatures();
            danger.update(0.1,world,robot,false);
            for (std::size_t i = 0; i < before.size(); ++i) {
                require(distance(before[i].position,danger.creatures()[i].position) <= 0.2900001,"creature exceeds speed");
                require(distance(danger.creatures()[i].position,robot) < distance(before[i].position,robot),"creature did not chase");
            }
        }},
        {"melee damages battery and respects cooldown",[] {
            WorldGrid world(40,40); DangerSystem danger(3,fast_config()); const Vec2 robot{20.5,20.5};
            danger.update(0.02,world,robot,false);
            int hits = 0; double last_hit = -100;
            for (int i = 0; i < 200; ++i) {
                const auto tick = danger.update(0.05,world,robot,false);
                if (tick.battery_damage > 0) {
                    close(tick.battery_damage,8);
                    require(i*0.05-last_hit >= 1.39999,"attack cooldown bypassed");
                    require(distance(danger.creatures().front().position,robot) <= 1.1,"long-range damage");
                    last_hit = i*0.05; ++hits;
                }
            }
            require(hits >= 3,"creature never reached/attacked robot");
        }},
        {"default warning lasts thirty seconds",[] {
            WorldGrid world(40,40); DangerSystem danger(7,{0.01,0.01,30,2,2,0});
            danger.update(0.01,world,{20.5,20.5},false);
            require(danger.warning(),"warning did not start");
            close(danger.seconds_remaining(),30);
            danger.update(29.5,world,{20.5,20.5},false);
            require(danger.warning(),"warning ended before thirty seconds");
            danger.update(0.5,world,{20.5,20.5},false);
            require(danger.active(),"warning did not end at thirty seconds");
        }},
        {"bite windup can be interrupted by laser",[] {
            WorldGrid world(40,40); DangerSystem danger(3,fast_config()); const Vec2 robot{20.5,20.5};
            danger.update(0.02,world,robot,false);
            bool winding = false;
            for (int i = 0; i < 120 && !winding; ++i) {
                auto tick = danger.update(0.05,world,robot,false);
                require(tick.battery_damage == 0,"bite landed before windup");
                winding = danger.creatures().front().bite_age >= 0;
            }
            require(winding,"creature never began bite");
            const auto animal = danger.creatures().front();
            require(animal.bite_age < 0.22,"windup was skipped");
            auto strike = danger.fire_laser(robot,animal.position,world,7,34);
            require(strike.valid && strike.hit && !strike.killed,"laser did not interrupt bite");
            require(danger.creatures().front().bite_age < 0,"bite animation continued after stun");
            for (int i = 0; i < 10; ++i)
                require(danger.update(0.05,world,robot,false).battery_damage == 0,"stunned creature bit robot");
        }},
        {"laser damages and defeats creatures without hitting through walls",[] {
            WorldGrid world(40,40); DangerSystem danger(7,fast_config()); const Vec2 robot{20.5,20.5};
            danger.update(0.02,world,robot,false);
            const Vec2 target = danger.creatures().front().position;
            const Vec2 midpoint{(target.x+robot.x)/2,(target.y+robot.y)/2};
            world.place_tile(int(midpoint.x),int(midpoint.y),TileType::Bedrock);
            const auto blocked = danger.fire_laser(robot,target,world,12);
            require(blocked.valid && !blocked.hit,"laser passed through bedrock");
            WorldGrid clear(40,40); DangerSystem exposed(7,fast_config());
            exposed.update(0.02,clear,robot,false);
            for (int n = 0; n < 3; ++n) {
                const auto shot = exposed.fire_laser(robot,exposed.creatures().front().position,clear,12);
                require(shot.valid && shot.hit,"laser missed stationary creature");
                require(shot.killed == (n == 2),"creature defeated on wrong shot");
            }
            require(exposed.creatures().front().health == 0,"dead creature kept health");
            require(!exposed.fire_laser(robot,target,clear,12).hit,"dead creature absorbed shot");
        }},
        {"habitat and airlock provide absolute shelter",[] {
            WorldGrid world(40,40); DangerSystem danger(7,fast_config(3));
            const Vec2 robot{7.0,8.5}; // Inside the open doorway, even if caller forgot sheltered.
            danger.update(0.02,world,robot,false);
            require(!danger.creatures().empty(),"sheltered player prevented outside spawns");
            for (int i = 0; i < 400; ++i) {
                require(danger.update(0.05,world,robot,false).battery_damage == 0,"sheltered robot attacked");
                for (const auto& creature : danger.creatures()) check_creature(creature,world);
            }
            require(danger.update(1,world,{20.5,20.5},true).battery_damage == 0,"explicit shelter flag ignored");
        }},
        {"walls block melee and creatures find an open passage",[] {
            WorldGrid world(40,40); DangerSystem danger(9,fast_config(3)); const Vec2 robot{20.5,20.5};
            danger.update(0.02,world,robot,false);
            for (int y = 18; y <= 22; ++y) for (int x = 18; x <= 22; ++x)
                if (x == 18 || x == 22 || y == 18 || y == 22) world.place_tile(x,y,TileType::Bedrock);
            for (int i = 0; i < 80; ++i) {
                require(danger.update(0.05,world,robot,false).battery_damage == 0,"attacked through sealed wall");
                for (const auto& creature : danger.creatures()) check_creature(creature,world);
            }
            // Bedrock is permanent, so use a second, identical seeded encounter
            // with a passage in the southern wall to test actual route finding.
            WorldGrid passage(40,40); DangerSystem route(9,fast_config(3));
            route.update(0.02,passage,robot,false);
            for (int y = 18; y <= 22; ++y) for (int x = 18; x <= 22; ++x)
                if ((x == 18 || x == 22 || y == 18 || y == 22) && !(x == 20 && y == 22)) passage.place_tile(x,y,TileType::Bedrock);
            double damage = 0;
            for (int i = 0; i < 400; ++i) {
                damage += route.update(0.05,passage,robot,false).battery_damage;
                for (const auto& creature : route.creatures()) check_creature(creature,passage);
            }
            require(damage > 0,"creatures could not route through passage");
        }},
        {"generated terrain collision and climb limits remain valid",[] {
            auto world = WorldGrid::generate_isometric(); DangerSystem danger(7,fast_config(3));
            const Vec2 robot{30.5,10.5}; danger.update(0.02,world,robot,false);
            require(!danger.creatures().empty(),"generated terrain has no reachable spawns");
            for (int i = 0; i < 500; ++i) {
                const auto before = danger.creatures();
                danger.update(0.02,world,robot,false);
                for (std::size_t n = 0; n < before.size(); ++n) {
                    const auto& creature = danger.creatures()[n]; check_creature(creature,world);
                    require(distance(before[n].position,creature.position) <= 0.0580001,"terrain movement tunneled");
                    const auto old = before[n].position, now = creature.position;
                    require(std::abs(world.elevation(int(std::floor(old.x)),int(std::floor(old.y)))-
                                     world.elevation(int(std::floor(now.x)),int(std::floor(now.y)))) <= 0.6500001,"creature climbed excessive height");
                }
            }
        }},
        {"invalid danger updates preserve scheduler",[] {
            WorldGrid world(40,40); DangerSystem danger(7); const double before = danger.seconds_remaining();
            danger.update(-1,world,{20.5,20.5},false);
            danger.update(std::numeric_limits<double>::quiet_NaN(),world,{20.5,20.5},false);
            danger.update(std::numeric_limits<double>::infinity(),world,{20.5,20.5},false);
            danger.update(1,world,{-1,20.5},false);
            danger.update(1,world,{std::numeric_limits<double>::quiet_NaN(),20.5},false);
            close(danger.seconds_remaining(),before);
            DangerSystem sanitized(7,{-1,std::numeric_limits<double>::infinity(),0,-1,-1,-3});
            require(std::isfinite(sanitized.seconds_remaining()) && sanitized.seconds_remaining() > 0,"invalid config accepted");
            DangerSystem huge_warning(7,{0.01,0.01,std::numeric_limits<double>::max(),1,1,0});
            huge_warning.update(1,world,{20.5,20.5},false);
            require(huge_warning.warning(),"large finite warning failed");
        }}
    };
    int passed = 0;
    for (const auto& test : tests) {
        try { test.second(); ++passed; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << passed << " environment tests passed\n";
}

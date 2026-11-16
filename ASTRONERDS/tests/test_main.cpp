#include "horizon/Renderer.hpp"
#include "horizon/SpaceBackground.hpp"
#include "horizon/TargetTool.hpp"
#include "horizon/PlanetTerrain.hpp"
#include "horizon/SceneLighting.hpp"
#include "horizon/RobotEyes.hpp"
#include "horizon/SceneShadows.hpp"
#include "horizon/BaseCamp.hpp"
#include "horizon/PlanetProjection.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace horizon;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void close(double a, double b, double epsilon = 0.001) { require(std::abs(a-b) < epsilon,"values differ"); }
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"invalid argument accepted");
}
WorldGrid floor_world() {
    WorldGrid world(64,72);
    for (int x = 0; x < world.width(); ++x) world.place_tile(x,24,TileType::Bedrock);
    return world;
}
void prepare_rocket(RocketRepairSystem& rocket, Unit7Entity& unit) {
    unit.inventory().add(Item::StructuralPlates,6);
    unit.inventory().add(Item::RefinedFuel,2);
    unit.inventory().add(Item::DataCore,1);
    require(rocket.weld_hull(unit).ok,"hull repair");
    require(rocket.hookup_fuel_lines(unit).ok,"fuel hookup");
    require(rocket.socket_data_core(unit).ok,"core install");
    require(rocket.calibrate_launch_control_valve(unit).ok,"valve calibration");
}
}
int main() {
    std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"inventory atomicity, repeated inputs and overflow",[] {
            Inventory inventory;
            require(inventory.add(Item::Regolith,1000),"add");
            require(!inventory.exchange({{Item::Regolith,600},{Item::Regolith,600}},{{Item::DataCore,1}}),"duplicate inputs allowed");
            require(inventory.count(Item::Regolith) == 1000 && inventory.count(Item::DataCore) == 0,"partial craft");
            require(!inventory.consume(Item::Regolith,-1),"negative consume");
            inventory.add(Item::Water,std::numeric_limits<std::int64_t>::max());
            require(!inventory.exchange({{Item::Regolith,1000}},{{Item::Water,1}}),"overflow craft");
            require(inventory.count(Item::Regolith) == 1000,"overflow consumed input");
            rejects([&] { inventory.count(Item::Count); });
        }},
        {"world edges, drops and deterministic generation",[] {
            rejects([] { WorldGrid invalid(-1,5); });
            rejects([] { WorldGrid::generate(30,30); });
            auto world = WorldGrid::generate();
            auto same = WorldGrid::generate();
            for (int y = 0; y < world.height(); ++y)
                for (int x = 0; x < world.width(); ++x)
                    require(world.get_tile_type(x,y) == same.get_tile_type(x,y),"seed differs");
            require(world.solid(-1,0) && world.solid(0,world.height()),"open boundary");
            auto core = world.data_core_position();
            auto drop = world.break_tile(core.x,core.y);
            require(drop && drop->item == Item::DataCore && drop->quantity == 1,"core drop");
            require(!world.break_tile(core.x,core.y),"duplicate core");
            require(!world.break_tile(0,world.height()-1),"bedrock broke");
            require(!world.place_tile(-1,1),"out of bounds placement");
        }},
        {"regolith advances one cell and respects entity occupancy",[] {
            WorldGrid world(12,12);
            world.place_tile(4,2);
            world.apply_gravity_to_loose_regolith();
            require(world.get_tile_type(4,3) == TileType::Regolith,"gravity missing");
            require(world.get_tile_type(4,4) == TileType::VacuumAir,"fell multiple cells");
            world.apply_gravity_to_loose_regolith(Aabb{{4,4},0.7,0.85});
            require(world.get_tile_type(4,3) == TileType::Regolith,"buried entity");
            world.apply_gravity_to_loose_regolith();
            require(world.get_tile_type(4,4) == TileType::Regolith,"grain stuck");
        }},
        {"mining requires reach and unobstructed grid rays",[] {
            auto world = floor_world(); Unit7Entity unit({10,23.15});
            world.place_tile(12,23,TileType::IronNickelOre);
            world.place_tile(13,23,TileType::NASADataCore);
            require(!unit.use_mining_laser(13,23,world).ok,"mined through ore");
            require(!unit.use_mining_laser(40,23,world).ok,"long range mining");
            for (int i = 0; i < 120; ++i) unit.use_mining_laser(12,23,world);
            require(unit.inventory().count(Item::IronNickelOre) == 1000,"ore not recovered");
            require(unit.inventory().count(Item::DataCore) == 0,"stole core");
            require(!unit.use_mining_laser(12,23,world,100).ok,"unbounded laser delta");
            WorldGrid corner(8,8); corner.place_tile(2,1); corner.place_tile(1,2);
            require(!corner.line_of_sight({1.5,1.5},{2,2}),"corner leak");
        }},
        {"placement costs resources and cannot intersect the robot",[] {
            auto world = floor_world(); Unit7Entity unit({10,23.15});
            unit.inventory().add(Item::Regolith,1000);
            require(!unit.place_tile(10,23,world).ok,"placed inside robot");
            require(unit.inventory().count(Item::Regolith) == 1000,"failed placement charged");
            require(unit.place_tile(12,23,world).ok,"placement failed");
            require(unit.inventory().count(Item::Regolith) == 0,"free placement");
            require(!unit.place_tile(11,23,world).ok,"unfunded placement");
        }},
        {"physics landing, jumping, wall climbing and no tunneling",[] {
            auto world = floor_world(); Unit7Entity unit({10,10});
            for (int i = 0; i < 240; ++i) unit.update(1.0/60,world);
            close(unit.position().y,23.15); require(unit.grounded(),"not grounded");
            unit.jump(); unit.update(1.0/60,world); require(unit.velocity().y < 0,"jump did not rise");
            for (int i = 0; i < 240; ++i) unit.update(1.0/60,world);
            for (int y = 17; y < 24; ++y) world.place_tile(12,y,TileType::Bedrock);
            unit.move_right();
            for (int i = 0; i < 120; ++i) unit.update(1.0/60,world);
            require(unit.position().x+0.7 <= 12.0001,"wall penetration");
            unit.stop_moving(); unit.set_climbing(true);
            const double y = unit.position().y;
            for (int i = 0; i < 30; ++i) unit.update(1.0/60,world);
            require(unit.position().y < y-1,"wall climb failed");
            const auto before = unit.position(); unit.update(100,world); close(before.x,unit.position().x);
        }},
        {"thermal extraction conserves mass and rejects bad config",[] {
            Unit7Entity unit({10,23.15}); NASA_ISRU_Refinery refinery(unit.center());
            unit.inventory().add(Item::HydratedMinerals,20000);
            require(refinery.thermal_extraction(unit).ok,"extraction failed");
            require(unit.inventory().count(Item::Water) == 1800,"wrong water yield");
            require(unit.inventory().count(Item::Water)+unit.inventory().count(Item::Slag) == 20000,"mass lost");
            rejects([&] { NASA_ISRU_Refinery invalid(unit.center(),{1.1,0.9,873.15}); });
            rejects([&] { NASA_ISRU_Refinery invalid(unit.center(),{0.1,std::numeric_limits<double>::quiet_NaN(),873.15}); });
            NASA_ISRU_Refinery remote({50,23}); require(!remote.electrolysis(unit).ok,"remote crafting");
        }},
        {"wreck workbench supports the refinery chain and rejects outdoor crafting",[] {
            NASA_ISRU_Refinery refinery(BaseCamp::craft_bench); refinery.set_indoor_only();
            Unit7Entity indoor({6.15,4.075});
            require(refinery.can_operate(indoor),"indoor workbench cannot be used");
            close(refinery.operating_position(indoor).x,5.5); close(refinery.operating_position(indoor).y,4.5);
            indoor.inventory().add(Item::HydratedMinerals,20000);
            require(refinery.thermal_extraction(indoor).ok && refinery.electrolysis(indoor).ok &&
                refinery.liquefy_propellant(indoor).ok && refinery.package_refined_fuel(indoor).ok,"indoor propellant chain failed");
            require(indoor.inventory().count(Item::RefinedFuel) == 1,"indoor fuel output incorrect");
            indoor.inventory().add(Item::IronNickelOre,1000); indoor.inventory().add(Item::Regolith,1000);
            require(refinery.alloy_smelting(indoor).ok && indoor.inventory().count(Item::StructuralPlates) == 2,"indoor smelting failed");
            indoor.inventory().add(Item::ScrapAlloys,1000);
            require(refinery.craft_upgrade(indoor,Upgrade::SolarThermalDrill).ok && indoor.has_upgrade(Upgrade::SolarThermalDrill),"indoor upgrade failed");
            require(indoor.inventory().count(Item::StructuralPlates) == 0 && indoor.inventory().count(Item::ScrapAlloys) == 0,"indoor upgrade cost changed");
            indoor.inventory().add(Item::WaterIce,1800);
            require(refinery.melt_ice(indoor).ok && indoor.inventory().count(Item::Water) == 1800,"indoor ice melting failed");
            Unit7Entity outdoor({3.15,4.075});
            outdoor.inventory().add(Item::IronNickelOre,1000); outdoor.inventory().add(Item::Regolith,1000);
            require(distance(outdoor.center(),BaseCamp::craft_bench) < 2.5,"outdoor rejection is not testing a nearby unit");
            require(!refinery.alloy_smelting(outdoor).ok,"wreck bench can be operated from outside");
            Game planar;
            require(planar.refinery().indoor_only(),"planar workbench is not shelter-only");
            close(planar.refinery().position().x,BaseCamp::craft_bench.x);
            close(planar.refinery().position().y,BaseCamp::craft_bench.y);
            require(!planar.refinery().secondary_bench(),"obsolete outdoor/indoor duplicate bench remains");
            Game side(floor_world()); require(!side.refinery().indoor_only(),"side-view bench behavior changed");
        }},
        {"secondary workbench rejects invalid, remote and offline operations",[] {
            NASA_ISRU_Refinery refinery({8.5,10.5}); refinery.set_secondary_bench({5.5,4.5});
            rejects([&] { refinery.set_secondary_bench({std::numeric_limits<double>::quiet_NaN(),4.5}); });
            rejects([&] { refinery.set_secondary_bench({5.5,std::numeric_limits<double>::infinity()}); });
            close(refinery.secondary_bench()->x,5.5); close(refinery.secondary_bench()->y,4.5);
            Unit7Entity remote({20.15,20.075});
            remote.inventory().add(Item::IronNickelOre,1000); remote.inventory().add(Item::Regolith,1000);
            require(!refinery.can_operate(remote) && !refinery.alloy_smelting(remote).ok,"remote indoor crafting allowed");
            require(remote.inventory().count(Item::IronNickelOre) == 1000 && remote.inventory().count(Item::Regolith) == 1000,"remote rejection spent ingredients");
            Unit7Entity outside_wall({3.15,4.075}); outside_wall.inventory().add(Item::WaterIce,1800);
            require(distance(outside_wall.center(),*refinery.secondary_bench()) < 2.5,"through-wall fixture is not in range");
            require(!refinery.can_operate(outside_wall) && !refinery.melt_ice(outside_wall).ok,"indoor bench can be used through habitat wall");
            require(outside_wall.inventory().count(Item::WaterIce) == 1800,"through-wall rejection spent ingredients");
            close(refinery.operating_position(outside_wall).x,8.5); close(refinery.operating_position(outside_wall).y,10.5);
            Unit7Entity entered({6.15,5.075});
            require(refinery.can_operate(entered),"entering habitat did not authorize indoor bench");
            close(refinery.operating_position(entered).x,5.5); close(refinery.operating_position(entered).y,4.5);
            Unit7Entity offline({6.15,4.075}); offline.inventory().add(Item::WaterIce,1800); offline.shutdown();
            require(!refinery.can_operate(offline) && !refinery.melt_ice(offline).ok,"offline indoor crafting allowed");
            require(offline.inventory().count(Item::WaterIce) == 1800,"offline rejection spent ingredients");
        }},
        {"water to gases to separate cryogenic propellant stage",[] {
            Unit7Entity unit({10,23.15}); NASA_ISRU_Refinery refinery(unit.center());
            unit.inventory().add(Item::Water,1800);
            require(refinery.electrolysis(unit).ok,"electrolysis failed");
            require(unit.inventory().count(Item::HydrogenGas) == 200 && unit.inventory().count(Item::OxygenGas) == 1600,"incorrect mass ratio");
            require(!refinery.package_refined_fuel(unit).ok,"gas packaged as liquid");
            require(refinery.liquefy_propellant(unit).ok,"liquefaction failed");
            require(refinery.package_refined_fuel(unit).ok,"fuel packaging failed");
            require(unit.inventory().count(Item::RefinedFuel) == 1,"no fuel kit");
            require(!refinery.electrolysis(unit).ok,"infinite water");
        }},
        {"smelting and upgrades consume exactly once and improve tools",[] {
            auto world = floor_world(); Unit7Entity unit({10,23.15});
            NASA_ISRU_Refinery refinery(unit.center());
            unit.inventory().add(Item::IronNickelOre,1000); unit.inventory().add(Item::Regolith,1000);
            require(refinery.alloy_smelting(unit).ok,"smelt failed");
            require(unit.inventory().count(Item::StructuralPlates) == 2 && unit.inventory().count(Item::Slag) == 1200,"smelt balance");
            unit.inventory().add(Item::ScrapAlloys,1000);
            require(refinery.craft_upgrade(unit,Upgrade::SolarThermalDrill).ok,"upgrade failed");
            require(!refinery.craft_upgrade(unit,Upgrade::SolarThermalDrill).ok,"duplicate upgrade");
            world.place_tile(12,23,TileType::IronNickelOre);
            for (int i = 0; i < 40; ++i) unit.use_mining_laser(12,23,world);
            require(unit.inventory().count(Item::IronNickelOre) == 1000,"drill did not speed mining");
        }},
        {"dust treads remove surface speed penalty",[] {
            WorldGrid world(64,72);
            for (int x = 0; x < 64; ++x) world.place_tile(x,24);
            Unit7Entity slow({10,23.15}), fast({10,23.15});
            NASA_ISRU_Refinery refinery(fast.center());
            fast.inventory().add(Item::StructuralPlates,2); fast.inventory().add(Item::ScrapAlloys,1000);
            require(refinery.craft_upgrade(fast,Upgrade::DustRepellentTreads).ok,"treads craft");
            slow.move_right(); fast.move_right();
            for (int i = 0; i < 120; ++i) { slow.update(1.0/60,world); fast.update(1.0/60,world); }
            require(fast.position().x > slow.position().x+3,"treads have no effect");
        }},
        {"repair ordering, proximity and repeat protections",[] {
            Unit7Entity unit({10,23.15}); RocketRepairSystem rocket(unit.center(),{10.5,6});
            require(!rocket.calibrate_launch_control_valve(unit).ok,"early calibration");
            require(!rocket.hookup_fuel_lines(unit).ok,"early fuel hookup");
            prepare_rocket(rocket,unit); require(rocket.ready(),"not ready");
            unit.inventory().add(Item::StructuralPlates,4);
            require(!rocket.weld_hull(unit).ok && unit.inventory().count(Item::StructuralPlates) == 4,"duplicate repair charge");
            Unit7Entity remote({40,23.15}); require(!rocket.socket_data_core(remote).ok,"remote repair");
        }},
        {"dose units, permanent damage and shielding effect",[] {
            Unit7Entity bare({10,23.15}), shielded({10,23.15});
            NASA_ISRU_Refinery refinery(shielded.center());
            shielded.inventory().add(Item::StructuralPlates,2); shielded.inventory().add(Item::Polyethylene,1000);
            require(refinery.craft_upgrade(shielded,Upgrade::RadiationShielding).ok,"shield craft");
            bare.apply_environment(3600,250,1); shielded.apply_environment(3600,250,1);
            close(bare.accumulated_dose_msv(),1); close(shielded.accumulated_dose_msv(),0.45);
            require(shielded.integrity() > bare.integrity(),"shield ineffective");
            const auto integrity = bare.integrity(); bare.apply_environment(0,250,1); close(bare.integrity(),integrity);
            bare.apply_environment(-100,250,1); close(bare.integrity(),integrity);
        }},
        {"storm sweeps surface and actual overburden attenuates it",[] {
            auto world = floor_world(); RadiationStorm storm;
            storm.start(); storm.update(1);
            require(storm.sample({40,23},world).dose_rate_msv_hr == 0,"front moved too fast");
            storm.update(5);
            auto exposed = storm.sample({10,23},world);
            world.place_tile(10,20,TileType::IronNickelOre);
            auto covered = storm.sample({10,23},world);
            require(covered.dose_rate_msv_hr < exposed.dose_rate_msv_hr,"cover ineffective");
            world.break_tile(10,20);
            close(storm.sample({10,23},world).dose_rate_msv_hr,exposed.dose_rate_msv_hr);
        }},
        {"sacrificial launch guards, fade and success state",[] {
            auto world = floor_world(); Unit7Entity ground({10,23.15});
            const Vec2 valve{10.5,6}; RocketRepairSystem rocket(ground.center(),valve); ClimaxEngine climax;
            require(!climax.activate_launch_valve(ground,rocket).ok,"launched before storm");
            prepare_rocket(rocket,ground); climax.update(1.0/60,ground,world,rocket);
            require(climax.state() == ClimaxState::SolarParticleEvent,"storm not triggered");
            require(!climax.activate_launch_valve(ground,rocket).ok,"launched from ground");
            Unit7Entity summit({valve.x-0.35,valve.y-0.425});
            require(climax.activate_launch_valve(summit,rocket).ok,"summit launch failed");
            require(!summit.alive() && rocket.launched(),"sacrifice not permanent");
            climax.update(3,summit,world,rocket); require(climax.fade_alpha() > 0 && climax.fade_alpha() < 1,"fade missing");
            climax.update(3,summit,world,rocket); require(climax.state() == ClimaxState::Complete,"not complete");
            require(std::string(climax.outcome_text()) == "Mission Complete. Earth Saved.","wrong ending");
            require(!climax.activate_launch_valve(summit,rocket).ok,"duplicate launch");
        }},
        {"unshielded failed climb cannot launch or craft after shutdown",[] {
            auto world = floor_world(); Unit7Entity ground({10,23.15});
            RocketRepairSystem rocket(ground.center(),{10.5,6}); ClimaxEngine climax;
            prepare_rocket(rocket,ground);
            for (int i = 0; i < 18000 && ground.alive(); ++i) climax.update(1.0/60,ground,world,rocket);
            require(climax.state() == ClimaxState::Failed && !ground.alive(),"storm not lethal");
            require(!climax.activate_launch_valve(ground,rocket).ok,"dead robot launched");
            NASA_ISRU_Refinery refinery(ground.center()); ground.inventory().add(Item::Water,1800);
            require(!refinery.electrolysis(ground).ok,"dead robot crafted");
            require(!ground.place_tile(11,23,world).ok,"dead robot built");
        }},
        {"fixed-step frame-rate invariance and bounded stalls",[] {
            Game sixty, one_twenty; InputState input; input.right = true;
            for (int i = 0; i < 60; ++i) sixty.advance(input,1.0/60);
            for (int i = 0; i < 120; ++i) one_twenty.advance(input,1.0/120);
            close(sixty.unit().position().x,one_twenty.unit().position().x);
            close(sixty.unit().position().y,one_twenty.unit().position().y);
            const auto before = sixty.simulated_seconds(); sixty.advance(input,100);
            close(sixty.simulated_seconds()-before,0.25);
            const auto time = sixty.simulated_seconds(); sixty.advance(input,std::numeric_limits<double>::quiet_NaN());
            close(time,sixty.simulated_seconds());
        }},
        {"software renderer produces opaque pixel art and safe coordinates",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            std::set<std::uint32_t> colors;
            for (auto pixel : renderer.pixels()) { require((pixel>>24) == 255,"nonopaque pixel"); colors.insert(pixel); }
            require(colors.size() > 30 && renderer.pixels().size() == 640*360,"empty renderer");
            auto tile = renderer.screen_to_tile(320,185);
            require(game.world().in_bounds(tile.x,tile.y),"bad camera transform");
            std::ostringstream output; TerminalRenderer terminal(output); terminal.render(game);
            require(output.str().find("Unit-01") != std::string::npos,"missing terminal output");
        }},
        {"robot models switch visibly without altering the mission or cursor picking",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer;
            const auto position = game.unit().center();
            const auto inventory = game.unit().inventory(); const auto tasks = game.tasks();
            const double time = game.simulated_seconds(), clock = game.presentation().clock;
            require(renderer.robot_model() == RobotModel::Mark2,"new robot is not the default");
            renderer.render(game); const auto mark2 = renderer.pixels();
            const auto ore = renderer.item_to_screen({13,8},game.world());
            const auto picked2 = renderer.screen_to_tile(int(ore.x),int(ore.y));
            renderer.toggle_robot_model(); renderer.render(game);
            require(renderer.robot_model() == RobotModel::Mark1,"original robot cannot be selected");
            const auto mark1 = renderer.pixels();
            std::size_t changed = 0;
            for (int y = PixelRenderer::scene_top; y < PixelRenderer::scene_bottom; ++y)
                for (int x = 0; x < PixelRenderer::width; ++x)
                    if (mark1[y*PixelRenderer::width+x] != mark2[y*PixelRenderer::width+x]) ++changed;
            require(changed > 50,"robot models only change the HUD label");
            const auto picked1 = renderer.screen_to_tile(int(ore.x),int(ore.y));
            require(picked1.x == 13 && picked1.y == 8 && picked2.x == picked1.x && picked2.y == picked1.y,"model selection changed raised ore picking");
            renderer.toggle_robot_model(); renderer.render(game);
            require(renderer.pixels() == mark2,"switching back did not restore the same robot frame");
            renderer.set_robot_model(RobotModel::Mark1); renderer.render(game);
            require(renderer.pixels() == mark1,"original robot changes after a model round trip");
            close(game.simulated_seconds(),time,1e-8); close(game.presentation().clock,clock,1e-8);
            close(game.unit().center().x,position.x,1e-8); close(game.unit().center().y,position.y,1e-8);
            for (std::size_t i = 0; i < item_count; ++i)
                require(game.unit().inventory().count(static_cast<Item>(i)) == inventory.count(static_cast<Item>(i)),"model selection changed inventory");
            const auto after = game.tasks();
            for (std::size_t i = 0; i < tasks.size(); ++i)
                require(tasks[i].progress == after[i].progress && tasks[i].complete == after[i].complete && tasks[i].active == after[i].active,"model selection changed mission progress");
        }},
        {"robot header selects models while modal overlays block selection",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            require(!renderer.world_input_at(20,15,game),"robot selector leaks world input");
            require(renderer.click(20,15,game) && renderer.robot_model() == RobotModel::Mark1,"robot header does not select original model");
            require(renderer.click(20,15,game) && renderer.robot_model() == RobotModel::Mark2,"robot header does not return to new model");
            renderer.toggle_help();
            require(renderer.click(20,15,game) && renderer.robot_model() == RobotModel::Mark2,"model selector leaks through field guide");
            renderer.close_overlays(); require(renderer.robot_model() == RobotModel::Mark2,"closing overlays reset model");
            renderer.toggle_inventory();
            require(renderer.click(20,15,game) && renderer.robot_model() == RobotModel::Mark2,"model selector leaks through inventory");
            renderer.close_overlays();
            renderer.set_pointer(20,15); renderer.render(game);
            require(renderer.inspection_text().find("MARK 2") != std::string::npos,"robot selector has no model hint");
        }},
        {"Mark 2 materials are editable while Mark 1 retains its original appearance",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            const auto original2 = renderer.pixels(); const auto default_appearance = renderer.robot_appearance();
            renderer.set_robot_model(RobotModel::Mark1); renderer.render(game); const auto original1 = renderer.pixels();
            RobotAppearance custom = default_appearance;
            custom.armor = 0xc04435; custom.trim = 0x5274b8; custom.optics = 0xe6b45f;
            renderer.set_robot_appearance(custom); renderer.render(game);
            std::size_t changed = 0;
            for (int y = 160; y < 220; ++y) for (int x = 285; x < 355; ++x)
                if (renderer.pixels()[y*640+x] != original2[y*640+x]) ++changed;
            require(changed > 10,"editable Mark 2 materials have no visible effect");
            close(game.simulated_seconds(),0,1e-8);
            renderer.set_robot_model(RobotModel::Mark1); renderer.render(game);
            require(renderer.pixels() == original1,"Mark 2 edits altered the original Mark 1");
            renderer.set_robot_appearance(default_appearance); renderer.render(game);
            require(renderer.pixels() == original2,"resetting materials did not restore Mark 2");
        }},
        {"minimap keeps landmarks and robot location inside its corner panel",[] {
            Game game(WorldGrid::generate_isometric()); Minimap map;
            const auto& world = game.world();
            for (Vec2 point : {Vec2{0,0},Vec2{double(world.width()),0},Vec2{0,double(world.height())},
                              Vec2{double(world.width()),double(world.height())},BaseCamp::center,game.rocket().ground_socket()}) {
                const auto screen = map.location(point,world);
                require(Minimap::contains(int(screen.x),int(screen.y)),"minimap landmark left panel bounds");
            }
            const auto origin = map.location({10,10},world), east = map.location({11,10},world), south = map.location({10,11},world);
            require(east.x > origin.x && south.y > origin.y,"minimap orientation is inconsistent");
            close(east.x-origin.x,south.y-origin.y,1e-8);
            std::vector<std::uint32_t> pixels(640*360,0xff102030); RobotAppearance appearance;
            map.render(pixels,game,appearance); const auto first = pixels;
            require(pixels[0] == 0xff102030 && pixels[350*640] == 0xff102030,"minimap paints outside its corner");
            const auto start = map.location(game.unit().center(),world);
            InputState drive; drive.right = drive.down = true;
            for (int i = 0; i < 60; ++i) game.advance(drive,1.0/60);
            const auto moved = map.location(game.unit().center(),world);
            require(distance(start,moved) > 2,"minimap location did not follow actual travel");
            const auto clock = game.simulated_seconds(); map.render(pixels,game,appearance);
            require(pixels != first,"minimap robot marker is static"); close(game.simulated_seconds(),clock,1e-8);
            for (auto pixel : pixels) require((pixel>>24) == 255,"minimap produced transparent pixels");
        }},
        {"minimap consumes world input and stays below the mission checklist",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            const int x = Minimap::left+12, y = Minimap::top+24;
            require(!renderer.world_input_at(x,y,game),"minimap click leaks to mining");
            const auto count = game.unit().inventory().count(Item::Regolith);
            require(renderer.click(x,y,game) && !renderer.modal_open(),"minimap does not consume clicks");
            require(game.unit().inventory().count(Item::Regolith) == count,"minimap spent inventory");
            renderer.toggle_tasks(); renderer.render(game);
            require(!renderer.world_input_at(x,y,game),"checklist enables mining through map");
            require(Minimap::left+Minimap::width == PixelRenderer::width-8 && Minimap::width <= 104 && Minimap::height <= 84,
                    "small map is not aligned to the right edge");
            require(Minimap::top > 206 && Minimap::top+Minimap::height <= 312,
                    "map overlaps expanded checklist or item dock");
            require(renderer.world_input_at(30,220,game),"old map corner still blocks the world");
        }},
        {"empty battery shuts down the mission without erasing structural integrity",[] {
            GameOptions options; options.danger_seed = 7; options.initial_battery = 0.0001;
            Game game(WorldGrid::generate_isometric(),options);
            game.advance({},0.25);
            require(game.unit().battery_percent() == 0 && !game.unit().alive(),"empty battery did not stop robot");
            require(game.finished() && game.climax().state() == ClimaxState::Failed,"empty battery did not fail mission");
            close(game.unit().integrity(),100); require(game.last_message().find("BATTERY EMPTY") != std::string::npos,"failure reason is not battery-specific");
            require(!game.perform(Action::Smelt).ok,"offline battery robot can perform tasks");
        }},
        {"heavy task energy checks reject without spending resources or power",[] {
            GameOptions options; options.danger_seed = 7; options.initial_battery = 1;
            Game game(WorldGrid::generate_isometric(),options);
            const auto before = game.unit().inventory();
            require(!game.perform(Action::Liquefy).ok,"underpowered heavy task succeeded");
            close(game.unit().battery_percent(),1,1e-8);
            for (std::size_t i = 0; i < item_count; ++i)
                require(game.unit().inventory().count(static_cast<Item>(i)) == before.count(static_cast<Item>(i)),"rejected task changed inventory");
            require(Game::action_battery_cost(Action::Liquefy) > Game::action_battery_cost(Action::PackageFuel),"heavy task energy is not weighted");
            require(!game.finished() && game.unit().alive(),"low but nonzero battery incorrectly failed the mission");
        }},
        {"walking to the charging dock refills battery and habitat prevents creature hits",[] {
            GameOptions options; options.danger_seed = 47; options.initial_battery = 60;
            options.danger.safe_min = options.danger.safe_max = 10; options.danger.warning_seconds = 2;
            options.danger.active_min = options.danger.active_max = 40;
            Game game(WorldGrid::generate_isometric(),options);
            const auto walk = [&](Vec2 target) {
                int attempts = 0;
                while (distance(game.unit().center(),target) > 0.12 && ++attempts < 500) {
                    const auto p = game.unit().center();
                    const int x = target.x-p.x > 0.05 ? 1 : target.x-p.x < -0.05 ? -1 : 0;
                    const int y = target.y-p.y > 0.05 ? 1 : target.y-p.y < -0.05 ? -1 : 0;
                    InputState input; input.right = x-y > 0; input.left = x-y < 0; input.down = x+y > 0; input.up = x+y < 0;
                    game.advance(input,1.0/60);
                }
                for (int i = 0; i < 18; ++i) game.advance({},1.0/60);
                require(attempts < 500,"charging dock route is blocked");
            };
            for (Vec2 target : {Vec2{10.5,12.3},Vec2{7,12.3},BaseCamp::approach,Vec2{7,6.5},BaseCamp::charger}) walk(target);
            require(game.inside_base() && game.at_charger() && game.charging(),"robot is not docked after entering base");
            const double before = game.unit().battery_percent();
            for (int i = 0; i < 180; ++i) game.advance({},1.0/60);
            require(game.unit().battery_percent() > before+11,"charging dock did not deliver power");
            for (int i = 0; i < 12*60; ++i) game.advance({},1.0/60);
            require(game.danger().active(),"shelter fixture missed the danger period");
            close(game.unit().battery_percent(),100,1e-5); close(game.attack_flash(),0,1e-8);
            require(!game.finished(),"sheltered charging failed the mission");
        }},
        {"danger warning emits an audible countdown before the hunt and paints the boundary",[] {
            GameOptions options; options.danger_seed = 47; options.danger.safe_min = options.danger.safe_max = 0.1;
            options.danger.warning_seconds = 2; options.danger.active_min = options.danger.active_max = 10;
            Game warning(WorldGrid::generate_isometric(),options);
            warning.advance({},0.25);
            require(warning.danger().warning() && !warning.danger().active(),"hunt began before warning countdown");
            bool audible = false;
            for (const auto& event : warning.presentation().sound_events()) audible = audible || event.cue == SoundCue::Alarm;
            require(audible,"warning countdown has no alarm cue");
            PixelRenderer alarm; alarm.render(warning);
            GameOptions quiet_options; quiet_options.danger_seed = 47;
            Game quiet(WorldGrid::generate_isometric(),quiet_options); quiet.advance({},0.25);
            PixelRenderer normal; normal.render(quiet);
            const auto a = alarm.pixels()[100*640+2], b = normal.pixels()[100*640+2];
            require(((a>>16)&255) > ((b>>16)&255),"warning has no red boundary alarm");
            for (int i = 0; i < 150; ++i) warning.advance({},1.0/60);
            require(warning.danger().active() && !warning.danger().creatures().empty(),"warning did not lead to physical creatures");
        }},
        {"combat laser consumes battery, emits sound and respects its firing interval",[] {
            GameOptions options; options.danger_seed = 47;
            options.danger.safe_min = options.danger.safe_max = 0.01;
            options.danger.warning_seconds = 0.01;
            options.danger.active_min = options.danger.active_max = 20;
            Game game(WorldGrid::generate_isometric(),options);
            game.advance({},0.1);
            require(game.danger().active(),"combat fixture did not start a hunt");
            const double before = game.unit().battery_percent();
            InputState fire; fire.combat_target = game.danger().creatures().front().position;
            game.advance(fire,1.0/60);
            require(game.combat_beam().has_value(),"laser has no visible beam");
            require(game.unit().battery_percent() < before-0.59,"laser did not spend charge");
            bool audible = false;
            for (const auto& event : game.presentation().sound_events()) audible |= event.cue == SoundCue::CombatFire;
            require(audible,"laser has no firing sound");
            const double after = game.unit().battery_percent();
            game.advance(fire,0.1);
            require(game.unit().battery_percent() > after-0.1,"laser bypassed its firing interval");
        }},
        {"Phobos night darkens the field while rendering leaves the real clock unchanged",[] {
            GameOptions options; options.danger_seed = 7; options.starting_solar_phase = 0.5;
            Game day(WorldGrid::generate_isometric(),options); options.starting_solar_phase = 0;
            Game night(WorldGrid::generate_isometric(),options);
            PixelRenderer sun, dark; sun.toggle_headlamp(); dark.toggle_headlamp(); sun.render(day); dark.render(night);
            std::uint64_t daylight = 0, nighttime = 0;
            const auto brightness = [](std::uint32_t p) { return ((p>>16)&255)+((p>>8)&255)+(p&255); };
            for (int y = 210; y < 270; ++y) for (int x = 410; x < 550; ++x) {
                daylight += brightness(sun.pixels()[y*640+x]); nighttime += brightness(dark.pixels()[y*640+x]);
            }
            require(daylight > nighttime*2,"night does not visibly darken outdoor terrain");
            PlanetProjection roof_view; roof_view.curved = false; roof_view.anchor = day.unit().center();
            const auto roof = roof_view.project({9.8,7.3},2.8).screen;
            const auto roof_pixel = std::size_t(int(roof.y))*640+int(roof.x);
            require(brightness(sun.pixels()[roof_pixel]) > brightness(dark.pixels()[roof_pixel])*2,
                "outside roof incorrectly receives indoor night lighting");
            const auto before = night.phobos().phase(); dark.render(night); close(night.phobos().phase(),before,1e-12);
            night.advance({},0.25); close(night.phobos().phase()-before,0.25/PhobosClock::gameplay_day_seconds,1e-12);
        }},
        {"animation lifetime and particle budget remain bounded",[] {
            Presentation effects;
            for (int i = 0; i < 100; ++i) effects.mining({2,2},0.5,true,0xffcc88);
            require(!effects.particles().empty() && effects.particles().size() <= 160,"particle budget exceeded");
            effects.activity({2,2},"CRAFT COMPLETE",true);
            effects.notify("Missing materials");
            for (int i = 0; i < 10; ++i) effects.update(0.25,{2,2},{-2,0},false,true);
            require(effects.particles().empty() && !effects.active_activity,"effects never expired");
            require(!effects.notice,"temporary notice became permanent HUD text");
            require(!effects.mining_target && effects.facing == -1,"pose did not reset");
            const auto clock = effects.clock;
            effects.update(std::numeric_limits<double>::quiet_NaN(),{},{},false,true);
            close(clock,effects.clock);
        }},
        {"mining animation follows valid hits, not rejected actions",[] {
            auto world = floor_world(); world.place_tile(14,23,TileType::IronNickelOre);
            Game game(std::move(world)); InputState laser; laser.mining_target = GridPos{14,23};
            game.advance(laser,0.1);
            require(game.presentation().mining_target.has_value(),"valid mining has no beam");
            require(game.presentation().mining_progress > 0,"missing mining progress");
            require(!game.perform(Action::Smelt).ok,"unfunded craft succeeded");
            require(!game.presentation().active_activity,"failed crafting animated a success");
            game.advance({},0.1); require(!game.presentation().mining_target,"beam persisted after release");
            laser.mining_target = GridPos{50,23}; game.advance(laser,0.1);
            require(!game.presentation().mining_target,"out of reach mining animated");
        }},
        {"HUD consumes mouse input and rendering does not advance animation",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            const auto before = renderer.pixels(); renderer.render(game);
            require(before == renderer.pixels(),"render changed a paused frame");
            require(renderer.world_input_at(480,120,game),"collapsed tasks cover gameplay");
            require(!renderer.world_input_at(480,70,game),"visible task card leaks input");
            require(renderer.world_input_at(240,240,game),"collapsed crafting covers gameplay");
            renderer.toggle_tasks(); renderer.render(game);
            require(!renderer.world_input_at(480,120,game),"task panel leaks world input");
            require(before != renderer.pixels(),"task toggle did not alter HUD");
            require(!renderer.world_input_at(20,20,game),"header leaks world input");
            require(renderer.world_input_at(10,20,game),"empty glass-header margin blocks the world");
            require(!renderer.world_input_at(200,340,game),"inventory bar leaks world input");
            require(renderer.world_input_at(40,330,game),"empty space beside compact dock blocks world input");
            require(!renderer.world_input_at(530,330,game),"materials button leaks world input");
            renderer.toggle_inventory(); renderer.render(game);
            require(renderer.modal_open() && !renderer.world_input_at(320,180,game),"open inventory leaks world input");
            renderer.toggle_help(); require(renderer.help_visible(),"guide failed to open");
            renderer.close_overlays(); require(!renderer.modal_open(),"guide failed to close");
            require(renderer.click(530,330,game) && renderer.modal_open(),"materials badge is not clickable");
            renderer.close_overlays();
            require(renderer.click(618,15,game) && renderer.help_visible(),"help badge is not clickable");
            require(renderer.click(550,56,game) && !renderer.modal_open(),"guide close label is not clickable");
        }},
        {"pause freezes the mission, battery, sunlight, threats and presentation",[] {
            Game game(WorldGrid::generate_isometric()); InputState input; input.right = true; input.jump = true;
            game.advance({},1.0/60);
            const auto position = game.unit().center();
            const double clock = game.simulated_seconds(), animation = game.presentation().clock;
            const double solar = game.phobos().phase(), battery = game.unit().battery_percent();
            const auto danger = game.danger().seconds_remaining();
            game.set_paused(true);
            for (int i = 0; i < 120; ++i) game.advance(input,1.0/60);
            close(game.simulated_seconds(),clock); close(game.presentation().clock,animation);
            close(game.phobos().phase(),solar); close(game.unit().battery_percent(),battery);
            close(game.danger().seconds_remaining(),danger);
            close(game.unit().center().x,position.x); close(game.unit().center().y,position.y);
            require(!game.perform(Action::Smelt).ok && !game.place_tile({12,10}).ok,"paused mission accepts world actions");
            game.set_paused(false); game.advance({},1.0/60);
            close(game.simulated_seconds()-clock,1.0/60,1e-8);
            require(game.unit().grounded(),"pause queued an unintended jump on resume");
        }},
        {"pause menu routes sound sliders, back, resume and explicit commands",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            renderer.escape_menu(); require(renderer.pause_visible() && renderer.paused(),"Escape did not pause");
            const auto time = game.simulated_seconds();
            require(renderer.click(320,150,game) && renderer.sound_settings_visible(),"sound settings button fails");
            require(!renderer.world_input_at(320,180,game),"settings leak world clicks");
            renderer.click(352,108,game); close(renderer.audio_settings().master,0.5);
            renderer.click(433,108,game); close(renderer.audio_settings().master,0.6);
            renderer.click(273,108,game); close(renderer.audio_settings().master,0.5);
            renderer.click(288,216,game); close(renderer.audio_settings().chatter,0);
            renderer.click(200,250,game); require(renderer.audio_settings().muted,"mute button fails");
            renderer.render(game); const auto frame = renderer.pixels(); renderer.render(game);
            require(frame == renderer.pixels(),"paused menu changes a fixed frame"); close(game.simulated_seconds(),time);
            renderer.escape_menu(); require(renderer.pause_visible() && !renderer.sound_settings_visible(),"Escape failed to return to pause");
            renderer.click(320,118,game); require(!renderer.paused(),"resume button fails");
            close(renderer.audio_settings().master,0.5); require(renderer.audio_settings().muted,"resume resets sound preferences");
            renderer.toggle_pause(); renderer.click(320,190,game);
            require(renderer.take_menu_command() == PixelRenderer::MenuCommand::Restart,"restart not routed to frontend");
            require(renderer.take_menu_command() == PixelRenderer::MenuCommand::None,"restart command repeats");
            renderer.click(320,225,game);
            require(renderer.take_menu_command() == PixelRenderer::MenuCommand::Quit,"quit button fails");
            renderer.close_overlays(); close(renderer.audio_settings().master,0.5);
        }},
        {"startup menu routes main, mission selection, options and quit",[] {
            PixelRenderer renderer;
            renderer.open_startup();
            require(renderer.startup_visible() && renderer.startup_page() == PixelRenderer::StartupPage::Main,
                    "startup did not open on the main page");
            renderer.render_startup(0); const auto still=renderer.pixels();
            renderer.render_startup(1); require(still != renderer.pixels(),"space background is not animated");
            renderer.startup_move(1); renderer.startup_activate();
            require(renderer.startup_page() == PixelRenderer::StartupPage::Missions,"missions button does not open selection");
            renderer.startup_click(320,170);
            require(!renderer.startup_visible() && renderer.take_startup_command() == PixelRenderer::StartupCommand::StartMission2,
                    "test Mission 2 does not start");
            renderer.open_startup(); renderer.startup_move(1); renderer.startup_activate();
            renderer.startup_click(320,216);
            require(!renderer.startup_visible() && renderer.take_startup_command() == PixelRenderer::StartupCommand::StartMission3,
                    "test Mission 3 does not start");
            renderer.open_startup(); renderer.startup_move(1); renderer.startup_activate(); renderer.startup_click(320,260);
            require(!renderer.startup_visible() && renderer.take_startup_command() == PixelRenderer::StartupCommand::StartMission4,
                    "test Mission 4 does not start");
            renderer.open_mission_select();renderer.startup_click(320,281);
            require(!renderer.startup_visible() && renderer.take_startup_command()==PixelRenderer::StartupCommand::StartMission6,
                    "Mission 6 does not replace the removed Mission 5 selector entry");
            renderer.open_startup(); renderer.startup_move(1); renderer.startup_activate();
            renderer.startup_click(320,130);
            require(!renderer.startup_visible() && renderer.take_startup_command() == PixelRenderer::StartupCommand::StartMission1,
                    "Mission 1 does not start");

            renderer.open_startup(); renderer.startup_click(320,230);
            require(renderer.startup_page() == PixelRenderer::StartupPage::Options,"options button does not open sound settings");
            renderer.startup_click(352,108); close(renderer.audio_settings().master,0.5);
            renderer.startup_click(400,250);
            require(renderer.startup_page() == PixelRenderer::StartupPage::Main && renderer.startup_selection() == 2,
                    "options back button loses its main-menu position");
            renderer.startup_move(1); renderer.startup_activate();
            require(renderer.take_startup_command() == PixelRenderer::StartupCommand::Quit,"startup quit is not routed to SDL");
        }},
        {"sound preferences separate chatter and effects and silence master mute",[] {
            AudioSettings settings; settings.master = 0.5; settings.effects = 0.8; settings.chatter = 0.2;
            close(settings.gain(SoundCue::Hit),0.4); close(settings.gain(SoundCue::RobotTalk),0.1);
            settings.muted = true; close(settings.gain(SoundCue::Hit),0); close(settings.gain(SoundCue::RobotTalkHappy),0);
            SoundEffects synth; std::vector<std::int16_t> buffer(9600);
            synth.play(SoundCue::Hit,settings.gain(SoundCue::Hit)); synth.render(buffer.data(),buffer.size()/2);
            for (const auto sample : buffer) require(sample == 0,"muted effect is audible");
            settings.muted = false; settings.master = 0; close(settings.gain(SoundCue::RobotTalk),0);
        }},
        {"clickable crafting follows real inventory and bench proximity",[] {
            auto world = floor_world(); world.place_tile(13,23,TileType::Regolith); world.place_tile(14,23,TileType::IronNickelOre);
            Game game(std::move(world)); PixelRenderer renderer;
            renderer.toggle_crafting(game);
            require(!renderer.action_at(60,90,game),"unfunded recipe is clickable");
            for (int x : {13,14}) {
                InputState input; input.mining_target = GridPos{x,23};
                for (int i = 0; i < 125; ++i) game.advance(input,1.0/60);
            }
            const auto action = renderer.action_at(60,90,game);
            require(action && *action == Action::Smelt,"funded smelting card is unavailable");
            const auto before = game.unit().inventory().count(Item::IronNickelOre);
            renderer.render(game); renderer.render(game);
            require(game.unit().inventory().count(Item::IronNickelOre) == before,"drawing consumed inventory");
            require(renderer.click(60,90,game) && game.unit().inventory().count(Item::StructuralPlates) == 2,"card did not invoke actual crafting");
            require(!renderer.action_at(60,90,game),"spent ingredients still show as affordable");
            require(!renderer.action_at(226,90,game),"gap between cards is clickable");
            renderer.close_overlays();
            InputState walk; walk.right = true;
            for (int i = 0; i < 180; ++i) game.advance(walk,1.0/60);
            renderer.toggle_crafting(game);
            require(!renderer.modal_open(),"craft menu opened away from bench");
        }},
        {"J mines while held and places only on a new press",[] {
            auto world = floor_world(); world.place_tile(13,23,TileType::Regolith); world.place_tile(14,23,TileType::IronNickelOre);
            Game game(std::move(world)); TargetTool tool;
            InputState dust; dust.mining_target = GridPos{13,23};
            for (int i = 0; i < 40; ++i) game.advance(dust,1.0/60);
            require(game.unit().inventory().count(Item::Regolith) == 1000,"fixture has no placement material");
            tool.press(game,{14,23});
            for (int i = 0; i < 150; ++i) {
                InputState input; input.mining_target = tool.target({14,23}); game.advance(input,1.0/60);
            }
            require(game.world().get_tile_type(14,23) == TileType::VacuumAir,"held mining placed a block back");
            require(game.unit().inventory().count(Item::IronNickelOre) == 1000,"J did not mine ore");
            require(game.unit().inventory().count(Item::Regolith) == 1000,"mining spent placement material");
            tool.release(); require(!tool.target({14,23}),"J release left laser active");
            tool.press(game,{14,23});
            require(game.world().get_tile_type(14,23) == TileType::Regolith,"J did not place into empty space");
            require(game.unit().inventory().count(Item::Regolith) == 0,"J placement was free");
            require(!tool.target({14,23}),"placement press became mining");
            tool.press(game,{-1,0}); require(!tool.target({-1,0}),"invalid target activated tool");
        }},
        {"3D background animates independently and clips to the world viewport",[] {
            BackgroundView view;
            std::vector<std::uint32_t> first(640*360,0xffabcdef), second = first;
            require(SpaceBackground::draw(first,view),"background rejected valid surface");
            require(SpaceBackground::draw(second,view) && first == second,"background is not deterministic");
            view.time = 2;
            SpaceBackground::draw(second,view);
            int animated = 0;
            for (int y = 32; y < 150; ++y) for (int x = 0; x < 640; ++x)
                if (first[y*640+x] != second[y*640+x]) ++animated;
            require(animated > 100,"sphere/mesh animation is static");
            for (int x = 0; x < 640; ++x)
                require(first[x] == 0xffabcdef && first[340*640+x] == 0xffabcdef,"background painted over HUD");
            const auto before = second;
            view.camera_x = 140; SpaceBackground::draw(second,view);
            require(second != before,"background has no parallax");
            view.time = std::numeric_limits<double>::quiet_NaN();
            require(!SpaceBackground::draw(second,view),"invalid background clock accepted");
        }},
        {"new missions open in daytime with sparse, sufficient resources",[] {
            Game game(WorldGrid::generate_isometric());
            require(!game.phobos().night() && game.phobos().sunlight()>0.99,"mission does not open in daytime");
            require(game.phobos().seconds_to_transition() > 100,"opening day ends too soon");
            for (unsigned seed : {7u,42u,137u}) {
                const auto world = WorldGrid::generate_isometric(72,64,seed);
                int resources = 0;
                for (int y = 1; y < 63; ++y) for (int x = 1; x < 71; ++x) {
                    const auto type = world.get_tile_type(x,y);
                    resources += type == TileType::Regolith || type == TileType::HydratedMinerals ||
                                 type == TileType::IronNickelOre || type == TileType::WaterIce;
                }
                require(resources > 90 && resources < 260,"resource field is crowded or depleted");
                for (int y : {8,12}) for (int x : {13,15,17})
                    require(world.get_tile_type(x,y) == TileType::IronNickelOre,"starter ore removed");
                for (int y : {14,16}) for (int x : {8,10,12})
                    require(world.get_tile_type(x,y) == TileType::Regolith,"starter dust removed");
            }
        }},
        {"default world is an overhead expedition with stable terrain heights",[] {
            Game game(WorldGrid::generate_isometric());
            auto same = WorldGrid::generate_isometric();
            require(same.width() == 144 && same.height() == 64,"double-width expedition dimensions missing");
            require(game.world().planar(),"default game is still side-scrolling");
            for (int y = 0; y < same.height(); ++y) for (int x = 0; x < same.width(); ++x) {
                require(same.get_tile_type(x,y) == game.world().get_tile_type(x,y),"island seed differs");
                close(same.elevation(x,y),game.world().elevation(x,y));
            }
            const auto start = game.unit().center();
            for (int i = 0; i < 180; ++i) game.advance({},1.0/60);
            close(start.x,game.unit().center().x); close(start.y,game.unit().center().y);
            require(same.place_tile(20,10),"fixture road isn't clear");
            same.apply_gravity_to_loose_regolith();
            require(same.get_tile_type(20,10) == TileType::Regolith,"southward gravity in overhead map");
            require(same.elevation(same.width()-9,14) > same.elevation(same.width()-14,14),"launch ramp is flat");
            require(game.rocket().ground_socket().x == 130.5,"rocket did not move with the wider eastern field");
            require(same.data_core_position().x == 132,"data core did not move with the wider eastern field");
        }},
        {"base habitat supports ordinary entrance, interior walking and exit",[] {
            const auto world = WorldGrid::generate_isometric();
            Unit7Entity unit({BaseCamp::approach.x-0.35,BaseCamp::approach.y-0.425});
            unit.move_on_plane({0,-1},false);
            for (int i = 0; i < 120 && unit.center().y > 6.2; ++i) unit.update(1.0/120,world);
            unit.move_on_plane({},false); for (int i = 0; i < 12; ++i) unit.update(1.0/120,world);
            require(BaseCamp::inside(unit.center()) && unit.center().y < 7,"robot cannot walk through habitat doorway");
            close(unit.elevation(),BaseCamp::floor_elevation); require(unit.grounded(),"habitat floor is not level");
            unit.move_on_plane({0,1},false);
            for (int i = 0; i < 150 && unit.center().y < BaseCamp::approach.y; ++i) unit.update(1.0/120,world);
            unit.move_on_plane({},false); for (int i = 0; i < 12; ++i) unit.update(1.0/120,world);
            require(!BaseCamp::inside(unit.center()) && unit.center().y > 9.5,"robot cannot leave habitat");
            for (int x : {6,7}) require(!world.solid(x,BaseCamp::south),"airlock is blocked");
        }},
        {"habitat walls block walking and jumping and remain protected",[] {
            auto world = WorldGrid::generate_isometric();
            Unit7Entity unit({3.15,5.075}); unit.move_on_plane({1,0},false);
            for (int i = 0; i < 80; ++i) unit.update(1.0/120,world);
            require(unit.position().x+0.7 <= BaseCamp::left+0.0001,"robot walked through habitat wall");
            unit.move_on_plane({1,0},true);
            for (int i = 0; i < 160; ++i) unit.update(1.0/120,world);
            require(unit.position().x+0.7 <= BaseCamp::left+0.0001,"robot jumped through habitat wall");
            require(!world.break_tile(BaseCamp::left,5),"habitat wall can be mined");
            require(!world.damage_tile(BaseCamp::left,5,1000),"habitat wall accepts mining damage");
            require(!world.place_tile(6,6) && !world.place_tile(6,8),"placement can obstruct habitat or airlock");
            require(world.get_tile_type(6,6) == TileType::VacuumAir && world.get_tile_type(6,8) == TileType::VacuumAir,"protected floor changed");
        }},
        {"WASD maps to screen-relative overhead movement",[] {
            Game north, east;
            const auto start = north.unit().center();
            InputState w; w.up = true;
            InputState d; d.right = true;
            for (int i = 0; i < 12; ++i) { north.advance(w,1.0/60); east.advance(d,1.0/60); }
            require(north.unit().center().x < start.x && north.unit().center().y < start.y,"W is not screen north");
            require(east.unit().center().x > start.x && east.unit().center().y < start.y,"D is not screen east");
            auto world = WorldGrid::generate_isometric();
            Unit7Entity axis({20.15,10.075}), diagonal({20.15,10.075});
            axis.move_on_plane({1,0},false); diagonal.move_on_plane({1,1},false);
            for (int i = 0; i < 20; ++i) { axis.update(1.0/120,world); diagonal.update(1.0/120,world); }
            close(std::hypot(axis.velocity().x,axis.velocity().y),std::hypot(diagonal.velocity().x,diagonal.velocity().y));
        }},
        {"ground traction stops promptly and reverses without a long skid",[] {
            auto world = WorldGrid::generate_isometric(); Unit7Entity unit({20.15,10.075});
            unit.move_on_plane({1,0},false);
            for (int i = 0; i < 30; ++i) unit.update(1.0/120,world);
            const auto before = unit.position();
            unit.move_on_plane({},false);
            for (int i = 0; i < 12; ++i) unit.update(1.0/120,world);
            require(distance(before,unit.position()) < 0.09,"robot coasts too far after release");
            close(unit.velocity().x,0,1e-8); close(unit.velocity().y,0,1e-8);
            unit.move_on_plane({1,0},false);
            for (int i = 0; i < 24; ++i) unit.update(1.0/120,world);
            unit.move_on_plane({-1,0},false);
            for (int i = 0; i < 18; ++i) unit.update(1.0/120,world);
            require(unit.velocity().x < -2,"robot keeps sliding forward on reversal");
        }},
        {"gentle downhill walking stays grounded without repeated jump sounds",[] {
            auto world = WorldGrid::generate_isometric();
            bool found = false;
            for (int y = 2; y < 39 && !found; ++y) for (int x = 2; x < 38 && !found; ++x) {
                const double drop = world.elevation(x,y)-world.elevation(x+1,y);
                if (drop < 0.01 || drop > 0.15 || world.solid(x,y) || world.solid(x+1,y) || world.solid(x+2,y)) continue;
                const double start = world.elevation(x,y);
                if (std::abs(world.elevation(x+2,y)-start) > 0.2) continue;
                Unit7Entity unit({x+0.15,y+0.075});
                unit.move_on_plane({1,0},false);
                for (int i = 0; i < 45; ++i) {
                    unit.update(1.0/120,world);
                    require(unit.grounded(),"small downhill step triggered a fall");
                    close(unit.vertical_velocity(),0,1e-8);
                }
                require(unit.center().x > x+1.2,"downhill fixture did not cross cell boundary");
                found = true;
            }
            require(found,"no gentle downhill fixture found");
        }},
        {"walking dust trails both tracks in every direction and stops without contact",[] {
            for (const Vec2 velocity : {Vec2{3,0},Vec2{-3,0},Vec2{0,3},Vec2{0,-3}}) {
                Presentation pose;
                pose.update(0.1,{10,10},velocity,true,true,true);
                int dust = 0; bool left = false, right = false;
                for (const auto& p : pose.particles()) if (p.dust) {
                    ++dust;
                    const Vec2 offset{p.position.x-10.35,p.position.y-10.425};
                    require(offset.x*velocity.x+offset.y*velocity.y < 0,"dust appears ahead of travel");
                    require(p.velocity.x*velocity.x+p.velocity.y*velocity.y < 0,"dust blows forwards");
                    const double cross = offset.x*velocity.y-offset.y*velocity.x;
                    left = left || cross < 0; right = right || cross > 0;
                }
                require(dust == 2 && left && right,"missing paired tread dust");
                for (int i = 0; i < 8; ++i) pose.update(0.25,{10,10},{},true,true,true);
                require(pose.particles().empty(),"dust persists after stopping");
                for (int i = 0; i < 8; ++i) pose.update(0.25,{10,10},velocity,false,true,true,-1);
                for (const auto& p : pose.particles()) require(!p.dust,"airborne treads emit walking dust");
                Presentation offline;
                for (int i = 0; i < 8; ++i) offline.update(0.25,{10,10},velocity,true,false,true);
                for (const auto& p : offline.particles()) require(!p.dust,"offline robot emits walking dust");
            }
        }},
        {"tread marks follow distance, persist at rest and skip airborne gaps",[] {
            Presentation a, b;
            a.update(1.0/60,{0,0},{2,0},true,true,true);
            b.update(1.0/120,{0,0},{2,0},true,true,true);
            for (int i = 1; i <= 60; ++i) a.update(1.0/60,{2.0*i/60,0},{2,0},true,true,true);
            for (int i = 1; i <= 120; ++i) b.update(1.0/120,{2.0*i/120,0},{2,0},true,true,true);
            require(a.tread_marks().size() == 14 && a.tread_marks().size() == b.tread_marks().size(),"track density depends on frame rate");
            for (std::size_t i = 0; i < a.tread_marks().size(); ++i) {
                close(a.tread_marks()[i].position.x,b.tread_marks()[i].position.x,1e-8);
                close(a.tread_marks()[i].position.y,0.425,1e-8);
            }
            const auto count = a.tread_marks().size(); const auto first = a.tread_marks().front().position;
            for (int i = 0; i < 60; ++i) a.update(1.0/60,{2,0},{},true,true,true);
            require(a.tread_marks().size() == count,"stationary robot produces tracks");
            close(a.tread_marks().front().position.x,first.x,1e-8);
            a.update(0.1,{3,0},{2,0},false,true,true);
            a.update(0.1,{4,0},{2,0},true,true,true);
            require(a.tread_marks().size() == count,"tracks bridged an airborne gap");
            a.update(0.1,{4.3,0},{2,0},true,true,true);
            require(a.tread_marks().size() == count+2,"tracks did not resume on landing");
            require(a.tread_marks().back().position.x > 4.5,"landing track was stretched across the jump");
        }},
        {"tread mark history has a fixed budget and expires",[] {
            Presentation effects;
            // Stress the presentation beyond normal driving speed to reach its
            // storage cap before the shorter trail lifetime expires.
            for (int i = 0; i < 5000; ++i) effects.update(1.0/240,{i/6.0,0},{40,0},true,true,true);
            require(effects.tread_marks().size() == Presentation::max_tread_marks,"track history is unbounded");
            for (int i = 0; i < int(Presentation::tread_lifetime*4)+2; ++i)
                effects.update(0.25,{4999/6.0,0},{},true,true,true);
            require(effects.tread_marks().empty(),"old tracks never expire");
        }},
        {"tread marks fade smoothly on the simulation clock and disappear at thirty seconds",[] {
            close(Presentation::tread_opacity(0),1,1e-8);
            close(Presentation::tread_opacity(5),1,1e-8);
            close(Presentation::tread_opacity(17.5),0.5,1e-8);
            require(Presentation::tread_opacity(29) < 0.01,"old tracks stay visibly dark");
            close(Presentation::tread_opacity(30),0,1e-8);
            close(Presentation::tread_opacity(31),0,1e-8);
            close(Presentation::tread_opacity(-1),0,1e-8);
            close(Presentation::tread_opacity(std::numeric_limits<double>::quiet_NaN()),0,1e-8);
            double previous = 1;
            for (int second = 0; second <= 30; ++second) {
                const double opacity = Presentation::tread_opacity(second);
                require(opacity >= 0 && opacity <= previous,"trail fade is not monotonic");
                previous = opacity;
            }
            Presentation effects;
            effects.update(0.1,{0,0},{2,0},true,true,true);
            effects.update(0.1,{0.2,0},{2,0},true,true,true);
            require(!effects.tread_marks().empty(),"missing trail fixture");
            const double born = effects.tread_marks().front().born;
            for (int i = 0; i < 100; ++i) effects.update(0,{0.2,0},{},true,true,true);
            close(effects.clock-born,0,1e-8);
            close(Presentation::tread_opacity(effects.clock-born),1,1e-8);
            for (int i = 0; i < 119; ++i) effects.update(0.25,{0.2,0},{},true,true,true);
            require(!effects.tread_marks().empty(),"trail disappeared before its lifetime");
            effects.update(0.25,{0.2,0},{},true,true,true);
            require(effects.tread_marks().empty(),"trail remains after its lifetime");
        }},
        {"jump respects tall walls and preserves raised ramp access",[] {
            auto world = WorldGrid::generate_isometric();
            world.place_tile(21,10,TileType::Bedrock);
            Unit7Entity jumper({20.15,10.075}); jumper.move_on_plane({1,0},true);
            for (int i = 0; i < 60; ++i) jumper.update(1.0/120,world);
            require(jumper.jump_height() > 1.2 && !jumper.grounded(),"jump did not rise");
            require(jumper.position().x+0.7 <= 21.0001,"jump passed through a tall wall");
            const double vz = jumper.vertical_velocity();
            jumper.move_on_plane({1,0},true); jumper.update(1.0/120,world);
            close(jumper.vertical_velocity(),vz-Unit7Entity::jump_gravity/120);
            Unit7Entity ramp({world.width()-13.85,14.075}); ramp.move_on_plane({1,0},false);
            for (int i = 0; i < 80; ++i) ramp.update(1.0/60,world);
            require(world.elevation(int(ramp.center().x),int(ramp.center().y)) >= 1.0,"could not walk up ramp");
            const int cliff_x = world.width()-9;
            world.break_tile(cliff_x,7); world.break_tile(cliff_x,8);
            Unit7Entity cliff({cliff_x+0.15,7.075}); cliff.move_on_plane({0,1},false);
            for (int i = 0; i < 80; ++i) cliff.update(1.0/60,world);
            require(cliff.center().y < 8,"walked through tall terrace edge");
        }},
        {"hard falls and fast wall impacts damage integrity while normal jumps stay safe",[] {
            auto world = WorldGrid::generate_isometric();
            for (int y = 9; y < 18; ++y) for (int x = 19; x <= 25; ++x) world.break_tile(x,y);
            Unit7Entity normal({20.15,10.075}); normal.update(1.0/60,world);
            normal.move_on_plane({},true);
            for (int i = 0; i < 110; ++i) normal.update(1.0/60,world);
            close(normal.integrity(),100);
            require(world.place_tile(20,10,TileType::Bedrock),"fall platform fixture failed");
            Unit7Entity drop({20.15,10.075}); drop.update(1.0/60,world);
            drop.move_on_plane({1,0},true);
            for (int i = 0; i < 125; ++i) drop.update(1.0/60,world);
            require(drop.integrity() < 99 && drop.alive(),"hard landing causes no structural damage");
            world.break_tile(20,10);
            require(world.place_tile(24,10,TileType::Bedrock),"wall fixture failed");
            Unit7Entity collision({20.15,10.075}); collision.move_on_plane({1,0},false);
            for (int i = 0; i < 90; ++i) collision.update(1.0/60,world);
            require(collision.integrity() < 100 && collision.integrity() > 90,"fast collision lacks bounded damage");
            const auto health = collision.integrity();
            for (int i = 0; i < 120; ++i) collision.update(1.0/60,world);
            close(collision.integrity(),health);
            collision.apply_damage(std::numeric_limits<double>::quiet_NaN()); close(collision.integrity(),health);
            collision.apply_damage(100); require(!collision.alive() && collision.integrity() == 0,"fatal damage did not shut actuators down");
        }},
        {"zero battery collapses the robot without changing completed mission state",[] {
            GameOptions options; options.initial_battery = 0.0001;
            Game game(WorldGrid::generate_isometric(),options); game.advance({},0.25);
            require(game.finished(),"empty battery fixture failed");
            const auto position = game.unit().center(); const auto seconds = game.simulated_seconds();
            const auto phase = game.phobos().phase();
            for (int i = 0; i < 20; ++i) game.advance({},0.1);
            require(game.presentation().collapse > 0.99 && game.presentation().head_bounce < -0.48,"robot does not visibly collapse");
            require(game.presentation().lean_forward > 0.60 && game.presentation().lean_side > 0.38 && game.unit().grounded(),
                    "collapsed chassis is not strongly slumped on ground");
            close(game.unit().integrity(),100); close(game.unit().battery_percent(),0);
            close(game.simulated_seconds(),seconds); close(game.phobos().phase(),phase);
            close(game.unit().center().x,position.x); close(game.unit().center().y,position.y);
            game.set_paused(true); const auto clock = game.presentation().clock;
            game.advance({},0.25); close(game.presentation().clock,clock);
        }},
        {"jump follows a ballistic arc without horizontal displacement",[] {
            auto world = WorldGrid::generate_isometric(); Unit7Entity unit({20.15,10.075});
            const auto start = unit.position(); unit.move_on_plane({},true);
            for (int i = 0; i < 30; ++i) unit.update(1.0/120,world);
            const double t = 0.25;
            close(unit.elevation(),Unit7Entity::jump_impulse*t-0.5*Unit7Entity::jump_gravity*t*t,0.00001);
            close(unit.vertical_velocity(),Unit7Entity::jump_impulse-Unit7Entity::jump_gravity*t,0.00001);
            close(unit.position().x,start.x); close(unit.position().y,start.y);
            double peak = unit.elevation();
            for (int i = 0; i < 140; ++i) { unit.update(1.0/120,world); peak = std::max(peak,unit.elevation()); }
            close(peak,Unit7Entity::jump_impulse*Unit7Entity::jump_impulse/(2*Unit7Entity::jump_gravity),0.001);
            require(unit.grounded(),"jump never landed"); close(unit.elevation(),0); close(unit.vertical_velocity(),0);
        }},
        {"airborne jump presses do not double-jump or queue a bounce",[] {
            auto world = WorldGrid::generate_isometric(); Unit7Entity unit({20.15,10.075});
            unit.move_on_plane({},true);
            for (int i = 0; i < 25; ++i) unit.update(1.0/120,world);
            const auto before = unit.vertical_velocity(); unit.move_on_plane({},true); unit.update(1.0/120,world);
            close(unit.vertical_velocity(),before-Unit7Entity::jump_gravity/120);
            for (int i = 0; i < 150; ++i) unit.update(1.0/120,world);
            require(unit.grounded() && unit.jump_height() == 0,"airborne press queued another jump");
        }},
        {"jump can land on a low block and gravity drops the robot after mining it",[] {
            auto world = WorldGrid::generate_isometric(); world.place_tile(21,10,TileType::Regolith);
            Unit7Entity unit({20.15,10.075}); unit.move_on_plane({1,0},true);
            int attempts = 0;
            while (unit.center().x < 21.3 && ++attempts < 200) unit.update(1.0/120,world);
            require(attempts < 200,"could not jump over the low block edge");
            unit.move_on_plane({},false);
            for (int i = 0; i < 160; ++i) unit.update(1.0/120,world);
            require(unit.grounded(),"did not land on block"); close(unit.elevation(),WorldGrid::obstacle_height(TileType::Regolith));
            require(world.break_tile(21,10).has_value(),"fixture block missing");
            unit.update(1.0/120,world); require(!unit.grounded() && unit.vertical_velocity() < 0,"removed support did not trigger gravity");
            for (int i = 0; i < 120; ++i) unit.update(1.0/120,world);
            require(unit.grounded(),"did not settle after losing support"); close(unit.elevation(),0);
        }},
        {"jump stays frame-rate independent and has a landing animation",[] {
            Game sixty, one_twenty; InputState jump; jump.jump = true;
            sixty.advance(jump,1.0/60); one_twenty.advance(jump,1.0/120);
            for (int i = 1; i < 30; ++i) sixty.advance({},1.0/60);
            for (int i = 1; i < 60; ++i) one_twenty.advance({},1.0/120);
            close(sixty.unit().elevation(),one_twenty.unit().elevation(),0.00001);
            close(sixty.unit().vertical_velocity(),one_twenty.unit().vertical_velocity(),0.00001);
            bool landed = false;
            for (int i = 0; i < 70; ++i) {
                sixty.advance({},1.0/60);
                if (sixty.unit().grounded()) { landed = true; require(sixty.presentation().landing > 0,"landing pose missing"); break; }
            }
            require(landed,"did not complete jump");
            require(sixty.presentation().body_bounce < -0.01,"landing does not compress suspension");
            bool rebound = false, head_lag = false;
            for (int i = 0; i < 120; ++i) {
                sixty.advance({},1.0/60);
                const auto& pose = sixty.presentation();
                rebound = rebound || pose.body_bounce > 0.001;
                head_lag = head_lag || std::abs(pose.head_bounce-pose.body_bounce) > 0.02;
                require(sixty.unit().grounded(),"visual bounce moved the collision body off the ground");
                require(std::abs(pose.body_bounce) < 0.2 && std::abs(pose.head_bounce) < 0.2,"suspension bounce is excessive");
            }
            require(rebound && head_lag,"landing lacks rebound or independent head motion");
            close(sixty.presentation().body_bounce,0,0.0001);
            close(sixty.presentation().head_bounce,0,0.0001);
        }},
        {"suspension scales with fall speed and settles consistently across frame rates",[] {
            Presentation gentle, hard, fine;
            gentle.update(1.0/60,{},{},false,true,true,-2);
            hard.update(1.0/60,{},{},false,true,true,-7);
            fine = hard;
            gentle.update(1.0/60,{},{},true,true,true,0);
            hard.update(1.0/60,{},{},true,true,true,0);
            fine.update(1.0/60,{},{},true,true,true,0);
            require(hard.body_bounce < gentle.body_bounce-0.015,"hard fall has no stronger compression");
            for (int i = 0; i < 60; ++i) {
                hard.update(1.0/60,{},{},true,true,true);
                fine.update(1.0/120,{},{},true,true,true);
                fine.update(1.0/120,{},{},true,true,true);
                close(hard.body_bounce,fine.body_bounce,0.00001);
                close(hard.head_bounce,fine.head_bounce,0.00001);
            }
            const auto body = hard.body_bounce, head = hard.head_bounce;
            hard.update(0,{},{},true,true,true);
            close(hard.body_bounce,body,1e-12); close(hard.head_bounce,head,1e-12);
            for (const auto& event : hard.sound_events())
                require(event.cue != SoundCue::Jump,"walking off a ledge emits a jump sound");
        }},
        {"isometric cursor picks the visible resource geometry",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            const GridPos ore{13,8};
            const auto screen = renderer.item_to_screen(ore,game.world());
            const auto picked = renderer.screen_to_tile(int(screen.x),int(screen.y));
            require(picked.x == ore.x && picked.y == ore.y,"cursor doesn't select raised ore face");
            require(renderer.screen_to_tile(-1,100).x == -1,"off-screen picking accepted");
            const auto center = renderer.tile_to_screen(game.unit().center(),game.world());
            const auto ground = renderer.screen_to_tile(int(center.x),int(center.y));
            require(game.world().in_bounds(ground.x,ground.y),"robot's floor is unpickable");
        }},
        {"overhead map rows do not create false radiation shielding",[] {
            auto world = WorldGrid::generate_isometric(); RadiationStorm storm; storm.start(); storm.update(6);
            const auto sample = storm.sample({20.5,10.5},world);
            close(sample.dose_rate_msv_hr,3500+8500*6.0/45);
        }},
        {"full isometric mission gathers, crafts, repairs and reaches the raised valve",[] {
            std::ostringstream output;
            if (!run_isometric_demo(output)) throw std::runtime_error(output.str());
            require(output.str().find("Earth Saved.") != std::string::npos,"isometric ending missing");
        }},
        {"mountain relief has stable peaks and graded hub routes",[] {
            close(PlanetTerrain::noise(2.5,4.1,7),PlanetTerrain::noise(2.5,4.1,7),0.000001);
            require(std::abs(PlanetTerrain::noise(2.5,4.1,7)-PlanetTerrain::noise(2.5,4.1,8)) > 0.001,"seed has no effect");
            auto world = WorldGrid::generate_isometric();
            close(world.elevation(10,10),0); close(world.elevation(20,10),0);
            double low = 10, high = -10;
            const int crater_x = int(world.width()*0.4), crater_y = int(world.height()*0.34);
            for (int y = crater_y-3; y <= crater_y+2; ++y) for (int x = crater_x-3; x <= crater_x+3; ++x) {
                low = std::min(low,world.elevation(x,y)); high = std::max(high,world.elevation(x,y));
            }
            require(high-low > 0.3,"planet surface is flat");
        }},
        {"mountains have continuous slopes and no generated holes",[] {
            for (unsigned seed : {7u,42u,137u}) {
                const auto world = WorldGrid::generate_isometric(72,64,seed);
                double highest = 0;
                for (int y = 1; y < 63; ++y) for (int x = 1; x < 71; ++x) {
                    require(!world.pit(x,y),"generated a hole");
                    require(world.elevation(x,y) > -2,"generated a deep depression");
                    highest = std::max(highest,world.elevation(x,y));
                    if (x > 19 && x < 48 && y > 20 && y < 55) {
                        const double average = (world.elevation(x-1,y)+world.elevation(x,y)+world.elevation(x-1,y-1)+world.elevation(x,y-1))/4;
                        close(PlanetTerrain::surface_height(world,{double(x),double(y)}),average,1e-8);
                    }
                }
                require(highest > 4,"mountains are flat");
                close(world.elevation(20,10),0);
            }
        }},
        {"crashed ship sits in a real basin with a walkable access channel",[] {
            const auto world = WorldGrid::generate_isometric();
            for (int y = BaseCamp::north; y <= BaseCamp::south; ++y)
                for (int x = BaseCamp::left; x <= BaseCamp::right; ++x)
                    close(world.elevation(x,y),BaseCamp::floor_elevation);
            require(world.elevation(2,2) > world.elevation(8,6)+0.5,"impact site has no raised rim around its depressed floor");
            close(world.elevation(10,10),0);
            double previous = world.elevation(7,8);
            for (int y = 9; y <= 11; ++y) {
                const double next = world.elevation(7,y);
                require(next >= previous && next-previous <= 0.31,"airlock route has an unwalkable crater step");
                previous = next;
            }
            close(previous,0);
        }},
        {"hover names stay in the bottom strip and clear when leaving an item",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            const auto before = renderer.pixels();
            const auto ore = renderer.item_to_screen({13,8},game.world());
            renderer.set_pointer(int(ore.x),int(ore.y)); renderer.render(game);
            require(renderer.inspection_text() == tile_definition(TileType::IronNickelOre).name,"world item name missing");
            require(std::equal(before.begin(),before.begin()+347*640,renderer.pixels().begin()),"hover name or popup obscures gameplay");
            require(before != renderer.pixels(),"bottom strip did not show the item name");
            renderer.set_pointer(-1,-1); renderer.render(game); require(renderer.inspection_text().empty(),"stale item name");
            renderer.set_pointer(205,15); renderer.render(game);
            require(renderer.inspection_text().find("ROBOT HEALTH") != std::string::npos,"robot health bar has no inspection label");
            renderer.toggle_inventory(); renderer.set_pointer(80,110); renderer.render(game);
            require(renderer.inspection_text() == item_name(Item::Regolith),"inventory item name not inspected");
        }},
        {"headlamp changes world lighting without changing picking or game state",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game); const auto on = renderer.pixels();
            const auto ore = renderer.item_to_screen({13,8},game.world());
            const auto pick = renderer.screen_to_tile(int(ore.x),int(ore.y));
            const auto clock = game.simulated_seconds(); const auto position = game.unit().center();
            renderer.toggle_headlamp(); renderer.render(game);
            int changed = 0;
            for (int y = 32; y < 316; ++y) for (int x = 0; x < 640; ++x)
                if (on[y*640+x] != renderer.pixels()[y*640+x]) ++changed;
            require(changed > 200,"headlamp has no visible light pool");
            const auto after = renderer.screen_to_tile(int(ore.x),int(ore.y));
            require(after.x == pick.x && after.y == pick.y,"lighting changed target picking");
            close(clock,game.simulated_seconds()); close(position.x,game.unit().center().x);
            renderer.render(game); require(!renderer.headlamp_on(),"headlamp toggled itself");
        }},
        {"headlamp rays respect tall blockers and light the full glass backdrop",[] {
            auto world = WorldGrid::generate_isometric(); Game clear(world);
            require(world.place_tile(10,12,TileType::Bedrock),"fixture lamp path is occupied"); Game blocked(std::move(world));
            std::vector<std::uint32_t> a(640*360,0xff606060), b = a;
            std::vector<GridPos> ids(640*360,GridPos{10,13});
            const LightingView view{-386,94,true};
            require(SceneLighting::apply(a,ids,clear,view) && SceneLighting::apply(b,ids,blocked,view),"lighting failed");
            require((a[170*640+320]&255) > (b[170*640+320]&255),"light passed through a tall blocker");
            require(a[0] != 0xff606060 && a[31*640] != 0xff606060,"terrain behind the floating header is not lit");
            require(a[350*640] != 0xff606060,"terrain behind the glass dock is not lit");
        }},
        {"eye spotlights follow jump height, head pitch and landing suspension",[] {
            Game game(WorldGrid::generate_isometric());
            const auto rest = RobotEyes::pose(game);
            close(distance(rest.emitters[0].position,rest.emitters[1].position),0.23,1e-8);
            close(rest.emitters[0].pitch,game.presentation().head_pitch,1e-8);
            InputState jump; jump.jump = true; game.advance(jump,1.0/60);
            for (int i = 0; i < 12; ++i) game.advance({},1.0/60);
            const auto ascent = RobotEyes::pose(game);
            require(ascent.emitters[0].elevation > rest.emitters[0].elevation+0.6,"eye light remains on the ground during ascent");
            require(ascent.pitch > rest.pitch+0.04,"head does not lift its gaze during takeoff");
            for (int i = 0; i < 36; ++i) game.advance({},1.0/60);
            const auto descent = RobotEyes::pose(game);
            require(descent.pitch < rest.pitch-0.035,"falling robot does not look toward its landing");
            require(descent.emitters[0].elevation > rest.emitters[0].elevation+0.4,"falling light detached from the head");
            bool landed = false;
            for (int i = 0; i < 30; ++i) {
                game.advance({},1.0/60);
                if (game.unit().grounded()) { landed = true; break; }
            }
            require(landed,"eye-light jump never landed");
            double min_pitch = 1, max_pitch = -1;
            for (int i = 0; i < 120; ++i) {
                game.advance({},1.0/60);
                const auto rig = RobotEyes::pose(game);
                min_pitch = std::min(min_pitch,rig.pitch); max_pitch = std::max(max_pitch,rig.pitch);
                require(std::isfinite(rig.emitters[0].elevation) && std::abs(rig.pitch) < 0.65,"landing light shakes excessively");
            }
            require(max_pitch-min_pitch > 0.04,"light has no landing recovery motion");
            close(RobotEyes::pose(game).pitch,rest.pitch,0.001);
            const auto clock = game.simulated_seconds(); const auto position = game.unit().center();
            PixelRenderer renderer; renderer.render(game); const auto frame = renderer.pixels();
            renderer.render(game); require(frame == renderer.pixels(),"rendering moves the light while paused");
            close(clock,game.simulated_seconds()); close(position.x,game.unit().center().x);
        }},
        {"eye lighting has directional falloff and shutters with blinking or shutdown",[] {
            EyeEmitter eye{{0,0},{0,1},1,-0.23,1};
            const double center = RobotEyes::irradiance(eye,{0,3},0);
            require(center > 0.1,"eye casts no usable light");
            require(RobotEyes::irradiance(eye,{0,-3},0) == 0,"eye casts behind its head");
            require(RobotEyes::irradiance(eye,{3,3},0) < center*0.1,"spotlight has no soft cone");
            require(RobotEyes::irradiance(eye,{0,8},0) < center*0.2,"beam has no distance attenuation");
            const double ground_pool = RobotEyes::irradiance(eye,{0,1},0);
            eye.elevation = 2.3;
            require(RobotEyes::irradiance(eye,{0,1},0) < ground_pool,"jump did not move the near light pool");
            Game blinking;
            for (int i = 0; i < 275; ++i) blinking.advance({},1.0/60);
            for (const auto& source : RobotEyes::pose(blinking).emitters)
                close(source.intensity,0,1e-8);
            GameOptions options; options.initial_battery = 0.0001;
            Game offline(WorldGrid::generate_isometric(),options);
            offline.advance({},0.25);
            for (const auto& source : RobotEyes::pose(offline).emitters)
                close(source.intensity,0,1e-8);
            Game game(WorldGrid::generate_isometric()); PixelRenderer on, off; off.toggle_headlamp(); on.render(game); off.render(game);
            int brightened = 0;
            for (int y = 180; y < 310; ++y) for (int x = 220; x < 410; ++x) {
                const auto a = on.pixels()[y*640+x], b = off.pixels()[y*640+x];
                if (int(a&255)-int(b&255) > int((a>>16)&255)-int((b>>16)&255)+4) ++brightened;
            }
            require(brightened > 100,"eyes do not illuminate the surroundings with blue light");
        }},
        {"sun shadows share direction, soften at the edge and detach with height",[] {
            ShadowCaster caster{{10,10},0,2,0.4,0.4,0,0.3};
            const double center = SceneShadows::opacity_at({10,10},0,caster);
            const double down_sun = SceneShadows::opacity_at({10.8,9.88},0,caster);
            const double up_sun = SceneShadows::opacity_at({9.2,10.12},0,caster);
            require(center > 0.4 && down_sun > up_sun+0.15,"shadow does not follow the sun");
            const double near_edge = SceneShadows::opacity_at({10.5,10.25},0,caster);
            const double far_edge = SceneShadows::opacity_at({10.5,10.5},0,caster);
            require(near_edge > far_edge && far_edge > 0,"shadow has no soft edge");
            caster.lift = 3;
            require(SceneShadows::opacity_at({10,10},0,caster) < center*0.2,"jumping shadow stayed attached to feet");
            require(SceneShadows::opacity_at({12,9.7},0,caster) > 0.1,"raised shadow did not move down-sun");
            require(SceneShadows::opacity_at({10,10},6,caster) == 0,"shadow projected above its caster");
            require(SceneShadows::opacity_at({std::numeric_limits<double>::quiet_NaN(),0},0,caster) == 0,"invalid shadow coordinate accepted");
        }},
        {"robot shadow follows tread orientation and fades away from contact",[] {
            ShadowCaster tread{{10,10},0,.22,.105,.35,.02,.25};
            tread.direction={0,0};
            const double along=SceneShadows::opacity_at({10,10.25},0,tread);
            const double across=SceneShadows::opacity_at({10.25,10},0,tread);
            require(along>across+.1,"tread shadow lost its narrow footprint");
            tread.heading=1.5707963267948966;
            close(SceneShadows::opacity_at({10.25,10},0,tread),along);
            const double grounded=SceneShadows::opacity_at({10,10},0,tread);
            tread.lift=2;
            require(SceneShadows::opacity_at({10,10},0,tread)<grounded,"airborne contact shadow did not soften");
            Game game;const auto casters=SceneShadows::robot_casters(game);
            require(casters.size()==4 && distance(casters[0].position,casters[1].position)>.4,"robot tracks do not cast separate contacts");
            require(casters[3].lift>casters[2].lift && casters[2].lift>casters[0].lift,"robot shadow parts lost their physical heights");
        }},
        {"ground shadow pass preserves picking, interface and opaque pixels",[] {
            Game game(WorldGrid::generate_isometric()); PlanetProjection projection; projection.curved = false;
            projection.anchor = game.unit().center();
            std::vector<std::uint32_t> pixels(640*360,0xffb8a890);
            std::vector<GridPos> ids(pixels.size(),{-1,-1});
            for (int y = 32; y < 316; ++y) for (int x = 0; x < 640; ++x) {
                const auto p = projection.unproject({x+0.5,y+0.5});
                if (p && game.world().in_bounds(int(std::floor(p->x)),int(std::floor(p->y))))
                    ids[y*640+x] = {int(std::floor(p->x)),int(std::floor(p->y))};
            }
            const auto before = ids;
            require(SceneShadows::apply(pixels,ids,game,projection),"ground shadow pass failed");
            std::size_t shaded = 0;
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                require(ids[i].x == before[i].x && ids[i].y == before[i].y,"shadow changed picking");
                require((pixels[i]>>24) == 255,"shadow made transparent pixels");
                if (pixels[i] != 0xffb8a890) ++shaded;
            }
            require(shaded > 100,"ground shadows are invisible");
            require(pixels[0] == 0xffb8a890 && pixels[350*640] == 0xffb8a890,"shadow shaded interface");
            pixels.pop_back(); require(!SceneShadows::apply(pixels,ids,game,projection),"invalid shadow buffer accepted");
        }},
        {"optical eyes close, reopen and double blink on simulation time",[] {
            Presentation pose;
            close(pose.eye_openness(),1);
            pose.clock = 4.53; const double closing = pose.eye_openness();
            require(closing > 0.1 && closing < 0.9,"eyelids snap shut without animation");
            pose.clock = 4.575; close(pose.eye_openness(),0);
            pose.clock = 4.65; const double opening = pose.eye_openness();
            require(opening > 0.1 && opening < 0.9,"eyelids snap open without animation");
            const double glow = pose.eye_glow();
            pose.update(0,{},{},true,true,true);
            close(pose.eye_openness(),opening); close(pose.eye_glow(),glow);
            pose.clock = 4.75; close(pose.eye_openness(),1);
            pose.clock = 10.175; close(pose.eye_openness(),0);
            pose.clock = 10.505; close(pose.eye_openness(),0);
            pose.clock = 11.2; close(pose.eye_openness(),1);
            for (int i = 0; i < 1200; ++i) {
                pose.clock = i/60.0;
                require(pose.eye_openness() >= 0 && pose.eye_openness() <= 1,"eyelid aperture is unbounded");
            }
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; renderer.render(game);
            const auto frame = renderer.pixels();
            int blue = 0;
            for (int y = 170; y < 230; ++y) for (int x = 280; x < 345; ++x) {
                const auto pixel = frame[y*640+x];
                if ((pixel&255) > 220 && ((pixel>>16)&255) < 160 && ((pixel>>8)&255) > 120) ++blue;
            }
            require(blue >= 4,"robot has no emissive blue eyes");
            renderer.render(game); require(frame == renderer.pixels(),"rendering advances the eye animation");
        }},
        {"walking suspension stays stable, settles at rest and yields to jumps",[] {
            Presentation pose;
            double low = 1, high = -1; bool head_lag = false;
            for (int i = 0; i < 180; ++i) {
                pose.update(1.0/60,{},{3,0},true,true,true);
                if (i > 60) { low = std::min(low,pose.body_bounce); high = std::max(high,pose.body_bounce); }
                head_lag = head_lag || std::abs(pose.head_bounce-pose.body_bounce) > 0.001;
                require(std::abs(pose.body_bounce) < 0.10 && std::abs(pose.head_bounce) < 0.10,"walking bounce is excessive");
            }
            require(high-low > 0.004 && high-low < 0.025 && head_lag,"walking suspension is unstable or rigid");
            const auto body = pose.body_bounce, head = pose.head_bounce;
            pose.update(0,{},{3,0},true,true,true);
            close(body,pose.body_bounce,1e-12); close(head,pose.head_bounce,1e-12);
            Presentation air = pose, stationary_air = pose;
            for (int i = 0; i < 20; ++i) {
                air.update(1.0/60,{},{3,0},false,true,true,-2);
                stationary_air.update(1.0/60,{},{},false,true,true,-2);
                close(air.body_bounce,stationary_air.body_bounce,1e-10);
                close(air.head_bounce,stationary_air.head_bounce,1e-10);
            }
            for (int i = 0; i < 120; ++i) pose.update(1.0/60,{},{},true,true,true);
            close(pose.body_bounce,0,0.0001); close(pose.head_bounce,0,0.0001);
        }},
        {"battery below forty produces a limp and charging restores the normal gait",[] {
            Presentation normal, low, boundary;
            for (int i = 0; i < 180; ++i) {
                normal.update(1.0/60,{},{3,0},true,true,true,0,100,100);
                low.update(1.0/60,{},{3,0},true,true,true,0,39,100);
                boundary.update(1.0/60,{},{3,0},true,true,true,0,40,100);
            }
            require(low.damage_level > 0.4 && low.lean_side > 0.03,"low battery has no damaged lean");
            require(low.head_pitch < normal.head_pitch-0.04,"low power head does not droop");
            require(low.left_tread_phase < normal.left_tread_phase-0.5,"damaged tread does not hitch");
            close(boundary.damage_level,0); close(normal.damage_level,0);
            for (int i = 0; i < 180; ++i) low.update(1.0/60,{},{3,0},true,true,true,0,100,100);
            require(low.damage_level < 0.0001 && std::abs(low.lean_side) < 0.001,"gait does not recover after recharging");
        }},
        {"robot steering uses smooth body motion and differential tracks",[] {
            Presentation pose; pose.update(1.0/60,{},{2,0},true,true,true);
            require(pose.heading > 0 && pose.heading < 1.5707963268,"chassis snapped instead of turning");
            require(pose.left_tread_phase != pose.right_tread_phase,"tracks have no turning differential");
            require(std::abs(pose.lean_forward) > 0,"acceleration has no body response");
            for (int i = 0; i < 120; ++i) pose.update(1.0/60,{},{2,0},true,true,true);
            require(std::abs(pose.heading) < 0.02,"chassis never settled to movement direction");
            require(std::abs(pose.lean_forward) <= 0.035 && std::abs(pose.lean_side) <= 0.025,"chassis sway exceeds stable suspension limits");
            pose.face_target({0,0},{0,1}); pose.update(1.0/60,{},{2,0},true,true,true);
            require(pose.head_heading > pose.heading+0.01,"head did not look toward the mining target");
        }},
        {"expedition projection preserves building size and reverses at every elevation",[] {
            PlanetProjection view; view.curved = false;
            const Vec2 station{8.5,10.5};
            const auto width = [&] { return distance(view.project(station,0).screen,view.project({station.x+1,station.y},0).screen); };
            const double original = width();
            for (Vec2 anchor : {station,Vec2{15,17},Vec2{30,12},Vec2{32,32}}) {
                view.anchor = anchor; view.anchor_altitude = 2.5;
                close(width(),original,1e-8);
                for (double altitude : {0.0,1.0,3.5}) {
                    const auto screen = view.project(station,altitude);
                    require(screen.visible,"expedition building vanished behind horizon");
                    const auto point = view.unproject(screen.screen,altitude);
                    require(bool(point),"expedition inverse failed");
                    close(point->x,station.x); close(point->y,station.y);
                }
            }
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer;
            require(!renderer.curved_camera(),"stable camera is not the default");
            renderer.toggle_camera(); renderer.render(game);
            require(renderer.curved_camera(),"globe camera cannot be selected");
        }},
        {"sound synthesis is audible, bounded and decays to silence",[] {
            SoundEffects synth;
            std::vector<std::int16_t> samples(SoundEffects::sample_rate*2);
            synth.render(samples.data(),samples.size()/2);
            for (auto sample : samples) require(sample == 0,"idle synthesizer is noisy");
            synth.play(SoundCue::Jump); synth.render(samples.data(),samples.size()/2);
            auto jump = samples;
            require(*std::max_element(samples.begin(),samples.end()) > 1000,"jump sound is silent");
            synth.reset(); synth.play(SoundCue::Craft); synth.render(samples.data(),samples.size()/2);
            require(jump != samples,"actions have identical sounds");
            synth.render(samples.data(),samples.size()/2);
            for (auto sample : samples) require(sample == 0,"one-shot sound did not finish");
            for (int i = 0; i < 100; ++i) synth.play(SoundCue::Break);
            synth.ambience(1,1,1,1,1); synth.render(samples.data(),samples.size()/2);
            for (auto sample : samples) require(std::abs(int(sample)) <= 26000,"audio overflow");
            synth.ambience(0,0,0,0,0);
            synth.render(samples.data(),samples.size()/2); synth.render(samples.data(),samples.size()/2);
            for (auto sample : samples) require(sample == 0,"ambience did not release");
        }},
        {"walking mechanics sound follows grounded travel and stops at rest",[] {
            Presentation pose;
            for (int i = 0; i < 60; ++i) pose.update(1.0/60,{},{},true,true,true);
            require(pose.sound_events().empty(),"idle robot emits walking machinery");
            for (int i = 0; i < 60; ++i) pose.update(1.0/60,{},{3,0},true,true,true);
            int steps = 0;
            for (const auto& event : pose.sound_events()) if (event.cue == SoundCue::ServoStep) ++steps;
            require(steps == 5,"gear clicks do not follow distance traveled");
            const auto serial = pose.sound_events().back().serial;
            for (int i = 0; i < 30; ++i) pose.update(1.0/60,{},{3,0},false,true,true,2);
            for (const auto& event : pose.sound_events())
                require(event.serial <= serial || event.cue != SoundCue::ServoStep,"walking mechanics continue while airborne");
            pose.update(1.0/60,{},{},true,true,true);
            const auto stopped = pose.sound_events().back().serial;
            for (int i = 0; i < 60; ++i) pose.update(1.0/60,{},{},true,true,true);
            require(pose.sound_events().back().serial == stopped,"stopped robot keeps clicking");
        }},
        {"robot chatter varies with spaced timing and pauses with simulation",[] {
            Presentation pose; std::set<SoundCue> variants; std::uint64_t serial = 0;
            double previous = -1; int count = 0;
            for (int i = 0; i < 120*60; ++i) {
                pose.update(1.0/60,{},{},true,true,true);
                for (const auto& event : pose.sound_events()) if (event.serial > serial) {
                    serial = event.serial;
                    if (!is_robot_talk(event.cue)) continue;
                    variants.insert(event.cue); ++count;
                    if (previous >= 0) require(pose.clock-previous >= 8.99 && pose.clock-previous < 21.1,"chatter overlaps or loses its timer");
                    previous = pose.clock;
                }
            }
            require(count >= 6 && count <= 13 && variants.size() >= 2,"chatter is missing or repetitive");
            const auto clock = pose.clock;
            for (int i = 0; i < 100; ++i) pose.update(0,{},{},true,true,true);
            close(pose.clock,clock); require(pose.sound_events().back().serial == serial,"paused simulation emits chatter");
            for (int i = 0; i < 30*60; ++i) pose.update(1.0/60,{},{},true,false,true);
            for (const auto& event : pose.sound_events())
                require(event.serial <= serial || !is_robot_talk(event.cue),"offline robot keeps talking");
            Presentation fresh;
            for (int i = 0; i < 8*60; ++i) fresh.update(1.0/60,{},{},true,true,true);
            require(fresh.sound_events().empty(),"new mission starts with immediate chatter");
        }},
        {"mechanical and robot chatter synthesis is distinct, bounded and finite",[] {
            SoundEffects synth; std::vector<std::int16_t> previous;
            std::vector<std::int16_t> samples(SoundEffects::sample_rate*4);
            for (SoundCue cue : {SoundCue::ServoStep,SoundCue::RobotTalk,SoundCue::RobotTalkCurious,
                                SoundCue::RobotTalkHappy,SoundCue::RobotTalkThinking}) {
                synth.reset(); synth.play(cue); synth.render(samples.data(),samples.size()/2);
                int peak = 0;
                for (auto sample : samples) peak = std::max(peak,std::abs(int(sample)));
                require(peak > 700 && peak <= 26000,"robot audio is silent or clipped");
                require(samples != previous,"robot sounds have identical synthesis");
                previous = samples;
                synth.render(samples.data(),samples.size()/2);
                for (auto sample : samples) require(sample == 0,"robot utterance did not finish");
            }
            synth.reset(); synth.ambience(0.25,0,0,0,0); synth.render(samples.data(),samples.size()/2);
            const auto slow = samples;
            synth.reset(); synth.ambience(0.85,0,0,0,0); synth.render(samples.data(),samples.size()/2);
            require(samples != slow,"walking motor does not respond to speed");
        }},
        {"gameplay sounds follow successful actions and stationary jumps",[] {
            Game game(WorldGrid::generate_isometric());
            require(game.presentation().sound_events().empty(),"unexpected initial sound");
            InputState jump; jump.jump = true; game.advance(jump,1.0/60);
            require(!game.presentation().sound_events().empty() && game.presentation().sound_events().back().cue == SoundCue::Jump,"stationary jump sound missing");
            for (int i = 0; i < 90; ++i) game.advance({},1.0/60);
            require(game.presentation().sound_events().back().cue == SoundCue::Land,"landing sound missing");
            require(!game.perform(Action::Smelt).ok,"invalid crafting fixture");
            require(game.presentation().sound_events().back().cue == SoundCue::Error,"failed crafting played success");
            Presentation effects;
            effects.mining({2,2},0.5,false,0xffffff);
            require(effects.sound_events().empty(),"mining hit played break sound");
            effects.mining({2,2},1,true,0xffffff);
            require(effects.sound_events().back().cue == SoundCue::Break,"mining break sound missing");
            for (int i = 0; i < 100; ++i) effects.activity({0,0},"craft",true);
            require(effects.sound_events().size() == 32 && effects.sound_events().back().serial == 101,"sound event history is unbounded");
        }},
        {"planet projection is curved, reversible and hides the far hemisphere",[] {
            PlanetProjection planet;
            auto center = planet.project(planet.anchor);
            close(center.screen.x,320); close(center.screen.y,200); require(center.visible,"robot's surface is hidden");
            for (Vec2 p : {planet.anchor,Vec2{12.5,9.5},Vec2{8.5,11.5},Vec2{13.5,12.5}}) {
                for (double altitude : {0.0,0.6,1.4}) {
                    const auto projected = planet.project(p,altitude);
                    const auto restored = planet.unproject(projected.screen,altitude);
                    require(projected.visible && restored.has_value(),"nearby surface isn't projectable");
                    close(p.x,restored->x,0.000001); close(p.y,restored->y,0.000001);
                }
            }
            require(PlanetProjection::horizon_y(80) > PlanetProjection::horizon_y(320)+50,"planet horizon is flat");
            require(!planet.project({planet.anchor.x+22,planet.anchor.y-22}).visible,"far hemisphere is visible");
            require(!planet.unproject({0,40}),"space was treated as planet ground");
            require(planet.project(planet.anchor,1).screen.y < center.screen.y-10,"jump height doesn't lift from the round surface");
        }},
        {"curved terrain picking drives the mining tool and keeps space unpickable",[] {
            Game game(WorldGrid::generate_isometric()); PixelRenderer renderer; TargetTool tool;
            renderer.toggle_camera();
            InputState approach; approach.right = true; approach.down = true;
            for (int i = 0; i < 20; ++i) game.advance(approach,1.0/60);
            for (int i = 0; i < 10; ++i) game.advance({},1.0/60);
            renderer.render(game);
            const GridPos ore{13,12}; const auto p = renderer.item_to_screen(ore,game.world());
            require(game.unit().near({ore.x+0.5,ore.y+0.5},3.1),"fixture ore is out of mining reach");
            const auto selected = renderer.screen_to_tile(int(p.x),int(p.y));
            require(selected.x == ore.x && selected.y == ore.y,"curved ore doesn't match the cursor");
            require(renderer.screen_to_tile(0,40).x == -1,"cursor selected ground in space");
            tool.press(game,selected);
            for (int i = 0; i < 130; ++i) {
                InputState input; input.mining_target = tool.target(selected); game.advance(input,1.0/60);
            }
            require(game.unit().inventory().count(Item::IronNickelOre) == 1000,"J failed on curved terrain");
            require(game.world().get_tile_type(ore.x,ore.y) == TileType::VacuumAir,"curved ore did not break");
            const auto before = renderer.tile_to_screen(game.refinery().position(),game.world());
            InputState movement; movement.right = true;
            for (int i = 0; i < 20; ++i) game.advance(movement,1.0/60);
            renderer.render(game); const auto after = renderer.tile_to_screen(game.refinery().position(),game.world());
            require(distance(before,after) > 1,"surface didn't roll with camera movement");
        }},
        {"task history survives consumed resources and tracks the full mission",[] {
            std::ostringstream output;
            bool saw_crafting = false, saw_complete = false;
            require(run_mission_demo(output,[&](const Game& game, const char* stage) {
                if (std::string(stage) == "crafting" && game.unit().inventory().count(Item::StructuralPlates) >= 6) {
                    const auto tasks = game.tasks();
                    require(tasks[0].complete && tasks[1].complete,"spent resources reset gathering task");
                    saw_crafting = true;
                }
                if (game.climax().state() == ClimaxState::Complete) {
                    for (const auto& task : game.tasks()) require(task.complete && !task.active,"completed mission has unfinished tasks");
                    saw_complete = true;
                }
            }),"observed mission failed");
            require(saw_crafting && saw_complete,"missing preview stages");
        }},
        {"full mission: mine, refine, upgrade, repair, climb and launch",[] {
            std::ostringstream output;
            if (!run_mission_demo(output)) throw std::runtime_error(output.str());
            require(output.str().find("Earth Saved.") != std::string::npos,"mission ending missing");
        }}
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size()-failed << '/' << tests.size() << " tests passed\n";
    return failed ? 1 : 0;
}

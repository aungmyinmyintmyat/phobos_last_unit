#include "horizon/Preview.hpp"
#include "horizon/Renderer.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <queue>

namespace horizon {
bool export_ship_preview(const std::string& directory) {
    namespace fs = std::filesystem;
    fs::create_directories(directory);
    Game game;
    // Reach the sealed campaign airlock, then use its normal interaction.
    for(const Vec2 target : {Vec2{7,10.5},Vec2{7,9.4}}) {
        int tick=0;
        while(!game.in_spaceship() && distance(game.unit().center(),target)>.12 && ++tick<900) {
            const auto p=game.unit().center();
            const int dx=target.x-p.x>.05 ? 1 : target.x-p.x<-.05 ? -1 : 0;
            const int dy=target.y-p.y>.05 ? 1 : target.y-p.y<-.05 ? -1 : 0;
            InputState input;
            input.right=dx-dy>0; input.left=dx-dy<0;
            input.down=dx+dy>0; input.up=dx+dy<0;
            game.advance(input,1.0/60);
        }
        if(tick>=900) { std::cerr<<"Ship preview could not reach the airlock.\n"; return false; }
    }
    if(game.interaction_label()!="ENTER SHIP" || !game.interact().ok) return false;
    for(int i=0;i<90;++i) game.advance({},1.0/60);
    if(!game.in_spaceship() || game.transitioning()) return false;
    PixelRenderer renderer;
    const auto still=[&](const char* name) {
        renderer.render(game);return renderer.write_ppm((fs::path(directory)/(std::string(name)+".ppm")).string());
    };
    const auto walk=[&](GridPos target) {
        const auto& world=game.world();const int width=world.width();const auto p=game.unit().center();
        const int start=int(p.y)*width+int(p.x),end=target.y*width+target.x;
        std::vector<int> prev(width*world.height(),-1);std::queue<int> q;q.push(start);prev[start]=start;
        while(!q.empty() && prev[end]<0) {
            const int at=q.front();q.pop();
            for(GridPos d:{GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}) {
                const int x=at%width+d.x,y=at/width+d.y,next=y*width+x;
                if(!world.in_bounds(x,y) || (world.solid(x,y) && !ShipInterior::monitor_door.contains(x,y) && !ShipInterior::cargo_door.contains(x,y) && !ShipInterior::storage_door.contains(x,y)) || prev[next]>=0) continue;
                prev[next]=at;q.push(next);
            }
        }
        if(prev[end]<0) return false;
        std::vector<int> path;for(int at=end;at!=start;at=prev[at]) path.push_back(at);
        std::reverse(path.begin(),path.end());
        for(int at:path) {
            const Vec2 target{at%width+.5,at/width+.5};int tick=0;
            while(distance(game.unit().center(),target)>.13 && ++tick<500) {
                const auto p=game.unit().center();
                const int dx=target.x-p.x>.06 ? 1 : target.x-p.x<-.06 ? -1 : 0;
                const int dy=target.y-p.y>.06 ? 1 : target.y-p.y<-.06 ? -1 : 0;
                InputState input;input.right=dx-dy>0;input.left=dx-dy<0;input.down=dx+dy>0;input.up=dx+dy<0;
                game.advance(input,1.0/60);
            }
            if(tick>=500 || !game.unit().alive()) return false;
        }
        for(int i=0;i<12;++i) game.advance({},1.0/60);
        return true;
    };
    if(!still("ship-airlock")) return false;
    if(!walk({16,10}) || !still("ship-blocked-forward-room")) return false;
    if(!walk({9,15}) || !game.storage_door_open()) return false;
    for(int i=0;i<30;++i) game.advance({},1.0/60);
    if(!walk({19,47}) || !still("ship-engine") || !walk({16,36}) || !still("monitor-door-closed")) return false;
    std::ofstream monitor_manifest(fs::path(directory)/"monitor-door-frames.tsv");
    for(int frame=0;frame<36;++frame) {
        renderer.render(game);
        std::ostringstream name;name<<"monitor-door_"<<std::setw(3)<<std::setfill('0')<<frame<<".ppm";
        if(!renderer.write_ppm((fs::path(directory)/name.str()).string())) return false;
        monitor_manifest<<name.str()<<'\n';
        for(int tick=0;tick<5;++tick) {
            InputState input;input.right=input.down=frame>=6 && frame<30;game.advance(input,1.0/60);
        }
    }
    if(game.unit().center().x<=20.4 || game.ship_door_open(ShipDoor::Monitor) || !still("monitor-door-entered")) return false;
    if(!walk({22,37}) || !still("ship-monitor") || !walk({24,36}) || !still("ship-pc-setup") || !still("ship-monitor-tvs")) return false;
    std::ofstream tv_manifest(fs::path(directory)/"monitor-tvs-frames.tsv");
    for(int frame=0;frame<36;++frame) {
        renderer.render(game);
        std::ostringstream name;name<<"monitor-tvs_"<<std::setw(3)<<std::setfill('0')<<frame<<".ppm";
        if(!renderer.write_ppm((fs::path(directory)/name.str()).string())) return false;
        tv_manifest<<name.str()<<'\n';
        for(int tick=0;tick<5;++tick) game.advance({},1.0/60);
    }
    if(!walk({21,21}) || !still("cargo-door-closed")) return false;
    std::ofstream door_manifest(fs::path(directory)/"cargo-door-frames.tsv");
    for(int frame=0;frame<36;++frame) {
        renderer.render(game);
        std::ostringstream name;name<<"cargo-door_"<<std::setw(3)<<std::setfill('0')<<frame<<".ppm";
        if(!renderer.write_ppm((fs::path(directory)/name.str()).string())) return false;
        door_manifest<<name.str()<<'\n';
        for(int tick=0;tick<5;++tick) {
            InputState input;input.right=input.down=frame>=6 && frame<30;game.advance(input,1.0/60);
        }
    }
    if(game.unit().center().x<=25.4 || !walk({27,17}) || game.cargo_door_open() || !still("cargo-door-entered")) return false;
    if(!walk({30,20}) || !still("ship-cargo")) return false;
    std::ofstream craft_manifest(fs::path(directory)/"cargo-rover-frames.tsv");
    for(int frame=0;frame<36;++frame) {
        renderer.render(game);
        std::ostringstream name;name<<"cargo-rover_"<<std::setw(3)<<std::setfill('0')<<frame<<".ppm";
        if(!renderer.write_ppm((fs::path(directory)/name.str()).string())) return false;
        craft_manifest<<name.str()<<'\n';
        for(int tick=0;tick<5;++tick) game.advance({},1.0/60);
    }
    if(!walk({25,21})) return false;
    for(int i=0;i<20;++i) game.advance({},1.0/60);
    if(!walk({23,21})) return false;
    for(const auto& shot:std::array<std::pair<GridPos,const char*>,2>{{{{6,20},"ship-storage"},{{19,17},"ship-crew"}}})
        if(!walk(shot.first) || !still(shot.second)) return false;
    if(!walk({19,17})) return false;
    std::ofstream manifest(fs::path(directory)/"ship-frames.tsv");
    for(int frame=0;frame<48;++frame) {
        renderer.render(game);
        std::ostringstream name; name<<"ship_"<<std::setw(3)<<std::setfill('0')<<frame<<".ppm";
        if(!renderer.write_ppm((fs::path(directory)/name.str()).string())) return false;
        manifest<<name.str()<<'\n';
        if(frame==0 && !renderer.write_ppm((fs::path(directory)/"ship-interior.ppm").string())) return false;
        for(int tick=0;tick<5;++tick) {
            InputState input;input.left=input.up=frame<24;input.jump=frame==12 && tick==0;
            game.advance(input,1.0/60);
        }
    }
    return bool(manifest) && bool(door_manifest) && bool(monitor_manifest) && bool(tv_manifest) && bool(craft_manifest);
}
bool export_preview(const std::string& directory) {
    namespace fs = std::filesystem;
    fs::create_directories(directory);
    std::ofstream manifest(fs::path(directory)/"frames.tsv");
    if (!manifest) return false;
    PixelRenderer renderer;
    std::map<std::string,int> ticks, frames;
    const std::map<std::string,int> limits{{"overview",1},{"jump",28},{"movement",24},{"mining",72},
        {"crafting",72},{"tasks",18},{"climb",50},{"launch",105}};
    bool writes_ok = true;
    const bool mission_ok = run_isometric_demo(std::cout,[&](const Game& game, const char* name) {
        const std::string stage = name;
        const int tick = ticks[stage]++;
        const bool final = game.climax().state() == ClimaxState::Complete;
        if ((!final && tick%3 != 0) || frames[stage] >= limits.at(stage)) return;
        if (stage == "mining" && !game.presentation().mining_target) return;
        renderer.render(game);
        if (stage == "overview") {
            if (!renderer.write_ppm((fs::path(directory)/"camp-exterior.ppm").string())) writes_ok = false;
            for (const auto model : {RobotModel::Mark1,RobotModel::Mark2}) {
                PixelRenderer comparison = renderer;
                comparison.set_robot_model(model); comparison.render(game);
                const auto filename = model == RobotModel::Mark1 ? "robot-mark1.ppm" : "robot-mark2.ppm";
                if (!comparison.write_ppm((fs::path(directory)/filename).string())) writes_ok = false;
            }
            PixelRenderer inspected = renderer;
            const auto ore = renderer.item_to_screen({13,8},game.world());
            inspected.set_pointer(int(ore.x),int(ore.y)); inspected.render(game);
            if (!inspected.write_ppm((fs::path(directory)/"inspection.ppm").string())) writes_ok = false;
            PixelRenderer unlit = renderer; unlit.toggle_headlamp(); unlit.render(game);
            if (!unlit.write_ppm((fs::path(directory)/"lamp-off.ppm").string())) writes_ok = false;
        }
        if (stage == "overview" || (stage == "crafting" && frames[stage] == 15) ||
            (stage == "tasks" && frames[stage] == 15)) {
            PixelRenderer detail = renderer;
            std::string detail_name;
            if (stage == "overview") { detail.toggle_help(); detail_name = "guide"; }
            if (stage == "crafting") { detail.toggle_crafting(game); detail_name = "craft-menu"; }
            if (stage == "tasks") { detail.toggle_tasks(); detail_name = "task-panel"; }
            detail.render(game);
            if (!detail.write_ppm((fs::path(directory)/(detail_name+".ppm")).string())) writes_ok = false;
            if (stage == "crafting") {
                detail.toggle_inventory(); detail.render(game);
                if (!detail.write_ppm((fs::path(directory)/"materials.ppm").string())) writes_ok = false;
            }
        }
        std::ostringstream filename;
        filename << stage << '_' << std::setw(3) << std::setfill('0') << frames[stage]++ << ".ppm";
        const auto path = fs::path(directory)/filename.str();
        if (!renderer.write_ppm(path.string())) writes_ok = false;
        manifest << stage << '\t' << filename.str() << '\t' << game.simulated_seconds() << '\n';
    });
    // A separate, ordinary drive/stop sequence isolates trail ageing from the
    // mission montage. Resting Unit-01 keeps the same camera framing throughout.
    Game trail_game;
    InputState driving; driving.right = driving.down = true;
    for (int i = 0; i < 60; ++i) trail_game.advance(driving,1.0/60);
    for (int i = 0; i < 60; ++i) trail_game.advance({},1.0/60);
    PixelRenderer trail_renderer;
    trail_renderer.toggle_headlamp(); // Idle head scanning must not change light over the marks.
    for (const char* name : {"tread-fresh","tread-fading","tread-gone"}) {
        trail_renderer.render(trail_game);
        if (!trail_renderer.write_ppm((fs::path(directory)/(std::string(name)+".ppm")).string())) writes_ok = false;
        if (std::string(name) != "tread-gone")
            for (int i = 0; i < 15*60; ++i) trail_game.advance({},1.0/60);
    }
    // Enter the rescue aircraft using ordinary movement, then approach its only
    // crafting rig inside; no exterior refinery remains in the planar mission.
    Game camp_game; PixelRenderer camp_renderer;
    std::ofstream camp_manifest(fs::path(directory)/"camp-frames.tsv");
    int camp_tick = 0, camp_frame = 0;
    const auto camp_step = [&](InputState input) {
        camp_game.advance(input,1.0/60);
        if (camp_tick++%5 != 0) return;
        camp_renderer.render(camp_game);
        std::ostringstream name; name << "camp-walk_" << std::setw(3) << std::setfill('0') << camp_frame++ << ".ppm";
        if (!camp_renderer.write_ppm((fs::path(directory)/name.str()).string())) writes_ok = false;
        camp_manifest << name.str() << '\n';
    };
    const auto camp_walk = [&](Vec2 target) {
        int attempts = 0;
        while (distance(camp_game.unit().center(),target) > 0.12 && ++attempts < 500) {
            const auto point = camp_game.unit().center();
            const int x = target.x-point.x > 0.05 ? 1 : target.x-point.x < -0.05 ? -1 : 0;
            const int y = target.y-point.y > 0.05 ? 1 : target.y-point.y < -0.05 ? -1 : 0;
            InputState input; input.right = x-y > 0; input.left = x-y < 0; input.down = x+y > 0; input.up = x+y < 0;
            camp_step(input);
        }
        for (int i = 0; i < 18; ++i) camp_step({});
        return attempts < 500;
    };
    bool entered = true;
    for (Vec2 target : {Vec2{10.5,12.3},Vec2{7,12.3},BaseCamp::approach,Vec2{7,6.5},Vec2{6.5,5.5}})
        if (!camp_walk(target)) { entered = false; break; }
    if (!entered || !camp_game.inside_base()) { std::cerr << "Habitat preview could not reach the interior.\n"; writes_ok = false; }
    camp_renderer.render(camp_game);
    if (!camp_renderer.write_ppm((fs::path(directory)/"camp-interior.ppm").string())) writes_ok = false;
    // A stationary cutaway isolates the survivor's breathing, tapping hand and
    // remote-control signal from camera motion for visual regression checks.
    std::ofstream astronaut_manifest(fs::path(directory)/"astronaut-frames.tsv");
    for (int frame = 0; frame < 30; ++frame) {
        for (int tick = 0; tick < 3; ++tick) camp_game.advance({},1.0/60);
        camp_renderer.render(camp_game);
        std::ostringstream name; name << "astronaut_" << std::setw(3) << std::setfill('0') << frame << ".ppm";
        if (!camp_renderer.write_ppm((fs::path(directory)/name.str()).string())) writes_ok = false;
        astronaut_manifest << name.str() << '\n';
    }
    camp_renderer.toggle_crafting(camp_game);
    if (!camp_renderer.modal_open()) { std::cerr << "Indoor workbench preview could not open crafting.\n"; writes_ok = false; }
    camp_renderer.render(camp_game);
    if (!camp_renderer.write_ppm((fs::path(directory)/"camp-workbench.ppm").string())) writes_ok = false;

    // Comparable noon and midnight states use the same terrain and real clock
    // rate. Only the initial local solar phase differs between these missions.
    const auto wait = [](Game& game,double seconds) {
        const int ticks = int(std::ceil(seconds*60));
        for (int i = 0; i < ticks; ++i) game.advance({},1.0/60);
    };
    const auto still = [&](const char* name,const Game& game) {
        PixelRenderer shot; shot.render(game);
        if (!shot.write_ppm((fs::path(directory)/(std::string(name)+".ppm")).string())) writes_ok = false;
    };
    for (const auto& scenario : {std::pair<const char*,double>{"daylight",0.5},{"night",0.0}}) {
        GameOptions options; options.danger_seed = 47; options.starting_solar_phase = scenario.second;
        Game lighting_game(WorldGrid::generate_isometric(),options);
        wait(lighting_game,0.5); still(scenario.first,lighting_game);
    }

    // Matched night shots and a real jump make lens falloff, terrain detail and
    // moving head-mounted illumination reviewable without changing live state.
    GameOptions optics_options; optics_options.danger_seed = 47;
    optics_options.starting_solar_phase = 0;
    Game optics_game(WorldGrid::generate_isometric(),optics_options);
    InputState optics_drive; optics_drive.down = true;
    for (int tick = 0; tick < 45; ++tick) optics_game.advance(optics_drive,1.0/60);
    wait(optics_game,0.25);
    PixelRenderer optics_on, optics_off; optics_off.toggle_headlamp();
    optics_on.render(optics_game); optics_off.render(optics_game);
    if (!optics_on.write_ppm((fs::path(directory)/"eye-night-on.ppm").string()) ||
        !optics_off.write_ppm((fs::path(directory)/"eye-night-off.ppm").string())) writes_ok = false;
    std::ofstream optics_manifest(fs::path(directory)/"eye-jump-frames.tsv");
    for (int frame = 0; frame < 26; ++frame) {
        for (int tick = 0; tick < 3; ++tick) {
            InputState input; input.jump = frame == 0 && tick == 0;
            optics_game.advance(input,1.0/60);
        }
        optics_on.render(optics_game);
        std::ostringstream name; name << "eye-jump_" << std::setw(3) << std::setfill('0') << frame << ".ppm";
        if (!optics_on.write_ppm((fs::path(directory)/name.str()).string())) writes_ok = false;
        optics_manifest << name.str() << '\n';
    }

    // A seeded short schedule exercises the complete warning before creatures
    // arrive, using normal simulation ticks and no changes to live entity state.
    GameOptions alarm_options; alarm_options.danger_seed = 47; alarm_options.initial_battery = 80;
    alarm_options.starting_solar_phase = 0;
    alarm_options.danger.safe_min = alarm_options.danger.safe_max = 0.25;
    alarm_options.danger.warning_seconds = 8;
    alarm_options.danger.active_min = alarm_options.danger.active_max = 30;
    Game alarm_game(WorldGrid::generate_isometric(),alarm_options);
    wait(alarm_game,0.5);
    if (!alarm_game.danger().warning()) { std::cerr << "Warning preview did not enter the countdown.\n"; writes_ok = false; }
    still("warning",alarm_game);
    PixelRenderer warning_menu; warning_menu.toggle_inventory(); warning_menu.render(alarm_game);
    if (!warning_menu.write_ppm((fs::path(directory)/"warning-menu.ppm").string())) writes_ok = false;
    std::ofstream alarm_manifest(fs::path(directory)/"alarm-frames.tsv");
    PixelRenderer alarm_renderer;
    for (int i = 0; i < 41; ++i) {
        alarm_renderer.render(alarm_game);
        std::ostringstream name; name << "alarm_" << std::setw(3) << std::setfill('0') << i << ".ppm";
        if (!alarm_renderer.write_ppm((fs::path(directory)/name.str()).string())) writes_ok = false;
        alarm_manifest << name.str() << '\n';
        if (i != 40) wait(alarm_game,0.25);
    }
    if (!alarm_game.danger().active() || alarm_game.danger().creatures().empty() || alarm_game.finished()) {
        std::cerr << "Creature preview did not reach a live threat encounter.\n"; writes_ok = false;
    }
    still("danger",alarm_game);

    GameOptions empty_options; empty_options.danger_seed = 47; empty_options.initial_battery = 0.0001;
    Game empty_game(WorldGrid::generate_isometric(),empty_options);
    std::ofstream collapse_manifest(fs::path(directory)/"collapse-frames.tsv");
    PixelRenderer collapse_renderer;
    for (int frame = 0; frame < 30; ++frame) {
        collapse_renderer.render(empty_game);
        std::ostringstream name; name << "collapse_" << std::setw(3) << std::setfill('0') << frame << ".ppm";
        if (!collapse_renderer.write_ppm((fs::path(directory)/name.str()).string())) writes_ok = false;
        collapse_manifest << name.str() << '\n';
        wait(empty_game,1.0/30.0);
    }
    if (empty_game.unit().battery_percent() > 0 || empty_game.climax().state() != ClimaxState::Failed) {
        std::cerr << "Battery preview did not shut down at zero charge.\n"; writes_ok = false;
    }
    wait(empty_game,0.25); // Final still includes the failure banner after the unobscured collapse clip.
    still("battery-empty",empty_game);

    GameOptions charge_options; charge_options.danger_seed = 47; charge_options.initial_battery = 50;
    charge_options.danger.safe_min = charge_options.danger.safe_max = 1000;
    Game charge_game(WorldGrid::generate_isometric(),charge_options);
    const auto charge_walk = [&](Vec2 target) {
        int attempts = 0;
        while (distance(charge_game.unit().center(),target) > 0.12 && ++attempts < 500) {
            const auto point = charge_game.unit().center();
            const int x = target.x-point.x > 0.05 ? 1 : target.x-point.x < -0.05 ? -1 : 0;
            const int y = target.y-point.y > 0.05 ? 1 : target.y-point.y < -0.05 ? -1 : 0;
            InputState input; input.right = x-y > 0; input.left = x-y < 0; input.down = x+y > 0; input.up = x+y < 0;
            charge_game.advance(input,1.0/60);
        }
        wait(charge_game,0.3);
        return attempts < 500;
    };
    bool docked = true;
    for (Vec2 target : {Vec2{10.5,12.3},Vec2{7,12.3},BaseCamp::approach,Vec2{7,6.5},BaseCamp::charger})
        if (!charge_walk(target)) { docked = false; break; }
    const double before_charge = charge_game.unit().battery_percent();
    wait(charge_game,3);
    if (!docked || !charge_game.inside_base() || !charge_game.charging() || charge_game.unit().battery_percent() <= before_charge) {
        std::cerr << "Charger preview could not walk to the pad and restore battery.\n"; writes_ok = false;
    }
    still("charging",charge_game);
    return mission_ok && writes_ok && bool(manifest) && bool(camp_manifest) && bool(alarm_manifest) &&
        bool(collapse_manifest) && bool(optics_manifest);
}
} // namespace horizon

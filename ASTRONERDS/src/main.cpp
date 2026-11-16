#include "horizon/Renderer.hpp"
#include "horizon/Preview.hpp"
#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace {
using namespace horizon;
void help() {
    std::cout << "Unit-01: PHOBOS / C++17 isometric expedition\n"
        "CLI: --terminal (default), --demo, --snapshot file.ppm, --startup-snapshot file.ppm, --preview directory, --ship-preview directory, --window, --side-window, --side-demo, --help\n\n"
        "Terminal commands (one action per line):\n"
        "  left/right/up/down [seconds]   jump [seconds]   wait [seconds]\n"
        "  mine X Y [seconds]    place X Y     look     scan     help     quit\n"
        "  extract   melt   electrolyze   liquefy   fuel   smelt\n"
        "  drill     treads     shield\n"
        "  hull      lines      core     calibrate     valve\n\n"
        "Movement defaults to 0.25 s, mining/climbing/waiting to 1 s. Maximum 30 s.\n"
        "Craft at the base bench; repair at the rocket ground socket.\n"
        "Calibration starts the SPE: reach the raised gantry valve and launch.\n"
        "Terminal simulation advances only during timed commands; window mode is real-time.\n"
        "Window: K switches robot Mark 1 / Mark 2 (or click U01 MK1/MK2 at top left).\n"
        "Mine H = hydrated minerals, O = ore, I = ice, . = loose dust, * = data core.\n"
        "Two fuel kits: 2 H blocks -> extract -> electrolyze -> liquefy -> fuel, twice.\n"
        "Hull + lines need 6 plates; all upgrades need another 6 plates.\n"
        "Each smelt consumes 1 kg ore + 1 kg dust and yields 2 plates.\n";
}
int terminal() {
    Game game;
    TerminalRenderer renderer(std::cout);
    renderer.render(game);
    std::cout << "Type help for commands.\n";
    const std::map<std::string,Action> actions{
        {"extract",Action::Extract},{"melt",Action::MeltIce},{"electrolyze",Action::Electrolyze},
        {"liquefy",Action::Liquefy},{"fuel",Action::PackageFuel},{"smelt",Action::Smelt},
        {"drill",Action::UpgradeDrill},{"treads",Action::UpgradeTreads},{"shield",Action::UpgradeShield},
        {"hull",Action::WeldHull},{"lines",Action::HookupLines},{"core",Action::SocketCore},
        {"calibrate",Action::Calibrate},{"valve",Action::ActivateValve}};
    std::string line;
    while (!game.finished() && std::cout << "\nunit-01> " && std::getline(std::cin,line)) {
        std::istringstream command(line);
        std::string name;
        if (!(command >> name)) continue;
        if (name == "quit" || name == "exit") break;
        if (name == "help") { help(); continue; }
        if (name == "collect" || name == "deliver") { std::cout << game.interact().message << "\n"; renderer.render(game); continue; }
        if (name == "scan" && game.mission1()) { for(const auto& s:game.supplies()) if(!s.collected) std::cout << "Supplement (" << s.position.x << "," << s.position.y << ")\n"; std::cout << "Deliver to astronaut (10.5,5.5).\n"; continue; }
        if (name == "scan") {
            const auto core = game.world().data_core_position();
            const auto rocket = game.rocket().ground_socket(), valve = game.rocket().launch_valve();
            const auto bench = game.refinery().position();
            std::cout << "Bench (" << bench.x << ',' << bench.y << "). Ore near (13,8), dust near (8,14), minerals near (15,17). Core (" << core.x << ',' << core.y
                      << "). Rocket (" << rocket.x << ',' << rocket.y << "). Summit valve ("
                      << valve.x << ',' << valve.y << ").\n";
            continue;
        }
        if (name == "look") { renderer.render(game); continue; }
        if (auto action = actions.find(name); action != actions.end()) {
            game.perform(action->second);
            game.advance({},1.0/60.0);
            renderer.render(game);
            continue;
        }
        if (name == "place") {
            GridPos tile;
            if (!(command >> tile.x >> tile.y)) { std::cout << "Usage: place X Y\n"; continue; }
            game.place_tile(tile); game.advance({},1.0/60.0); renderer.render(game); continue;
        }
        InputState input;
        double seconds = name == "left" || name == "right" ? 0.25 : 1.0;
        if (name == "left") input.left = true;
        else if (name == "right") input.right = true;
        else if (name == "up") input.up = true;
        else if (name == "down") input.down = true;
        else if (name == "jump") input.jump = true;
        else if (name == "climb") { input.climb = true; input.up = true; }
        else if (name == "mine") {
            GridPos target;
            if (!(command >> target.x >> target.y)) { std::cout << "Usage: mine X Y [seconds]\n"; continue; }
            input.mining_target = target;
        } else if (name != "wait") { std::cout << "Unknown command. Type help.\n"; continue; }
        command >> std::ws;
        if (!command.eof() && !(command >> seconds)) { std::cout << "Invalid duration.\n"; continue; }
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > 30) { std::cout << "Duration must be 0..30 seconds (exclusive of zero).\n"; continue; }
        double remaining = seconds;
        while (remaining > 1e-9) {
            const double dt = std::min(remaining,1.0/60.0);
            game.advance(input,dt);
            if (dt >= 1.0/60.0-1e-9) input.jump = false;
            remaining -= dt;
        }
        renderer.render(game);
    }
    return game.climax().state() == ClimaxState::Failed ? 1 : 0;
}
}
int main(int argc, char** argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "--terminal";
        if (mode == "--help" || mode == "-h") { help(); return 0; }
        if (mode == "--demo") return horizon::run_isometric_demo(std::cout) ? 0 : 1;
        if (mode == "--side-demo") return horizon::run_mission_demo(std::cout) ? 0 : 1;
        if (mode == "--ship-preview") {
            if (argc != 3) { std::cerr << "Usage: horizon --ship-preview directory\n"; return 2; }
            return horizon::export_ship_preview(argv[2]) ? 0 : 1;
        }
        if (mode == "--preview") {
            if (argc != 3) { std::cerr << "Usage: horizon --preview directory\n"; return 2; }
            return horizon::export_preview(argv[2]) ? 0 : 1;
        }
        if (mode == "--snapshot") {
            if (argc != 3) { std::cerr << "Usage: horizon --snapshot file.ppm\n"; return 2; }
            horizon::Game game; horizon::PixelRenderer renderer; renderer.render(game);
            if (!renderer.write_ppm(argv[2])) { std::cerr << "Cannot write snapshot.\n"; return 1; }
            std::cout << "Saved " << argv[2] << '\n'; return 0;
        }
        if (mode == "--startup-snapshot") {
            if (argc != 3) { std::cerr << "Usage: horizon --startup-snapshot file.ppm\n"; return 2; }
            horizon::PixelRenderer renderer; renderer.open_startup(); renderer.render_startup(1.25);
            if (!renderer.write_ppm(argv[2])) { std::cerr << "Cannot write startup snapshot.\n"; return 1; }
            std::cout << "Saved " << argv[2] << '\n'; return 0;
        }
        if (mode == "--window" || mode == "--window-smoke" || mode == "--side-window") {
#ifdef HORIZON_HAS_SDL
            return horizon::run_sdl(mode == "--window-smoke" ? 3 : 0,mode == "--side-window");
#else
            std::cerr << "This build has no SDL2 development library. Install SDL2, pkg-config and rebuild; or use --terminal.\n";
            return 2;
#endif
        }
        if (mode == "--terminal") return terminal();
        std::cerr << "Unknown option: " << mode << ". Use --help.\n"; return 2;
    } catch (const std::exception& error) {
        std::cerr << "Horizon: " << error.what() << '\n'; return 1;
    }
}

#include "horizon/Game.hpp"
#include <algorithm>
#include <ostream>
#include <queue>

namespace horizon {
namespace {
std::vector<GridPos> path_to(const WorldGrid& world, GridPos from, GridPos to) {
    if (!world.in_bounds(to.x,to.y) || world.solid(to.x,to.y) || world.pit(to.x,to.y)) return {};
    const int width = world.width(), size = width*world.height();
    const int start = from.y*width+from.x, goal = to.y*width+to.x;
    if (start == goal) return {to};
    std::vector<int> previous(size,-1); std::queue<int> queue;
    previous[start] = start; queue.push(start);
    while (!queue.empty() && previous[goal] < 0) {
        const int current = queue.front(); queue.pop();
        const int x = current%width, y = current/width;
        for (GridPos delta : {GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}) {
            const int nx = x+delta.x, ny = y+delta.y;
            if (!world.in_bounds(nx,ny) || world.solid(nx,ny) || world.pit(nx,ny) || std::abs(world.elevation(x,y)-world.elevation(nx,ny)) > 0.65) continue;
            const int next = ny*width+nx;
            if (previous[next] >= 0) continue;
            previous[next] = current; queue.push(next);
        }
    }
    if (previous[goal] < 0) return {};
    std::vector<GridPos> result;
    for (int node = goal; node != start; node = previous[node]) result.push_back({node%width,node/width});
    std::reverse(result.begin(),result.end()); return result;
}
}
bool run_isometric_demo(std::ostream& output, const DemoObserver& observe) {
    Game game(WorldGrid::generate_isometric());
    const char* stage = "overview";
    if (observe) observe(game,stage);
    const auto tick = [&](InputState input) {
        game.advance(input,1.0/60);
        if (observe) observe(game,stage);
    };
    const auto wait = [&](double duration) { for (int i = 0; i < int(duration*60); ++i) tick({}); };
    const auto walk_path = [&](const std::vector<GridPos>& path) {
        if (path.empty()) return false;
        for (auto cell : path) {
            const Vec2 target{cell.x+0.5,cell.y+0.5};
            int attempts = 0;
            while (distance(game.unit().center(),target) > 0.13 && ++attempts < 600 && game.unit().alive()) {
                const auto p = game.unit().center();
                const double dx = target.x-p.x, dy = target.y-p.y;
                InputState input;
                // Correct both axes before squeezing past an obstacle. Steering
                // only on the dominant axis can strand a body against a corner.
                const int wx = dx > 0.06 ? 1 : dx < -0.06 ? -1 : 0;
                const int wy = dy > 0.06 ? 1 : dy < -0.06 ? -1 : 0;
                input.right = wx-wy > 0; input.left = wx-wy < 0;
                input.down = wx+wy > 0; input.up = wx+wy < 0;
                tick(input);
            }
            if (attempts >= 600 || !game.unit().alive()) {
                output << "Blocked walking to " << cell.x << ',' << cell.y << " from " << game.unit().center().x << ',' << game.unit().center().y << '\n';
                return false;
            }
        }
        wait(0.12); return true;
    };
    const auto walk_near = [&](Vec2 target) {
        const auto p = game.unit().center();
        const GridPos from{int(p.x),int(p.y)};
        std::vector<GridPos> best;
        for (int y = int(target.y)-1; y <= int(target.y)+1; ++y)
            for (int x = int(target.x)-1; x <= int(target.x)+1; ++x) {
                if (distance(target,{x+0.5,y+0.5}) > 1.1) continue;
                auto path = path_to(game.world(),from,{x,y});
                if (!path.empty() && (best.empty() || path.size() < best.size())) best = std::move(path);
            }
        return walk_path(best);
    };
    const auto mine = [&](GridPos cell) {
        stage = "movement";
        if (!walk_near({cell.x+0.5,cell.y+0.5})) return false;
        stage = "mining";
        for (int i = 0; i < 360 && game.world().solid(cell.x,cell.y); ++i) {
            InputState input; input.mining_target = cell; tick(input);
        }
        if (game.world().solid(cell.x,cell.y)) { output << "Mining blocked: " << cell.x << ',' << cell.y << '\n'; return false; }
        wait(0.25); return true;
    };
    const auto act = [&](Action action) {
        const auto result = game.perform(action);
        output << (result.ok ? "  OK: " : "  FAIL: ") << result.message << '\n';
        if (observe && result.ok && action != Action::ActivateValve) wait(1.8);
        return result.ok;
    };
    stage = "jump";
    InputState jumping; jumping.jump = true; tick(jumping); wait(1.35);
    output << "Unit-01 / isometric expedition mission\nExploring the hub and mining real resource nodes...\n";
    for (int y : {8,12}) for (int x : {13,15,17}) if (!mine({x,y})) return false;
    for (int y : {14,16}) for (int x : {8,10,12}) if (!mine({x,y})) return false;
    for (int x : {15,17}) if (!mine({x,17})) return false;
    if (!mine(game.world().data_core_position())) return false;
    stage = "movement";
    if (!walk_near(game.refinery().position())) return false;
    stage = "crafting";
    for (int i = 0; i < 6; ++i) if (!act(Action::Smelt)) return false;
    for (Action action : {Action::UpgradeDrill,Action::UpgradeTreads,Action::UpgradeShield}) if (!act(action)) return false;
    for (int i = 0; i < 2; ++i)
        for (Action action : {Action::Extract,Action::Electrolyze,Action::Liquefy,Action::PackageFuel}) if (!act(action)) return false;
    stage = "movement";
    if (!walk_near(game.rocket().ground_socket())) return false;
    stage = "tasks";
    for (Action action : {Action::WeldHull,Action::HookupLines,Action::SocketCore,Action::Calibrate}) if (!act(action)) return false;
    wait(0.05);
    stage = "climb"; // Compatibility name for the preview clip; now a raised ramp.
    const auto valve = game.rocket().launch_valve();
    const auto p = game.unit().center();
    if (!walk_path(path_to(game.world(),{int(p.x),int(p.y)},{int(valve.x),int(valve.y)}))) return false;
    if (!act(Action::ActivateValve)) return false;
    stage = "launch";
    wait(5.2);
    output << game.climax().outcome_text() << '\n';
    return game.climax().state() == ClimaxState::Complete && game.rocket().launched() && !game.unit().alive();
}
} // namespace horizon

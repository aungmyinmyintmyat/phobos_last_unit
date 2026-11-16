#include "horizon/TargetTool.hpp"
#include "horizon/Game.hpp"

namespace horizon {
void TargetTool::press(Game& game, GridPos aim) {
    mining_ = false;
    if (!game.unit().alive() || !game.world().in_bounds(aim.x,aim.y)) return;
    const auto type = game.world().get_tile_type(aim.x,aim.y);
    if (type == TileType::VacuumAir) game.place_tile(aim);
    else mining_ = tile_definition(type).mineable;
}
}

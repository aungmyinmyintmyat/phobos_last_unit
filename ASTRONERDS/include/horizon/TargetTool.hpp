#pragma once
#include "horizon/Types.hpp"
#include <optional>

namespace horizon {
class Game;
// J chooses its mode on key-down. A mined block becoming empty never switches a
// held mining key into placement; release and press again to place deliberately.
class TargetTool {
public:
    void press(Game& game, GridPos aim);
    void release() { mining_ = false; }
    std::optional<GridPos> target(GridPos aim) const { return mining_ ? std::optional<GridPos>(aim) : std::nullopt; }
private:
    bool mining_ = false;
};
}

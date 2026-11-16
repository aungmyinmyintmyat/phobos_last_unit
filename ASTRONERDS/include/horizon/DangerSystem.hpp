#pragma once
#include "horizon/Types.hpp"
#include <vector>

namespace horizon {
class WorldGrid;
enum class DangerPhase { Calm, Warning, Active };
struct DangerConfig {
    double safe_min = 180, safe_max = 300, warning_seconds = 30;
    double active_min = 45, active_max = 65;
    int creature_count = 3;
};
struct Creature {
    Vec2 position;
    double heading = 0, stride = 0, attack_cooldown = 0;
    double health = 100, bite_age = -1, hit_flash = 0, death_age = -1, stagger = 0;
};
struct LaserStrike {
    bool valid = false, hit = false, killed = false;
    Vec2 endpoint;
    int creature_index = -1;
};
struct DangerTick {
    double battery_damage = 0;
    bool warning_started = false, active_started = false, cleared = false, beep = false;
};
class DangerSystem {
public:
    explicit DangerSystem(std::uint32_t seed = 7, DangerConfig config = {});
    // Ignore invalid/nonpositive dt; bound a single catch-up call to 600 seconds.
    DangerTick update(double dt, const WorldGrid& world, Vec2 robot, bool sheltered);
    LaserStrike fire_laser(Vec2 origin, Vec2 aim, const WorldGrid& world, double range = 7, double damage = 34);
    DangerPhase phase() const { return phase_; }
    double seconds_remaining() const { return remaining_; }
    bool warning() const { return phase_ == DangerPhase::Warning; }
    bool active() const { return phase_ == DangerPhase::Active; }
    const std::vector<Creature>& creatures() const { return creatures_; }
private:
    double random_between(double low, double high);
    bool walkable(Vec2 point, const WorldGrid& world) const;
    bool clear_path(Vec2 from, Vec2 to, const WorldGrid& world) const;
    void refresh_navigation(const WorldGrid& world, Vec2 target);
    void move_creatures(double dt, const WorldGrid& world, Vec2 robot, bool sheltered, DangerTick& tick);
    void spawn(const WorldGrid& world, Vec2 robot, bool sheltered);
    std::uint32_t sequence_ = 7;
    DangerConfig config_;
    DangerPhase phase_ = DangerPhase::Calm;
    double remaining_ = 0;
    std::vector<Creature> creatures_;
    std::vector<int> navigation_;
    GridPos navigation_goal_{-1,-1};
    int navigation_width_ = 0, navigation_height_ = 0;
    double navigation_remaining_ = 0;
};
}

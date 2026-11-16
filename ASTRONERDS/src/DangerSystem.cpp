#include "horizon/DangerSystem.hpp"
#include "horizon/BaseCamp.hpp"
#include "horizon/WorldGrid.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace horizon {
namespace {
constexpr double creature_radius = 0.23;
constexpr double creature_speed = 2.9;
constexpr double max_step = 0.65;
constexpr double attack_range = 1.1;
constexpr double attack_period = 1.4;
constexpr double bite_impact = 0.22;
constexpr std::array<GridPos,4> neighbors{{{1,0},{0,1},{-1,0},{0,-1}}};
GridPos cell(Vec2 p) { return {int(std::floor(p.x)),int(std::floor(p.y))}; }
bool valid_point(Vec2 p, const WorldGrid& world) {
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 && p.x < world.width() && p.y < world.height();
}
double positive(double value, double fallback) { return std::isfinite(value) && value > 0 ? std::max(0.01,value) : fallback; }
// These buckets yield five-second warning pulses, then every last ten seconds.
double warning_bucket(double remaining) {
    return remaining <= 10 ? std::ceil(remaining-1e-9) : 10+std::ceil((remaining-10)/5-1e-9);
}
}
DangerSystem::DangerSystem(std::uint32_t seed, DangerConfig config) : sequence_(seed), config_(config) {
    config_.safe_min = positive(config_.safe_min,180);
    config_.safe_max = positive(config_.safe_max,300);
    config_.active_min = positive(config_.active_min,45);
    config_.active_max = positive(config_.active_max,65);
    config_.warning_seconds = positive(config_.warning_seconds,30);
    if (config_.safe_min > config_.safe_max) std::swap(config_.safe_min,config_.safe_max);
    if (config_.active_min > config_.active_max) std::swap(config_.active_min,config_.active_max);
    config_.creature_count = std::clamp(config_.creature_count,0,16);
    remaining_ = random_between(config_.safe_min,config_.safe_max);
}
double DangerSystem::random_between(double low, double high) {
    // Own RNG stream: terrain generation and rendering cannot change a warning.
    sequence_ = sequence_*1664525u+1013904223u;
    return low+(high-low)*(double(sequence_)/4294967296.0);
}
bool DangerSystem::walkable(Vec2 point, const WorldGrid& world) const {
    if (!valid_point(point,world)) return false;
    const auto center = cell(point);
    for (double dx : {-creature_radius,creature_radius}) for (double dy : {-creature_radius,creature_radius}) {
        Vec2 corner{point.x+dx,point.y+dy};
        if (!valid_point(corner,world) || BaseCamp::inside(corner)) return false;
        const auto at = cell(corner);
        if (world.solid(at.x,at.y) || world.pit(at.x,at.y) || BaseCamp::wall(at.x,at.y) ||
            std::abs(world.elevation(at.x,at.y)-world.elevation(center.x,center.y)) > max_step) return false;
    }
    return !BaseCamp::inside(point);
}
bool DangerSystem::clear_path(Vec2 from, Vec2 to, const WorldGrid& world) const {
    if (!walkable(from,world) || !walkable(to,world)) return false;
    const int steps = std::max(1,int(std::ceil(distance(from,to)/0.1)));
    auto previous = cell(from);
    for (int i = 1; i <= steps; ++i) {
        const double t = double(i)/steps;
        const Vec2 point{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t};
        if (!walkable(point,world)) return false;
        const auto at = cell(point);
        if (std::abs(world.elevation(at.x,at.y)-world.elevation(previous.x,previous.y)) > max_step) return false;
        previous = at;
    }
    return true;
}
void DangerSystem::refresh_navigation(const WorldGrid& world, Vec2 target) {
    navigation_width_ = world.width(); navigation_height_ = world.height();
    navigation_.assign(std::size_t(navigation_width_)*navigation_height_,-1);
    navigation_goal_ = {-1,-1};
    const auto origin = cell(target);
    double nearest = std::numeric_limits<double>::max();
    // Robots may be jumping over a solid tile; aim for a nearby accessible cell.
    for (int y = origin.y-3; y <= origin.y+3; ++y) for (int x = origin.x-3; x <= origin.x+3; ++x) {
        Vec2 center{x+0.5,y+0.5};
        const double range = distance(center,target);
        if (range < nearest && walkable(center,world)) { nearest = range; navigation_goal_ = {x,y}; }
    }
    navigation_remaining_ = 0.5;
    if (navigation_goal_.x < 0) return;
    std::vector<GridPos> queue;
    queue.reserve(navigation_.size());
    queue.push_back(navigation_goal_);
    navigation_[std::size_t(navigation_goal_.y)*navigation_width_+navigation_goal_.x] = 0;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto current = queue[head];
        const auto index = std::size_t(current.y)*navigation_width_+current.x;
        for (const auto delta : neighbors) {
            const GridPos next{current.x+delta.x,current.y+delta.y};
            if (!world.in_bounds(next.x,next.y)) continue;
            const auto next_index = std::size_t(next.y)*navigation_width_+next.x;
            if (navigation_[next_index] >= 0 || !walkable({next.x+0.5,next.y+0.5},world) ||
                std::abs(world.elevation(next.x,next.y)-world.elevation(current.x,current.y)) > max_step) continue;
            navigation_[next_index] = navigation_[index]+1;
            queue.push_back(next);
        }
    }
}
void DangerSystem::spawn(const WorldGrid& world, Vec2 robot, bool sheltered) {
    creatures_.clear();
    const Vec2 target = sheltered ? BaseCamp::approach : robot;
    refresh_navigation(world,target);
    const auto try_spawn = [&](Vec2 point) {
        if (!walkable(point,world) || distance(point,target) < 5) return false;
        const auto at = cell(point);
        if (navigation_[std::size_t(at.y)*navigation_width_+at.x] < 0) return false;
        for (const auto& creature : creatures_) if (distance(point,creature.position) < 0.9) return false;
        creatures_.push_back({point,0,0,0});
        return true;
    };
    for (int n = 0; n < config_.creature_count; ++n) {
        bool placed = false;
        for (int attempt = 0; attempt < 64 && !placed; ++attempt) {
            const double angle = random_between(0,6.2831853071795864769), radius = random_between(5,9);
            placed = try_spawn({target.x+std::cos(angle)*radius,target.y+std::sin(angle)*radius});
        }
        // A blocked ring can have only a few suitable cells. Never force a spawn
        // into a wall, onto the robot or inside the habitat to meet the count.
        if (!placed) {
            const auto around = cell(target);
            for (int y = around.y-9; y <= around.y+9 && !placed; ++y)
                for (int x = around.x-9; x <= around.x+9 && !placed; ++x) {
                    const Vec2 point{x+0.5,y+0.5};
                    if (distance(point,target) <= 9) placed = try_spawn(point);
                }
        }
    }
}
void DangerSystem::move_creatures(double dt, const WorldGrid& world, Vec2 robot, bool sheltered, DangerTick& tick) {
    const Vec2 target = sheltered ? BaseCamp::approach : robot;
    navigation_remaining_ -= dt;
    if (navigation_remaining_ <= 0 || navigation_width_ != world.width() || navigation_height_ != world.height())
        refresh_navigation(world,target);
    for (auto& creature : creatures_) {
        if (creature.health <= 0) { creature.death_age += dt; continue; }
        creature.hit_flash = std::max(0.0,creature.hit_flash-dt);
        creature.stagger = std::max(0.0,creature.stagger-dt);
        creature.attack_cooldown = std::max(0.0,creature.attack_cooldown-dt);
        if (creature.stagger > 0) { creature.bite_age = -1; continue; }
        if (!walkable(creature.position,world)) continue; // Newly placed obstacles cannot be crossed.
        const double range = distance(creature.position,robot);
        if (!sheltered && range <= attack_range && clear_path(creature.position,robot,world)) {
            if (creature.bite_age < 0 && creature.attack_cooldown <= 1e-9) creature.bite_age = 0;
            if (creature.bite_age >= 0) {
                const double previous = creature.bite_age;
                creature.bite_age += dt;
                if (previous < bite_impact && creature.bite_age >= bite_impact) {
                    tick.battery_damage += 8;
                    creature.attack_cooldown = attack_period;
                }
                if (creature.bite_age >= 0.55) creature.bite_age = -1;
            }
            creature.heading = std::atan2(robot.y-creature.position.y,robot.x-creature.position.x);
            continue;
        }
        creature.bite_age = -1;
        Vec2 waypoint = target;
        if (!clear_path(creature.position,target,world)) {
            const auto at = cell(creature.position);
            const auto index = std::size_t(at.y)*navigation_width_+at.x;
            int best = navigation_[index];
            waypoint = {at.x+0.5,at.y+0.5};
            for (const auto delta : neighbors) {
                const GridPos next{at.x+delta.x,at.y+delta.y};
                if (!world.in_bounds(next.x,next.y)) continue;
                const int value = navigation_[std::size_t(next.y)*navigation_width_+next.x];
                const Vec2 candidate{next.x+0.5,next.y+0.5};
                if (value >= 0 && (best < 0 || value < best) && clear_path(creature.position,candidate,world)) {
                    best = value; waypoint = candidate;
                }
            }
        }
        const double travel = distance(creature.position,waypoint);
        if (travel <= 0.01) continue;
        const double step = std::min(creature_speed*dt,travel);
        const Vec2 next{creature.position.x+(waypoint.x-creature.position.x)*step/travel,
                        creature.position.y+(waypoint.y-creature.position.y)*step/travel};
        if (!clear_path(creature.position,next,world)) continue;
        creature.heading = std::atan2(next.y-creature.position.y,next.x-creature.position.x);
        creature.position = next;
        creature.stride += step;
    }
}
LaserStrike DangerSystem::fire_laser(Vec2 origin, Vec2 aim, const WorldGrid& world, double range, double damage) {
    LaserStrike strike;
    if (phase_ != DangerPhase::Active || !valid_point(origin,world) ||
        !std::isfinite(aim.x) || !std::isfinite(aim.y) || !std::isfinite(range) || range <= 0 ||
        !std::isfinite(damage) || damage <= 0) return strike;
    const double length = distance(origin,aim);
    if (length < 1e-6) return strike;
    const Vec2 direction{(aim.x-origin.x)/length,(aim.y-origin.y)/length};
    double end = range;
    // Sample terrain first so walls and mined rock stop the shot before a creature.
    for (double t = 0.08; t <= range; t += 0.08) {
        const Vec2 p{origin.x+direction.x*t,origin.y+direction.y*t};
        if (!valid_point(p,world)) { end = t-0.08; break; }
        const auto at = cell(p);
        if (world.solid(at.x,at.y) || BaseCamp::wall(at.x,at.y)) { end = t-0.08; break; }
    }
    int best = -1;
    for (std::size_t i = 0; i < creatures_.size(); ++i) {
        const auto& creature = creatures_[i];
        if (creature.health <= 0) continue;
        const Vec2 offset{creature.position.x-origin.x,creature.position.y-origin.y};
        const double t = offset.x*direction.x+offset.y*direction.y;
        if (t < 0 || t > end || t >= range) continue;
        const double side = std::hypot(offset.x-direction.x*t,offset.y-direction.y*t);
        if (side <= 0.42) { end = t; best = int(i); }
    }
    strike.valid = true;
    strike.endpoint = {origin.x+direction.x*end,origin.y+direction.y*end};
    if (best >= 0) {
        auto& creature = creatures_[std::size_t(best)];
        creature.health = std::max(0.0,creature.health-damage);
        creature.hit_flash = 0.23;
        creature.bite_age = -1;
        creature.stagger = 0.28;
        creature.attack_cooldown = std::max(creature.attack_cooldown,0.85);
        strike.hit = true; strike.creature_index = best;
        if (creature.health <= 0) { creature.death_age = 0; strike.killed = true; }
    }
    return strike;
}
DangerTick DangerSystem::update(double dt, const WorldGrid& world, Vec2 robot, bool sheltered) {
    DangerTick tick;
    if (!std::isfinite(dt) || dt <= 0 || !valid_point(robot,world)) return tick;
    dt = std::min(dt,600.0);
    sheltered = sheltered || BaseCamp::inside(robot);
    while (dt > 1e-9) {
        const double step = std::min({dt,remaining_,0.05});
        if (phase_ == DangerPhase::Active) move_creatures(step,world,robot,sheltered,tick);
        const double previous = remaining_;
        remaining_ = std::max(0.0,remaining_-step);
        dt -= step;
        if (phase_ == DangerPhase::Warning && warning_bucket(previous) != warning_bucket(remaining_)) tick.beep = true;
        if (remaining_ > 1e-9) continue;
        if (phase_ == DangerPhase::Calm) {
            phase_ = DangerPhase::Warning;
            remaining_ = config_.warning_seconds;
            tick.warning_started = tick.beep = true;
        } else if (phase_ == DangerPhase::Warning) {
            phase_ = DangerPhase::Active;
            remaining_ = random_between(config_.active_min,config_.active_max);
            spawn(world,robot,sheltered);
            tick.active_started = tick.beep = true;
        } else {
            phase_ = DangerPhase::Calm;
            remaining_ = random_between(config_.safe_min,config_.safe_max);
            creatures_.clear();
            tick.cleared = true;
        }
    }
    return tick;
}
}

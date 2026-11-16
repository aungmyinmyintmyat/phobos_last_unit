#pragma once
#include "horizon/Types.hpp"
#include "horizon/SoundEffects.hpp"
#include <deque>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace horizon {
struct Particle {
    Vec2 position, velocity;
    double age = 0, lifetime = 0.5;
    std::uint32_t color = 0xffffff;
    int size = 1;
    bool dust = false;
};
struct ActivityAnimation {
    Vec2 position;
    std::string label;
    double age = 0, duration = 1.6;
    bool crafting = false;
    Item item = Item::Count;
    std::int64_t quantity = 0;
};
struct TreadMark {
    Vec2 position;
    double heading = 0, born = 0;
};
// Presentation has its own deterministic particle sequence. It never consumes the
// terrain RNG or alters inventory, collision, mining speed or recipe outcomes.
class Presentation {
public:
    void update(double dt, Vec2 position, Vec2 velocity, bool grounded, bool alive, bool planar = false,
                double vertical_velocity = 0, double battery = 100, double integrity = 100,
                double terrain_elevation = std::numeric_limits<double>::quiet_NaN(), bool vanished = false);
    void damage(Vec2 position, double amount);
    void face_target(Vec2 from, Vec2 target);
    double eye_openness() const;
    double eye_glow() const {
        return (0.92+0.08*std::sin(clock*2.2))*(1-damage_level*(0.25+0.12*std::sin(clock*23)));
    }
    void mining(GridPos target, double progress, bool broke, std::uint32_t color);
    void activity(Vec2 position, std::string label, bool crafting, Item item = Item::Count, std::int64_t quantity = 0);
    void placement(GridPos position);
    void notify(std::string message) { notice = std::move(message); notice_age = 0; sound(SoundCue::Error); }
    void sound(SoundCue cue);
    const std::vector<SoundEvent>& sound_events() const { return sound_events_; }
    double clock = 0, tread_phase = 0, landing = 0, mining_progress = 0;
    int facing = 1;
    double heading = 1.5707963267948966, head_heading = 1.5707963267948966;
    double left_tread_phase = 0, right_tread_phase = 0, lean_forward = 0, lean_side = 0;
    double head_pitch = -0.23;
    double damage_level = 0, damage_flash = 0, collapse = 0;
    double collapse_age() const { return collapse_age_; }
    // World-height suspension offsets; the head follows the chassis on its own spring.
    double body_bounce = 0, head_bounce = 0, tread_tuck = 0;
    double visual_elevation = std::numeric_limits<double>::quiet_NaN();
    std::optional<GridPos> mining_target;
    std::optional<ActivityAnimation> active_activity;
    std::optional<std::string> notice;
    double notice_age = 0;
    const std::vector<Particle>& particles() const { return particles_; }
    const std::deque<TreadMark>& tread_marks() const { return tread_marks_; }
    static constexpr std::size_t max_tread_marks = 4096;
    static constexpr double tread_lifetime = 30, tread_fade_start = 5, tread_spacing = 0.14;
    static double tread_opacity(double age);
private:
    void emit(Vec2 position, std::uint32_t color, int count, double speed = 1);
    std::vector<Particle> particles_;
    void update_treads(Vec2 position, bool contact);
    std::deque<TreadMark> tread_marks_;
    std::optional<Vec2> previous_contact_;
    double tread_distance_ = 0, previous_track_heading_ = 0;
    std::uint32_t sequence_ = 7;
    double dust_timer_ = 0, spark_timer_ = 0;
    bool was_grounded_ = true;
    bool was_alive_ = true;
    Vec2 previous_velocity_{};
    double previous_vertical_velocity_ = 0, body_bounce_velocity_ = 0, head_bounce_velocity_ = 0;
    double head_pitch_velocity_ = 0;
    double gait_weight_ = 0;
    double collapse_age_ = 0, damage_spark_timer_ = 0;
    double servo_distance_ = 0, chatter_delay_ = 9;
    std::uint32_t chatter_sequence_ = 0x7a27u;
    std::vector<SoundEvent> sound_events_;
    std::uint64_t sound_serial_ = 0;
    double head_goal_ = 1.5707963267948966, aim_timeout_ = 0;
};
}

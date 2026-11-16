#include "horizon/Presentation.hpp"
#include <algorithm>
#include <utility>

namespace horizon {
namespace {
// Exact damped spring step for a fixed target, stable even on a long frame.
void spring(double& position, double& velocity, double target, double frequency, double damping, double dt) {
    const double decay = frequency*damping, wave = frequency*std::sqrt(1-damping*damping);
    const double offset = position-target, b = (velocity+decay*offset)/wave;
    const double sine = std::sin(wave*dt), cosine = std::cos(wave*dt), envelope = std::exp(-decay*dt);
    position = target+envelope*(offset*cosine+b*sine);
    velocity = envelope*((b*wave-decay*offset)*cosine-(offset*wave+decay*b)*sine);
}
}
double Presentation::eye_openness() const {
    // A short lid closure and a slower reopening, with an occasional double blink.
    // Simulation time keeps this deterministic and freezes it with the field guide.
    const double phase = std::fmod(clock,11.2);
    double openness = 1;
    for (double start : {4.5,10.1,10.43}) {
        const double elapsed = phase-start;
        double t = 1;
        if (elapsed >= 0 && elapsed < 0.06) t = 1-elapsed/0.06;
        else if (elapsed >= 0.06 && elapsed < 0.095) t = 0;
        else if (elapsed >= 0.095 && elapsed < 0.23) t = (elapsed-0.095)/0.135;
        openness = std::min(openness,t*t*(3-2*t));
    }
    return openness;
}
double Presentation::tread_opacity(double age) {
    if (!std::isfinite(age) || age < 0 || age >= tread_lifetime) return 0;
    const double t = std::clamp((age-tread_fade_start)/(tread_lifetime-tread_fade_start),0.0,1.0);
    return 1-t*t*(3-2*t);
}
void Presentation::sound(SoundCue cue) {
    if (sound_events_.size() >= 32) sound_events_.erase(sound_events_.begin());
    sound_events_.push_back({++sound_serial_,cue});
}
void Presentation::emit(Vec2 position, std::uint32_t color, int count, double speed) {
    for (int i = 0; i < count && particles_.size() < 160; ++i) {
        sequence_ = sequence_*1664525u+1013904223u;
        const double angle = (sequence_%6283)/1000.0;
        const double magnitude = (0.6+((sequence_>>12)%100)/70.0)*speed;
        particles_.push_back({position,{std::cos(angle)*magnitude,std::sin(angle)*magnitude-0.8},
                              0,0.3+(sequence_%40)/100.0,color,i%4 == 0 ? 2 : 1});
    }
}
void Presentation::update_treads(Vec2 position, bool contact) {
    while (!tread_marks_.empty() && clock-tread_marks_.front().born >= tread_lifetime) tread_marks_.pop_front();
    if (!contact) { previous_contact_.reset(); tread_distance_ = 0; return; }
    const Vec2 center{position.x+0.35,position.y+0.425};
    if (previous_contact_) {
        const double length = distance(center,*previous_contact_);
        if (length > 2) tread_distance_ = 0; // Never connect a reset/teleport.
        else if (length > 1e-8) {
            const double turn = std::atan2(std::sin(heading-previous_track_heading_),std::cos(heading-previous_track_heading_));
            const int count = int(std::floor((tread_distance_+length+1e-9)/tread_spacing));
            for (int i = 1; i <= count; ++i) {
                const double along = i*tread_spacing-tread_distance_;
                const double fraction = std::clamp(along/length,0.0,1.0);
                const Vec2 stamp{previous_contact_->x+(center.x-previous_contact_->x)*fraction,
                                 previous_contact_->y+(center.y-previous_contact_->y)*fraction};
                tread_marks_.push_back({stamp,previous_track_heading_+turn*fraction,clock});
                if (tread_marks_.size() > max_tread_marks) tread_marks_.pop_front();
            }
            tread_distance_ = std::max(0.0,tread_distance_+length-count*tread_spacing);
        }
    }
    previous_contact_ = center; previous_track_heading_ = heading;
}
void Presentation::update(double dt, Vec2 position, Vec2 velocity, bool grounded, bool alive, bool planar,
                          double vertical_velocity, double battery, double integrity, double terrain_elevation, bool vanished) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 0.25) return;
    clock += dt;
    if (vanished) {
        particles_.clear(); mining_target.reset(); active_activity.reset(); notice.reset();
        damage_flash = 0; collapse = 0; collapse_age_ += dt; was_alive_ = false;
        return;
    }
    // Smooth stair-height changes while driving; preserve the real jump arc.
    if (std::isfinite(terrain_elevation)) {
        if (!std::isfinite(visual_elevation) || !grounded || !alive)
            visual_elevation = terrain_elevation;
        else visual_elevation += (terrain_elevation-visual_elevation)*(1-std::exp(-dt*12));
    }
    damage_flash = std::max(0.0,damage_flash-dt);
    const double weakness = battery < 40 ? 0.45+0.55*std::clamp((40-battery)/40,0.0,1.0) : 0;
    const double target_damage = alive ? std::max(weakness,std::clamp((70-integrity)/70,0.0,1.0)) : 1;
    damage_level += (target_damage-damage_level)*(1-std::exp(-dt*8));
    if (!alive) collapse_age_ += dt;
    else collapse_age_ = 0;
    if (was_alive_ && !alive) {
        sound(SoundCue::Land);
        damage_flash = 0.65;
        emit({position.x+0.35,position.y+0.425},0xff9b63,14,1.0);
        emit({position.x+0.35,position.y+0.425},0x59636a,9,0.55);
    }
    was_alive_ = alive;
    const double collapse_t = std::clamp(collapse_age_/0.70,0.0,1.0);
    collapse = collapse_t*collapse_t*(3-2*collapse_t);
    if (notice) { notice_age += dt; if (notice_age > 2.2) notice.reset(); }
    mining_target.reset();
    landing = std::max(0.0,landing-dt);
    if (alive && std::abs(velocity.x) > 0.1) facing = velocity.x > 0 ? 1 : -1;
    const double speed = planar ? std::hypot(velocity.x,velocity.y) : std::abs(velocity.x);
    tread_phase += (grounded ? speed : 0)*dt*9;
    const double gait_target = alive && grounded ? std::clamp(speed/3.0,0.0,1.0) : 0;
    gait_weight_ += (gait_target-gait_weight_)*(1-std::exp(-dt*9));
    if (planar) {
        const double target = speed > 0.1 && alive ? std::atan2(velocity.y,velocity.x) : heading;
        const double difference = std::atan2(std::sin(target-heading),std::cos(target-heading));
        const double turn_rate = std::clamp(difference*(1-std::exp(-dt*14))/dt,-7.0,7.0);
        heading += turn_rate*dt;
        if (grounded) {
            const double hitch = 1-damage_level*0.36*std::pow(std::max(0.0,std::sin(tread_phase*0.4)),4);
            left_tread_phase += (speed*hitch-turn_rate*0.25)*dt*9;
            right_tread_phase += (speed+turn_rate*0.25)*dt*9;
        }
        const Vec2 acceleration{(velocity.x-previous_velocity_.x)/dt,(velocity.y-previous_velocity_.y)/dt};
        double pitch = grounded ? std::clamp(-(acceleration.x*std::cos(heading)+acceleration.y*std::sin(heading))*0.0008,-0.022,0.022)
                                      : std::clamp(-vertical_velocity*0.009,-0.045,0.045);
        double roll = grounded ? std::clamp(-speed*turn_rate*0.0008,-0.014,0.014) : 0;
        if (alive) {
            pitch += damage_level*0.08;
            roll += damage_level*(0.08+gait_weight_*std::sin(tread_phase*0.4)*0.055);
        } else { pitch = collapse*0.72; roll = collapse*0.48; }
        const double blend = 1-std::exp(-dt*12);
        lean_forward += (pitch-lean_forward)*blend; lean_side += (roll-lean_side)*blend;
        aim_timeout_ = std::max(0.0,aim_timeout_-dt);
        const double look = aim_timeout_ > 0 ? head_goal_ : heading+(speed < 0.1 ? std::sin(clock*0.8)*0.1 : 0);
        const double head_delta = std::atan2(std::sin(look-head_heading),std::cos(look-head_heading));
        head_heading += std::clamp(head_delta*(1-std::exp(-dt*12.0)),-5.0*dt,5.0*dt);
    }
    update_treads(position,planar && grounded && alive);
    previous_velocity_ = velocity;
    if (!was_grounded_ && grounded && alive) {
        sound(SoundCue::Land);
        landing = 0.16;
        const double impact = std::clamp(-previous_vertical_velocity_,0.0,8.0);
        body_bounce_velocity_ -= impact*0.65;
        head_bounce_velocity_ -= impact*0.12;
        head_pitch_velocity_ -= impact*0.24;
        emit({position.x+0.35,position.y+0.85},0xa79c86,4+int(impact),0.35+impact*0.06);
    }
    if (was_grounded_ && !grounded && alive) {
        if (vertical_velocity > 0) {
            sound(SoundCue::Jump);
            body_bounce_velocity_ -= std::min(vertical_velocity,8.0)*0.20;
            head_bounce_velocity_ -= std::min(vertical_velocity,8.0)*0.12;
            emit({position.x+0.35,position.y+0.425},0xb8a98d,6,0.5);
        }
    }
    // Substeps let the head follow the moving body target with a slight delay.
    const int steps = std::max(1,int(std::ceil(dt*240)));
    const double step = dt/steps;
    const double body_target = !alive ? -0.16*collapse : !grounded ? std::clamp(-vertical_velocity*0.006,-0.035,0.035) :
        gait_weight_*0.0045*(1-std::cos(tread_phase*0.45))-
        damage_level*gait_weight_*(0.018+0.045*std::pow(std::max(0.0,std::sin(tread_phase*0.4)),4));
    for (int i = 0; i < steps; ++i) {
        spring(body_bounce,body_bounce_velocity_,body_target,26,0.68,step);
        const double head_target = body_bounce+(!alive ? -0.38*collapse :
            !grounded ? std::clamp(-vertical_velocity*0.005,-0.025,0.025) : -0.045*damage_level);
        spring(head_bounce,head_bounce_velocity_,head_target,21,0.62,step);
        const double pitch_target = -0.23+lean_forward*0.65-(alive ? damage_level*0.20 : collapse*1.15)+
            (alive && !grounded ? std::clamp(vertical_velocity*0.035,-0.20,0.16) : 0)+
            std::clamp((body_bounce-head_bounce)*0.8,-0.08,0.08);
        spring(head_pitch,head_pitch_velocity_,pitch_target,16,0.56,step);
    }
    const double tuck_target = alive && !grounded ? 0.065*(1-std::clamp(-vertical_velocity/6.0,0.0,1.0)) : 0;
    tread_tuck += (tuck_target-tread_tuck)*(1-std::exp(-dt*22));
    previous_vertical_velocity_ = vertical_velocity;
    was_grounded_ = grounded;
    damage_spark_timer_ += dt;
    if (alive && grounded && speed > 0.2 && damage_level > 0.28 && damage_spark_timer_ > 0.48) {
        emit({position.x+0.3,position.y+0.45},0xffa464,3+int(damage_level*3),0.62);
        damage_spark_timer_ = 0;
    }
    if (alive && grounded && std::isfinite(speed) && speed > 0.2) {
        servo_distance_ += speed*dt;
        if (servo_distance_ >= 0.58) {
            sound(SoundCue::ServoStep);
            servo_distance_ = std::fmod(servo_distance_,0.58);
        }
    } else servo_distance_ = 0;
    // An independent sequence keeps chatter from changing terrain or particle RNG.
    if (alive) {
        chatter_delay_ -= dt;
        if (chatter_delay_ <= 0 && grounded && landing <= 0 && !active_activity) {
            chatter_sequence_ = chatter_sequence_*1664525u+1013904223u;
            const auto variant = (chatter_sequence_>>16)%4;
            sound(static_cast<SoundCue>(static_cast<int>(SoundCue::RobotTalk)+variant));
            chatter_sequence_ = chatter_sequence_*1664525u+1013904223u;
            chatter_delay_ = 9+(chatter_sequence_>>8)%12000/1000.0;
        }
    }
    spark_timer_ += dt;
    if (alive && grounded && speed > 0.5) {
        // Distance-based paired puffs leave both track heels, opposite travel.
        // No screen/world southward gravity: this is a drifting surface cloud.
        dust_timer_ += planar ? speed*dt : dt;
        const double spacing = planar ? 0.18 : 0.09;
        while (dust_timer_ >= spacing) {
            dust_timer_ -= spacing;
            if (!planar) {
                emit({position.x+(facing > 0 ? 0 : 0.7),position.y+0.8},0x8b8176,2,0.4);
                continue;
            }
            const Vec2 back{-velocity.x/speed,-velocity.y/speed};
            for (int side : {-1,1}) {
                if (particles_.size() >= 160) break;
                sequence_ = sequence_*1664525u+1013904223u;
                const double spread = 0.12+((sequence_>>12)%100)/450.0;
                const double lag = std::min(dust_timer_,speed*dt);
                const Vec2 origin{position.x+0.35+back.x*(0.67+lag)-back.y*side*0.26,
                                  position.y+0.425+back.y*(0.67+lag)+back.x*side*0.26};
                particles_.push_back({origin,{back.x*0.48-back.y*side*spread,
                                             back.y*0.48+back.x*side*spread},
                    0,0.78+(sequence_%36)/100.0,side < 0 ? 0x928579U : 0x746c65U,2,true});
            }
        }
    } else dust_timer_ = 0;
    if (active_activity) {
        active_activity->age += dt;
        if (active_activity->age >= active_activity->duration) active_activity.reset();
    }
    for (auto& p : particles_) {
        p.age += dt;
        if (p.dust) {
            const double drag = std::exp(-dt*2.5);
            p.velocity.x *= drag; p.velocity.y *= drag;
        } else p.velocity.y += 3.5*dt;
        p.position.x += p.velocity.x*dt; p.position.y += p.velocity.y*dt;
    }
    particles_.erase(std::remove_if(particles_.begin(),particles_.end(),
        [](const Particle& p) { return p.age >= p.lifetime; }),particles_.end());
}
void Presentation::damage(Vec2 position,double amount) {
    damage_flash = 0.58;
    sound(SoundCue::Hit);
    body_bounce_velocity_ -= std::min(3.5,amount*0.16);
    head_pitch_velocity_ -= std::min(1.3,amount*0.055);
    emit({position.x+0.35,position.y+0.425},0xffa368,10+int(std::min(8.0,amount*0.3)),1.0);
    emit({position.x+0.35,position.y+0.425},0x687078,4,0.5);
}
void Presentation::face_target(Vec2 from, Vec2 target) {
    if (distance(from,target) < 0.001) return;
    head_goal_ = std::atan2(target.y-from.y,target.x-from.x); aim_timeout_ = 0.15;
}
void Presentation::mining(GridPos target, double progress, bool broke, std::uint32_t color) {
    mining_target = target;
    mining_progress = std::clamp(progress,0.0,1.0);
    const Vec2 point{target.x+0.5,target.y+0.5};
    if (spark_timer_ > 0.045) { emit(point,0xffd694,3,1.2); spark_timer_ = 0; }
    if (broke) { sound(SoundCue::Break); emit(point,color,18,1.8); emit(point,0xe8faff,7,1.2); }
}
void Presentation::activity(Vec2 position, std::string label, bool crafting, Item item, std::int64_t quantity) {
    sound(crafting ? SoundCue::Craft : quantity > 0 ? SoundCue::Pickup : SoundCue::Repair);
    active_activity = ActivityAnimation{position,std::move(label),0,1.6,crafting,item,quantity};
    emit(position,crafting ? 0x85e6d0 : 0xf1cc7d,20,1.4);
}
void Presentation::placement(GridPos position) {
    sound(SoundCue::Place);
    emit({position.x+0.5,position.y+0.5},0xb8a18a,10,0.7);
}
}

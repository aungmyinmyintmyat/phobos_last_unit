#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace horizon {
enum class SoundCue { Jump, Land, Break, Pickup, Craft, Repair, Place, Error, Interface, Launch,
                      Alarm, Hit, Charge, LowBattery, BiteCrack, CombatFire, CombatImpact,
                      ServoStep, RobotTalk, RobotTalkCurious, RobotTalkHappy, RobotTalkThinking, Count };
inline bool is_robot_talk(SoundCue cue) {
    return cue >= SoundCue::RobotTalk && cue <= SoundCue::RobotTalkThinking;
}
struct AudioSettings {
    double master = 0.85, effects = 1.0, machinery = 0.8, chatter = 0.7;
    bool muted = false;
    double gain(SoundCue cue) const {
        return muted ? 0 : master*(is_robot_talk(cue) ? chatter : effects);
    }
};
struct SoundEvent { std::uint64_t serial; SoundCue cue; };
// Deterministic, asset-free stereo synthesis. Only the SDL frontend owns an audio
// device; offline previews and simulation can use this without SDL or threads.
class SoundEffects {
public:
    static constexpr int sample_rate = 48000;
    void play(SoundCue cue, double gain = 1);
    void ambience(double motor, double mining, double factory, double storm, double launch);
    void render(std::int16_t* stereo, std::size_t frames);
    void reset();
private:
    struct Voice { SoundCue cue = SoundCue::Interface; double age = 1, duration = 0, phase = 0, gain = 1; };
    std::array<Voice,24> voices_{};
    std::array<double,5> targets_{}, levels_{};
    std::uint32_t random_ = 73;
    double clock_ = 0, filtered_noise_ = 0;
    double motor_phase_ = 0, servo_phase_ = 0, gear_phase_ = 0;
};
}

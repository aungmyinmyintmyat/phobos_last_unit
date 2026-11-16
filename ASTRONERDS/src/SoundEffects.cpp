#include "horizon/SoundEffects.hpp"
#include <algorithm>
#include <cmath>

namespace horizon {
namespace {
constexpr double tau = 6.283185307179586;
double wave(double cycles) { return std::sin(tau*cycles); }
}
void SoundEffects::reset() { *this = SoundEffects{}; }
void SoundEffects::play(SoundCue cue, double gain) {
    const int cue_index = static_cast<int>(cue);
    if (cue_index < 0 || cue_index >= static_cast<int>(SoundCue::Count)) return;
    auto voice = std::find_if(voices_.begin(),voices_.end(),[](const Voice& v) { return v.age >= v.duration; });
    if (voice == voices_.end()) voice = std::max_element(voices_.begin(),voices_.end(),
        [](const Voice& a,const Voice& b) { return a.age/a.duration < b.age/b.duration; });
    constexpr std::array<double,static_cast<std::size_t>(SoundCue::Count)> durations{{
        0.27,0.23,0.32,0.32,0.85,0.65,0.19,0.18,0.075,1.6,0.32,0.22,0.6,0.45,0.28,0.19,0.24,
        0.13,0.88,1.04,0.76,1.18}};
    *voice = {cue,0,durations[cue_index],0,std::isfinite(gain) ? std::clamp(gain,0.0,1.0) : 0};
}
void SoundEffects::ambience(double motor, double mining, double factory, double storm, double launch) {
    targets_ = {motor,mining,factory,storm,launch};
    for (auto& value : targets_) value = std::isfinite(value) ? std::clamp(value,0.0,1.0) : 0;
}
void SoundEffects::render(std::int16_t* stereo, std::size_t frames) {
    if (!stereo) return;
    constexpr double dt = 1.0/sample_rate;
    for (std::size_t i = 0; i < frames; ++i) {
        clock_ += dt;
        random_ = random_*1664525u+1013904223u;
        const double noise = double(random_>>8)/8388607.5-1;
        filtered_noise_ += (noise-filtered_noise_)*0.045;
        for (std::size_t j = 0; j < levels_.size(); ++j) levels_[j] += (targets_[j]-levels_[j])*0.0015;
        // Continuous phases let the geared motor change pitch smoothly with speed.
        motor_phase_ = std::fmod(motor_phase_+(62+levels_[0]*88)*dt,1.0);
        servo_phase_ = std::fmod(servo_phase_+(310+levels_[0]*520)*dt,1.0);
        gear_phase_ = std::fmod(gear_phase_+(2+levels_[0]*5)*dt,1.0);
        double sound = levels_[0]*(0.047*wave(motor_phase_)+0.020*wave(motor_phase_*2)+
            0.024*wave(servo_phase_+0.11*wave(motor_phase_))+filtered_noise_*0.13)*
            (0.78+0.22*wave(gear_phase_));
        sound += levels_[1]*(0.035*wave(clock_*430+0.7*wave(clock_*31))+noise*0.04);
        sound += levels_[2]*(0.027*wave(clock_*83)+0.018*wave(clock_*166))*(0.85+0.15*wave(clock_*3));
        sound += levels_[3]*(filtered_noise_*0.15+0.023*wave(clock_*520)*(0.5+0.5*wave(clock_*2)));
        sound += levels_[4]*(filtered_noise_*0.65+0.055*wave(clock_*47));
        for (auto& v : voices_) {
            if (v.age >= v.duration) continue;
            const double t = v.age/v.duration;
            double decay = (1-t)*(1-t);
            double frequency = 300, tone = 0;
            switch (v.cue) {
                case SoundCue::Jump: frequency = 170+530*t; tone = wave(v.phase)*0.15; break;
                case SoundCue::Land: frequency = 110-65*t; tone = wave(v.phase)*0.22+filtered_noise_*0.3; break;
                case SoundCue::Break: frequency = 160-120*t; tone = noise*0.14+wave(v.phase)*0.17; break;
                case SoundCue::Pickup: frequency = t < 0.45 ? 660 : 990; tone = wave(v.phase)*0.12; break;
                case SoundCue::Craft: frequency = t < 0.33 ? 392 : t < 0.66 ? 523.25 : 783.99; tone = (wave(v.phase)+0.3*wave(v.phase*2))*0.13; break;
                case SoundCue::Repair: frequency = 220+440*t; tone = wave(v.phase)*0.11+noise*0.06*(1-t); break;
                case SoundCue::Place: frequency = 130-60*t; tone = wave(v.phase)*0.2+noise*0.05; break;
                case SoundCue::Error: frequency = t < 0.5 ? 180 : 135; tone = wave(v.phase)*0.11; break;
                case SoundCue::Interface: frequency = 880; tone = wave(v.phase)*0.075; break;
                case SoundCue::Launch: frequency = 55+40*t; tone = filtered_noise_*0.7+wave(v.phase)*0.14; break;
                case SoundCue::Alarm: frequency = t < 0.5 ? 740 : 1040; tone = wave(v.phase)*0.16; break;
                case SoundCue::Hit: frequency = 150-100*t; tone = wave(v.phase)*0.19+noise*0.13; break;
                case SoundCue::Charge: frequency = t < 0.33 ? 440 : t < 0.66 ? 660 : 880; tone = wave(v.phase)*0.1; break;
                case SoundCue::LowBattery: frequency = t < 0.35 ? 520 : t < 0.7 ? 390 : 260; tone = wave(v.phase)*0.13; break;
                case SoundCue::BiteCrack: {
                    const double crunch = std::exp(-std::fmod(v.age,0.048)*110);
                    frequency = 96-54*t;
                    tone = noise*0.47*crunch+filtered_noise_*0.33+wave(v.phase)*0.23;
                    break;
                }
                case SoundCue::CombatFire:
                    frequency = 1160-770*t+45*wave(t*17);
                    tone = wave(v.phase)*0.25+wave(v.phase*2)*0.08+noise*0.035;
                    break;
                case SoundCue::CombatImpact:
                    frequency = 420-250*t;
                    tone = noise*0.31+wave(v.phase)*0.17+filtered_noise_*0.16;
                    break;
                case SoundCue::ServoStep:
                    frequency = 460-250*t;
                    tone = wave(v.phase)*0.065+wave(v.phase*2.73)*0.025+filtered_noise_*0.10;
                    break;
                case SoundCue::RobotTalk: case SoundCue::RobotTalkCurious:
                case SoundCue::RobotTalkHappy: case SoundCue::RobotTalkThinking: {
                    // Speech-like electronic syllables: four distinct pitch contours,
                    // little pauses and a metallic formant instead of a repeated beep.
                    constexpr double notes[4][6] = {
                        {210,310,245,365,285,200}, {190,230,280,245,320,410},
                        {280,350,420,315,395,330}, {220,175,260,190,235,165}};
                    const int variant = static_cast<int>(v.cue)-static_cast<int>(SoundCue::RobotTalk);
                    const double syllables = t*6;
                    const int syllable = std::min(5,int(syllables));
                    const double phase = syllables-syllable;
                    const double gate = phase < 0.80 ? std::pow(std::sin(phase/0.80*3.1415926536),2) : 0;
                    frequency = notes[variant][syllable]*(1+0.10*wave(phase*0.75+variant*0.13));
                    tone = gate*(wave(v.phase+0.04*wave(v.age*37))*0.13+
                        wave(v.phase*2)*0.043+wave(v.phase*5)*0.018+filtered_noise_*0.025);
                    decay = std::min(1.0,(1-t)/0.10);
                    break;
                }
                case SoundCue::Count: break;
            }
            // Attack and decay prevent clicks, including when frequencies change.
            sound += tone*std::min(1.0,v.age/0.008)*decay*v.gain;
            v.phase = std::fmod(v.phase+frequency*dt,1.0); v.age += dt;
        }
        const auto sample = std::int16_t(std::tanh(sound*1.3)*26000);
        stereo[i*2] = sample;
        stereo[i*2+1] = sample;
    }
}
}

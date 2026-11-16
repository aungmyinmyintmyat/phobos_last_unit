#include "horizon/PhobosClock.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace horizon {
PhobosClock::PhobosClock(double starting_phase) {
    if (!std::isfinite(starting_phase)) starting_phase = default_starting_phase;
    starting_phase = std::fmod(starting_phase,1.0);
    if (starting_phase < 0) starting_phase += 1.0;
    cycle_seconds_ = std::min(starting_phase*gameplay_day_seconds,std::nextafter(gameplay_day_seconds,0.0));
}
void PhobosClock::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    const long double total = static_cast<long double>(cycle_seconds_)+seconds;
    const long double days = std::floor(total/gameplay_day_seconds);
    const auto available = std::numeric_limits<std::uint64_t>::max()-day_;
    if (days >= static_cast<long double>(available)) day_ = std::numeric_limits<std::uint64_t>::max();
    else day_ += static_cast<std::uint64_t>(days);
    cycle_seconds_ = std::min(static_cast<double>(std::fmod(total,static_cast<long double>(gameplay_day_seconds))),
                              std::nextafter(gameplay_day_seconds,0.0));
}
double PhobosClock::phase() const { return cycle_seconds_/gameplay_day_seconds; }
double PhobosClock::sunlight() const {
    constexpr double tau = 6.2831853071795864769;
    const double light = std::cos(tau*(phase()-0.5));
    return light < 1e-12 ? 0.0 : std::min(1.0,light);
}
bool PhobosClock::night() const { return phase() < 0.25 || phase() >= 0.75; }
double PhobosClock::seconds_to_transition() const {
    const double p = phase();
    return ((p < 0.25 ? 0.25 : p < 0.75 ? 0.75 : 1.25)-p)*gameplay_day_seconds;
}
std::string PhobosClock::local_time() const {
    const int seconds = std::clamp(int(std::floor(phase()*86400.0+1e-8)),0,86399);
    std::string time = "00:00:00";
    const auto write_pair = [&](int at, int value) {
        time[at] = char('0'+value/10); time[at+1] = char('0'+value%10);
    };
    write_pair(0,seconds/3600); write_pair(3,(seconds/60)%60); write_pair(6,seconds%60);
    return time;
}
std::uint64_t PhobosClock::day_number() const { return day_; }
}

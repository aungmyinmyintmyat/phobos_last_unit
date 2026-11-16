#pragma once
#include <cstdint>
#include <string>

namespace horizon {
class PhobosClock {
public:
    // NASA NSSDCA Mars Fact Sheet: Phobos sidereal rotation/orbit 0.31891
    // Earth days, Mars sidereal year 686.980 Earth days. Uniform prograde model:
    // 1 / mean solar day = 1 / rotation - 1 / Mars year.
    // A mean local clock, without an absolute epoch, libration or Mars eclipses.
    static constexpr double rotation_days = 0.31891, mars_year_days = 686.980;
    static constexpr double solar_day_seconds = 86400.0/(1.0/rotation_days-1.0/mars_year_days);
    static constexpr double time_scale = 60.0;
    static constexpr double gameplay_day_seconds = solar_day_seconds/time_scale;
    static constexpr double default_starting_phase = 0.5;
    explicit PhobosClock(double starting_phase = default_starting_phase);
    void advance(double seconds);
    double phase() const;
    double sunlight() const;
    bool night() const;
    double seconds_to_transition() const;
    std::string local_time() const;
    std::uint64_t day_number() const;
private:
    double cycle_seconds_ = 0;
    std::uint64_t day_ = 1;
};
}

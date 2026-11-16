#include "horizon/RobotEyes.hpp"
#include "horizon/Game.hpp"
#include "horizon/PlanetTerrain.hpp"
#include <algorithm>

namespace horizon {
Vec2 RobotEyePose::point(double across, double forward, double height, double& z) const {
    const double f = forward*std::cos(pitch)-(height-eye_height)*std::sin(pitch);
    z = elevation+eye_height+forward*std::sin(pitch)+(height-eye_height)*std::cos(pitch);
    return {center.x+std::sin(heading)*across+std::cos(heading)*f,
            center.y-std::cos(heading)*across+std::sin(heading)*f};
}
RobotEyePose RobotEyes::pose(const Game& game, RobotModel model) {
    const auto& unit = game.unit(); const auto& animation = game.presentation();
    const auto p = unit.center();
    const bool classic = model == RobotModel::Mark1;
    const bool moving = unit.grounded() && std::hypot(unit.velocity().x,unit.velocity().y) > 0.2;
    const double bob = moving ? 0 :
        classic && unit.alive() ? std::sin(animation.clock*2.4)*0.004 : 0;
    RobotEyePose rig;
    rig.heading = animation.head_heading; rig.pitch = animation.head_pitch;
    rig.center = classic ? Vec2{p.x-std::cos(animation.heading)*0.03,p.y-std::sin(animation.heading)*0.03} :
        Vec2{p.x+std::cos(rig.heading)*0.015,p.y+std::sin(rig.heading)*0.015};
    rig.center.x += std::cos(rig.heading)*animation.collapse*0.12;
    rig.center.y += std::sin(rig.heading)*animation.collapse*0.12;
    const double ground = std::isfinite(animation.visual_elevation) ? animation.visual_elevation :
        unit.elevation()+(game.world().continuous_ground() ? 0 : PlanetTerrain::surface_height(game.world(),p)-game.world().elevation(int(p.x),int(p.y)));
    rig.elevation = ground+animation.head_bounce+bob;
    rig.eye_height = classic ? 0.9 : 0.91;
    for (int i = 0; i < 2; ++i) {
        auto& eye = rig.emitters[i];
        eye.position = rig.point((i ? 1 : -1)*(classic ? 0.16 : 0.115),classic ? 0.18 : 0.161,
                                 rig.eye_height,eye.elevation);
        eye.forward = {std::cos(rig.heading),std::sin(rig.heading)}; eye.pitch = rig.pitch;
        eye.intensity = unit.alive() ? animation.eye_glow()*animation.eye_openness() : 0;
    }
    return rig;
}
double RobotEyes::irradiance(const EyeEmitter& eye, Vec2 point, double elevation) {
    const double dx = point.x-eye.position.x, dy = point.y-eye.position.y, dz = elevation-eye.elevation;
    const double length_squared = dx*dx+dy*dy+dz*dz;
    if (length_squared < 0.0001 || length_squared >= 90.25 || eye.intensity <= 0) return 0;
    const double length = std::sqrt(length_squared);
    const double along = (dx*eye.forward.x+dy*eye.forward.y)*std::cos(eye.pitch)+dz*std::sin(eye.pitch);
    const double cosine = std::clamp(along/length,-1.0,1.0);
    // A lens has a focused center and a faint, broad spill rather than a solid
    // disk. Cosine-space Gaussians stay smooth as the head pitches through a
    // jump; the outer envelope fades both lobes to zero without a hard rim.
    const double angle = std::max(0.0,1-cosine);
    // const double edge = std::clamp((cosine-std::cos(0.70))/(1-std::cos(0.70)),0.0,1.0);
    const double edge = std::clamp((cosine-std::cos(1.0))/(1-std::cos(1.0)),0.0,1.0);
    const double envelope = edge*edge*(3-2*edge);
    // const double core = std::exp(-angle/0.037), spill = std::exp(-angle/0.12);
    const double core = std::exp(-angle/0.08), spill = std::exp(-angle/0.28);
    const double range = 1-std::pow(length/9.5,4);
    return eye.intensity*(core*0.76+spill*0.24)*envelope*range*range/
        (1+length_squared*0.10)*0.78;
}
}

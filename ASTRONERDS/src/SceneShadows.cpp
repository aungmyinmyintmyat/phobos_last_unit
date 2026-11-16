#include "horizon/SceneShadows.hpp"
#include "horizon/Game.hpp"
#include "horizon/PlanetProjection.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace horizon {
namespace {
double smooth(double a, double b, double x) {
    const double t = std::clamp((x-a)/(b-a),0.0,1.0);
    return t*t*(3-2*t);
}
}
double SceneShadows::opacity_at(Vec2 point, double receiver_height, const ShadowCaster& c) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(receiver_height) ||
        !std::isfinite(c.position.x) || !std::isfinite(c.position.y) || !std::isfinite(c.ground_height) ||
        !std::isfinite(c.height) || !std::isfinite(c.lift) || !std::isfinite(c.radius_x) ||
        !std::isfinite(c.radius_y) || !std::isfinite(c.strength) || !std::isfinite(c.heading) ||
        !std::isfinite(c.direction.x) || !std::isfinite(c.direction.y) || c.height <= 0 || c.radius_x <= 0 || c.radius_y <= 0) return 0;
    const double bottom = c.ground_height+std::max(0.0,c.lift)-receiver_height;
    const double upper = bottom+c.height;
    if (upper <= 0) return 0;
    const Vec2 start{c.position.x+c.direction.x*std::max(0.0,bottom),c.position.y+c.direction.y*std::max(0.0,bottom)};
    const Vec2 end{c.position.x+c.direction.x*upper,c.position.y+c.direction.y*upper};
    const Vec2 axis{end.x-start.x,end.y-start.y};
    const double length2 = axis.x*axis.x+axis.y*axis.y;
    const double t = length2 > 1e-8 ? std::clamp(((point.x-start.x)*axis.x+(point.y-start.y)*axis.y)/length2,0.0,1.0) : 0;
    const Vec2 center{start.x+axis.x*t,start.y+axis.y*t};
    // Penumbra grows with separation from the receiver, while the contact patch
    // stays tight and dark. Raised objects cast a displaced, softer shadow.
    const double separation = std::max(0.0,bottom)+c.height*t;
    const double softness = 0.08+separation*0.07;
    const double cs=std::cos(c.heading), sn=std::sin(c.heading);
    const double px=point.x-center.x, py=point.y-center.y;
    const double dx=(px*cs+py*sn)/c.radius_x, dy=(-px*sn+py*cs)/c.radius_y;
    const double radial = std::sqrt(dx*dx+dy*dy);
    const double penumbra = softness/std::min(c.radius_x,c.radius_y);
    const double cx=point.x-c.position.x, cy=point.y-c.position.y;
    const double contact_x=(cx*cs+cy*sn)/(c.radius_x*0.85);
    const double contact_y=(-cx*sn+cy*cs)/(c.radius_y*0.85);
    const double contact_distance2 = contact_x*contact_x+contact_y*contact_y;
    if (radial >= 1+penumbra && (bottom > 2 || contact_distance2 > 4.5)) return 0;
    const double body = (1-smooth(std::max(0.05,0.78-penumbra*0.25),1+penumbra,radial))*
        std::clamp(c.strength,0.0,0.65)/(1+std::max(0.0,bottom)*0.07);
    const double contact = bottom > 2 || contact_distance2 > 4.5 ? 0 :
        std::exp(-contact_distance2*2.2)*0.34*std::exp(-std::max(0.0,bottom)*3.5);
    return std::clamp(1-(1-body)*(1-contact),0.0,0.6);
}
std::array<ShadowCaster,4> SceneShadows::robot_casters(const Game& game) {
    const auto p=game.unit().center();const auto& pose=game.presentation();
    const double heading=pose.heading;
    const Vec2 right{std::sin(heading),-std::cos(heading)};
    const double ground=game.unit().ground_elevation();
    const double z=std::isfinite(pose.visual_elevation) ? pose.visual_elevation : game.unit().elevation();
    const double lift=std::max(0.0,z-ground);
    const Vec2 direction=game.in_spaceship() ? Vec2{.18,.10} : sun_shadow;
    // Separate tread contacts, chassis and head keep the silhouette aligned
    // with steering. Elevated pieces cast softer, displaced shadows.
    return {{{{p.x-right.x*.235,p.y-right.y*.235},ground,.22,.105,.35,lift+.02,.25,heading-1.5707963267948966,direction},
             {{p.x+right.x*.235,p.y+right.y*.235},ground,.22,.105,.35,lift+.02,.25,heading-1.5707963267948966,direction},
             {p,ground,.38,.25,.30,lift+.25,.28,heading-1.5707963267948966,direction},
             {p,ground,.36,.24,.22,lift+.65,.23,pose.head_heading-1.5707963267948966,direction}}};
}
bool SceneShadows::apply(std::vector<std::uint32_t>& pixels, const std::vector<GridPos>& ids,
                         const Game& game, const PlanetProjection& projection) {
    constexpr int width = 640, height = 360, top = 0, bottom = 360;
    if (pixels.size() != width*height || ids.size() != pixels.size()) return false;
    const auto& world = game.world();
    const auto robot = game.unit().center(), station = game.refinery().position(), ship = game.rocket().ground_socket();
    std::vector<ShadowCaster> casters;
    double lowest_receiver = std::numeric_limits<double>::infinity(), highest_receiver = -lowest_receiver;
    for (int y = 0; y < world.height(); ++y) for (int x = 0; x < world.width(); ++x) {
        lowest_receiver = std::min(lowest_receiver,world.elevation(x,y));
        highest_receiver = std::max(highest_receiver,world.elevation(x,y));
        if (!world.solid(x,y) || (x == 8 && (y == 9 || y == 10)) ||
            (x == world.width()-14 && (y == 11 || y == 12))) continue;
        const Vec2 p{x+0.5,y+0.5};
        if ((x == 0 || y == 0 || x == world.width()-1 || y == world.height()-1) && distance(p,robot) > 7) continue;
        double object_height = WorldGrid::obstacle_height(world.get_tile_type(x,y)), radius = 0.42;
        if (BaseCamp::wall(x,y)) { object_height = 0.92; radius = 0.48; }
        else if (BaseCamp::fixture(x,y) != CampFixture::None) {
            const auto fixture = BaseCamp::fixture(x,y);
            object_height = fixture == CampFixture::Bunk ? 0.55 : fixture == CampFixture::Workbench ? 0.85 : 1.25;
        }
        casters.push_back({p,world.elevation(x,y),object_height,radius,radius,0,0.28});
    }
    const double station_ground = world.elevation(int(station.x),int(station.y));
    casters.push_back({station,station_ground,1.35,0.68,0.54,0,0.32});
    casters.push_back({{station.x-0.33,station.y-0.08},station_ground,1.38,0.24,0.25,0,0.3});
    for (auto solar : BaseCamp::solar)
        casters.push_back({solar,world.elevation(int(solar.x),int(solar.y)),0.08,0.57,0.42,0.7,0.24});
    casters.push_back({BaseCamp::center,BaseCamp::floor_elevation,0.5,4.75,2.80,2.3,0.24});
    casters.push_back({{9.45,1.78},BaseCamp::floor_elevation,0.62,1.95,0.62,0.65,0.18});
    casters.push_back({{12.0,9.90},BaseCamp::floor_elevation,0.55,1.30,0.85,0.60,0.18});
    if (!game.unit().fell_into_pit())
        for (const auto& caster:robot_casters(game)) casters.push_back(caster);
    const double rocket_ground = world.elevation(int(ship.x),int(ship.y));
    const double rocket_lift = game.rocket().launched() ? std::pow(game.climax().launch_elapsed(),2)*0.6 : 0;
    casters.push_back({ship,rocket_ground,0.2,0.86,0.78,0,0.22});
    casters.push_back({{ship.x-0.79,ship.y-0.64},rocket_ground+0.18,2.95,0.09,0.1,0,0.28});
    casters.push_back({ship,rocket_ground,5.3,0.48,0.48,0.35+rocket_lift,0.32});
    // A half-resolution mask is bilinearly reconstructed below. Soft penumbrae
    // need smooth coverage rather than full-resolution per-caster ray sampling.
    constexpr int step = 2, mask_width = width/step, mask_height = (bottom-top)/step;
    std::vector<float> opacity(mask_width*mask_height,0);
    std::vector<Vec2> ground_points(opacity.size(),{std::numeric_limits<double>::quiet_NaN(),0});
    //const double sunlight = game.survival_enabled() ? game.phobos().sunlight() : 1;
    const double sunlight = (game.survival_enabled() && !game.in_spaceship())
    ? game.phobos().sunlight()
    : 1;
    for (auto caster : casters) {
        caster.strength *= sunlight;
        const auto source = projection.project(caster.position,caster.ground_height);
        if (!source.visible || source.depth < 0.06) continue;
        const double spread = 0.22+(caster.height+caster.lift)*0.1;
        const double reach = caster.height+caster.lift+std::max(0.0,caster.ground_height-lowest_receiver);
        const double cs=std::cos(caster.heading), sn=std::sin(caster.heading);
        const double rx=std::hypot(caster.radius_x*cs,caster.radius_y*sn)+spread;
        const double ry=std::hypot(caster.radius_x*sn,caster.radius_y*cs)+spread;
        const double x0=caster.position.x-rx+std::min(0.0,caster.direction.x*reach);
        const double x1=caster.position.x+rx+std::max(0.0,caster.direction.x*reach);
        const double y0=caster.position.y-ry+std::min(0.0,caster.direction.y*reach);
        const double y1=caster.position.y+ry+std::max(0.0,caster.direction.y*reach);
        int left = width, right = -1, first = bottom, last = top-1;
        for (double x : {x0,x1}) for (double y : {y0,y1})
            for (double z : {lowest_receiver-0.1,highest_receiver+0.1}) {
                const auto q = projection.project({x,y},z);
                if (!q.visible) continue;
                left = std::min(left,int(std::floor(q.screen.x))-2); right = std::max(right,int(std::ceil(q.screen.x))+2);
                first = std::min(first,int(std::floor(q.screen.y))-2); last = std::max(last,int(std::ceil(q.screen.y))+2);
            }
        for (int my = std::max(0,(first-top)/step); my <= std::min(mask_height-1,(last-top+1)/step); ++my)
            for (int mx = std::max(0,left/step); mx <= std::min(mask_width-1,(right+1)/step); ++mx) {
                const int x = mx*step+1, y = top+my*step+1;
                const auto index = std::size_t(my)*mask_width+mx; const auto id = ids[y*width+x];
                if (!world.in_bounds(id.x,id.y)) continue;
                auto& point = ground_points[index]; const double z = world.elevation(id.x,id.y);
                if (!std::isfinite(point.x)) {
                    const auto inverse = projection.unproject({x+0.5,y+0.5},z);
                    if (!inverse) continue;
                    point = {std::clamp(inverse->x,id.x-0.1,id.x+1.1),std::clamp(inverse->y,id.y-0.1,id.y+1.1)};
                }
                opacity[index] = std::max(opacity[index],float(opacity_at(point,z,caster)));
            }
    }
    for (int y = top; y < bottom; ++y) for (int x = 0; x < width; ++x) {
        const auto i = std::size_t(y)*width+x; const auto id = ids[i];
        if (!world.in_bounds(id.x,id.y)) continue;
        const int ix = x == 0 ? -1 : (x-1)/step, iy = y == top ? -1 : (y-top-1)/step;
        const double fx = x%step == 0 ? 0.5 : 0, fy = (y-top)%step == 0 ? 0.5 : 0;
        const auto sample = [&](int sx,int sy) { return opacity[std::clamp(sy,0,mask_height-1)*mask_width+std::clamp(sx,0,mask_width-1)]; };
        const double amount = sample(ix,iy)*(1-fx)*(1-fy)+sample(ix+1,iy)*fx*(1-fy)+
                              sample(ix,iy+1)*(1-fx)*fy+sample(ix+1,iy+1)*fx*fy;
        if (amount <= 0.001) continue;
        const auto original = pixels[i];
        // Cool ambient light remains in the shadow instead of crushing to black.
        const auto channel = [&](int shift,double gain) { return unsigned(((original>>shift)&255)*(1-amount*gain)); };
        pixels[i] = 0xff000000|(channel(16,0.95)<<16)|(channel(8,0.87)<<8)|channel(0,0.72);
    }
    return true;
}
}

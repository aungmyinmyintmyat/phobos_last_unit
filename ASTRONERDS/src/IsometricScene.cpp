#include "horizon/IsometricScene.hpp"
#include "horizon/Game.hpp"
#include "horizon/SpaceBackground.hpp"
#include "horizon/PlanetTerrain.hpp"
#include "horizon/SceneLighting.hpp"
#include "horizon/RobotEyes.hpp"
#include "horizon/SceneShadows.hpp"
#include "horizon/BaseCamp.hpp"
#include <algorithm>
#include <array>

namespace horizon {
namespace {
constexpr int width = 640, height = 360, top = 0, bottom = 360;
double corner_height(const WorldGrid& world, int x, int y) {
    double sum = 0; int count = 0;
    for (int dy : {-1,0}) for (int dx : {-1,0})
        if (world.in_bounds(x+dx,y+dy)) { sum += world.elevation(x+dx,y+dy); ++count; }
    return count ? sum/count : 0;
}
bool cliff_cell(const WorldGrid& world, int x, int y) {
    (void)world; (void)x; (void)y;
    return false;
}
double surface_height(const WorldGrid& world, Vec2 p) {
    return PlanetTerrain::surface_height(world,p);
}
std::uint32_t tint(std::uint32_t color, double amount) {
    auto c = [&](int shift) { return unsigned(std::clamp(((color>>shift)&255)*amount,0.0,255.0)); };
    return (c(16)<<16)|(c(8)<<8)|c(0);
}
std::uint32_t shelter_dust(double x,double y,std::uint32_t seed) {
    // Shared world-space samples form one continuous layer of swept regolith.
    const double broad = PlanetTerrain::noise(x*0.24,y*0.24,seed+271);
    const double fine = PlanetTerrain::noise(x*0.85,y*0.85,seed+307);
    return tint(0xb3a080,0.92+broad*0.08+fine*0.025);
}
void draw_planet_shell(std::vector<std::uint32_t>& pixels, const PlanetProjection& projection, const WorldGrid& world) {
    constexpr int block = 5;
    const double radius = PlanetProjection::radius, cy = PlanetProjection::center_y();
    for (int y = top; y < bottom; y += block) for (int x = 0; x < width; x += block) {
        const Vec2 center{x+block*0.5,y+block*0.5};
        const auto point = projection.unproject(center);
        std::uint32_t color = 0x777063;
        if (point && (projection.curved || !world.in_bounds(int(std::floor(point->x)),int(std::floor(point->y)))))
            color = PlanetTerrain::material(point->x,point->y,world.seed(),world.width(),world.height());
        if (!projection.curved) {
            for (int py = y; py < std::min(bottom,y+block); ++py)
                for (int px = x; px < std::min(width,x+block); ++px) pixels[py*width+px] = 0xff000000|tint(color,0.68);
            continue;
        }
        const double nx = (center.x-PlanetProjection::center_x)/radius, ny = (center.y-cy)/radius;
        const double nz = std::sqrt(std::max(0.0,1-nx*nx-ny*ny));
        color = tint(color,0.48+0.45*std::max(0.0,-nx*0.4-ny*0.6+nz*0.65));
        const double haze = std::pow(1-nz,3)*0.28;
        std::uint32_t body = 0;
        for (int shift : {0,8,16}) body |= unsigned(((color>>shift)&255)*(1-haze)+((0x82969cu>>shift)&255)*haze)<<shift;
        for (int py = y; py < std::min(bottom,y+block); ++py) for (int px = x; px < std::min(width,x+block); ++px) {
            const double dx = px-PlanetProjection::center_x, dy = py-cy, r2 = dx*dx+dy*dy;
            if (r2 <= radius*radius) pixels[py*width+px] = 0xff000000|body;
            else if (r2 < (radius+2)*(radius+2)) {
                const auto old = pixels[py*width+px];
                const double alpha = (1-(std::sqrt(r2)-radius)/2)*0.2;
                std::uint32_t rim = 0;
                for (int shift : {0,8,16}) rim |= unsigned(((old>>shift)&255)*(1-alpha)+((0x8ea6adu>>shift)&255)*alpha)<<shift;
                pixels[py*width+px] = 0xff000000|rim;
            }
        }
    }
}
struct Canvas {
    std::vector<std::uint32_t>& pixels;
    std::vector<GridPos>& ids;
    const PlanetProjection& projection;
    struct Glow { std::size_t pixel; std::uint32_t color; };
    std::vector<Glow> glows{};
    bool hull_space = false;
    struct ObjectMask {
        std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(width*height,0);
        int left = width, right = -1, first = height, last = -1;
    };
    ObjectMask* object_mask = nullptr;
    Vec2 object_origin{};
    double object_angle=0;
    Vec2 object_point(Vec2 p) const {
        const double cs=std::cos(object_angle),sn=std::sin(object_angle);
        const double x=p.x-object_origin.x,y=p.y-object_origin.y;
        return {object_origin.x+x*cs-y*sn,object_origin.y+x*sn+y*cs};
    }
    Vec2 project(Vec2 p, double z) const {
        if(object_angle!=0) p=object_point(p);
        if (hull_space) z += BaseCamp::floor_elevation+(p.x-8)*0.075;
        return projection.project(p,z).screen;
    }
    void pixel(int x, int y, std::uint32_t color, GridPos id = {-1,-1}) {
        if (x < 0 || x >= width || y < top || y >= bottom) return;
        pixels[y*width+x] = 0xff000000|color;
        if (id.x >= 0) ids[y*width+x] = id;
        if (object_mask) {
            object_mask->pixels[y*width+x] = 1;
            object_mask->left = std::min(object_mask->left,x);
            object_mask->right = std::max(object_mask->right,x);
            object_mask->first = std::min(object_mask->first,y);
            object_mask->last = std::max(object_mask->last,y);
        }
    }
    void rect(int x, int y, int w, int h, std::uint32_t color) {
        for (int row = std::max(top,y); row < std::min(bottom,y+h); ++row)
            for (int col = std::max(0,x); col < std::min(width,x+w); ++col) pixel(col,row,color);
    }
    void emissive_rect(int x,int y,int w,int h,std::uint32_t color) {
        for (int py = std::max(top,y); py < std::min(bottom,y+h); ++py)
            for (int px = std::max(0,x); px < std::min(width,x+w); ++px) {
                pixel(px,py,color); glows.push_back({std::size_t(py)*width+px,0xff000000|color});
            }
    }
    void sensor_eye(Vec2 center, int w, int h, double openness, std::uint32_t color, double intensity, bool alive) {
        w += 1; h += 1; // Larger expressive cartoon optics belong only to the robot.
        const int x = int(std::lround(center.x)), y = int(std::lround(center.y));
        if (!alive || openness < 0.08) {
            rect(x-w/2,y,w,1,0x253b4b);
            return;
        }
        // Local blue bloom is drawn with the head and obeys foreground occlusion.
        const double radius = w*0.5+1.2;
        for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
            const int px = x+dx, py = y+dy;
            if (px < 0 || px >= width || py < top || py >= bottom) continue;
            const double falloff = std::max(0.0,1-std::hypot(dx,dy*1.2)/radius);
            const double alpha = falloff*falloff*0.24*openness*intensity;
            if (alpha <= 0) continue;
            const auto old = pixels[py*width+px]; std::uint32_t glow = 0;
            for (int shift : {0,8,16})
                glow |= unsigned(((old>>shift)&255)*(1-alpha)+((color>>shift)&255)*alpha)<<shift;
            emissive_rect(px,py,1,1,glow);
        }
        const int lid_height = std::max(1,int(std::lround(h*openness)));
        emissive_rect(x-w/2,y-lid_height/2,w,lid_height,tint(color,intensity));
        if (openness > 0.65) {
            const auto red = ((color>>16)&255), green = ((color>>8)&255), blue = color&255;
            const auto highlight = (unsigned(red+(255-red)*0.65)<<16)|
                (unsigned(green+(255-green)*0.65)<<8)|unsigned(blue+(255-blue)*0.65);
            emissive_rect(x,y,1,1,highlight);
        }
    }
    void line(Vec2 a, Vec2 b, std::uint32_t color) {
        const int steps = std::max(1,int(std::max(std::abs(a.x-b.x),std::abs(a.y-b.y))));
        for (int i = 0; i <= steps; ++i) pixel(int(a.x+(b.x-a.x)*i/steps),int(a.y+(b.y-a.y)*i/steps),color);
    }
    void triangle(Vec2 a, Vec2 b, Vec2 c, std::uint32_t color, GridPos id = {-1,-1}) {
        auto edge = [](Vec2 p, Vec2 q, Vec2 r) { return (r.x-p.x)*(q.y-p.y)-(r.y-p.y)*(q.x-p.x); };
        const double area = edge(a,b,c); if (std::abs(area) < 0.01) return;
        const int left = std::max(0,int(std::floor(std::min({a.x,b.x,c.x}))));
        const int right = std::min(width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
        const int first = std::max(top,int(std::floor(std::min({a.y,b.y,c.y}))));
        const int last = std::min(bottom-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
        for (int y = first; y <= last; ++y) for (int x = left; x <= right; ++x) {
            const Vec2 p{x+0.5,y+0.5};
            if (edge(a,b,p)/area >= -0.0001 && edge(b,c,p)/area >= -0.0001 && edge(c,a,p)/area >= -0.0001)
                pixel(x,y,color,id);
        }
    }
    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, std::uint32_t color, GridPos id = {-1,-1}) {
        triangle(a,b,c,color,id); triangle(a,c,d,color,id);
    }
    void emissive_quad(Vec2 a,Vec2 b,Vec2 c,Vec2 d,std::uint32_t color) {
        quad(a,b,c,d,color);int left=width,right=0,first=bottom,last=top;
        for(const auto p:{a,b,c,d}) {
            left=std::min(left,int(std::floor(p.x)));right=std::max(right,int(std::ceil(p.x)));
            first=std::min(first,int(std::floor(p.y)));last=std::max(last,int(std::ceil(p.y)));
        }
        for(int y=std::max(top,first);y<=std::min(bottom-1,last);++y)
            for(int x=std::max(0,left);x<=std::min(width-1,right);++x)
                if(pixels[y*width+x]==(0xff000000|color)) glows.push_back({std::size_t(y)*width+x,0xff000000|color});
    }
    void emissive_line(Vec2 a,Vec2 b,std::uint32_t color) {
        const int n=std::max(1,int(std::max(std::abs(a.x-b.x),std::abs(a.y-b.y))));
        for(int i=0;i<=n;++i) emissive_rect(int(a.x+(b.x-a.x)*i/n),int(a.y+(b.y-a.y)*i/n),1,1,color);
    }
    template<std::size_t N>
    void blend_polygon(const std::array<Vec2,N>& points, std::uint32_t color, double alpha) {
        alpha = std::clamp(alpha,0.0,1.0); if (alpha <= 0) return;
        double minx = points[0].x, maxx = minx, miny = points[0].y, maxy = miny;
        for (auto p : points) { minx = std::min(minx,p.x); maxx = std::max(maxx,p.x); miny = std::min(miny,p.y); maxy = std::max(maxy,p.y); }
        const auto edge = [](Vec2 a,Vec2 b,Vec2 p) { return (p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x); };
        const auto inside = [&](Vec2 p,Vec2 a,Vec2 b,Vec2 c) {
            const double area = edge(a,b,c);
            return std::abs(area) >= 0.001 && edge(a,b,p)/area >= 0 && edge(b,c,p)/area >= 0 && edge(c,a,p)/area >= 0;
        };
        for (int y = std::max(top,int(std::floor(miny))); y <= std::min(bottom-1,int(std::ceil(maxy))); ++y)
            for (int x = std::max(0,int(std::floor(minx))); x <= std::min(width-1,int(std::ceil(maxx))); ++x) {
                const Vec2 p{x+0.5,y+0.5}; bool covered = false;
                for (std::size_t i = 1; i+1 < N; ++i) covered = covered || inside(p,points[0],points[i],points[i+1]);
                if (!covered) continue;
                const auto old = pixels[y*width+x]; std::uint32_t blended = 0;
                for (int shift : {0,8,16}) blended |= unsigned(((old>>shift)&255)*(1-alpha)+((color>>shift)&255)*alpha)<<shift;
                pixels[y*width+x] = 0xff000000|blended; // Visible-face IDs stay untouched.
            }
    }
    void blend_triangle(Vec2 a, Vec2 b, Vec2 c, std::uint32_t color, double alpha) {
        blend_polygon(std::array<Vec2,3>{{a,b,c}},color,alpha);
    }
    void blend_quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, std::uint32_t color, double alpha) {
        blend_polygon(std::array<Vec2,4>{{a,b,c,d}},color,alpha);
    }
    void terrain_triangle(Vec2 a, Vec2 b, Vec2 c, std::uint32_t ca, std::uint32_t cb, std::uint32_t cc, GridPos id) {
        const auto edge = [](Vec2 p,Vec2 q,Vec2 r) { return (r.x-p.x)*(q.y-p.y)-(r.y-p.y)*(q.x-p.x); };
        const double area = edge(a,b,c); if (std::abs(area) < 0.01) return;
        const int left = std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))), right = std::min(width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
        const int first = std::max(top,int(std::floor(std::min({a.y,b.y,c.y})))), last = std::min(bottom-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
        for (int y = first; y <= last; ++y) for (int x = left; x <= right; ++x) {
            const Vec2 p{x+0.5,y+0.5};
            const double wc = edge(a,b,p)/area, wa = edge(b,c,p)/area, wb = 1-wa-wc;
            if (wa < -0.0001 || wb < -0.0001 || wc < -0.0001) continue;
            std::uint32_t color = 0;
            for (int shift : {0,8,16}) color |= unsigned(std::clamp(((ca>>shift)&255)*wa+((cb>>shift)&255)*wb+((cc>>shift)&255)*wc,0.0,255.0))<<shift;
            pixel(x,y,color,id);
        }
    }
    void box(Vec2 p, double w, double d, double z, double h, std::uint32_t color, GridPos id = {-1,-1}) {
        if(object_angle!=0) {
            const std::array<Vec2,4> points{{{p.x-w/2,p.y-d/2},{p.x+w/2,p.y-d/2},{p.x+w/2,p.y+d/2},{p.x-w/2,p.y+d/2}}};
            std::array<int,4> order{{0,1,2,3}};
            const auto depth=[&](int i) {
                const auto a=object_point(points[i]),b=object_point(points[(i+1)%4]);return a.x+a.y+b.x+b.y;
            };
            std::sort(order.begin(),order.end(),[&](int a,int b){return depth(a)<depth(b);});
            for(int i:order) {
                const auto a=points[i],b=points[(i+1)%4];
                quad(project(a,z+h),project(a,z),project(b,z),project(b,z+h),tint(color,i%2 ? .56 : .76),id);
            }
            quad(project(points[0],z+h),project(points[1],z+h),project(points[2],z+h),project(points[3],z+h),color,id);
            return;
        }
        const Vec2 a{p.x-w/2,p.y-d/2}, b{p.x+w/2,p.y-d/2}, c{p.x+w/2,p.y+d/2}, e{p.x-w/2,p.y+d/2};
        const auto at = project(a,z+h), bt = project(b,z+h), ct = project(c,z+h), et = project(e,z+h);
        quad(bt,project(b,z),project(c,z),ct,tint(color,0.56),id);
        quad(ct,project(c,z),project(e,z),et,tint(color,0.76),id);
        quad(at,bt,ct,et,color,id);
        line(et,ct,tint(color,1.15));
    }
    void oriented_box(Vec2 p, double w, double d, double z, double h, std::uint32_t color,
                      double heading, double pitch = 0, double roll = 0) {
        const Vec2 forward{std::cos(heading),std::sin(heading)}, right{forward.y,-forward.x};
        const std::array<Vec2,4> local{{{-w/2,-d/2},{w/2,-d/2},{w/2,d/2},{-w/2,d/2}}};
        std::array<Vec2,4> bottom{},upper{},ground{};
        std::array<int,4> order{{0,1,2,3}};
        for (int i = 0; i < 4; ++i) {
            const auto q = local[i];
            ground[i] = {p.x+right.x*q.x+forward.x*q.y,p.y+right.y*q.x+forward.y*q.y};
            const double tilt = q.y*pitch+q.x*roll;
            bottom[i] = project(ground[i],z+tilt); upper[i] = project(ground[i],z+h+tilt);
        }
        const auto depth = [&](int i) { const auto a = ground[i], b = ground[(i+1)%4]; return a.x+a.y+b.x+b.y; };
        std::sort(order.begin(),order.end(),[&](int a,int b) { return depth(a) < depth(b); });
        for (int i : order) {
            const int next = (i+1)%4;
            const double dx = ground[next].x-ground[i].x, dy = ground[next].y-ground[i].y;
            const double diffuse = std::max(0.0,(-dy*0.6+dx*0.8)/std::hypot(dx,dy));
            quad(upper[i],bottom[i],bottom[next],upper[next],tint(color,0.57+diffuse*0.25));
        }
        quad(upper[0],upper[1],upper[2],upper[3],color);
    }
    void crystal(Vec2 p, double z, double h, std::uint32_t color, GridPos id) {
        const auto tip = project(p,z+h), low = project(p,z);
        const Vec2 l{low.x-5,low.y-4}, r{low.x+5,low.y-4};
        triangle(tip,l,low,color,id); triangle(tip,low,r,tint(color,0.58),id);
        line(tip,l,tint(color,1.3));
    }
};

Vec2 between(Vec2 a,Vec2 b,double t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
}

// Independent, repeatable shape channels. The cell-derived phase is stable
// across frames and cameras; changing orientation is only one of many traits.
double material_random(double phase,unsigned channel) {
    auto value=std::uint32_t(std::int64_t(std::floor(phase*1000000)))+channel*0x9e3779b9u;
    value=(value^(value>>16))*0x7feb352du;
    value=(value^(value>>15))*0x846ca68bu;
    value^=value>>16;
    return (value&0xffffff)/16777216.0;
}

// Rounded, impact-broken rock; asymmetric shoulders avoid a gem-like pyramid.
void faceted_stone(Canvas& c,Vec2 p,double z,double radius,double h,
                   std::uint32_t color,GridPos id,double phase,std::uint32_t inclusion=0) {
    const int sides=5+int(material_random(phase,1)*5);
    std::array<Vec2,9> base{},shoulder{},crown{};
    const double stretch=0.56+material_random(phase,2)*0.44;
    const double crown_width=0.27+material_random(phase,3)*0.37;
    const Vec2 lean{(material_random(phase,4)-0.5)*radius*0.28,(material_random(phase,5)-0.5)*radius*0.28};
    const auto offset=[&](double angle,double r) {
        const double ax=std::cos(angle)*r, ay=std::sin(angle)*r*stretch;
        return Vec2{p.x+ax*std::cos(phase)-ay*std::sin(phase),p.y+ax*std::sin(phase)+ay*std::cos(phase)};
    };
    for(int i=0;i<sides;++i) {
        const double angle=i*6.28318530718/sides+(material_random(phase,10+i)-0.5)*0.20;
        const double rough=0.73+0.27*material_random(phase,20+i);
        const auto shoulder_point=offset(angle,radius*(0.61+0.29*material_random(phase,30+i)));
        const auto crown_point=offset(angle,radius*(crown_width+0.09*material_random(phase,40+i)));
        base[i]=c.project(offset(angle,radius*rough),z);
        shoulder[i]=c.project(shoulder_point,z+h*(0.32+0.28*material_random(phase,50+i)));
        crown[i]=c.project({crown_point.x+lean.x,crown_point.y+lean.y},z+h*(0.73+0.22*material_random(phase,60+i)));
    }
    const auto cap=c.project({p.x+lean.x,p.y+lean.y},z+h);
    for(int i=0;i<sides;++i) {
        const int next=(i+1)%sides;
        const double light=0.64+0.15*std::cos(phase+(i+0.5)*6.28318530718/sides+2.3);
        c.quad(shoulder[i],base[i],base[next],shoulder[next],tint(color,light),id);
        c.quad(crown[i],shoulder[i],shoulder[next],crown[next],tint(color,light+0.14),id);
        c.triangle(crown[i],crown[next],cap,tint(color,light+0.28),id);
        if (inclusion && material_random(phase,70+i) > 0.46) {
            // Inclusions sit inside the crown face, never outside its silhouette.
            const auto a=between(crown[i],cap,0.28), b=between(crown[next],cap,0.42);
            c.triangle(between(a,b,0.15),between(a,b,0.70),between(b,cap,0.36),tint(inclusion,light+0.25),id);
            c.line(between(a,b,0.25),between(a,b,0.58),tint(inclusion,1.12));
        } else if (!inclusion && i == 2 && radius > 0.23 && h > 0.35) {
            const auto a=between(crown[i],cap,0.22), b=between(crown[next],cap,0.44);
            const auto joint=between(between(a,b,0.5),cap,0.15);
            c.line(a,joint,tint(color,0.56)); c.line(joint,b,tint(color,0.56));
        }
    }
}

// A chipped, flat cleavage plate. Top-edge heights tilt together rather than
// converging on a spike; fine seams stay attached to the actual side faces.
void mineral_slab(Canvas& c,Vec2 p,double z,double radius,double h,
                  std::uint32_t color,GridPos id,double phase,std::uint32_t seam) {
    const int sides=5+int(material_random(phase,81)*3);
    std::array<Vec2,7> base{},rim{};
    const double aspect=0.48+0.47*material_random(phase,82);
    const double tilt=0.04+0.23*material_random(phase,83);
    for (int i=0;i<sides;++i) {
        const double angle=i*6.28318530718/sides;
        const double rough=0.70+0.29*material_random(phase,90+i);
        const double ax=std::cos(angle)*radius*rough, ay=std::sin(angle)*radius*rough*aspect;
        const Vec2 q{p.x+ax*std::cos(phase)-ay*std::sin(phase),p.y+ax*std::sin(phase)+ay*std::cos(phase)};
        base[i]=c.project(q,z);
        rim[i]=c.project({q.x+radius*0.08,q.y-radius*0.05},z+h*(1-tilt+tilt*std::cos(angle+phase)));
    }
    for (int i=0;i<sides;++i) {
        const int next=(i+1)%sides;
        const double light=0.66+0.14*std::cos(phase+(i+0.5)*6.28318530718/sides+2.3);
        c.quad(rim[i],base[i],base[next],rim[next],tint(color,light),id);
        if (seam) c.line(between(base[i],rim[i],0.53),between(base[next],rim[next],0.57),tint(seam,light));
    }
    for (int i=1;i<sides-1;++i) c.triangle(rim[0],rim[i],rim[i+1],tint(color,0.94+0.03*(i%3)),id);
    if (seam) {
        const auto a=between(rim[0],rim[3],0.18+material_random(phase,101)*0.18),
                   b=between(rim[1],rim[4],0.40+material_random(phase,102)*0.24),
                   d=between(rim[2],rim[sides-1],0.55+material_random(phase,103)*0.20);
        c.line(a,b,tint(seam,0.91)); c.line(b,d,tint(seam,1.06));
    }
}

void ice_shard(Canvas& c,Vec2 p,double z,double radius,double h,
               std::uint32_t color,GridPos id,double phase) {
    const int sides=4+int(material_random(phase,111)*4);
    std::array<Vec2,7> base{},neck{};
    const double taper=0.40+material_random(phase,112)*0.48;
    const double aspect=0.43+material_random(phase,113)*0.48;
    const double lean=(material_random(phase,114)-0.5)*radius*0.64;
    const double slope=0.08+material_random(phase,115)*0.32;
    for(int i=0;i<sides;++i) {
        const double angle=phase+i*6.28318530718/sides;
        const double rough=0.75+0.24*material_random(phase,120+i);
        const Vec2 edge{p.x+std::cos(angle)*radius*rough,p.y+std::sin(angle)*radius*rough*aspect};
        const Vec2 inner{p.x+std::cos(angle)*radius*taper+lean,p.y+std::sin(angle)*radius*taper*aspect};
        base[i]=c.project(edge,z);
        neck[i]=c.project(inner,z+h*(1-slope+slope*std::cos(angle-phase)));
    }
    for(int i=0;i<sides;++i) {
        const int next=(i+1)%sides;
        c.quad(neck[i],base[i],base[next],neck[next],tint(color,0.76+0.12*std::cos(phase+i+2.3)),id);
        const auto a=between(base[i],neck[i],0.18+material_random(phase,130+i)*0.26),
                   b=between(base[next],neck[next],0.55+material_random(phase,140+i)*0.25);
        const auto bend=between(between(a,b,0.52),between(neck[i],neck[next],0.5),0.18);
        c.line(between(a,bend,0.2),bend,tint(color,1.07));
        c.line(bend,between(bend,b,0.78),tint(color,1.13));
    }
    for(int i=1;i<sides-1;++i) c.triangle(neck[0],neck[i],neck[i+1],tint(color,1.03+(i%2)*0.06),id);
    c.line(neck[2],neck[3],0xe1e9e5); // Frost on a broken edge, not a glowing gem.
}

void surface_scatter(Canvas& c,const WorldGrid& world,int x,int y) {
    if (BaseCamp::footprint(x,y) || world.pit(x,y) || world.solid(x,y)) return;
    // Integer noise samples give an even random draw; a slower field groups
    // debris into occasional pockets, leaving about nine cells in ten clear.
    const double pocket=PlanetTerrain::noise(x*0.23,y*0.23,world.seed()+887);
    const double density=PlanetTerrain::noise(x*17,y*19,world.seed()+907);
    const double coverage=(y == 10 ? 0.025 : 0.055)+pocket*0.09;
    if (density > coverage) return;
    const double ox=0.25+PlanetTerrain::noise(x*7,y*3,world.seed()+919)*0.50;
    const double oy=0.25+PlanetTerrain::noise(x*3,y*11,world.seed()+937)*0.50;
    const Vec2 p{x+ox,y+oy}; const double z=surface_height(world,p)+0.006;
    const double slope=std::abs(surface_height(world,{p.x+0.18,p.y})-surface_height(world,{p.x-0.18,p.y}))+
                       std::abs(surface_height(world,{p.x,p.y+0.18})-surface_height(world,{p.x,p.y-0.18}));
    if (slope > 0.25) return; // Loose chips settle on shelves, not steep walls.
    const double choice=PlanetTerrain::noise(x*13,y*23,world.seed()+953);
    const double phase=PlanetTerrain::noise(x*6.3,y*6.9,world.seed()+977)*3.0;
    const double size=PlanetTerrain::noise(x*29,y*7,world.seed()+983);
    const double radius=0.07+size*0.10;
    // A subdued contact shadow anchors each piece to the ground.
    std::array<Vec2,7> shadow{};
    for (int i=0;i<7;++i) {
        const double angle=i*6.28318530718/7;
        const Vec2 q{p.x+0.025+std::cos(angle)*radius*1.15,p.y+0.025+std::sin(angle)*radius};
        shadow[i]=c.project(q,surface_height(world,q)+0.003);
    }
    c.blend_polygon(shadow,0x292825,0.20);
    if(choice < 0.72) {
        // Most loose pieces are dark, carbon-rich broken rock.
        faceted_stone(c,p,z,radius,0.045+size*0.12,tint(0x79736a,0.85+size*0.20),{-1,-1},phase);
        if(size > 0.65) {
            const Vec2 chip{p.x+0.17,p.y-0.11};
            faceted_stone(c,chip,surface_height(world,chip)+0.004,0.045,0.035,0x82786b,{-1,-1},phase+1.1);
        }
    } else if(choice < 0.97) {
        // Silicate fragments lie as thin, light-catching layered flakes.
        mineral_slab(c,p,z,radius*1.15,0.045,0x9d9180,{-1,-1},phase,0);
    } else {
        // Sparse exposed ice is a small shard, never a second collectible sample.
        ice_shard(c,p,z,0.06,0.10+size*0.07,0x91afb5,{-1,-1},phase);
    }
}

void settled_dust(Canvas& c,const WorldGrid& world,int x,int y,std::uint32_t color) {
    if (world.solid(x,y)) return;
    const double deposit=PlanetTerrain::noise(x*0.61,y*0.57,world.seed()+613);
    if (deposit < 0.60) return;
    const double px=x+0.36+PlanetTerrain::noise(x*3.1,y*2.7,world.seed()+631)*0.28;
    const double py=y+0.36+PlanetTerrain::noise(x*2.3,y*3.3,world.seed()+647)*0.28;
    const double phase=PlanetTerrain::noise(x*7,y*11,world.seed()+659)*6.28318530718;
    const double rx=0.23+deposit*0.12, ry=0.14+deposit*0.09;
    // Nested irregular contours feather the edge without rectangular decals.
    // Every vertex follows the terrain, in both expedition and globe views.
    for (int layer=0;layer<3;++layer) {
        std::array<Vec2,9> contour{};
        const double scale=1.0-layer*0.22;
        for (int i=0;i<9;++i) {
            const double angle=i*6.28318530718/9;
            const double rough=0.88+0.12*std::sin(i*2.17+phase);
            const double ax=std::cos(angle)*rx*scale*rough, ay=std::sin(angle)*ry*scale*rough;
            const Vec2 q{px+ax*std::cos(phase)-ay*std::sin(phase),py+ax*std::sin(phase)+ay*std::cos(phase)};
            contour[i]=c.project(q,surface_height(world,q)+0.004);
        }
        c.blend_polygon(contour,tint(color,1.24),0.06+(deposit-0.60)*0.12);
    }
}

// Only ship geometry receives the buried, nose-down pose. Cabin furniture and
// terrain retain their real world elevations, shared with movement and lighting.
struct HullSpace {
    Canvas& canvas; bool previous;
    explicit HullSpace(Canvas& c) : canvas(c), previous(c.hull_space) { c.hull_space = true; }
    ~HullSpace() { canvas.hull_space = previous; }
};
// Trace each station's actual rendered silhouette at its normal depth. Later
// robots and wing panels occlude both the line and its small emissive halo.
struct InteractionOutline {
    Canvas& canvas;
    Canvas::ObjectMask mask;
    Canvas::ObjectMask* previous;
    double strength;
    std::uint32_t accent;
    InteractionOutline(Canvas& c,bool active,double time,std::uint32_t color) : canvas(c), previous(c.object_mask),
        strength(active ? 0.75 : 0.35+0.06*std::sin(time*2.0)), accent(color) { c.object_mask = &mask; }
    ~InteractionOutline() {
        canvas.object_mask = previous;
        if (mask.right < mask.left) return;
        const auto covered = [&](int x,int y) {
            return x >= 0 && x < width && y >= top && y < bottom && mask.pixels[y*width+x] != 0;
        };
        for (int y = std::max(top,mask.first-2); y <= std::min(bottom-1,mask.last+2); ++y)
            for (int x = std::max(0,mask.left-2); x <= std::min(width-1,mask.right+2); ++x) {
                if (covered(x,y)) continue;
                int nearest = 5;
                for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                    if (dx*dx+dy*dy < nearest && covered(x+dx,y+dy)) nearest = dx*dx+dy*dy;
                if (nearest > 4) continue;
                const auto old = canvas.pixels[y*width+x];
                const double alpha = nearest <= 2 ? strength : 0.18*strength;
                const auto green = accent;
                std::uint32_t color = 0;
                for (int shift : {0,8,16})
                    color |= unsigned(((old>>shift)&255)*(1-alpha)+((green>>shift)&255)*alpha)<<shift;
                canvas.emissive_rect(x,y,1,1,color);
            }
    }
};
// Shared food/water pictograms float above uncollected supplies in either map.
void supply_marker(Canvas& c,Vec2 p,double height) {
    const auto q=c.project(p,height);
    c.rect(int(q.x)-9,int(q.y)-3,6,6,0xd7a763);
    for(Vec2 chip : {Vec2{-8,-2},Vec2{-5,0},Vec2{-7,2}}) c.rect(int(q.x+chip.x),int(q.y+chip.y),1,1,0x76513a);
    c.triangle({q.x+5,q.y-5},{q.x+1,q.y+1},{q.x+8,q.y+1},0x65bdeb);
    c.rect(int(q.x)+2,int(q.y)+1,6,3,0x65bdeb);
}
void data_chip_bank(Canvas& c,const Game& game,double time) {
    const Vec2 p=BaseCamp::data_bank;
    const double ground=surface_height(game.world(),p);
    const auto at=[&](double x,double y,double z) { return c.project({p.x+x,p.y+y},ground+z); };
    const GridPos id{int(p.x),int(p.y)};
    InteractionOutline outline(c,game.near_data_chip(),time,0x72ddf5);
    const double proximity=std::clamp(1.0-distance(game.unit().center(),p)/8.0,0.0,1.0);
    if (proximity > 0.0) {
        const double height=1.5+proximity*2.0;
        constexpr int sides=16;
        const double radius=0.76;
        for (int side=0;side<sides;++side) {
            const double a=6.28318530718*side/sides,b=6.28318530718*(side+1)/sides;
            c.blend_quad(at(radius*std::cos(a),radius*std::sin(a),0.24),
                         at(radius*std::cos(b),radius*std::sin(b),0.24),
                         at(radius*std::cos(b),radius*std::sin(b),height),
                         at(radius*std::cos(a),radius*std::sin(a),height),
                         0x8feaff,0.045+proximity*0.035);
        }
    }
    c.box(p,0.72,0.52,ground+0.02,0.12,0x2d3237,id);
    c.box({p.x,p.y},0.54,0.36,ground+0.14,0.08,0x4d646e,id);
    c.quad(at(-0.18,-0.12,0.22),at(0.18,-0.12,0.22),at(0.18,0.12,0.22),at(-0.18,0.12,0.22),0x9ae5ff,id);
}
void rescue_passenger(Canvas& c,Vec2 p,double z,double seated);
void damaged_shuttle(Canvas& c,const Game& game) {
    if(game.rescue_launch_complete()) return;
    InteractionOutline outline(c,game.mission_number()==3 && game.records_copied() && !game.mission3_complete(),game.presentation().clock,0x54ff87);
    const Vec2 p=BaseCamp::damaged_shuttle;
    const double ground=surface_height(game.world(),p)+game.rescue_ship_lift();
    const GridPos id{int(p.x),int(p.y)};
    const auto at=[&](double x,double y,double z) { return c.project({p.x+x,p.y+y},ground+z); };
    c.blend_quad(at(-2.9,-1.9,0.012),at(2.8,-1.9,0.012),at(2.8,1.9,0.012),at(-2.9,1.9,0.012),0x40362f,0.42);
    if(game.rescue_ship_repaired()) {
        if(game.rescue_stage()==Game::RescueStage::LaunchingShip) {
            const double flame=1.2+.4*std::sin(game.presentation().clock*45);
            for(double x:{-1.1,.9}) {
                c.triangle(at(x-.25,0,.1),at(x+.25,0,.1),at(x,0,-flame),0xef8f48);
                c.triangle(at(x-.13,0,.1),at(x+.13,0,.1),at(x,0,-flame*.7),0xe4f8ff);
            }
        }
        c.box(p,3.85,1.42,ground+.06,.26,0x343b3e,id);
        for(double side:{-1.0,1.0}) {
            c.quad(at(-1.4,side*.6,.5),at(-2.2,side*1.65,.28),at(-.7,side*1.65,.28),at(.3,side*.6,.5),0xb6cbcf,id);
            c.line(at(-2.2,side*1.65,.29),at(-.7,side*1.65,.29),0xdfa55c);
        }
        // Rear hatch opens onto a ground-level ramp. The passenger travels along
        // this opening, then sits below the roof inside the enclosed cabin.
        const double door=game.rescue_door_phase();
        c.box({p.x-2.25,p.y},.65,.7,ground+.10,.08,0x667f89,id);
        c.box({p.x,p.y-.65},3.6,.12,ground+.32,1.6,0x8fa8b4,id);
        c.box(RescueShip::passenger_seat,.7,.65,ground+.64,.12,0x354f68,id);
        c.box({RescueShip::passenger_seat.x-.35,p.y},.12,.65,ground+.76,.65,0x52748b,id);
        if(game.rescue_stage()==Game::RescueStage::Unloading || game.astronaut_in_rescue_ship()) {
            const auto passenger=game.rescue_passenger_position();
            const auto previous_origin=c.object_origin;const double previous_angle=c.object_angle;
            c.object_origin=passenger;
            c.object_angle=-1.57079632679*(game.rescue_stage()==Game::RescueStage::Unloading ? std::clamp(game.rescue_stage_progress()/.45,0.0,1.0) : 1.0);
            rescue_passenger(c,passenger,ground+game.rescue_passenger_height(),1);
            c.object_origin=previous_origin;c.object_angle=previous_angle;
        }
        // Foreground wall and roof occlude the passenger rather than drawing
        // the suit on top of the spacecraft after the entire scene is painted.
        c.box({p.x,p.y+.65},3.6,.12,ground+.32,1.6,0xc6d3d7,id);
        c.box({p.x+1.78,p.y},.12,1.3,ground+.32,1.6,0xb5c8cf,id);
        for(double side:{-.57,.57}) c.box({p.x-1.88,p.y+side},.12,.16,ground+.32,1.6,0xb5c8cf,id);
        c.box({p.x+.7,p.y},1.65,1.3,ground+RescueShip::roof_height,.12,0xd5dedb,id);
        if(door<.99) c.box({p.x-1.88,p.y+door*.85},.12,.98*(1-door),ground+.32,1.6,0x52748b,id);
        if(door<1) c.box({p.x-.85,p.y},1.45,1.3,ground+RescueShip::roof_height,.12*(1-door),0xc6d5d9,id);
        const auto hatch=at(-1.96,0,RescueShip::roof_height+.03);
        c.emissive_rect(int(hatch.x)-1,int(hatch.y)-1,3,2,door>.5 ? 0x8ef3b3 : 0xefad63);
        for(double side:{-.55,.55}) {
            const auto lamp=at(1.8,side,.58);c.emissive_rect(int(lamp.x)-1,int(lamp.y)-1,3,2,0x8ef3b3);
        }
        return;
    }
    for (Vec2 debris : {Vec2{-2.8,-1.35},Vec2{-2.5,1.62},Vec2{2.6,-1.05},Vec2{2.9,0.85},
               Vec2{1.95,1.52},Vec2{-1.75,-1.82}}) {
     const double size=debris.x>2 ? 0.32 : 0.24;
     c.box({p.x+debris.x,p.y+debris.y},size,size*0.72,ground+0.015,0.10,
        (int(std::abs(debris.x*10+debris.y*7))%2) ? 0x535553 : 0x786952,id);
    }

    // The main body points right; its broad left-side wing roots are torn away.
    c.box(p,3.85,1.42,ground+0.06,0.26,0x343b3e,id);
    c.box({p.x-0.12,p.y},3.42,1.26,ground+0.32,0.22,0xc0c0b7,id);
    c.quad(at(0.92,-0.56,0.54),at(1.92,-0.40,0.48),at(2.12,0.02,0.42),
        at(1.75,0.48,0.48),0xaaaead,id);
    c.quad(at(0.78,-0.34,0.57),at(1.52,-0.23,0.53),at(1.60,0.12,0.51),
        at(0.82,0.24,0.57),0x263e4d,id);
    c.line(at(0.85,-0.28,0.58),at(1.48,-0.18,0.55),0x91b7c0);

    // Two uneven wing fragments leave the silhouette visibly broken, not airborne.
    c.quad(at(-1.20,-0.56,0.49),at(-2.20,-1.48,0.16),at(-1.42,-1.62,0.14),
        at(-0.35,-0.62,0.48),0xb9b9af,id);
    c.quad(at(-1.52,-1.28,0.20),at(-2.26,-1.48,0.16),at(-2.54,-1.12,0.12),
        at(-2.08,-0.95,0.18),0x747b79,id);
    c.quad(at(-1.18,0.55,0.49),at(-2.42,1.40,0.12),at(-1.66,1.74,0.12),
        at(-0.42,0.64,0.45),0xaeb1ad,id);
    c.quad(at(-0.92,0.75,0.43),at(-1.62,1.40,0.16),at(-1.33,1.56,0.14),
        at(-0.42,0.80,0.43),0x777e7b,id);

    // Worn orange rescue markings and exposed fracture lines.
    c.quad(at(-0.42,-0.62,0.55),at(-0.12,-0.62,0.55),at(0.05,0.62,0.55),
        at(-0.25,0.62,0.55),0xd97a3f,id);
    c.quad(at(0.34,-0.60,0.55),at(0.49,-0.59,0.55),at(0.65,0.59,0.52),
        at(0.48,0.60,0.53),0xd97a3f,id);
    c.line(at(-1.55,-0.88,0.29),at(-1.14,-0.52,0.48),0x343b3e);
    c.line(at(-1.34,0.82,0.38),at(-0.92,0.58,0.48),0x3c4142);
    c.box({p.x+1.82,p.y+0.48},0.46,0.36,ground+0.12,0.12,0x8c4640,id);
    const auto warning=at(1.82,0.48,0.27);
    c.emissive_rect(int(warning.x)-1,int(warning.y)-1,3,2,0xf09b68);
}
// Screen-space cloth stays legible at the game's native 640x360 resolution.
// Each stripe bends independently around a small, phase-shifted ripple.
void myanmar_flag(Canvas& c, Vec2 pole_top, int length, int flag_height, double clock, double motion) {
    const auto cloth = [&](double u,double v) -> Vec2 {
        const double flutter = motion*u*std::sin(clock*6.0+u*7.5);
        return {pole_top.x+1+length*u+flutter*1.2,
                pole_top.y+flag_height*v+flutter*2.0+motion*u*u*std::sin(clock*4.5+u*4.0)};
    };
    constexpr std::array<std::uint32_t,3> bands{{0xf4cf4e,0x43865a,0xc74445}};
    for (int band = 0; band < 3; ++band) for (int segment = 0; segment < 6; ++segment) {
        const double a = double(segment)/6, b = double(segment+1)/6;
        const double upper = double(band)/3, lower = double(band+1)/3;
        c.quad(cloth(a,upper),cloth(b,upper),cloth(b,lower),cloth(a,lower),bands[band]);
    }
    c.line(cloth(0,0),cloth(1,0),0xffde72);
    c.line(cloth(0,1),cloth(1,1),0x8e3038);
    const auto center = cloth(0.50,0.50);
    const double outer = std::max(1.8,flag_height*0.42), inner = outer*0.42;
    for (int i = 0; i < 10; ++i) {
        const auto point = [&](int index) {
            const double a = -1.5707963268+index*0.62831853072, r = index%2 ? inner : outer;
            return Vec2{center.x+std::cos(a)*r,center.y+std::sin(a)*r};
        };
        c.triangle(center,point(i),point((i+1)%10),0xffffff);
    }
}
[[maybe_unused]] void camp_flagpole(Canvas& c,const Game& game) {
    const Vec2 p{12.45,9.35};
    const double z = surface_height(game.world(),p);
    const auto foot = c.project(p,z), top_point = c.project(p,z+4.45);
    c.box(p,0.19,0.19,z,0.11,0x617177);
    c.line(foot,top_point,0xb1c0bd);
    c.rect(int(top_point.x),int(top_point.y)-1,2,2,0xd4ceae);
    myanmar_flag(c,top_point,30,16,game.presentation().clock,0.8);
}
void material_formation(Canvas& c,Vec2 p,double z,double h,TileType type,GridPos id,double phase) {
    const auto random=[&](unsigned channel) { return material_random(phase,200+channel); };
    const double variation=random(0);
    const int form=int(random(1)*3);
    const auto at=[&](double x,double y) {
        return Vec2{p.x+x*std::cos(phase)-y*std::sin(phase),p.y+x*std::sin(phase)+y*std::cos(phase)};
    };
    if (type == TileType::SilicateBarrier) {
        const int layers=2+int(random(2)*4);
        std::array<double,5> thickness{}; double total=0, level=0;
        for (int i=0;i<layers;++i) { thickness[i]=0.55+random(10+i); total+=thickness[i]; }
        for (int i=0;i<layers;++i) {
            const double step=thickness[i]/total;
            const auto center=at((random(20+i)-0.5)*0.15,(random(30+i)-0.5)*0.12);
            const double radius=(0.45+random(40+i)*0.10)*(1-level*(0.15+variation*0.25));
            mineral_slab(c,center,z+h*level,radius,h*std::min(1-level,step*1.16),
                         tint(0xa99b85,0.86+level*0.22+random(50+i)*0.08),id,phase+i*0.47,0xc7b99f);
            level+=step;
        }
        if (form != 0) mineral_slab(c,at(0.25,0.23),z,0.16+random(3)*0.08,h*0.10,0x9e917c,id,phase+0.6,0);
    } else if (type == TileType::HydratedMinerals) {
        // Earthy nodules expose overlapping mineral scales and pale seams.
        faceted_stone(c,at(-0.05,-0.05),z,0.31+random(2)*0.14,h,0x727e6d,id,phase,0xa3ae94);
        const int scales=1+int(random(3)*4);
        for (int i=0;i<scales;++i) {
            const auto center=at((random(10+i)-0.5)*0.42,0.10+random(20+i)*0.17);
            mineral_slab(c,center,z,0.18+random(30+i)*0.17,h*(0.25+random(40+i)*0.39),
                         tint(0x87917a,0.85+random(50+i)*0.18),id,phase+i*1.31,0xb0b69e);
        }
    } else if (type == TileType::IceBarrier || type == TileType::WaterIce) {
        // Broad broken plates have sloping tops and milky frost, with blue-grey
        // interiors. Secondary pieces sit at the foot of the main fracture.
        ice_shard(c,at(-0.03,-0.05),z,0.26+random(2)*0.16,h,0xa9c7ce,id,phase);
        const int shards=1+int(random(3)*3);
        for (int i=0;i<shards;++i) {
            const auto center=at(-0.25+i*0.23,0.12+random(10+i)*0.13);
            ice_shard(c,center,z,0.13+random(20+i)*0.12,h*(0.22+random(30+i)*0.50),
                      tint(0xb1cbd0,0.85+random(40+i)*0.17),id,phase+1.83+i*2.17);
        }
    } else if (type == TileType::SupplyBarrier) {
        const double split=form == 0 ? 1 : 0.42+random(2)*0.20;
        faceted_stone(c,at(-0.04,-0.02),z,0.46+random(3)*0.10,h*std::min(1.0,split+0.13),0x656763,id,phase);
        if (form != 0) faceted_stone(c,at((random(4)-0.5)*0.18,-0.04),z+h*split,
                                    0.32+random(5)*0.12,h*(1-split),0x85827b,id,phase+0.8);
        const int chips=1+int(random(6)*3);
        for (int i=0;i<chips;++i) {
            const auto center=at(-0.27+i*0.25,0.17+random(10+i)*0.10);
            faceted_stone(c,center,z,0.13+random(20+i)*0.12,h*(0.14+random(30+i)*0.24),0x626661,id,phase+i*1.9+1.1);
        }
    } else if (type == TileType::IronNickelOre) {
        // Dense charcoal host rock with small dull metallic inclusions.
        faceted_stone(c,at(-0.03,-0.03),z,0.31+random(2)*0.15,h,0x625b53,id,phase,0xb7b2a2);
        for (int i=0;i<form+1;++i) {
            const auto center=at((random(10+i)-0.5)*0.48,0.12+random(20+i)*0.15);
            faceted_stone(c,center,z,0.11+random(30+i)*0.14,h*(0.23+random(40+i)*0.40),
                          tint(0x84745f,0.83+random(50+i)*0.20),id,phase+i*1.7+1.3,0xaaa99c);
        }
    } else if (type == TileType::Regolith) {
        // A continuous granular dome, without hard crystal edges or a peak.
        constexpr int sides=12;
        std::array<Vec2,sides> foot{},rim{};
        const auto summit=at((random(2)-0.5)*0.24,(random(3)-0.5)*0.20);
        const auto cap=c.project(summit,z+h);
        const double aspect=0.58+random(4)*0.40, shoulder=0.43+random(5)*0.34;
        for (int i=0;i<sides;++i) {
            const double angle=i*6.28318530718/sides;
            const double radius=0.41+0.10*random(10+i);
            const auto q=at(std::cos(angle)*radius,std::sin(angle)*radius*aspect);
            foot[i]=c.project(q,z);
            rim[i]=c.project(between(summit,q,0.56+random(30+i)*0.17),z+h*(shoulder+0.05*std::sin(angle+phase)));
        }
        for (int i=0;i<sides;++i) {
            const int next=(i+1)%sides;
            const double light=0.86+0.08*std::cos(i*6.28318530718/sides+2.3);
            const auto color=tint(0x9b8c75,light);
            c.quad(rim[i],foot[i],foot[next],rim[next],color,id);
            c.terrain_triangle(rim[i],rim[next],cap,color,tint(0x9b8c75,light+0.025),0xaaa089,id);
            const auto grain=between(between(rim[i],rim[next],0.38),cap,0.18+0.13*(i%3));
            c.pixel(int(grain.x),int(grain.y),tint(color,i%2 ? 0.87 : 1.12));
        }
    } else if (type == TileType::ScrapAlloy) {
        // Torn folded sheet metal retains manufactured edges and a bent rib.
        const auto sheet=[&](double x,double y,double lift) {
            x*=0.68+random(2)*0.38; y*=0.72+random(3)*0.34;
            return c.project({p.x+x*std::cos(phase)-y*std::sin(phase),
                              p.y+x*std::sin(phase)+y*std::cos(phase)},z+h*lift);
        };
        c.box({p.x-0.12,p.y},0.16,0.61,z,h*0.74,0x5d6868,id);
        const auto a=sheet(-0.28-random(4)*0.09,0.28,0.08+random(5)*0.22), b=sheet(0.29,0.25,0.05+random(6)*0.14);
        const double fold=0.30+random(7)*0.37;
        const auto d=sheet(-0.30,-0.08,fold), e=sheet(0.32,-0.07,fold*0.8);
        c.quad(a,b,e,d,0x929c98,id);
        c.quad(d,e,sheet(0.26,-0.30,0.82+variation*0.12),sheet(-0.27,-0.34,1),0x687575,id);
        c.line(d,e,0xc1c7bb);
        c.line(between(a,d,0.4),between(b,e,0.4),0x8b6755);
    } else {
        faceted_stone(c,p,z,0.38+random(2)*0.13,h,0x77766e,id,phase);
        if (form != 0) mineral_slab(c,at(0.18,0.18),z,0.21+random(3)*0.09,h*(0.18+random(4)*0.22),0x666861,id,phase+0.7,0x414640);
    }
}

void node(Canvas& c, const WorldGrid& world, int x, int y, double time) {
    const Vec2 p{x+0.5,y+0.5}; const double z = surface_height(world,p);
    const GridPos id{x,y}; const auto type = world.get_tile_type(x,y);

    if (type == TileType::NASADataCore) {
        c.box(p,0.8,0.8,z,0.35,0x768b89,id);
        c.box(p,0.47,0.45,z+0.35,0.35,0x8af4ba,id);
        const auto q = c.project(p,z+0.8);
        c.rect(int(q.x)-1,int(q.y)-int(9+std::sin(time*3)*4),2,5,0xc5ffe1);
    } else {
        const double phase=PlanetTerrain::noise(x*17,y*23,world.seed()+41)*6.28318530718;
        material_formation(c,p,z,WorldGrid::obstacle_height(type),type,id,phase);
    }
    const double progress = world.mining_progress(x,y);
    if (progress > 0) {
        auto a = c.project({p.x-0.1,p.y+0.4},z+0.6), b = c.project({p.x+0.15,p.y+0.4},z+0.15);
        c.line(a,b,0x26323b);
        auto q = c.project(p,z+1.4);
        c.rect(int(q.x)-10,int(q.y),20,2,0x23343f); c.rect(int(q.x)-10,int(q.y),int(progress*20),2,0x9ee8ca);
    }
}
void robot_mark1(Canvas& c, const Game& game) {
    const auto p = game.unit().center(); const auto& pose = game.presentation();
    const double offset = game.world().continuous_ground() ? 0 : surface_height(game.world(),p)-game.world().elevation(int(p.x),int(p.y));
    const double z = std::isfinite(pose.visual_elevation) ? pose.visual_elevation : game.unit().elevation()+offset;
    const double lift = game.unit().jump_height();
    const double heading = pose.heading, head = pose.head_heading;
    const Vec2 forward{std::cos(heading),std::sin(heading)}, right{forward.y,-forward.x};
    const auto local = [&](double x,double y) { return Vec2{p.x+right.x*x+forward.x*y,p.y+right.y*x+forward.y*y}; };
    const bool moving = game.unit().grounded() && std::hypot(game.unit().velocity().x,game.unit().velocity().y) > 0.2;
    const double bob = moving ? 0 : game.unit().alive() ? std::sin(pose.clock*2.4)*0.004 : 0;
    const bool airborne = !game.unit().grounded() && (lift > 0.001 || std::abs(game.unit().vertical_velocity()) > 0.001);
    const double landing = std::clamp(-pose.body_bounce,0.0,0.12), tuck = pose.tread_tuck;
    const double body_z = z+bob+pose.body_bounce, head_z = z+bob+pose.head_bounce;
    for (int side : {-1,1}) {
        const auto track = local(side*0.23,0);
        c.oriented_box(track,0.22,0.72,z+tuck,0.2,0x58616b,heading,pose.lean_forward*0.3,0);
        const double phase = side < 0 ? pose.left_tread_phase : pose.right_tread_phase;
        for (int i = 0; i < 4; ++i) {
            const double along = -0.29+std::fmod(i*0.16+phase*0.025+1000,0.64);
            auto q = c.project(local(side*0.34,along),z+0.11+tuck);
            c.rect(int(q.x),int(q.y),2,2,i%2 ? 0x929a99 : 0x333d47);
        }
    }
    const auto body_color = !game.unit().alive() ? 0x765e50u : game.unit().integrity() < 50 ? 0xb36c59u : 0xc6a363u;
    c.oriented_box(p,0.56+landing*0.2,0.48+landing*0.15,body_z+0.22,
                   0.5-landing*0.25,body_color,heading,pose.lean_forward,pose.lean_side);
    // Suspension struts and a weathered equipment panel follow the chassis.
    c.oriented_box(local(0,0.23),0.34,0.07,body_z+0.37,0.18,0x7f735c,heading);
    c.oriented_box(local(0,-0.05),0.18,0.15,body_z+0.72,std::clamp(0.13+head_z-body_z,0.025,0.18),0x6e7d84,heading);
    const auto eyes = RobotEyes::pose(game,RobotModel::Mark1);
    c.oriented_box(eyes.center,0.7,0.35,head_z+0.79,0.25,0xaebbbc,head,eyes.pitch);
    const Vec2 look{std::cos(head),std::sin(head)};
    if (look.x+look.y > -0.35) {
        for (const auto& emitter : eyes.emitters) {
            const auto eye = c.project(emitter.position,emitter.elevation);
            c.rect(int(eye.x)-3,int(eye.y)-2,7,5,0x203645);
            c.sensor_eye(eye,5,3,pose.eye_openness(),0x49baff,pose.eye_glow(),game.unit().alive());
        }
    }
    const double recoil = pose.mining_target ? std::sin(pose.clock*65)*0.035 : 0;
    const auto tool = local(0.43,0.1+recoil);
    c.oriented_box(tool,0.22,0.2,body_z+(!game.unit().alive() ? 0.30 : airborne ? 0.62 : 0.47),0.16,0x8b9a9e,heading);
    if (pose.mining_target) {
        const auto tip = c.project(tool,z+0.6);
        const int spin = int(pose.clock*32)%4;
        c.line({tip.x-3,tip.y-spin},{tip.x+3,tip.y+spin},0xe3fff0);
    }
    if (game.unit().has_upgrade(Upgrade::RadiationShielding))
        c.oriented_box(local(-0.28,-0.05),0.06,0.4,body_z+0.3,0.36,0x97bda7,heading);
    if (pose.damage_level > 0.35) {
        c.line(c.project(local(0.29,0.1),body_z+0.59),c.project(local(0.29,-0.05),body_z+0.38),0x554e47);
    }
    if (pose.damage_flash > 0.08) {
        const auto spark = c.project(local(0.31,0.02),body_z+0.62);
        c.emissive_rect(int(spark.x)-2,int(spark.y)-2,4,3,pose.damage_flash > 0.28 ? 0xff7c54 : 0xe9b06e);
    }
    if (pose.collapse > 0.18) {
        c.oriented_box(local(-0.42,0.18),0.31,0.20,z+0.025,0.06,0x9a704e,heading+0.72,0.10);
        c.line(c.project(local(-0.24,0.04),body_z+0.45),c.project(local(-0.48,0.25),z+0.10),0xc66c4f);
    }
}
void robot_mark2(Canvas& c, const Game& game, const RobotAppearance& appearance) {
    const auto p = game.unit().center(); const auto& pose = game.presentation();
    const bool alive = game.unit().alive(), mining = bool(pose.mining_target);
    const double offset = game.world().continuous_ground() ? 0 : surface_height(game.world(),p)-game.world().elevation(int(p.x),int(p.y));
    const double z = std::isfinite(pose.visual_elevation) ? pose.visual_elevation : game.unit().elevation()+offset;
    const double heading = pose.heading, head = pose.head_heading;
    const Vec2 forward{std::cos(heading),std::sin(heading)}, right{forward.y,-forward.x};
    const auto local = [&](double x,double y) { return Vec2{p.x+right.x*x+forward.x*y,p.y+right.y*x+forward.y*y}; };
    const bool airborne = !game.unit().grounded() &&
        (game.unit().jump_height() > 0.001 || std::abs(game.unit().vertical_velocity()) > 0.001);
    const double bob = 0;
    const double tuck = pose.tread_tuck;
    const double chassis_z = z+bob+pose.body_bounce, head_z = z+bob+pose.head_bounce;
    const auto armor = alive ? pose.damage_flash > 0.22 ? 0xe8957bu :
        game.unit().integrity() < 50 ? tint(appearance.armor,0.78) : appearance.armor : tint(appearance.armor,0.52);
    const auto trim = alive ? appearance.trim : tint(appearance.trim,0.55);
    const auto optics = alive ? appearance.optics : 0x26383bu;
    const auto at = [&](double x,double y,double h) {
        return c.project(local(x,y),chassis_z+h+y*pose.lean_forward+x*pose.lean_side);
    };
    // Chamfered sheet-metal housings: narrower bevel rings avoid the toy-like
    // solid cuboids of Mark 1 while retaining the same gameplay footprint.
    const auto housing = [&](Vec2 center,double w,double d,double bottom,double h,double angle,std::uint32_t color,
                             double pitch = 0,double roll = 0) {
        const double bevel = std::min(w,d)*0.28;
        const Vec2 f{std::cos(angle),std::sin(angle)}, r{f.y,-f.x};
        const std::array<Vec2,8> shape{{{-w/2+bevel,-d/2},{w/2-bevel,-d/2},{w/2,-d/2+bevel},{w/2,d/2-bevel},
            {w/2-bevel,d/2},{-w/2+bevel,d/2},{-w/2,d/2-bevel},{-w/2,-d/2+bevel}}};
        std::array<Vec2,8> ground{},low{},upper{}; std::array<int,8> order{};
        for (int i = 0; i < 8; ++i) {
            const auto q = shape[i];
            ground[i] = {center.x+r.x*q.x+f.x*q.y,center.y+r.y*q.x+f.y*q.y};
            const double tilt = pitch*q.y+roll*q.x;
            low[i] = c.project(ground[i],bottom+tilt);
            upper[i] = c.project(ground[i],bottom+h+tilt); order[i] = i;
        }
        std::sort(order.begin(),order.end(),[&](int a,int b) {
            const int an = (a+1)%8, bn = (b+1)%8;
            return ground[a].x+ground[a].y+ground[an].x+ground[an].y <
                   ground[b].x+ground[b].y+ground[bn].x+ground[bn].y;
        });
        const auto sun = SceneShadows::sun_shadow;
        for (int i : order) {
            const int next = (i+1)%8;
            const double dx = ground[next].x-ground[i].x, dy = ground[next].y-ground[i].y;
            const double diffuse = std::max(0.0,(-dy*sun.x+dx*sun.y)/(std::hypot(dx,dy)*std::hypot(sun.x,sun.y)));
            c.quad(upper[i],low[i],low[next],upper[next],tint(color,0.61+diffuse*0.34));
        }
        const auto roof = c.project(center,bottom+h);
        for (int i = 0; i < 8; ++i) c.triangle(roof,upper[i],upper[(i+1)%8],color);
        for (int i : {3,4,5}) c.line(upper[i],upper[(i+1)%8],tint(color,1.12));
    };
    const auto track = [&](int side) {
        const double outer = side*0.34, inner = side*0.13;
        const std::array<Vec2,8> profile{{{-0.25,0.025},{0.25,0.025},{0.355,0.085},{0.355,0.19},
            {0.25,0.255},{-0.25,0.255},{-0.355,0.19},{-0.355,0.085}}};
        std::array<Vec2,8> a{},b{};
        for (int i = 0; i < 8; ++i) {
            const auto q = profile[i];
            a[i] = c.project(local(outer,q.x),z+q.y+tuck);
            b[i] = c.project(local(inner,q.x),z+q.y+tuck);
        }
        const auto center = c.project(local(outer,0),z+0.14+tuck);
        for (int i = 0; i < 8; ++i) {
            const int next = (i+1)%8;
            c.quad(a[i],b[i],b[next],a[next],i == 4 ? 0x414a4c : 0x282f32);
            c.triangle(center,a[i],a[next],0x252e31);
        }
        const double phase = side < 0 ? pose.left_tread_phase : pose.right_tread_phase;
        // The top run advances with real distance, while road wheels rotate at
        // the same rate. Each track retains its differential steering phase.
        for (int bar = 0; bar < 6; ++bar) {
            const double along = std::fmod(bar*0.105+phase/9+1000,0.63)-0.315;
            const auto q = [&](double across,double y) { return c.project(local(side*0.235+across,y),z+0.258+tuck); };
            c.quad(q(-0.098,along-0.019),q(0.098,along-0.019),q(0.098,along+0.019),q(-0.098,along+0.019),0x58615f);
        }
        if (side*(right.x+right.y) > -0.05) {
            for (int wheel = 0; wheel < 4; ++wheel) {
                const double along = -0.24+wheel*0.16;
                const auto hub = c.project(local(outer+side*0.003,along),z+0.135+tuck);
                for (int facet = 0; facet < 10; ++facet) {
                    const double a0 = facet*0.6283185307, a1 = (facet+1)*0.6283185307;
                    const auto rim = [&](double a) { return c.project(local(outer+side*0.004,along+std::cos(a)*0.074),z+0.135+std::sin(a)*0.074+tuck); };
                    c.triangle(hub,rim(a0),rim(a1),facet < 5 ? 0x7b8581 : 0x48565a);
                }
                const auto spoke = c.project(local(outer+side*0.008,along+std::cos(phase*0.8)*0.046),z+0.135+std::sin(phase*0.8)*0.046+tuck);
                c.line(hub,spoke,0xb0b6ac); c.rect(int(hub.x),int(hub.y),1,1,0x313e43);
            }
            c.line(a[0],a[1],0x181f24);
            c.line(a[4],a[5],game.unit().has_upgrade(Upgrade::DustRepellentTreads) ? tint(trim,0.95) : 0x89918a);
        }
    };
    const int far_side = right.x+right.y > 0 ? -1 : 1;
    track(far_side); track(-far_side);
    housing(p,0.6,0.54,chassis_z+0.22,0.14,heading,0x45545a,pose.lean_forward*0.5,pose.lean_side*0.5);
    // Short exposed suspension links meet the actual tread centers.
    for (int side : {-1,1}) for (double along : {-0.19,0.19}) {
        c.line(at(side*0.18,along,0.34),c.project(local(side*0.29,along-0.045),z+0.18+tuck),0xadb5ac);
    }
    housing(p,0.6,0.49,chassis_z+0.34,0.29,heading,armor,pose.lean_forward,pose.lean_side);
    housing(local(0,-0.19),0.42,0.11,chassis_z+0.39,0.19,heading,0x52626a,pose.lean_forward,pose.lean_side);
    c.oriented_box(local(0,0.239),0.4,0.035,chassis_z+0.42,0.055,trim,heading,pose.lean_forward,pose.lean_side);
    housing(p,0.565,0.43,chassis_z+0.63,0.045,heading,tint(armor,1.07),pose.lean_forward,pose.lean_side);
    // Recessed access hatch, roof cooling slots, captive fasteners and a few
    // exposed metal edges provide a plausible serviceable machine at pixel scale.
    c.oriented_box(local(0.07,0.045),0.25,0.18,chassis_z+0.676,0.008,tint(armor,0.82),heading);
    for (int slot = 0; slot < 4; ++slot)
        c.line(at(-0.18+slot*0.063,-0.19,0.683),at(-0.18+slot*0.063,-0.07,0.683),0x46575d);
    for (double x : {-0.225,0.225}) for (double y : {-0.16,0.16}) {
        const auto screw = at(x,y,0.682); c.rect(int(screw.x),int(screw.y),1,1,0x66736f);
    }
    c.line(at(0.13,0.205,0.68),at(0.24,0.17,0.68),tint(armor,1.17));
    const auto aerial_base = at(-0.23,-0.16,0.68), aerial_tip = at(-0.23,-0.18,1.12);
    c.line(aerial_base,aerial_tip,0x4b5b60); c.rect(int(aerial_tip.x),int(aerial_tip.y),1,1,0xb7c1b5);
    // An articulated shoulder, elbow and cable place the cutter at the existing
    // mining beam origin, so visual detail cannot change aiming or reach.
    const double recoil = mining ? std::sin(pose.clock*65)*0.012 : 0;
    const double tool_z = !alive ? 0.28 : airborne ? 0.57 : 0.46;
    const auto shoulder = at(0.285,-0.04,0.53), elbow = at(0.365,0.015,tool_z+0.02);
    const auto wrist = at(0.43,0.1+recoil,tool_z+0.065);
    c.line(shoulder,elbow,0x38494f); c.line({shoulder.x+1,shoulder.y},elbow,0xb0bab3);
    c.line(elbow,wrist,0x8f9b99); c.line({elbow.x,elbow.y-2},{wrist.x,wrist.y-2},0x3e5055);
    const auto tool = local(0.43,0.1+recoil);
    housing(tool,0.17,0.22,chassis_z+tool_z,0.135,heading,game.unit().has_upgrade(Upgrade::SolarThermalDrill) ? trim : 0x8c9c9b);
    const auto tip = at(0.43,0.225+recoil,tool_z+0.065);
    c.line(wrist,tip,0x34454b);
    if (mining) {
        const int spin = int(pose.clock*40)%3;
        c.line({tip.x-2,tip.y-spin},{tip.x+2,tip.y+spin},0xc9e4d7);
    }
    if (game.unit().has_upgrade(Upgrade::RadiationShielding))
        housing(local(-0.29,-0.015),0.065,0.36,chassis_z+0.34,0.29,heading,0x8eaaa0,pose.lean_forward,pose.lean_side);
    // Blue optical eyes sit inside the sealed sensor pod on its yaw bearing.
    housing(local(0,-0.015),0.2,0.19,chassis_z+0.68,std::clamp(0.115+head_z-chassis_z,0.025,0.18),heading,0x53666c);
    const auto eyes = RobotEyes::pose(game,RobotModel::Mark2);
    const Vec2 look{std::cos(head),std::sin(head)};
    const auto head_point = [&](double x,double y,double h) {
        double elevation = 0;
        const auto point = eyes.point(x,y,h,elevation);
        return c.project(point,elevation);
    };
    housing(eyes.center,0.51,0.27,head_z+0.815,0.195,head,tint(armor,1.02),eyes.pitch);
    if (look.x+look.y > -0.35) {
        c.quad(head_point(-0.203,0.145,0.85),head_point(0.203,0.145,0.85),
               head_point(0.203,0.145,0.97),head_point(-0.203,0.145,0.97),0x304249);
        for (double lens_x : {-0.115,0.115}) {
            const auto center = head_point(lens_x,0.161,0.91);
            const auto lens_point = [&](double angle,double radius) {
                return head_point(lens_x+std::cos(angle)*radius,0.161,0.91+std::sin(angle)*radius);
            };
            for (int ring = 0; ring < 12; ++ring) {
                const double a = ring*0.5235987756, b = (ring+1)*0.5235987756;
                c.quad(lens_point(a,0.056),lens_point(b,0.056),lens_point(b,0.035),lens_point(a,0.035),ring < 6 ? 0x849592 : 0x1f3037);
                c.triangle(center,lens_point(a,0.035),lens_point(b,0.035),0x142c42);
            }
            c.sensor_eye(center,3,3,pose.eye_openness(),appearance.optics,pose.eye_glow(),alive);
        }
    }
    const auto status = head_point(-0.19,-0.08,1.014);
    c.rect(int(status.x),int(status.y),1,1,alive && std::fmod(pose.clock,1.4) < 1.15 ? optics : 0x324247);
    if (pose.damage_level > 0.35) {
        const int near = right.x+right.y > 0 ? 1 : -1;
        c.line(at(near*0.315,-0.12,0.59),at(near*0.315,0.02,0.41),0x414a4d);
        c.line(at(near*0.317,0.03,0.58),at(near*0.317,0.12,0.49),0x6e5847);
    }
    if (pose.damage_flash > 0.08) {
        const auto spark = at(0.31,0.03,0.61);
        c.emissive_rect(int(spark.x)-2,int(spark.y)-2,5,4,pose.damage_flash > 0.28 ? 0xff754e : 0xefb06a);
        c.line({spark.x-5,spark.y+3},{spark.x+4,spark.y-5},0xffc17a);
    }
    if (pose.collapse > 0.18) {
        housing(local(-0.43,0.18),0.34,0.21,z+0.025,0.055,heading+0.78,tint(appearance.armor,0.62),0.12);
        c.line(at(-0.24,0.04,0.48),c.project(local(-0.50,0.25),z+0.09),0xcf6d50);
        c.line(at(-0.21,-0.02,0.43),c.project(local(-0.43,0.33),z+0.07),0x37454a);
    }
}
void creature(Canvas& c, const Game& game, const Creature& animal) {
    const Vec2 p = animal.position;
    const double z = surface_height(game.world(),p);
    const auto view = c.projection.project(p,z);
    if (!view.visible || view.depth < 0.08 || view.screen.x < -35 || view.screen.x > width+35 ||
        view.screen.y < top-30 || view.screen.y > bottom+35) return;
    const Vec2 forward{std::cos(animal.heading),std::sin(animal.heading)}, right{forward.y,-forward.x};
    const auto local = [&](double across,double along) {
        return Vec2{p.x+right.x*across+forward.x*along,p.y+right.y*across+forward.y*along};
    };
    const auto at = [&](double across,double along,double height) { return c.project(local(across,along),z+height); };
    const bool dying = animal.health <= 0;
    const bool winding = animal.bite_age >= 0 && animal.bite_age < 0.22;
    const bool snapping = animal.bite_age >= 0.22 && animal.bite_age < 0.34;
    const bool recoiling = animal.bite_age >= 0.34 && animal.bite_age < 0.55;
    const bool poised = !dying && !winding && animal.attack_cooldown < 0.18 &&
        distance(p,game.unit().center()) < 1.55 && !game.inside_base();
    const double lift = dying ? -0.11 : winding ? -0.035 : snapping ? 0.16 : recoiling ? 0.07 : 0;
    // Alternating tripod gait: six bent legs follow actual distance traveled.
    // The low, faceted carapace is an original fictional creature design.
    for (int side : {-1,1}) for (int leg = 0; leg < 3; ++leg) {
        const double phase = animal.stride*14+(leg%2 ? 3.1415926536 : 0)+(side < 0 ? 3.1415926536 : 0);
        const double step = dying ? 0 : std::sin(phase)*0.11;
        const double foot_lift = dying ? 0 : std::max(0.0,std::cos(phase))*0.12;
        const double along = -0.24+leg*0.23;
        const auto hip = at(side*0.18,along,0.2+lift);
        const auto knee = at(side*0.43,along-0.1,0.25+foot_lift*0.6);
        const auto foot = at(side*0.57,along+step,0.025+foot_lift);
        c.line(hip,knee,0x44373e); c.line({hip.x,hip.y-1},{knee.x,knee.y-1},0x9d7b6f);
        c.line(knee,foot,0x775e5a); c.rect(int(knee.x),int(knee.y)-1,2,2,0xbd927c);
        c.line(foot,{foot.x+side*2,foot.y+1},0x2a2c32);
    }
    const auto shell = [&](double along,double radius_x,double radius_y,double bottom,double height,std::uint32_t color) {
        const auto crown = at(0,along,bottom+height);
        for (int facet = 0; facet < 10; ++facet) {
            const double a = facet*0.6283185307, b = (facet+1)*0.6283185307;
            const auto a0 = at(std::cos(a)*radius_x,along+std::sin(a)*radius_y,bottom);
            const auto b0 = at(std::cos(b)*radius_x,along+std::sin(b)*radius_y,bottom);
            c.triangle(crown,a0,b0,tint(color,0.65+0.035*facet));
        }
    };
    const std::uint32_t shell_color = animal.hit_flash > 0 ? 0xffbd9d : dying ? 0x554d51 : snapping ? 0xad6c60 : 0x8c6b62;
    shell(-0.10,0.29,0.36,0.15+lift,dying ? 0.11 : 0.29,shell_color);
    shell(0.25,0.205,0.19,0.14+lift,dying ? 0.09 : 0.2,dying ? 0x423d42 : 0x614b50);
    c.line(at(0,-0.37,0.20+lift),at(0,0.12,0.43+lift),0xb09179);
    for (double across : {-0.115,0.115}) {
        const auto eye = at(across,0.38,0.29+lift);
        if (!dying) c.emissive_rect(int(eye.x)-1,int(eye.y)-1,3,2,winding || snapping ? 0xffb194 : 0xf06459);
        const auto jaw = at(across*1.4,0.4,0.13+lift);
        const double jaw_open = winding ? 0.12+animal.bite_age*0.7 : snapping ? 0.08 : recoiling ? 0.06 : 0;
        const auto tip = at(across*(0.5+jaw_open*2),snapping ? 0.78 : winding ? 0.62 : 0.54,
                            (snapping ? 0.025 : 0.09)+lift-jaw_open);
        c.line(jaw,tip,0xd0b59b);
    }
    if (poised) {
        const auto tell = at(0,0.02,0.64);
        c.rect(int(tell.x),int(tell.y)-4,1,3,0xff9a7b);
        c.rect(int(tell.x),int(tell.y),1,1,0xff9a7b);
    }
    if (snapping) {
        const auto flare = at(0,0.69,0.18+lift);
        c.emissive_rect(int(flare.x)-2,int(flare.y)-2,4,3,0xff8e70);
    }
    if (dying && animal.death_age < 0.5) {
        const auto spark = at(0,0,0.2+animal.death_age*0.5);
        c.emissive_rect(int(spark.x),int(spark.y),2,2,0xff806d);
    }
}
void cartoon_eye_rays(Canvas& c,const Game& game,RobotModel model) {
    if (!game.unit().alive()) return;
    const auto rig = RobotEyes::pose(game,model);
    for (const auto& eye : rig.emitters) {
        if (eye.intensity < 0.05) continue;
        const Vec2 side{-eye.forward.y,eye.forward.x};
        const GridPos start{int(eye.position.x),int(eye.position.y)};
        const auto at = [&](double along,double across) {
            const Vec2 point{eye.position.x+eye.forward.x*along*std::cos(eye.pitch)+side.x*across,
                             eye.position.y+eye.forward.y*along*std::cos(eye.pitch)+side.y*across};
            return c.projection.project(point,eye.elevation+std::sin(eye.pitch)*along);
        };
        for (int segment = 0; segment < 12; ++segment) {
            const double a = 0.04+segment*0.13, b = a+0.13;
            const Vec2 point{eye.position.x+eye.forward.x*b*std::cos(eye.pitch),
                             eye.position.y+eye.forward.y*b*std::cos(eye.pitch)};
            const int x = int(std::floor(point.x)), y = int(std::floor(point.y));
            if (!game.world().in_bounds(x,y)) break;
            const double z = eye.elevation+std::sin(eye.pitch)*b;
            const double floor = surface_height(game.world(),point);
            if (z < floor+0.035 || ((x != start.x || y != start.y) &&
                game.world().solid(x,y) && z < game.world().elevation(x,y)+
                game.world().obstacle_height(x,y))) break;
            const double alpha = std::max(0.0,1-b/1.7)*eye.intensity;
            const auto a0 = at(a,-0.012-a*0.038), a1 = at(a,0.012+a*0.038),
                       b0 = at(b,-0.012-b*0.038), b1 = at(b,0.012+b*0.038);
            if (!a0.visible || !a1.visible || !b0.visible || !b1.visible) break;
            c.blend_quad(a0.screen,a1.screen,b1.screen,b0.screen,0x4faeff,alpha*0.07);
            const auto u0 = at(a,-0.010), u1 = at(a,0.010), v0 = at(b,-0.010), v1 = at(b,0.010);
            c.blend_quad(u0.screen,u1.screen,v1.screen,v0.screen,0x9ce6ff,alpha*0.12);
        }
    }
}
void charger_pad(Canvas& c, const Game& game) {
    if (!game.survival_enabled()) return;
    const Vec2 p = BaseCamp::charger;
    const double z = surface_height(game.world(),p)+0.018, time = game.presentation().clock;
    const auto view = c.projection.project(p,z);
    if (!view.visible || view.depth < 0.08 || view.screen.x < -80 || view.screen.x > width+80 ||
        view.screen.y < top-80 || view.screen.y > bottom+80) return;
    InteractionOutline highlight(c,game.charging(),time,0x6be4f5);
    const auto at = [&](double x,double y,double h = 0.0) { return c.project({p.x+x,p.y+y},z+h); };
    const auto light = game.charging() ? 0xb7faff : 0x57c9df;
    // Low, open-front induction dock: the parking area is visible between rails.
    c.box(p,1.42,1.38,z,0.07,0x344b59);
    c.box(p,1.24,1.20,z+0.07,0.025,0x627883);
    c.quad(at(-0.46,-0.43,0.10),at(0.46,-0.43,0.10),at(0.46,0.48,0.10),at(-0.46,0.48,0.10),0x253b49);
    for (int side : {-1,1}) {
        c.box({p.x+side*0.60,p.y},0.13,1.22,z+0.09,0.10,0x94a8ae);
        c.line(at(side*0.59,-0.53,0.20),at(side*0.59,0.51,0.20),light);
        c.box({p.x+side*0.57,p.y-0.50},0.20,0.22,z+0.19,0.82,0x536e7b);
        c.line(at(side*0.57,-0.36,0.32),at(side*0.57,-0.36,0.94),light);
    }
    // Battery cabinet and segmented charge meter sit behind the robot.
    c.box({p.x,p.y-0.57},0.84,0.24,z+0.12,0.91,0x71868d);
    c.box({p.x,p.y-0.42},0.62,0.035,z+0.67,0.23,0x1e3444);
    for (int i = 0; i < 5; ++i) {
        const auto q = at(-0.23+i*0.115,-0.39,0.79);
        c.emissive_rect(int(q.x)-1,int(q.y)-2,2,4,
            !game.charging() || game.unit().battery_percent() >= (i+1)*20 ? light : 0x395969);
    }
    // Lightning bolt and approach chevrons identify charging without clutter.
    c.line(at(0.10,-0.29,0.11),at(-0.13,0.04,0.11),light);
    c.line(at(-0.13,0.04,0.11),at(0.12,0.04,0.11),light);
    c.line(at(0.12,0.04,0.11),at(-0.08,0.31,0.11),light);
    for (double y : {0.51,0.66}) {
        c.line(at(-0.19,y,0.11),at(0,y-0.12,0.11),light);
        c.line(at(0,y-0.12,0.11),at(0.19,y,0.11),light);
    }
    if (game.charging()) {
        // Travelling bands rise along both induction columns while docked.
        for (int side : {-1,1}) for (int i = 0; i < 3; ++i) {
            const double phase = std::fmod(time*0.8+i/3.0,1.0);
            c.line(at(side*0.53,-0.34,0.27+phase*0.65),
                   at(side*0.36,-0.18,0.27+phase*0.65),tint(light,0.65+0.35*phase));
        }
    }
}
bool camp_cutaway(const Game& game) {
    return BaseCamp::inside(game.unit().center());
}
void injured_astronaut(Canvas& c,const Game& game,Vec2 bed,double ground) {
    const double time = game.presentation().clock;
    const double breath = 0.5-0.5*std::cos(time*2.1);
    const double lift = breath*0.025;
    struct Point { double x,y,z; };
    const auto at = [&](Point p) { return c.project({bed.x+p.x,bed.y+p.y},ground+p.z); };
    constexpr auto suit = 0xd0cbbbu, shaded_suit = 0xb0b0a5u, seam = 0x5a6566u;
    constexpr auto orange = 0xb9753fu, glove = 0x596265u;

    // Rounded pressure suit sections are actual shaded surfaces. They remain
    // attached to the cot in either camera and give elbows/knees real volume.
    const auto rounded = [&](Point center,Point radius,std::uint32_t color) {
        struct Facet { std::array<Point,4> points; double depth,light; };
        std::array<Facet,60> faces{};
        const auto vertex = [&](int band,int slice) {
            const double latitude = -1.57079632679+band*3.14159265359/6;
            const double longitude = slice*6.28318530718/10;
            return Point{center.x+radius.x*std::cos(latitude)*std::cos(longitude),
                         center.y+radius.y*std::cos(latitude)*std::sin(longitude),
                         center.z+radius.z*std::sin(latitude)};
        };
        for (int band = 0; band < 6; ++band) for (int slice = 0; slice < 10; ++slice) {
            auto& face = faces[band*10+slice];
            face.points = {{vertex(band,slice),vertex(band,slice+1),
                            vertex(band+1,slice+1),vertex(band+1,slice)}};
            face.depth = 0;
            for (auto p : face.points) face.depth += p.x+p.y+p.z*1.4;
            const double latitude = -1.57079632679+(band+0.5)*3.14159265359/6;
            const double longitude = (slice+0.5)*6.28318530718/10;
            const double diffuse = -0.34*std::cos(latitude)*std::cos(longitude)-
                                   0.40*std::cos(latitude)*std::sin(longitude)+0.84*std::sin(latitude);
            face.light = 0.66+0.34*std::max(0.0,diffuse);
        }
        std::sort(faces.begin(),faces.end(),[](const Facet& a,const Facet& b) { return a.depth < b.depth; });
        for (const auto& f : faces)
            c.quad(at(f.points[0]),at(f.points[1]),at(f.points[2]),at(f.points[3]),tint(color,f.light));
    };
    // Backpack acts as a low bolster; the torso reclines into it with one knee
    // slightly raised, and the bandaged leg extends under its own weight.
    rounded({-0.06,-0.13,0.48},{0.27,0.34,0.13},0x687475);
    rounded({0.00,0.13,0.49},{0.24,0.21,0.13},shaded_suit);
    rounded({0.00,-0.08,0.60+lift},{0.26,0.31,0.17},suit);
    rounded({-0.14,0.32,0.50},{0.12,0.24,0.11},suit);
    rounded({-0.16,0.51,0.48},{0.11,0.21,0.10},shaded_suit);
    rounded({0.15,0.28,0.56},{0.12,0.20,0.13},shaded_suit);
    rounded({0.18,0.46,0.51},{0.11,0.19,0.12},suit);
    rounded({-0.16,0.66,0.46},{0.13,0.14,0.10},glove);
    rounded({0.19,0.61,0.46},{0.13,0.14,0.11},glove);
    c.line(at({-0.26,0.69,0.43}),at({-0.08,0.74,0.43}),0x3c484c);
    c.line(at({0.09,0.67,0.43}),at({0.30,0.69,0.43}),0x3c484c);
    rounded({-0.15,0.33,0.59},{0.12,0.09,0.025},0xc9c1ac);
    c.line(at({-0.25,0.30,0.61}),at({-0.05,0.34,0.61}),0x976452);
    c.line(at({0.07,0.30,0.67}),at({0.25,0.34,0.67}),orange);
    c.line(at({0.00,-0.22,0.76+lift}),at({0.00,0.10,0.66+lift}),seam);
    c.line(at({-0.19,-0.08,0.73+lift}),at({-0.12,0.12,0.64+lift}),0xe4dfce);

    // Ribbed neck seal and an oval helmet follow the reclined shoulders.
    rounded({0.01,-0.32,0.65+lift*0.4},{0.18,0.13,0.11},seam);
    rounded({0.02,-0.43,0.78+lift*0.4},{0.29,0.27,0.25},suit);
    const auto head = at({0.04,-0.48,0.83+lift*0.4});
    const int hx = int(std::lround(head.x)), hy = int(std::lround(head.y));
    // An inset oval visor: a tapered jaw, side-lit skin and a receding hairline
    // keep facial features legible without making the head a flat square.
    for (int y = -6; y <= 6; ++y) for (int x = -7; x <= 7; ++x) {
        const double r2 = x*x/49.0+y*y/36.0;
        if (r2 > 1) continue;
        const auto rim = y < -2 && x < 3 ? 0xeee6d0u : y > 3 ? 0x8b9693u : 0xb7bcb0u;
        c.pixel(hx+x,hy+y,r2 > 0.71 ? rim : 0x354750);
    }
    for (int y = -4; y <= 4; ++y) for (int x = -4; x <= 4; ++x) {
        if (x*x/21.0+(y+0.4)*(y+0.4)/25.0 > 1 || (y >= 3 && std::abs(x) > 2)) continue;
        const double shade = std::clamp(1.02-x*0.045-y*0.018,0.73,1.12);
        c.pixel(hx+x,hy+y,tint(0xc49370,shade));
    }
    c.line({double(hx-4),double(hy-3)},{double(hx-1),double(hy-4)},0xc8c9bb);
    c.line({double(hx),double(hy-4)},{double(hx+3),double(hy-3)},0x9caaa5);
    c.pixel(hx-4,hy-2,0xe0d8bf);
    c.line({double(hx-3),double(hy-2)},{double(hx-1),double(hy-2)},0x6e5647);
    c.line({double(hx+1),double(hy-2)},{double(hx+3),double(hy-1)},0x6e5647);
    const bool blink = std::fmod(time,4.7) > 4.52;
    c.pixel(hx-2,hy-1,blink ? 0x956b51 : 0x353b3a);
    c.pixel(hx+2,hy,blink ? 0x956b51 : 0x353b3a);
    c.pixel(hx,hy,0xdeb28a);
    c.pixel(hx+1,hy+1,0x92644d);
    c.pixel(hx-3,hy+1,0xa67d60);
    c.pixel(hx+3,hy+1,0x8f6653);
    c.line({double(hx-2),double(hy+3)},{double(hx+2),double(hy+3)},0xc9c8b7);
    c.pixel(hx-3,hy+2,0xb2b7aa);
    c.pixel(hx+3,hy+2,0x9ca99f);
    c.pixel(hx,hy+3,0x775e4c);
    c.pixel(hx+1,hy+4,0xc4c4b3);
    c.line({double(hx-5),double(hy-5)},{double(hx),double(hy-6)},0xc78b4c);
    // Recessed side ear fittings and a tiny glass reflection on the rim.
    c.rect(hx-8,hy-1,2,4,0x667779);
    c.rect(hx+7,hy,2,3,0x59696d);
    c.line({double(hx-5),double(hy-4)},{double(hx-6),double(hy-2)},0xe4ebdf);

    // Arms rest against the torso around a hand-held remote. Only the right
    // glove makes a small deliberate thumb press, and breathing raises the chest.
    const double tap = std::pow(std::max(0.0,std::sin(time*2.7)),8.0)*0.035;
    rounded({-0.24,-0.14,0.63+lift},{0.12,0.20,0.12},suit);
    rounded({-0.22,0.04,0.65+lift},{0.11,0.16,0.10},shaded_suit);
    rounded({0.25,-0.12,0.65+lift},{0.11,0.19,0.12},suit);
    rounded({0.22,0.06,0.68+lift+tap},{0.10,0.14,0.10},shaded_suit);
    c.line(at({-0.32,-0.16,0.72+lift}),at({-0.16,-0.12,0.75+lift}),0xa16a51);
    c.line(at({-0.31,-0.10,0.73+lift}),at({-0.15,-0.06,0.75+lift}),0xd1c5ac);
    c.oriented_box({bed.x,bed.y+0.13},0.31,0.23,ground+0.76+lift,0.08,0x37484f,1.40,-0.12);
    rounded({-0.14,0.15,0.79+lift},{0.075,0.09,0.055},glove);
    rounded({0.14,0.16-tap,0.80+lift+tap},{0.075,0.09,0.055},glove);
    const auto remote = at({0.01,0.16,0.86+lift});
    c.emissive_rect(int(remote.x)-1,int(remote.y),3,1,0x87c6ce);
    c.pixel(int(remote.x)+2,int(remote.y)+1,0xc3a064);
    if (std::fmod(time,2.8) < 0.12) {
        const auto signal = at({0.22,0.08,1.02});
        c.pixel(int(signal.x),int(signal.y),0x83bdc3);
    }
}
void base_camp_cell(Canvas& c, const Game& game, int x, int y) {
    const Vec2 p{x+0.5,y+0.5};
    const double ground = game.world().elevation(x,y), time = game.presentation().clock;
    const auto at = [&](double px,double py,double z) { return c.project({px,py},ground+z); };
    if (BaseCamp::doorway(x,y)) {
        const bool close = distance(game.unit().center(),BaseCamp::entrance) < 2.4;
        InteractionOutline outline(c,true,time,0x43ff79);
        if (!game.campaign()) {
            c.box({p.x,8.28},0.96,0.18,ground+0.005,0.055,0x75868c,{x,y});
            const double outer = x == 6 ? 6.02 : 7.98;
            const double inner = x == 6 ? 6.15 : 7.85;
            c.line(at(outer,8.27,0.06),at(outer,8.18,1.48),0x43ff79);
            c.line(at(inner,8.28,0.08),at(inner,8.19,1.38),0xb8c8c9);
            const auto top_outer=at(outer,8.18,1.48), top_inner=at(inner,8.19,1.38);
            c.line(top_outer,top_inner,0x43ff79);
            const auto lamp=at(outer,8.15,1.22);
            c.emissive_rect(int(lamp.x)-1,int(lamp.y)-1,close ? 4 : 3,close ? 4 : 3,0x43ff79);
            const auto chevron=at(p.x,8.47,0.09);
            c.line({chevron.x-5,chevron.y-2},chevron,0x43ff79);
            c.line(chevron,{chevron.x+5,chevron.y-2},0x43ff79);
            return;
        }
        // Keep the passage visually sealed; map transfer still uses proximity interaction.
        const auto left_bottom = at(6.02,8.18,0.04), right_bottom = at(7.98,8.18,0.04);
        const auto right_top = at(7.98,8.18,1.72), left_top = at(6.02,8.18,1.72);
        c.quad(left_bottom,right_bottom,right_top,left_top,0x344652,{x,y});
        c.quad(at(6.12,8.16,0.12),at(7.88,8.16,0.12),at(7.88,8.16,1.58),at(6.12,8.16,1.58),0x536570,{x,y});
        c.line(at(7.0,8.145,0.14),at(7.0,8.145,1.56),0x2a3943);
        c.line(at(6.2,8.14,0.28),at(7.8,8.14,0.28),0x82949a);
        c.line(at(6.2,8.14,1.42),at(7.8,8.14,1.42),0x82949a);
        const auto lamp = at(7.72,8.12,1.18);
        c.emissive_rect(int(lamp.x)-1,int(lamp.y)-1,close ? 4 : 3,close ? 4 : 3,close ? 0x43ff79 : 0x80a59c);
        return;
    }
    if (BaseCamp::wall(x,y)) {
        // Close the exterior sightline into the cabin; lower the wall only for
        // the interior cutaway view after the player has entered.
        const bool vertical = x == BaseCamp::left || x == BaseCamp::right;
        const double heading = vertical ? 0.0 : 1.57079632679;
        const double wall_height = camp_cutaway(game) ? 1.28 : 2.35;
        c.oriented_box(p,vertical ? 0.17 : 0.92,vertical ? 0.92 : 0.17,
                       ground+0.02,wall_height,0xd7d7df,heading,0,0);
        c.oriented_box({p.x,p.y+0.015},vertical ? 0.19 : 0.94,vertical ? 0.94 : 0.19,
                       ground+0.72,0.14,0x9b1c57,heading,0,0);
        const auto seam_a=vertical ? at(p.x,p.y-0.38,0.38) : at(p.x-0.38,p.y,0.38);
        const auto seam_b=vertical ? at(p.x,p.y+0.38,0.38) : at(p.x+0.38,p.y,0.38);
        c.line(seam_a,seam_b,0x9b9daa);
        return;
    }
    switch (BaseCamp::fixture(x,y)) {
        case CampFixture::Workbench: {
            if(game.mission1()) {
                c.box(p,1.25,0.8,ground+0.05,0.65,0x52697a,{x,y});
                c.box(p,1.4,0.95,ground+0.70,0.10,0xb6c5bd,{x,y});
                c.box(p,0.5,0.42,ground+0.80,0.03,0x314c62,{x,y});
                const auto lens=at(p.x,p.y,1.65+std::sin(time*1.8)*0.045);
                for(int i=0;i<20;++i) { double a=i*6.2831853/20,b=(i+1)*6.2831853/20;
                    c.line({lens.x+std::cos(a)*5,lens.y+std::sin(a)*5},{lens.x+std::cos(b)*5,lens.y+std::sin(b)*5},0x9befff); }
                c.line({lens.x+4,lens.y+4},{lens.x+9,lens.y+10},0x8de9ff);
                c.emissive_rect(int(lens.x)-2,int(lens.y)-2,3,3,0xc5faff);
                break;
            }
            // Low anchor and loose tools beneath the improvised processing rig.
            c.box(p,0.90,0.90,ground+0.01,0.08,0x435159,{x,y});
            c.oriented_box({p.x-0.24,p.y+0.23},0.24,0.55,ground+0.09,0.13,0xaeb1a9,-0.58,0.06);
            c.oriented_box({p.x+0.30,p.y-0.22},0.17,0.34,ground+0.09,0.18,0x394852,0.81,-0.09);
            c.line(at(p.x-0.42,p.y-0.18,0.14),at(p.x+0.34,p.y+0.34,0.16),0xc17d55);
            c.line(at(p.x-0.36,p.y-0.13,0.15),at(p.x+0.22,p.y+0.38,0.17),0x303b43);
            break;
        }
        case CampFixture::Console: {
            const Vec2 q{p.x+0.17,p.y-0.13};
            c.box(p,0.82,0.78,ground+0.01,0.07,0x3f4b51,{x,y});
            c.oriented_box(q,0.60,0.50,ground+0.08,0.54,0x536e78,-0.18,0.04,0.06);
            c.oriented_box({q.x-0.06,q.y-0.13},0.74,0.13,ground+0.58,0.58,0x70868b,-0.18,-0.14);
            c.quad(at(q.x-0.31,q.y+0.025,0.72),at(q.x+0.30,q.y-0.15,0.72),
                at(q.x+0.30,q.y-0.15,1.08),at(q.x-0.31,q.y+0.025,1.08),0x203f4c);
            for (int row = 0; row < 3; ++row) {
                const double h = 0.80+row*0.10;
                c.line(at(q.x-0.24,q.y-0.005,h),at(q.x+0.05+row*0.04,q.y-0.09,h),row == 1 ? 0x9fddbc : 0x6ea6b3);
            }
            for (int lamp = 0; lamp < 3; ++lamp) {
                const auto light = at(q.x-0.20+lamp*0.18,q.y+0.18-lamp*0.05,0.66);
                c.rect(int(light.x),int(light.y),2,1,lamp == int(time*2)%3 ? 0xa1e8c2 : 0x557f88);
            }
            break;
        }
        case CampFixture::Bunk:
            // A field cot assembled from crater stones and a surviving thermal pad.
            {
            const Vec2 bed{p.x+0.10,p.y-0.08};
            c.box(p,0.94,0.94,ground+0.01,0.06,0x444a4b,{x,y});
            for (int i = 0; i < 8; ++i) {
                const double side = i%2 ? 0.43 : -0.43;
                const double along = -0.46+(i/2)*0.31;
                c.box({bed.x+side,bed.y+along},0.22+(i%3)*0.035,0.25,ground+0.13,0.16+(i%2)*0.05,
                      i%3 ? 0x716655 : 0x887761,{x,y});
            }
            c.oriented_box(bed,0.72,0.98,ground+0.20,0.15,0x7b8984,0.09,0.03);
            c.oriented_box({bed.x,bed.y-0.36},0.58,0.25,ground+0.35,0.10,0xc9c5b5,0.09);
            injured_astronaut(c,game,bed,ground-0.05);
            }
            break;
        case CampFixture::Locker:
            c.box(p,0.92,0.88,ground+0.01,0.07,0x424d50,{x,y});
            c.oriented_box({p.x-0.06,p.y+0.05},0.73,1.18,ground+0.08,0.38,0x708991,-0.72,0.12,-0.09);
            c.line(at(p.x-0.40,p.y+0.02,0.48),at(p.x+0.34,p.y+0.21,0.48),0xb8b9ab);
            c.line(at(p.x-0.33,p.y-0.10,0.49),at(p.x+0.20,p.y+0.04,0.49),0x435c64);
            c.oriented_box({p.x+0.36,p.y-0.28},0.26,0.20,ground+0.08,0.20,0xb94c48,0.31);
            break;
        case CampFixture::LifeSupport: {
            c.box(p,0.88,0.78,ground+0.01,0.08,0x45565d,{x,y});
            c.oriented_box({p.x-0.21,p.y-0.05},0.34,0.34,ground+0.09,1.20,0x9aa9a4,0.12,0.10);
            for (double band : {0.31,0.92})
                c.line(at(p.x-0.39,p.y-0.05,band),at(p.x-0.04,p.y-0.05,band+0.03),0x577680);
            c.oriented_box({p.x+0.24,p.y+0.10},0.34,0.94,ground+0.10,0.33,0x899993,0.88,0.14);
            c.line(at(p.x-0.18,p.y,1.28),at(p.x+0.33,p.y+0.31,0.43),0xc07b55);
            c.line(at(p.x+0.33,p.y+0.31,0.43),at(p.x+0.48,p.y+0.18,0.22),0x3b4f56);
            const auto light = at(p.x+0.14,p.y+0.25,0.42);
            c.emissive_rect(int(light.x)-1,int(light.y)-1,3,2,0xa1d4b7);
            break;
        }
        case CampFixture::None: break;
    }
}
// Shallow armor blocks and their inset tile seams match the reference's voxel hull.
void ship_plate(Canvas& c,Vec2 p,double w,double d,double z,double h,std::uint32_t color) {
    HullSpace space(c);
    const GridPos id{BaseCamp::left,BaseCamp::north};
    c.box(p,w,d,z,h,color,id);
    const int nx = std::max(1,int(std::ceil(w/0.62))), ny = std::max(1,int(std::ceil(d/0.62)));
    const double dx = w/nx, dy = d/ny;
    const auto at = [&](double x,double y) { return c.project({x,y},z+h+0.004); };
    for (int x = 0; x < nx; ++x) for (int y = 0; y < ny; ++y) {
        const double left = p.x-w/2+x*dx, north = p.y-d/2+y*dy;
        const auto a = at(left,north), b = at(left+dx,north), e = at(left+dx,north+dy), f = at(left,north+dy);
        c.quad(a,b,e,f,tint(color,0.94+0.02*((x+y*3)%4)),id);
        c.line(a,b,tint(color,0.72)); c.line(a,f,tint(color,0.76));
        c.line(f,e,tint(color,1.06));
    }
}
struct ShipPanel { Vec2 center; double w,d,z,h; std::uint32_t color; };
constexpr std::array<ShipPanel,3> near_wing_panels{{
    {{11.52,9.02},2.45,1.52,0.45,0.72,0xe2ded2},
    {{12.52,10.15},1.95,0.76,0.22,0.49,0xe2ded2},
    {{12.52,10.65},1.95,0.38,0.17,0.49,0xc84c4b}
}};
[[maybe_unused]] ShipPanel near_wing_cell(int panel,int cell) {
    const auto& p = near_wing_panels[panel];
    const int nx = int(std::ceil(p.w/0.62)), ny = int(std::ceil(p.d/0.62));
    const double w = p.w/nx, d = p.d/ny;
    return {{p.center.x-p.w/2+(cell%nx+0.5)*w,p.center.y-p.d/2+(cell/nx+0.5)*d},w,d,p.z,p.h,p.color};
}
[[maybe_unused]] void base_camp_wing_damage(Canvas& c) {
    HullSpace space(c);
    const auto at = [&](double x,double y,double z) { return c.project({x+1.6,y},z); };
    const GridPos id{BaseCamp::left,BaseCamp::north};
    constexpr auto dark = 0x34424eu;
    // Torn starboard wing: exposed black spar, loose red cap and peeled plates.
    c.quad(at(10.25,9.35,0.88),at(11.28,9.24,0.87),at(11.35,9.76,0.56),at(10.48,10.12,0.50),dark,id);
    for (double x : {10.48,10.88,11.18})
        c.line(at(x,9.30,0.92),at(x+0.16,10.12,0.48),0x9c9182);
    c.quad(at(10.48,10.12,0.50),at(11.35,9.76,0.56),at(11.82,10.60,0.11),at(10.94,10.94,0.10),0xddd4c0,id);
    ship_plate(c,{13.20,10.72},0.80,0.48,0.04,0.18,0xb64443);
    c.line(at(10.26,9.42,0.90),at(10.78,9.62,0.78),0x2d3035);
}
[[maybe_unused]] void base_camp_wings(Canvas& c,const Game& game) {
    const auto at = [&](double x,double y,double z) { return c.project({x,y},z); };
    const GridPos id{BaseCamp::left,BaseCamp::north};
    if (!c.projection.project(BaseCamp::center,2.7).visible) return;
    constexpr auto white = 0xd8d4c9u, red = 0xb94947u, dark = 0x313c46u;
    // Old impact scars remain, but there is no intact fuselage or recognizable
    // base spaceship: only two enormous detached wing spans form the shelter.
    const auto ground = [&](double x,double y) { return at(x,y,surface_height(game.world(),{x,y})+0.025); };
    for (double y : {4.45,7.55}) {
        c.blend_quad(ground(10.5,y-0.42),ground(15.6,y-0.25),ground(16.2,y+0.25),ground(10.5,y+0.5),0x1f2024,0.40);
        c.line(ground(12.4,y),ground(15.8,y+0.18),0x4a423c);
    }
    HullSpace space(c);
    // Western wing: broad, bent and snapped into three differently pitched leaves.
    c.quad(at(4.45,3.42,1.70),at(0.82,1.72,0.55),at(0.48,3.18,0.38),at(4.18,4.35,1.86),white,id);
    c.quad(at(4.18,4.36,1.84),at(0.48,3.18,0.38),at(1.02,5.18,0.24),at(4.52,5.32,1.62),tint(white,0.82),id);
    c.quad(at(1.02,5.18,0.25),at(0.42,5.54,0.12),at(1.38,6.46,0.10),at(3.82,5.58,1.18),dark,id);
    c.quad(at(3.95,3.73,1.82),at(1.10,2.42,0.72),at(0.96,2.86,0.59),at(4.08,4.16,1.80),red,id);
    for (double x : {1.35,2.15,2.95,3.72})
        c.line(at(x,2.72+(x-1.3)*0.35,0.70+(x-1.3)*0.33),at(x+0.22,4.78+(x-1.3)*0.12,0.55+(x-1.3)*0.38),0x687277);

    // Eastern wing reaches far across the crater. Missing bites and black spars
    // keep it from reading as a complete aircraft silhouette.
    c.quad(at(11.42,3.55,1.92),at(15.82,1.92,0.48),at(16.28,3.28,0.28),at(11.20,4.62,1.76),white,id);
    c.quad(at(11.20,4.63,1.74),at(16.28,3.28,0.28),at(15.72,5.18,0.18),at(11.48,5.54,1.55),tint(white,0.88),id);
    c.quad(at(12.02,3.51,1.78),at(15.57,2.18,0.53),at(15.72,2.65,0.43),at(11.82,3.98,1.80),red,id);
    c.triangle(at(13.08,4.25,1.38),at(14.22,3.62,0.72),at(14.52,4.71,0.42),dark,id);
    c.triangle(at(15.72,5.18,0.18),at(16.34,4.72,0.10),at(15.30,4.50,0.24),dark,id);
    for (double x : {11.75,12.65,13.65,14.78})
        c.line(at(x,3.74-(x-11.7)*0.24,1.72-(x-11.7)*0.28),at(x+0.15,5.05-(x-11.7)*0.03,1.48-(x-11.7)*0.31),0x697479);

    // Torn hinges at the shelter edge explain how the two spans came to rest here.
    c.box({4.28,4.52},0.46,0.54,1.28,0.38,dark,id);
    c.box({11.35,4.58},0.52,0.56,1.36,0.36,dark,id);
    c.line(at(4.46,4.34,1.61),at(4.82,4.71,1.22),0xb97a59);
    c.line(at(11.18,4.38,1.69),at(10.82,4.77,1.25),0xb97a59);
}
// A single clean rescue aircraft replaces the former camp, detached wings and
// impact debris. The broad lower silhouette is drawn behind cabin objects.
void rescue_aircraft_lower(Canvas& c,const Game&) {
    const GridPos id{BaseCamp::left,BaseCamp::north};
    if (!c.projection.project(BaseCamp::center,2.5).visible) return;
    HullSpace space(c);
    const auto at=[&](double x,double y,double z){ return c.project({x,y},z); };
    constexpr auto white=0xdad9e1u, shade=0xa8a7b5u, dark=0x17183fu;

    // A flatter, more box-like hull reads as a realistic rescue shuttle from the
    // outside, with a simple slab body instead of exaggerated wing surfaces.
    c.quad(at(4.85,3.08,0.16),at(10.98,3.08,0.16),at(11.30,7.75,0.16),at(4.58,7.75,0.16),shade,id);
    c.quad(at(4.58,7.75,0.16),at(11.30,7.75,0.16),at(10.20,9.55,0.10),at(5.70,9.55,0.10),white,id);
    c.quad(at(5.10,3.22,0.22),at(10.76,3.22,0.22),at(10.96,7.42,0.22),at(4.92,7.42,0.22),tint(white,0.86),id);
    c.line(at(4.85,3.08,0.20),at(5.70,9.55,0.12),0xf1eef2);
    c.line(at(11.00,3.08,0.20),at(10.20,9.55,0.12),0x777787);

    // Dark aerodynamic belly and red rear power ports.
    c.quad(at(5.12,7.82,0.13),at(10.92,7.82,0.13),at(8.72,9.30,0.08),at(6.18,9.30,0.08),dark,id);
    for(double x:{6.35,7.0,8.7,9.35}) {
        const auto port=at(x,3.04,0.35);
        c.emissive_rect(int(port.x)-1,int(port.y)-1,3,2,x<8 ? 0xa61a58 : 0x6c143f);
    }
}

// Upper shell is depth-sorted after the cabin. It becomes a clean cutaway near
// the entrance so the player and interior remain readable.
void rescue_aircraft_upper(Canvas& c,const Game& game) {
    HullSpace space(c);
    const GridPos id{BaseCamp::left,BaseCamp::north};
    const auto at=[&](double x,double y,double z){ return c.project({x,y},z); };
    constexpr auto white=0xe1dfe6u, dark=0x17183fu, red=0xa31355u;
    const bool cut=camp_cutaway(game);

    // A boxier upper shell keeps the craft looking like a flat, realistic hull from
    // the outside while still preserving a readable cockpit and cabin shape.
    c.quad(at(5.08,3.15,1.24),at(10.72,3.15,1.24),at(10.48,5.42,1.24),at(5.32,5.42,1.24),white,id);
    c.quad(at(5.32,3.24,1.24),at(10.48,3.24,1.24),at(10.62,7.20,0.84),at(5.20,7.20,0.84),tint(white,0.93),id);
    c.quad(at(5.90,4.18,1.42),at(9.92,4.18,1.42),at(9.76,5.08,1.34),at(6.06,5.08,1.34),dark,id);
    c.line(at(6.06,4.20,1.44),at(9.76,4.20,1.44),0x56588b);

    if (!cut) {
        c.quad(at(5.22,5.42,1.24),at(10.48,5.42,1.24),at(11.02,7.58,0.94),at(4.72,7.58,0.94),white,id);
        c.quad(at(5.05,6.48,1.54),at(10.90,6.48,1.54),at(10.98,6.84,1.42),at(4.96,6.84,1.42),red,id);
        for(double y:{5.72,6.02,6.32})
            c.line(at(5.05,y,1.48),at(10.92,y,1.49),0x92909e);
    } else {
        // Slim roof rails make the cutaway read as an aircraft cabin.
        for(double x:{4.75,11.05}) {
            c.line(at(x,5.20,1.98),at(x,7.72,1.18),white);
            c.line(at(x+(x<8 ? 0.13 : -0.13),5.24,1.91),at(x+(x<8 ? 0.13 : -0.13),7.66,1.12),red);
        }
    }

    // Keep the hull sealed and gate-like from the outside: the rescue craft reads as
    // a solid original structure rather than a visible passenger door or ramp.
    const auto left=at(6.0,8.13,0.10), right=at(8.0,8.13,0.10);
    const auto right_top=at(8.0,8.13,1.68), left_top=at(6.0,8.13,1.68);
    c.quad(left,right,right_top,left_top,0x53616a,id);
    c.quad(at(6.14,8.11,0.18),at(7.86,8.11,0.18),at(7.86,8.11,1.58),at(6.14,8.11,1.58),0x344652,id);
    c.line(at(7.0,8.095,0.20),at(7.0,8.095,1.56),0x263640);
    c.line(at(6.22,8.095,0.32),at(7.78,8.095,0.32),0x82949a);
    c.line(at(6.22,8.095,1.42),at(7.78,8.095,1.42),0x82949a);
}
[[maybe_unused]] void impact_rubble(Canvas& c,const Game& game,bool bury_bow) {
    const auto& world = game.world();
    const auto at = [&](double x,double y,double lift = 0.0) {
        return c.project({x,y},surface_height(world,{x,y})+lift);
    };
    if (bury_bow) {
        // Foreground earth covers the nose where it has ploughed below the rim.
        c.quad(at(1.85,6.20,0.05),at(3.05,6.80,0.26),at(3.54,7.80,0.18),at(1.84,7.23,0.03),0x796752);
        c.triangle(at(1.85,6.20,0.05),at(3.05,6.80,0.26),at(2.10,5.85,0.10),0xa08a6c);
        c.triangle(at(3.05,6.80,0.26),at(3.54,7.80,0.18),at(3.95,7.48,0.04),0x5e5145);
        for (int i = 0; i < 7; ++i) {
            const double x = 2.0+i*0.25, y = 6.9+i*0.08;
            c.line(at(x,y,0.08),at(x+0.18,y+0.16,0.05),i%2 ? 0xb19a78 : 0x51473e);
        }
        return;
    }
    // Broken soil strata follow the actual crater surface, with an irregular
    // inner scorch band and a pale, lifted lip rather than a painted flat circle.
    for (int i = 0; i < 56; ++i) {
        const double a = i*6.28318530718/56, b = (i+1)*6.28318530718/56;
        const auto edge = [&](double angle,double radius) {
            radius += 0.018*std::sin(angle*7)+0.012*std::cos(angle*11);
            const Vec2 p{7.7+7.3*radius*std::cos(angle),5.1+4.8*radius*std::sin(angle)};
            return at(p.x,p.y,0.014);
        };
        const Vec2 center{7.7+8.0*std::cos(a),5.1+5.2*std::sin(a)};
        if (center.x < 1 || center.y < 1 || (center.y > 8 && center.x > 5 && center.x < 13)) continue;
        c.blend_quad(edge(a,0.93),edge(b,0.93),edge(b,1.04),edge(a,1.04),0x4b3e33,0.32);
        c.blend_quad(edge(a,1.10),edge(b,1.10),edge(b,1.22),edge(a,1.22),0xb7a080,0.24);
        if (i%3 != 0) c.line(edge(a,1.16),edge(b,1.16),0x9d896d);
    }
    for (int i = 0; i < 42; ++i) {
        const double a = i*6.28318530718/42;
        const Vec2 p{7.7+8.15*std::cos(a),5.1+5.35*std::sin(a)};
        if (p.x < 1.0 || p.y < 1.0 || (p.y > 8 && p.x > 5 && p.x < 13)) continue;
        const double z = surface_height(world,p), size = 0.15+(i%4)*0.065;
        c.box(p,size*1.2,size,z,0.12+(i%3)*0.07,i%2 ? 0x8f7a61 : 0x74634f);
        if (i%3 == 0) c.line(at(p.x-0.4,p.y,0.015),at(p.x+0.35,p.y+0.15,0.015),0x9c8669);
    }
}
[[maybe_unused]] void base_camp_roof(Canvas& c,const Game& game) {
    HullSpace space(c);
    const auto at = [&](double x,double y,double z) { return c.project({x,y},z); };
    const GridPos id{BaseCamp::left,BaseCamp::north};
    if (!c.projection.project(BaseCamp::center,2.5).visible) return;
    constexpr auto white = 0xd9d5cbu, red = 0xbb4847u, dark = 0x303944u;
    const bool cut = camp_cutaway(game);

    // The habitable floor is no longer an intact fuselage. A torn main wing
    // has come to rest across the crater and is held up as a low survival roof.
    // Its far half remains in cutaway mode, so the shelter still has a clear
    // silhouette while the cot and workstations become visible.
    const auto panel = [&](std::array<Vec2,4> p,std::array<double,4> z,std::uint32_t color) {
        c.quad(at(p[0].x,p[0].y,z[0]),at(p[1].x,p[1].y,z[1]),
               at(p[2].x,p[2].y,z[2]),at(p[3].x,p[3].y,z[3]),color,id);
        for (int edge = 0; edge < 4; ++edge)
            c.line(at(p[edge].x,p[edge].y,z[edge]+0.008),
                   at(p[(edge+1)%4].x,p[(edge+1)%4].y,z[(edge+1)%4]+0.008),
                   edge == 2 ? tint(color,1.10) : tint(color,0.68));
    };
    panel({{{4.05,3.18},{7.68,2.92},{7.54,4.36},{4.38,4.63}}},
          {{2.12,2.34,2.38,2.15}},white);
    panel({{{7.92,2.91},{11.96,3.35},{11.17,4.70},{8.03,4.36}}},
          {{2.34,2.08,2.04,2.38}},tint(white,0.93));
    // A missing central panel exposes the black spar and torn copper harness.
    c.quad(at(7.54,3.02,2.30),at(7.94,2.94,2.32),at(8.05,4.34,2.36),at(7.54,4.36,2.37),dark,id);
    for (double y : {3.18,3.55,3.92,4.24})
        c.line(at(7.61,y,2.36),at(7.98,y-0.03,2.36),0x7b8587);
    c.line(at(7.72,3.13,2.40),at(7.86,3.58,2.18),0xd18b5e);
    c.line(at(7.86,3.58,2.18),at(7.74,4.12,2.05),0x6a4135);

    if (!cut) {
        panel({{{4.38,4.63},{7.54,4.36},{7.16,6.23},{4.73,6.72}}},
              {{2.15,2.38,2.15,1.92}},white);
        panel({{{8.03,4.36},{11.17,4.70},{10.55,6.48},{8.34,6.15}}},
              {{2.38,2.04,1.78,2.13}},white);
        panel({{{4.73,6.72},{7.16,6.23},{6.63,8.06},{5.12,8.42}}},
              {{1.92,2.15,1.45,1.25}},tint(white,0.88));
        panel({{{8.34,6.15},{10.55,6.48},{9.82,8.27},{8.58,7.86}}},
              {{2.13,1.78,1.24,1.46}},tint(white,0.91));
        // Rescue-red identification stripe is split by the impact tear.
        c.quad(at(4.55,5.56,2.05),at(7.35,5.18,2.29),at(7.27,5.55,2.25),at(4.63,5.94,1.99),red,id);
        c.quad(at(8.17,5.19,2.29),at(10.90,5.51,1.92),at(10.78,5.88,1.86),at(8.25,5.55,2.24),red,id);
        // Ragged hole above the controller and bunk: the dark underskin makes
        // the astronaut faintly visible before the player enters the shelter.
        c.triangle(at(8.25,6.17,2.11),at(9.10,6.27,1.98),at(8.56,7.18,1.64),dark,id);
        c.line(at(8.25,6.17,2.13),at(8.56,7.18,1.66),0xb9afa0);
        c.line(at(9.10,6.27,2.00),at(8.56,7.18,1.66),0x746e68);
    } else {
        // In cutaway, only broken ribs remain over the near half.
        for (double x : {4.72,10.45}) {
            c.line(at(x,4.45,2.22),at(x-0.12,7.72,1.48),0xc8c6bd);
            c.line(at(x-0.03,4.46,2.18),at(x+0.18,7.62,1.42),dark);
        }
    }

    // Bent landing spars now serve as shelter columns, visibly tying the wing
    // roof to the recessed base rather than making it float above the room.
    for (const auto& support : std::array<std::array<double,5>,4>{{
            {{4.62,4.38,2.14,4.88,3.98}},{{10.98,4.55,2.01,10.72,4.10}},
            {{5.05,7.91,1.31,5.30,7.54}},{{9.95,7.78,1.35,9.66,7.45}}}}) {
        c.line(at(support[0],support[1],support[2]),at(support[3],support[4],0.20),dark);
        c.line(at(support[0]+0.06,support[1],support[2]),at(support[3]+0.06,support[4],0.20),0x8b9494);
    }
    // Orange emergency lamps wash the rock cot and workbench without adding a
    // new gameplay light source.
    for (Vec2 lamp : {Vec2{5.15,4.48},Vec2{9.92,4.53}}) {
        const auto q = at(lamp.x,lamp.y,1.83);
        c.emissive_rect(int(q.x)-1,int(q.y)-1,3,2,0xe7a45d);
    }

    // Broken hinge blocks and severed actuators are the only raised mechanisms;
    // no cockpit, fuselage or engine body remains around the shelter.
    c.box({4.52,3.48},0.62,0.44,1.70,0.31,dark,id);
    c.box({11.36,3.88},0.66,0.48,1.48,0.34,dark,id);
    c.line(at(4.76,3.45,1.91),at(5.12,3.72,1.68),0x9b876f);
    c.line(at(4.75,3.52,1.84),at(5.08,3.80,1.55),0xc07856);
    c.line(at(11.62,3.69,1.68),at(11.98,3.53,1.45),0x9b876f);
    c.line(at(11.62,3.79,1.61),at(12.02,3.92,1.34),0xc07856);

    // Saw-tooth tears along both exposed edges replace a clean manufactured
    // outline with visibly shattered, heat-darkened wing metal.
    for (int i = 0; i < 9; ++i) {
        const double x = 4.30+i*0.82;
        const double y = 3.08+0.05*std::sin(i*2.1);
        c.triangle(at(x,y,2.17+0.02*i),at(x+0.34,y-0.25,2.02+0.02*i),
                   at(x+0.61,y+0.03,2.16+0.015*i),i%3 ? tint(white,0.72) : dark,id);
    }
}

struct CrashFragment { Vec2 p; double w,d,h,heading; std::uint32_t color; };
constexpr std::array<CrashFragment,8> crash_fragments{{
    {{3.05,9.72},1.18,0.42,0.16,-0.35,0xd1ccc1},
    {{4.18,10.45},0.56,0.34,0.20, 0.62,0xb74746},
    {{13.72,3.02},1.42,0.46,0.18,0.24,0xd7d2c6},
    {{14.55,4.12},0.48,0.31,0.22,-0.71,0x35414b},
    {{14.18,8.05},1.06,0.39,0.17,0.88,0xbe4b49},
    {{2.42,3.28},0.74,0.36,0.15,0.48,0x34404a},
    {{5.18,1.62},0.92,0.31,0.14,-0.20,0xd5d0c5},
    {{11.92,9.22},0.62,0.28,0.18,0.35,0x9fa49f}
}};
[[maybe_unused]] void crash_fragment(Canvas& c,const Game& game,int index) {
    const auto& fragment = crash_fragments[std::size_t(index)];
    const double z = surface_height(game.world(),fragment.p)+0.025;
    c.oriented_box(fragment.p,fragment.w,fragment.d,z,fragment.h,fragment.color,fragment.heading,0.12*(index%3-1));
    const Vec2 forward{std::cos(fragment.heading),std::sin(fragment.heading)};
    const Vec2 right{forward.y,-forward.x};
    const auto end = [&](double across,double along,double lift) {
        return c.project({fragment.p.x+right.x*across+forward.x*along,
                          fragment.p.y+right.y*across+forward.y*along},z+lift);
    };
    c.line(end(-fragment.w*0.35,-fragment.d*0.35,fragment.h+0.01),
           end(fragment.w*0.35,fragment.d*0.35,fragment.h+0.01),0x4b5053);
    // Exposed rods protrude from alternating fragments and make the scatter
    // read as torn machinery rather than ordinary rocks.
    if (index%2 == 0) {
        c.line(end(fragment.w*0.36,0,fragment.h*0.7),end(fragment.w*0.72,0.08,fragment.h*0.45),0x858c88);
        c.line(end(fragment.w*0.69,0.08,fragment.h*0.43),end(fragment.w*0.84,0.12,fragment.h*0.30),0xc17b57);
    }
}
void interior_wreckage_floor(Canvas& c,const Game& game) {
    struct Scrap { Vec2 p; double w,d,heading; std::uint32_t color; };
    constexpr std::array<Scrap,4> scraps{{
        {{4.76,4.05},0.65,0.24,-0.18,0xa5aaa3}, {{5.12,3.86},0.42,0.20,0.12,0x586368},
        {{10.82,7.38},0.52,0.22,0.24,0x7a8583}, {{10.48,7.64},0.30,0.19,0.46,0xa99b84}
    }};
    for (std::size_t i = 0; i < scraps.size(); ++i) {
        const auto& part = scraps[i];
        const double z = surface_height(game.world(),part.p)+0.012;
        c.oriented_box(part.p,part.w,part.d,z,0.035+(i%3)*0.018,part.color,part.heading,
                       0.025*(int(i%3)-1),0.035*(int((i+1)%3)-1));
    }
    const double floor = BaseCamp::floor_elevation+0.085;
    const auto at = [&](double x,double y,double lift = 0.0) { return c.project({x,y},floor+lift); };
    // Short cable runs stay beside the salvage piles, away from the clear route.
    constexpr std::array<std::array<Vec2,4>,2> cables{{
        std::array<Vec2,4>{{{4.65,5.62},{4.96,5.78},{5.48,5.62},{5.72,5.80}}},
        std::array<Vec2,4>{{{10.78,7.42},{10.53,7.10},{10.67,6.68},{10.32,6.42}}}
    }};
    for (const auto& cable : cables) {
        for (int point = 0; point < 3; ++point) {
            c.line(at(cable[point].x,cable[point].y),at(cable[point+1].x,cable[point+1].y),
                   point%2 ? 0x374247 : 0x85644e);
        }
        const auto plug = at(cable[3].x,cable[3].y,0.015);
        c.rect(int(plug.x)-1,int(plug.y)-1,3,2,0x718086);
    }
}
void base(Canvas& c, const Game& game) {
    const auto p = game.refinery().position();
    const auto& pose = game.presentation();
    const bool processing = pose.active_activity && pose.active_activity->crafting;
    const double cycle = processing ? pose.active_activity->age : pose.clock*0.15;
    const double ground = surface_height(game.world(),p);
    const auto at = [&](Vec2 point,double lift) { return c.project(point,ground+lift); };
    // A broad amber worktop, four legs and a tool rack read as a craft table.
    {
        InteractionOutline highlight(c,processing,pose.clock,0xf4bc70);
        for (double x : {-0.53,0.53}) for (double y : {-0.32,0.32})
            c.box({p.x+x,p.y+y},0.13,0.13,ground+0.05,0.70,0x56636c);
        c.box(p,1.14,0.65,ground+0.24,0.10,0x364852);
        c.box(p,1.40,0.94,ground+0.74,0.13,0xb9a17d);
        c.box(p,1.24,0.78,ground+0.87,0.035,0x52616a);
        c.line(at({p.x-0.68,p.y+0.48},0.85),at({p.x+0.68,p.y+0.48},0.85),0xffcb7e);
        // Drawer bank below the top; clean front edge keeps the table silhouette.
        c.box({p.x+0.37,p.y},0.40,0.62,ground+0.36,0.34,0x71828a);
        for (double z : {0.44,0.59})
            c.line(at({p.x+0.28,p.y+0.32},z),at({p.x+0.47,p.y+0.32},z),0xd2ba8d);
        c.box({p.x,p.y-0.39},1.18,0.08,ground+0.90,0.59,0x364853);
        for (int i = 0; i < 4; ++i) {
            const double x = p.x-0.43+i*0.21;
            c.line(at({x,p.y-0.33},1.06),at({x,p.y-0.33},1.34),0xc1c7bc);
            c.box({x,p.y-0.32},0.12,0.055,ground+1.28,0.08,i%2 ? 0xdea35f : 0x92b8bf);
        }
        // Workpiece and compact vice on an inset fabrication mat.
        c.box({p.x-0.15,p.y+0.05},0.62,0.43,ground+0.91,0.025,0x263f49);
        c.box({p.x-0.16,p.y+0.06},0.31,0.24,ground+0.94,0.09,0xb2c6c5);
        c.box({p.x+0.48,p.y+0.19},0.22,0.28,ground+0.91,0.16,0x8298a0);
        c.line(at({p.x+0.33,p.y+0.19},1.02),at({p.x+0.68,p.y+0.19},1.02),0xe6c38e);
        const double stroke = processing ? (0.5-0.5*std::cos(cycle*12))*0.20 : 0;
        c.box({p.x-0.16,p.y-0.16},0.10,0.12,ground+1.07+stroke,0.18,0xd8a65f);
        c.line(at({p.x-0.16,p.y-0.34},1.44),at({p.x-0.16,p.y-0.16},1.24+stroke),0xc3ceca);
        const auto status = at({p.x+0.42,p.y-0.33},1.42);
        c.emissive_rect(int(status.x)-2,int(status.y)-1,5,2,processing ? 0xffdf9a : 0xc99f66);
        if (processing) {
            const double progress = std::clamp(cycle/pose.active_activity->duration,0.0,1.0);
            const auto q = at({p.x-0.15,p.y+0.05},1.06);
            c.emissive_rect(int(q.x)-2,int(q.y)-1,4,2,0xffd895);
            c.line(at({p.x-0.6,p.y+0.48},0.87),
                   at({p.x-0.6+1.2*progress,p.y+0.48},0.87),0xffedba);
        }
    }
    // The detached utility arrays sit west of the habitat, keeping the lit
    // entrance and its approach clear.
    for (const Vec2 q : BaseCamp::solar) {
        const double ground = surface_height(game.world(),q);
        c.box(q,0.1,0.1,ground,0.7,0x77929b); c.box(q,1.1,0.8,ground+0.7,0.08,0x3b6b92);
        for (int j = 0; j < 4; ++j) {
            const auto a = c.project({q.x-0.43+j*0.27,q.y-0.38},ground+0.8), b = c.project({q.x-0.43+j*0.27,q.y+0.38},ground+0.8);
            c.line(a,b,0x8dbbc9);
        }
    }
}
void rocket(Canvas& c, const Game& game) {
    const auto p = game.rocket().ground_socket();
    const bool launched = game.rocket().launched();
    const double time = game.presentation().clock;
    const double lift = launched ? std::pow(game.climax().launch_elapsed(),2)*0.6 : 0;
    const double ground = game.world().elevation(int(p.x),int(p.y)), z = ground+lift;
    constexpr double tau = 6.283185307179586;
    constexpr int sides = 20;
    const auto radial = [&](double radius,double angle) { return Vec2{p.x+radius*std::cos(angle),p.y+radius*std::sin(angle)}; };
    // The launch table and service mast stay on the ground as the vehicle rises.
    c.box(p,1.8,1.7,ground,0.18,0x53616a);
    const auto throat = c.project(p,ground+0.185);
    for (int i = 0; i < sides; ++i)
        c.triangle(throat,c.project(radial(0.43,i*tau/sides),ground+0.19),
            c.project(radial(0.43,(i+1)*tau/sides),ground+0.19),0x29333b);
    for (int i = 0; i < 4; ++i) {
        const auto foot = radial(0.69,i*tau/4+0.7853981634);
        c.box(foot,0.18,0.18,ground+0.18,0.12,0x89918e);
    }
    const Vec2 mast{p.x-0.79,p.y-0.64};
    c.box(mast,0.16,0.18,ground+0.18,2.95,0x56646c);
    for (int i = 0; i < 5; ++i)
        c.line(c.project({mast.x-0.09,mast.y},ground+0.45+i*0.5),
            c.project({mast.x+0.09,mast.y},ground+0.8+i*0.5),0x93a09c);
    if (!launched) {
        c.line(c.project(mast,ground+2.76),c.project({p.x-0.28,p.y-0.26},ground+2.76),0xb4b6a3);
        c.line(c.project(mast,ground+2.68),c.project({p.x-0.28,p.y-0.26},ground+2.68),0x414d54);
    }
    if (launched) {
        // Layered translucent exhaust widens below the bell and loses opacity
        // toward its turbulent tail instead of painting a rectangular flame.
        const auto nozzle = c.project(p,z+0.29);
        const double flicker = 0.5+0.5*std::sin(time*37)+0.15*std::sin(time*71);
        const double end_z = std::max(ground+0.19,z+0.29-(2.15+flicker*0.55));
        const auto tail = c.project(p,end_z);
        const double length = std::max(1.0,distance(nozzle,tail));
        const Vec2 axis{(tail.x-nozzle.x)/length,(tail.y-nozzle.y)/length}, normal{-axis.y,axis.x};
        const double radius = std::max(2.0,distance(nozzle,c.project({p.x+0.14,p.y-0.14},z+0.29)));
        const auto jet = [&](double along,double across) {
            const double wobble = std::sin(time*49+along*9)*along*0.65;
            return Vec2{nozzle.x+axis.x*length*along+normal.x*(across*radius+wobble),
                        nozzle.y+axis.y*length*along+normal.y*(across*radius+wobble)};
        };
        c.blend_quad(jet(0,-0.45),jet(0,0.45),jet(0.28,1.3),jet(0.28,-1.3),0xf48d53,0.28);
        c.blend_quad(jet(0.28,-1.3),jet(0.28,1.3),jet(0.7,0.85),jet(0.7,-0.85),0xe7814e,0.18);
        c.blend_triangle(jet(0.7,-0.85),jet(0.7,0.85),jet(1,0),0xcc6850,0.09);
        c.blend_quad(jet(0,-0.33),jet(0,0.33),jet(0.3,0.62),jet(0.3,-0.62),0xffd193,0.85);
        c.blend_triangle(jet(0.3,-0.62),jet(0.3,0.62),jet(0.84,0),0xffae65,0.48);
        c.blend_triangle(jet(0,-0.25),jet(0,0.25),jet(0.43,0),0xfff7dc,0.96);
        if (lift < 1.8) {
            const double glow = (1-lift/1.8)*0.12;
            for (int i = 0; i < sides; ++i)
                c.blend_triangle(c.project(p,ground+0.2),c.project(radial(0.85,i*tau/sides),ground+0.2),
                    c.project(radial(0.85,(i+1)*tau/sides),ground+0.2),0xffb477,glow);
        }
    }
    // Rings of shaded facets form a circular hull and curved fairing. This is an
    // original small launch vehicle, with no cockpit window or crystal nose.
    std::array<int,sides> order{};
    for (int i = 0; i < sides; ++i) order[i] = i;
    std::sort(order.begin(),order.end(),[](int a,int b) {
        const double aa = (a+0.5)*tau/sides, bb = (b+0.5)*tau/sides;
        return std::cos(aa)+std::sin(aa) < std::cos(bb)+std::sin(bb);
    });
    const auto section = [&](double low,double high,double lower_radius,double upper_radius,std::uint32_t color) {
        for (int i : order) {
            const double a = i*tau/sides, b = (i+1)*tau/sides, mid = (a+b)*0.5;
            const auto sun = SceneShadows::sun_shadow;
            const double diffuse = std::max(0.0,(-std::cos(mid)*sun.x-std::sin(mid)*sun.y)/std::hypot(sun.x,sun.y));
            const double sheen = std::pow(std::max(0.0,(-std::cos(mid)+std::sin(mid))*0.7071067812),8)*0.2;
            const double light = 0.67+diffuse*0.32+sheen;
            c.quad(c.project(radial(lower_radius,a),z+low),c.project(radial(lower_radius,b),z+low),
                c.project(radial(upper_radius,b),z+high),c.project(radial(upper_radius,a),z+high),tint(color,light));
        }
    };
    const auto fin = [&](double angle) {
        const auto root = radial(0.405,angle), tip = radial(0.77,angle);
        const Vec2 edge{tip.x-std::sin(angle)*0.055,tip.y+std::cos(angle)*0.055};
        c.triangle(c.project(root,z+1.49),c.project(tip,z+0.58),c.project(root,z+0.57),0x8c9797);
        c.triangle(c.project(root,z+1.49),c.project(edge,z+0.62),c.project(tip,z+0.58),0xc5ceca);
        c.line(c.project(root,z+1.49),c.project(edge,z+0.62),0xd6dcd3);
    };
    for (int i = 0; i < 4; ++i) {
        const double angle = i*tau/4;
        if (std::cos(angle)+std::sin(angle) < 0) fin(angle);
    }
    // Bell throat, expanding nozzle, heat-resistant skirt and two tank stages.
    section(0.29,0.65,0.23,0.12,0x56616a);
    section(0.29,0.34,0.245,0.24,0x95a19e);
    section(0.65,1.04,0.36,0.425,0x4d5961);
    const auto shell = game.rocket().hull_welded() ? 0xe1e6dfu : 0xa5aba7u;
    section(1.04,2.65,0.425,0.425,shell);
    section(1.06,1.12,0.432,0.432,0xbd8f70);
    section(2.65,2.82,0.429,0.412,0x4b5962);
    section(2.82,3.77,0.412,0.412,shell);
    section(3.77,3.89,0.412,0.46,0xc1ccc7);
    section(3.89,4.42,0.46,0.46,0xe8ece5);
    const double nose_z[] = {4.42,4.65,4.86,5.04,5.16,5.21};
    const double nose_r[] = {0.46,0.425,0.34,0.22,0.095,0.006};
    for (int i = 0; i < 5; ++i) section(nose_z[i],nose_z[i+1],nose_r[i],nose_r[i+1],0xe8ece5);
    section(3.87,3.91,0.466,0.466,0x6f7e83);
    // Small tank seams and the external service conduit give the scale of sheet
    // metal instead of large decorative panels.
    for (double angle : {0.12,1.48}) {
        const auto seam = radial(0.43,angle);
        c.line(c.project(seam,z+1.25),c.project(seam,z+2.5),tint(shell,0.6));
    }
    const auto conduit = radial(0.445,0.57);
    c.line(c.project(conduit,z+1.16),c.project(conduit,z+3.68),0x87938f);
    for (double height : {1.3,1.95,2.45,3.05,3.55}) {
        const auto a = radial(0.449,0.49), b = radial(0.449,0.65);
        c.line(c.project(a,z+height),c.project(b,z+height),0xcbd3cd);
    }
    for (int i = 0; i < 4; ++i) {
        const double angle = i*tau/4;
        if (std::cos(angle)+std::sin(angle) >= 0) fin(angle);
    }
}
}
namespace {
// Sample only the receiving floor or fixture top. Opaque geometry is drawn
// afterwards, so a shadow cannot paint over a wall or the robot itself.
void ship_robot_shadow(Canvas& c,const Game& game,const ShipRect* support=nullptr,double receiver=0) {
    const auto p=game.unit().center();const auto casters=SceneShadows::robot_casters(game);
    const double extent=1.5+std::max(0.0,game.unit().elevation()-receiver)*.4;
    int left=width,right=-1,first=bottom,last=top-1;
    for(double dx:{-extent,extent}) for(double dy:{-extent,extent}) {
        const auto q=c.project({p.x+dx,p.y+dy},receiver);
        left=std::min(left,int(std::floor(q.x)));right=std::max(right,int(std::ceil(q.x)));
        first=std::min(first,int(std::floor(q.y)));last=std::max(last,int(std::ceil(q.y)));
    }
    for(int y=std::max(top,first);y<=std::min(bottom-1,last);++y)
        for(int x=std::max(0,left);x<=std::min(width-1,right);++x) {
            const auto q=c.projection.unproject({x+.5,y+.5},receiver);
            if(!q) continue;
            const int gx=int(std::floor(q->x)),gy=int(std::floor(q->y));
            if(support ? !support->contains(gx,gy) : !ShipInterior::floor(gx,gy)) continue;
            double amount=0;
            for(const auto& caster:casters) amount=std::max(amount,SceneShadows::opacity_at(*q,receiver,caster));
            if(amount<.001) continue;
            const auto old=c.pixels[y*width+x];std::uint32_t color=0;
            for(int shift:{0,8,16}) {
                const double gain=shift==16 ? .95 : shift==8 ? .87 : .72;
                color|=unsigned(((old>>shift)&255)*(1-amount*gain))<<shift;
            }
            c.pixels[y*width+x]=0xff000000|color;
        }
}
std::uint32_t ship_debris_seed(int x,int y) {
    std::uint32_t n=std::uint32_t(x)*374761393u+std::uint32_t(y)*668265263u+971u;
    n=(n^(n>>13))*1274126177u;return n^(n>>16);
}
void ship_crash_object(Canvas& c,Vec2 p,double z,std::uint32_t seed,double scale=1) {
    const double angle=(seed%628)*.01,cs=std::cos(angle),sn=std::sin(angle);
    const auto at=[&](double x,double y,double h=0) {
        return c.project({p.x+(x*cs-y*sn)*scale,p.y+(x*sn+y*cs)*scale},z+h*scale);
    };
    const auto plate=[&](double w,double d,double h,std::uint32_t color) {
        c.oriented_box(p,w*scale,d*scale,z,h*scale,color,angle);
    };
    // Low, decorative wreckage never changes deck collision or interaction.
    c.blend_quad(at(-.35,-.28),at(.35,-.28),at(.35,.28),at(-.35,.28),0x071119,.24);
    switch(seed%3) {
    case 0: // Loose open-ended wrench.
        c.line(at(-.28,-.23,.035),at(.16,.2,.035),0xb4c0bf);
        c.line(at(-.3,-.2,.035),at(.13,.23,.035),0x6f8794);
        c.line(at(.16,.2,.035),at(.27,.17,.035),0xb4c0bf);
        c.line(at(.16,.2,.035),at(.14,.32,.035),0xb4c0bf);
        break;
    case 1: // A disconnected cable and coupling.
        for(int i=0;i<9;++i) {
            const double a=i*.65,b=(i+1)*.65;
            c.line(at(std::cos(a)*.3,std::sin(a)*.24,.025),at(std::cos(b)*.3,std::sin(b)*.24,.025),0x526977);
        }
        c.line(at(.29,0,.035),at(.48,-.23,.035),0x9f7950);
        plate(.12,.19,.09,0x809393);
        break;
    default: // Loose electronics with exposed copper tracks.
        plate(.42,.53,.05,0x355b50);
        c.line(at(-.13,-.19,.06),at(-.13,.2,.06),0xb69a59);
        c.line(at(-.13,.05,.06),at(.14,.05,.06),0xb69a59);
        c.quad(at(-.04,-.14,.07),at(.13,-.14,.07),at(.13,-.01,.07),at(-.04,-.01,.07),0x152833);
        break;
    }
}
constexpr std::array<Vec2,5> ship_toppled_seats{{{13.1,20.2},{15.4,17.9},{18.9,20.1},{18.0,23.6},{13.7,23.6}}};
void ship_toppled_seat(Canvas& c,int index) {
    const auto p=ship_toppled_seats[index];
    const double angle=std::array<double,5>{{.3,-.8,1.9,2.7,-1.3}}[index];
    const double cs=std::cos(angle),sn=std::sin(angle);
    const auto at=[&](double x,double y,double z){return c.project({p.x+x*cs-y*sn,p.y+x*sn+y*cs},z);};
    // Fallen seat: broad backrest on the deck, cushion tipped up, exposed legs.
    c.blend_quad(at(-.55,-.7,.01),at(.55,-.7,.01),at(.55,.7,.01),at(-.55,.7,.01),0x08121b,.25);
    c.oriented_box(p,.8,.9,.04,.13,0x3c5568,angle);
    c.quad(at(-.43,-.57,.18),at(.43,-.57,.18),at(.43,.19,.18),at(-.43,.19,.18),0x56758b);
    c.quad(at(-.43,.19,.18),at(.43,.19,.18),at(.43,.38,.49),at(-.43,.38,.49),0x6b8799);
    for(double side:{-.32,.32}) {
        c.line(at(side,.36,.43),at(side,.73,.22),0x9babb1);
        c.line(at(side,.73,.22),at(side,.76,.12),0x475d6b);
        c.line(at(side,-.48,.19),at(side,-.7,.38),0x8097a4);
    }
    c.line(at(-.24,-.37,.19),at(.02,-.19,.19),0x253c4b);
}
void ship_injured_astronaut(Canvas& c,double time) {
    const Vec2 cot{9.15,17.45};
    const double breath=std::sin(time*1.3)*.012;
    // Reference suit: white shell, navy visor and gloves, orange chest controls.
    c.box({cot.x+.35,cot.y},1.0,.68,.48,.32+breath,0xdce3ed);
    c.box({cot.x+.30,cot.y},.58,.48,.81+breath,.05,0x677489);
    c.box({cot.x+.30,cot.y},.46,.37,.86+breath,.04,0xe6ebf1);
    c.box({cot.x+.30,cot.y},.23,.12,.90+breath,.025,0xd58b55);
    for(double side:{-.30,.30}) {
        c.box({cot.x+1.02,cot.y+side*.65},.72,.25,.48,.22,0xd7dfeb);
        c.box({cot.x+1.35,cot.y+side*.65},.22,.29,.48,.18,0x283247);
        c.box({cot.x+.22,cot.y+side*1.45},.85,.21,.48,.23,0xc6d1e1);
        c.box({cot.x+.63,cot.y+side*1.45},.20,.24,.48,.21,0x30394e);
        c.box({cot.x-.42,cot.y+side},.22,.13,.57,.32,0x7c8a9e);
    }
    c.box({cot.x-.42,cot.y},.68,.72,.50,.44,0xe1e7f0);
    c.box({cot.x-.42,cot.y},.50,.55,.94,.035,0x161c3d);
    c.box({cot.x-.49,cot.y-.10},.22,.14,.976,.012,0x434b74);
    c.line(c.project({cot.x-.68,cot.y-.26},.98),c.project({cot.x-.2,cot.y-.26},.98),0x99abc9);

    // Floating diagnostic pane faces the cot and carries compact status readouts.
    const double y=19.5;
    const auto q=[&](double x,double z){return c.project({x-2.6,y},z);};
    const double pulse=.85+.15*std::sin(time*2.1);
    c.blend_quad(q(11.10,1.12),q(12.80,1.12),q(12.80,2.16),q(11.10,2.16),0x36c9e8,.25);
    c.line(q(11.10,1.12),q(12.80,1.12),tint(0x76eeff,pulse));
    c.line(q(12.80,1.12),q(12.80,2.16),0x46cce8);
    c.line(q(12.80,2.16),q(11.10,2.16),0x46cce8);
    c.line(q(11.10,2.16),q(11.10,1.12),0x46cce8);
    c.emissive_line(q(11.25,2.00),q(12.57,2.00),0x8eeeff);
    c.emissive_line(q(11.25,1.82),q(11.80,1.82),0x61d9ef);
    c.emissive_line(q(11.25,1.65),q(12.12,1.65),0x61d9ef);
    c.emissive_line(q(11.25,1.48),q(11.63,1.48),0x61d9ef);
    c.emissive_line(q(11.82,1.48),q(12.53,1.48),0xe7b74f);
    c.emissive_rect(int(q(12.49,1.82).x),int(q(12.49,1.82).y),3,2,0xef6757);
    c.emissive_line(q(11.25,1.30),q(11.66,1.30),0x70e8b5);
    c.emissive_line(q(11.82,1.30),q(12.35,1.30),0x70e8b5);
}
void ship_wall_tvs(Canvas& c,const Game& game,int x,int y) {
    if(y!=31) return;
    for(int channel=0;channel<int(ShipInterior::televisions.size());++channel) {
        const auto tv=ShipInterior::televisions[channel];const double left=tv.x-1.55,right=tv.x+1.55;
        if(x+1<=left || x>=right) continue;
        const auto at=[&](double px,double z){return c.project({px,tv.y},z+1.1);};
        const auto panel=[&](double a,double b,double low,double high,std::uint32_t color,bool lit) {
            a=std::max(a,double(x));b=std::min(b,x+1.0);if(a>=b) return;
            if(lit) c.emissive_quad(at(a,low),at(b,low),at(b,high),at(a,high),color);
            else c.quad(at(a,low),at(b,low),at(b,high),at(a,high),color);
        };
        const auto trace=[&](double a,double za,double b,double zb,std::uint32_t color) {
            if(std::abs(b-a)<.00001) {if(a<x || a>x+1) return;}
            else {
                if(b<a) {std::swap(a,b);std::swap(za,zb);}
                if(b<x || a>x+1) return;
                const double start=std::max(a,double(x)),end=std::min(b,x+1.0),slope=(zb-za)/(b-a);
                zb=za+(end-a)*slope;za+=(start-a)*slope;a=start;b=end;
            }
            c.emissive_line(at(a,za),at(b,zb),color);
        };
        panel(left,right,.64,1.79,0x3e5467,false);
        panel(left+.08,right-.08,.72,1.71,0x0b1724,false);
        panel(left+.16,right-.16,.79,1.64,0x103851,true);
        const double time=game.presentation().clock;
        if(channel==0) {
            for(int i=0;i<35;++i) {
                const double a=left+.25+i*.075,b=a+.075;
                trace(a,1.2+.22*std::sin(i*.48+time*1.9),b,1.2+.22*std::sin((i+1)*.48+time*1.9),0x69d6df);
            }
        } else if(channel==1) {
            for(int i=0;i<32;++i) {
                const double a=i*.19634954,b=(i+1)*.19634954;
                trace(tv.x+.72*std::cos(a),1.23+.29*std::sin(a),tv.x+.72*std::cos(b),1.23+.29*std::sin(b),0x4d9ccf);
            }
            trace(tv.x,1.23,tv.x+.72*std::cos(time*.9),1.23+.29*std::sin(time*.9),0x82edcf);
        } else {
            for(int i=0;i<4;++i) trace(left+.25,.9+i*.17,right-.25,.9+i*.17,0x236b97);
            for(int i=0;i<10;++i) {
                const double px=left+.3+(i*17%25)*.1,z=.93+(i*7%13)*.045;
                if(px<x || px>=x+1) continue;
                const auto q=at(px,z);c.emissive_rect(int(q.x),int(q.y),2,1,i==int(time*.8)%10 ? 0xbceaf3 : 0x58abcb);
            }
        }
        // Failed LCD patches and intermittent static sit beneath fixed glass
        // fractures. Use simulation time so redraws and pausing stay stable.
        const auto sx=[&](double u){return left+.17+u*2.76;};
        const auto sz=[](double v){return .80+v*.82;};
        const auto patch=[&](double u,double v,double w,double h,std::uint32_t color) {
            panel(sx(u),sx(std::min(1.0,u+w)),sz(v),sz(std::min(1.0,v+h)),color,true);
        };
        const double impact=channel==0 ? .65 : channel==1 ? .34 : .57;
        patch(impact-.06,0,.12,.66,0x071019);
        patch(impact-.12,.34,.24,.18,0x142030);
        patch(impact+.06,.55,.14,.45,0x080e19);
        patch(.04,.06,.9,.035,0x6f355c);
        const unsigned tick=unsigned(time*8);
        for(unsigned i=0;i<28;++i) {
            const unsigned hash=(i*1664525u+tick*1013904223u+unsigned(channel)*374761393u)^((i+tick)*2246822519u);
            if((hash&3)==0) continue;
            const double u=.02+(hash%91)*.01,v=.03+((hash>>9)%91)*.01;
            patch(u,v,.02+((hash>>18)%5)*.012,.014,(hash&8) ? 0x477c91 : 0x8b527c);
        }
        const auto fracture=[&](std::initializer_list<Vec2> points) {
            auto p=points.begin();auto previous=*p++;
            for(;p!=points.end();++p) {
                trace(sx(previous.x),sz(previous.y)-.025,sx(p->x),sz(p->y)-.025,0x142334);
                trace(sx(previous.x),sz(previous.y),sx(p->x),sz(p->y),0xc2d5da);
                previous=*p;
            }
        };
        fracture({{.04,.92},{.19,.78},{impact-.16,.8},{impact-.1,.61},{impact,.49},{impact+.04,.29},{impact-.03,.16},{impact+.03,.02}});
        fracture({{.02,.16},{.18,.25},{impact-.2,.22},{impact-.1,.4},{impact,.49},{impact+.13,.61},{impact+.21,.57},{.97,.82}});
        fracture({{impact-.1,.61},{impact-.28,.54},{impact-.34,.62},{.02,.57}});
        fracture({{impact+.13,.61},{impact+.1,.79},{impact+.24,.88},{.98,.96}});
        fracture({{impact+.04,.29},{impact+.21,.32},{impact+.25,.18},{.98,.09}});
        fracture({{impact-.1,.4},{impact-.18,.33},{impact-.14,.12}});
        trace(left+.16,1.66,right-.16,1.66,0x7498ad);
    }
}
void ship_wall(Canvas& c,const Game& game,int x,int y,bool aged) {
    const bool powered=!ShipInterior::forward_power_outage(x,y);
    const double original_h=ShipInterior::visible_wall_height(x,y);
    const double h=aged && original_h<2.5 ? 1.58 : original_h;
    const auto at=[&](double dx,double dy,double z){return c.project({x+dx,y+dy},z);};
    const GridPos id{x,y};
    // Flush cap plates and continuous faces avoid a row of separate cubes.
    c.quad(at(1,0,h),at(1,0,0),at(1,1,0),at(1,1,h),0x465563,id);
    c.quad(at(1,1,h),at(1,1,0),at(0,1,0),at(0,1,h),0x61717d,id);
    c.quad(at(0,0,h),at(1,0,h),at(1,1,h),at(0,1,h),0x99a6af,id);
    if(ShipInterior::floor(x+1,y)) {
        c.line(at(1,0,.12),at(1,1,.12),0x303f4a);
        c.line(at(1,0,h-.12),at(1,1,h-.12),0xa6bec7);
        if(y%3==0) c.line(at(1,0,.16),at(1,0,h-.15),0x354653);
    }
    if(ShipInterior::floor(x,y+1)) {
        c.line(at(0,1,.12),at(1,1,.12),0x3b4b56);
        c.line(at(0,1,h-.12),at(1,1,h-.12),0xc0ccd0);
        if(x%3==0) c.line(at(0,1,.16),at(0,1,h-.15),0x465762);
    }
    if(powered && (x+y)%4==0) {
        const auto light=at(.5,.5,h+.01);
        const bool storage=ShipInterior::room_at({x+1.5,y+.5})==1 || ShipInterior::room_at({x+.5,y+1.5})==1;
        const bool cargo=ShipInterior::room_at({x+1.5,y+.5})==2 || ShipInterior::room_at({x+.5,y+1.5})==2;
        c.emissive_rect(int(light.x)-2,int(light.y),4,1,storage|| (aged&&cargo) ? 0xffc34d : 0xc2dcd9);
    }
    if(aged && (x*7+y*11)%5==0) {
        const auto mark=at(.28,.0,.32+(x%3)*.13);
        c.line(mark,at(.54,.0,.39+(x%3)*.13),0x865d43);
        c.line(at(.73,1,.55),at(.91,1,.48),0x39454b);
    }
    const auto impact=ship_debris_seed(x,y);
    if(aged && impact%100<52) {
        const auto fracture=[&](bool east) {
            auto state=ship_debris_seed(x+(east ? 113 : 367),y+491);
            const auto random=[&]() {
                state^=state<<13;state^=state>>17;state^=state<<5;
                return double(state&0xffff)/65535.0;
            };
            const auto q=[&](double u,double z) {
                u=std::clamp(u,.04,.96);z=std::clamp(z,.04,.96);
                return east ? at(1.003,u,z*h) : at(u,1.003,z*h);
            };
            const double lip=random()<.5 ? -.018 : .018;
            const auto cut=[&](Vec2 a,Vec2 b) {
                c.line(q(a.x,a.y),q(b.x,b.y),0x17242e);
                c.line(q(a.x+lip,a.y),q(b.x+lip,b.y),0x8b9ba1);
            };
            const int segments=3+int(random()*4);
            const double angle=random()*6.28318530718;
            const double length=.32+random()*.65;
            const Vec2 center{.27+random()*.46,.3+random()*.4};
            const Vec2 along{std::cos(angle),std::sin(angle)},side{-along.y,along.x};
            Vec2 previous{center.x-along.x*length*.5,center.y-along.y*length*.5};
            for(int i=1;i<=segments;++i) {
                const double t=double(i)/segments-.5,jitter=(random()-.5)*.24;
                const Vec2 next{center.x+along.x*length*t+side.x*jitter,
                                center.y+along.y*length*t+side.y*jitter};
                cut(previous,next);
                if(i<segments && random()<.62) {
                    const double sign=random()<.5 ? -1 : 1;
                    const double reach=.10+random()*.25;
                    const Vec2 fork{next.x+side.x*reach*sign+along.x*.06,
                                    next.y+side.y*reach*sign+along.y*.06};
                    cut(next,fork);
                    if(random()<.4) cut(fork,{fork.x+along.x*.1+side.x*.05,fork.y+along.y*.1+side.y*.05});
                }
                previous=next;
            }
            if(random()<.5) {
                const double u=.15+random()*.6,z=.2+random()*.55;
                c.blend_triangle(q(u,z),q(u+.07+random()*.13,z+.08),q(u-.05,z-.1-random()*.12),0x1b2b35,.5);
            }
        };
        fracture(true);fracture(false);
        // Independently varied splits and chips on the cap plating.
        const double u=.16+((impact>>8)%56)*.01,v=.2+((impact>>18)%50)*.01;
        const double direction=(impact&128) ? -1 : 1;
        c.triangle(at(u,v,h+.005),at(u+.09,v+.1,h+.005),at(u-.08,v+.18,h+.005),0x53616a);
        c.line(at(u,v,h+.009),at(u+direction*.13,v-.12,h+.009),0x253742);
        c.line(at(u+direction*.13,v-.12,h+.009),at(u+direction*.08,v-.18,h+.009),0x253742);
    }
    if(powered && aged && (x+y)%9==0) {
        const auto warning=at(.5,.5,h-.17);
        c.emissive_rect(int(warning.x)-1,int(warning.y),2,1,0xe55a46);
    }
    ship_wall_tvs(c,game,x,y);
}
void ship_sliding_door(Canvas& c,const ShipRect& d,double open,bool passable) {
    const double x=d.x+d.w+.01;
    const auto at=[&](double y,double z){return c.project({x,y},z);};
    c.box({x,d.y-.08},.16,.22,0,2.15,0x4c606c);
    c.box({x,d.y+d.h+.08},.16,.22,0,2.15,0x4c606c);
    c.line(at(d.y,2.1),at(d.y+d.h,2.1),0xb8c6c8);
    const double half=d.h*.5, travel=open*(half-.10);
    const auto panel=[&](double y0,double y1) {
        c.quad(at(y0,.08),at(y1,.08),at(y1,1.98),at(y0,1.98),0x657984);
        c.line(at(y0,.18),at(y1,.18),0x314550);
        c.line(at(y0,1.88),at(y1,1.88),0xa6b8bb);
    };
    panel(d.y,d.y+half-travel);
    panel(d.y+half+travel,d.y+d.h);
    for(double y:{d.y+.12,d.y+d.h-.12}) {
        const auto q=at(y,2.06);
        c.emissive_rect(int(q.x)-2,int(q.y)-1,4,2,passable ? 0x63e69b : 0xe9b45d);
    }
}
void ship_station_marker(Canvas& c,const ShipRect& b,bool observed,double time) {
    const double pulse=.76+.24*std::sin(time*4);
    const auto green=tint(0x55f29a,pulse);
    const auto p=[&](double x,double y){return c.project({x,y},.04);};
    const auto pz=[&](double x,double y,double h){return c.project({x,y},h);};
    c.line(p(b.x-.08,b.y-.08),p(b.x+b.w+.08,b.y-.08),green);
    c.line(p(b.x+b.w+.08,b.y-.08),p(b.x+b.w+.08,b.y+b.h+.08),green);
    c.line(p(b.x+b.w+.08,b.y+b.h+.08),p(b.x-.08,b.y+b.h+.08),green);
    c.line(p(b.x-.08,b.y+b.h+.08),p(b.x-.08,b.y-.08),green);
    const Vec2 center{b.x+b.w*.5,b.y+b.h*.5};
    const double angle=time*1.8;
    const Vec2 right{std::cos(angle)*.38,std::sin(angle)*.38};
    const Vec2 forward{-right.y,right.x};
    const double icon_z=1.55+.08*std::sin(time*2.4);
    c.line(pz(center.x-right.x,center.y-right.y,icon_z),pz(center.x+right.x,center.y+right.y,icon_z),green);
    c.line(pz(center.x-forward.x,center.y-forward.y,icon_z),pz(center.x+forward.x,center.y+forward.y,icon_z),green);
    if(observed) {
        for(int i=0;i<3;++i) {
            const double a=angle+i*2.094395102;
            const auto q=pz(center.x+std::cos(a)*.55,center.y+std::sin(a)*.55,icon_z);
            c.emissive_rect(int(q.x)-1,int(q.y)-1,3,3,green);
        }
    } else {
        c.line(pz(center.x-.28,center.y-.28,icon_z+.18),pz(center.x+.28,center.y+.28,icon_z-.18),green);
        c.line(pz(center.x-.28,center.y+.28,icon_z-.18),pz(center.x+.28,center.y-.28,icon_z+.18),green);
    }
}
void ship_worktable(Canvas& c,const Game& game,const ShipRect& b,bool observed) {
    const double w=b.w,d=b.h,h=.9;
    const Vec2 p{b.x+w*.5,b.y+d*.5};
    const auto at=[&](double x,double y,double z){return c.project({p.x+x,p.y+y},z);};
    const auto box=[&](double x,double y,double bw,double bd,double z,double bh,std::uint32_t color) {
        c.box({p.x+x,p.y+y},bw,bd,z,bh,color);
    };
    // Braced steel frame, recessed shelf and a slightly open parts drawer.
    for(double dx:{-w*.42,w*.42}) for(double dy:{-d*.34,d*.34}) {
        box(dx,dy,.13,.13,.04,h-.18,0x647477);
        box(dx,dy,.22,.22,.01,.06,0x303d41);
    }
    box(0,0,w-.35,d-.45,.22,.06,0x4f5d5c);
    box(-.58,0,.75,.75,.28,.21,0x74634c);
    box(.5,.06,.64,.61,.28,.16,0x53696b);
    box(0,d*.34,w-.25,.10,.39,.10,0x3e4c50);
    box(.65,.37,.93,.86,.53,.20,0x677572);
    box(.65,.84,.93,.07,.53,.22,0x89928a);
    box(.65,.89,.32,.05,.61,.04,0xc0bba4);
    box(0,0,w,d,h-.14,.14,observed ? 0x8a9891 : 0x9b805b);
    // Metal edge strips, exposed corner bolts, scoring and chipped finish.
    box(0,d*.5-.035,w,.07,h-.12,.10,0x778383);
    for(double dx:{-w*.45,w*.45}) for(double dy:{-d*.43,d*.43}) {
        const auto q=at(dx,dy,h+.01);c.rect(int(q.x),int(q.y),2,1,0xced0b9);
    }
    for(int i=0;i<12;++i) {
        const double x=-1.30+(i*37%235)*.01,y=-.85+(i*23%165)*.01;
        c.line(at(x,y,h+.01),at(x+.13+(i%3)*.07,y+.035,h+.01),i%3 ? 0x5e655b : 0xc1b18a);
    }
    box(-.05,.02,1.60,.92,h,.018,observed ? 0x254b50 : 0x31564b);
    for(int i=0;i<5;++i)
        c.line(at(-.78+i*.30,-.40,h+.02),at(-.78+i*.30,.45,h+.02),0x526e64);
    // Paper notes, a loose circuit board and a tangled copper lead.
    c.oriented_box({p.x-.92,p.y+.18},.38,.52,h+.025,.018,0xc6bda0,.25);
    c.line(at(-1.08,.11,h+.05),at(-.81,.16,h+.05),0x777e72);
    c.line(at(-1.06,.23,h+.05),at(-.86,.27,h+.05),0x777e72);
    box(.98,.23,.43,.40,h+.02,.04,0x3b7863);
    for(int i=0;i<3;++i) box(.85+i*.12,.25,.055,.13,h+.06,.025,0xc3ae78);
    for(int i=0;i<16;++i) {
        const double a=i*.392699,beta=(i+1)*.392699;
        c.line(at(-1.01+std::cos(a)*.20,-.47+std::sin(a)*.18,h+.035),
               at(-1.01+std::cos(beta)*.20,-.47+std::sin(beta)*.18,h+.035),0xc1844d);
    }
    c.line(at(-.82,-.47,h+.035),at(-.59,-.65,h+.035),0xc1844d);
    // Wrench and screwdriver lie at different angles across the front lip.
    const auto wrench=[&](double x,double y,double angle) {
        const auto q=[&](double along,double across) {
            return at(x+std::cos(angle)*along-std::sin(angle)*across,
                      y+std::sin(angle)*along+std::cos(angle)*across,h+.07);
        };
        c.line(q(-.23,0),q(.22,0),0xd0d3bd);
        c.line(q(-.23,.04),q(.22,.04),0x8eaaa7);
        for(double side:{-.10,.10}) {
            c.line(q(.18,0),q(.29,side),0xd0d3bd);
            c.line(q(.29,side),q(.40,side),0xd0d3bd);
        }
    };
    wrench(-.43,.72,-.22);wrench(.77,-.68,.48);
    c.oriented_box({p.x+.30,p.y+.72},.12,.29,h+.025,.07,0xb56842,-.8);
    c.line(at(.41,.83,h+.065),at(.68,1.0,h+.065),0xc6d3c5);
    // Open pliers, loose washers and screws in a shallow parts tray.
    for(double side:{-.09,.09}) {
        c.line(at(.02+side,-.71,h+.06),at(.16,-.60,h+.06),0xc3c7b1);
        c.line(at(.16,-.60,h+.06),at(.35+side,-.44,h+.06),0xbc774e);
    }
    box(1.02,.69,.52,.36,h+.02,.045,0x8a9387);
    box(1.02,.69,.42,.26,h+.065,.006,0x354649);
    for(int i=0;i<5;++i) {
        const auto q=at(.86+(i*11%31)*.01,.60+(i*7%17)*.01,h+.08);
        c.rect(int(q.x),int(q.y),2,1,i%2 ? 0xd0b879 : 0xbbc9be);
    }
    // A shaded yellow lamp with a stem and angled arm at each rear corner.
    box(-1.19,-.74,.32,.28,h+.01,.06,0x465759);
    box(-1.19,-.74,.065,.065,h+.07,.63,0x9b9c86);
    c.line(at(-1.19,-.74,h+.70),at(-.82,-.60,h+.83),0xc8bd8d);
    box(-.82,-.60,.40,.28,h+.75,.09,0xa99764);
    const auto bulb=at(-.82,-.60,h+.74);
    c.emissive_rect(int(bulb.x)-3,int(bulb.y),6,2,0xffd77c);
    if(observed) {
        // Compact microscope: weighted foot, curved support, stage and eyepiece.
        box(.65,-.25,.50,.47,h+.03,.07,0xa5ada3);
        box(.82,-.38,.13,.16,h+.10,.48,0x889da0);
        box(.64,-.25,.34,.29,h+.27,.045,0x33484b);
        c.line(at(.82,-.38,h+.57),at(.56,-.22,h+.68),0xc1c7b5);
        box(.56,-.22,.14,.18,h+.48,.22,0xc6c9b6);
        box(.56,-.22,.18,.20,h+.68,.07,0x34464a);
        for(int i=0;i<3;++i) {
            const double x=-.49+i*.35;
            box(x,.20,.30,.33,h+.025,.045,0xb3b6a0);
            box(x,.20,.21,.24,h+.07,.025,0x283b40);
            const auto color=game.sample_observed(i) ? 0x85c6a0 : i==1 ? 0x96cbd3 : i==0 ? 0xc1ac7c : 0x92867a;
            c.oriented_box({p.x+x,p.y+.20},.15,.18,h+.095,.07,color,i*.7);
        }
    } else {
        // Bench vise with separate jaws and a screw handle, plus soldering stand.
        box(-.43,-.08,.51,.47,h+.025,.10,0x596f76);
        box(-.43,-.08,.26,.36,h+.125,.13,0x718a8e);
        for(double dx:{-.20,.20}) box(-.43+dx,-.08,.13,.43,h+.23,.13,0xabb6ae);
        c.line(at(-.43,.10,h+.19),at(-.43,.45,h+.19),0xc7c6ac);
        c.line(at(-.58,.45,h+.09),at(-.28,.45,h+.29),0xc7c6ac);
        box(.63,-.25,.43,.47,h+.02,.15,0x617476);
        box(.63,-.25,.27,.30,h+.17,.045,0x35464a);
        c.line(at(.61,-.18,h+.23),at(.80,-.43,h+.44),0xd5c399);
        c.line(at(.80,-.43,h+.44),at(.89,-.53,h+.48),0xa1b8b4);
    }
    ship_station_marker(c,b,observed,game.presentation().clock);
}
void ship_cargo_rover(Canvas& c,Vec2 p,bool repaired,double ground=0,double wheel_phase=0,bool powered=true) {
    constexpr double scale=ShipInterior::cargo_rover_scale;
    const auto at=[&](double x,double y,double z){return c.project({p.x+x*scale,p.y+y*scale},ground+z*scale);};
    const auto box=[&](double x,double y,double w,double d,double z,double h,std::uint32_t color) {
        c.box({p.x+x*scale,p.y+y*scale},w*scale,d*scale,ground+z*scale,h*scale,color);
    };
    const auto wheel=[&](double x,double side) {
        const double inner=side*.91,outer=side*1.3;
        const auto rim=[&](double angle,double radius,double y) {
            return at(x+std::cos(angle)*radius,y,.5+std::sin(angle)*radius);
        };
        // Faceted cylinders retain a round tyre silhouette at native resolution.
        for(int i=0;i<20;++i) {
            const double a=i*.3141592654,b=(i+1)*.3141592654;
            c.quad(rim(a,.5,inner),rim(a,.5,outer),rim(b,.5,outer),rim(b,.5,inner),
                   tint(0x303842,.65+.3*std::max(0.0,std::sin(a))));
        }
        const auto disc=[&](double radius,std::uint32_t color) {
            const auto center=at(x,outer,.5);
            for(int i=0;i<20;++i) {
                const double a=i*.3141592654,b=(i+1)*.3141592654;
                c.triangle(center,rim(a,radius,outer),rim(b,radius,outer),color);
            }
        };
        disc(.49,0x1b232b);disc(.34,0x899295);disc(.28,0x46535d);disc(.22,0x74808a);
        for(int i=0;i<20;++i) {
            const double a=i*.3141592654;
            c.line(rim(a,.43,outer),rim(a,.5,outer),0x4a5157);
            if(i%2==0) c.line(rim(a,.5,outer),rim(a+.08,.5,inner),0x48515a);
        }
        for(int i=0;i<6;++i) {
            const auto bolt=rim(i*1.04719755+wheel_phase,.265,outer);c.rect(int(bolt.x),int(bolt.y),1,1,0xb4b9ad);
        }
    };
    if(repaired) for(double x:{-1.65,-.7}) wheel(x,-1);
    for(double x:{.25,1.25}) wheel(x,-1);
    box(0,0,3.65,1.8,.43,.48,0x303d49);
    for(double side:{-1.0,1.0}) {
        box(0,side*.93,3.5,.14,.65,.13,0xc48029);
        for(double x:{-.55,.4}) {
            box(x,side*.99,.64,.28,.48,.42,0x48525b);
            c.line(at(x-.25,side*1.14,.58),at(x+.25,side*1.14,.58),0xd29e32);
        }
        box(-.35,side*1.13,.85,.32,.38,.08,0x62798a);
        for(int i=0;i<5;++i)
            c.line(at(-.7+i*.16,side*1.01,.47),at(-.7+i*.16,side*1.27,.47),0x4d9abe);
    }
    if(repaired) for(double x:{-1.65,-.7}) wheel(x,1);
    for(double x:{.25,1.25}) wheel(x,1);
    // A low cargo platform with chamfered corners and a damaged service recess.
    const std::array<Vec2,6> deck{{{-1.92,-1.13},{1.64,-1.13},{1.92,-.85},
                                  {1.92,.85},{1.64,1.13},{-1.92,1.13}}};
    for(int i=0;i<6;++i) {
        const auto a=deck[i],b=deck[(i+1)%6];
        if(b.y-a.y<=b.x-a.x) continue;
        c.quad(at(a.x,a.y,1.15),at(b.x,b.y,1.15),at(b.x,b.y,.96),at(a.x,a.y,.96),0x384652);
        c.line(at(a.x,a.y,1.15),at(b.x,b.y,1.15),0x819096);
    }
    for(int i=1;i<5;++i)
        c.triangle(at(deck[0].x,deck[0].y,1.15),at(deck[i].x,deck[i].y,1.15),
                   at(deck[i+1].x,deck[i+1].y,1.15),0x59616a);
    for(double x:{-1.36,-.82,-.28,.26,.8,1.34}) {
        c.line(at(x,-1.08,1.16),at(x,1.08,1.16),0x303d49);
        for(double y:{-.72,-.18,.36,.9}) {
            c.line(at(x+.04,y,1.16),at(std::min(x+.49,1.62),y,1.16),0x3b4650);
            const auto bolt=at(x+.08,y+.06,1.17);c.rect(int(bolt.x),int(bolt.y),1,1,0x92988f);
        }
    }
    c.quad(at(-1.77,-.62,1.17),at(-1.06,-.62,1.17),at(-1.06,.62,1.17),at(-1.77,.62,1.17),0x182630);
    for(double x:{-1.64,-.5,.65,1.52}) for(double side:{-.91,.91}) {
        c.quad(at(x-.12,side-.1,1.17),at(x+.12,side-.1,1.17),
               at(x+.12,side+.1,1.17),at(x-.12,side+.1,1.17),0x303e48);
        c.line(at(x-.09,side+.07,1.18),at(x+.09,side+.07,1.18),0xc6a14d);
    }
    if(!repaired) {
    // A torn rear fender exposes the empty axle and dangling wiring.
    box(-1.24,1.05,.75,.28,.86,.09,0x9d682e);
    box(-1.25,.98,.16,.55,.4,.12,0x30343b);
    c.quad(at(-1.9,.55,1.18),at(-1.3,.48,1.18),at(-1.01,1.12,1.18),at(-1.9,1.12,1.18),0x222d35);
    for(int i=0;i<4;++i) {
        const double x=-1.75+i*.19;
        c.line(at(x,1.03,.86),at(x-.1,1.21,.44),0x18242d);
        c.line(at(x-.1,1.21,.44),at(x+.02,1.28,.22),0x6d5741);
    }
    } else {
        for(double side:{-1.0,1.0}) {
            box(-1.3,side*1.05,1.05,.18,.86,.09,0xc48029);
            box(-1.25,side*.96,.8,.17,.6,.22,0x48525b);
        }
    }
    box(1.93,0,.1,1.6,.77,.12,0x283743);
    for(double side:{-.65,.65}) {
        c.emissive_quad(at(1.974,side-.16,.96),at(1.974,side+.16,.96),
                        at(1.974,side+.16,1.09),at(1.974,side-.16,1.09),powered ? 0xe5f1df : 0x29333c);
        box(1.85,side*1.4,.15,.2,.82,.09,0xc37c2d);
    }
}
void rescue_passenger(Canvas& c,Vec2 p,double z,double seated) {
    // White suit, orange chest controls, dark visor and bent legs in the harness.
    c.box(p,.52,.48,z,.48,0xdce3ed);
    c.box({p.x,p.y+.25},.28,.07,z+.16,.22,0xd58b55);
    for(double side:{-.18,.18}) {
        c.box({p.x+side,p.y+.36},.22,.48,z-.08,.20,0xd7dfeb);
        c.box({p.x+side,p.y+.54},.21,.18,z-.08-.28*seated,.28*seated+.1,0xc6d1e1);
        c.box({p.x+side,p.y+.59},.24,.26,z-.14-.28*seated,.13,0x283247);
        c.box({p.x+side*1.9,p.y+.12},.15,.36,z+.12,.17,0xc6d1e1);
    }
    c.box(p,.54,.52,z+.48,.41,0xe1e7f0);
    c.box({p.x,p.y+.27},.39,.045,z+.59,.19,0x161c3d);
    c.line(c.project({p.x-.25,p.y+.26},z+.10),c.project({p.x+.25,p.y+.26},z+.40),0x3b4e5a);
    c.line(c.project({p.x+.25,p.y+.27},z+.10),c.project({p.x-.25,p.y+.27},z+.40),0x3b4e5a);
}
void rescue_transport(Canvas& c,const Game& game,Vec2 p,double ground,bool rover=true) {
    const auto previous_origin=c.object_origin;const double previous_angle=c.object_angle;
    if(game.carrier_mobile()) {c.object_origin=p;c.object_angle=game.escort_carrier_heading();}
    if(rover) ship_cargo_rover(c,p,true,ground,game.escort_wheel_phase(),game.carrier_battery_percent()>0);
    const bool installing=game.rescue_stage()==Game::RescueStage::InstallingSeat;
    if(game.carrier_seat_installed() || installing) {
        const double progress=installing ? game.rescue_stage_progress() : 1.0;
        const double z=ground+.6+(1-progress)*.65;
        c.box(p,.86,.76,z,.12,0x354f68);
        c.box({p.x,p.y-.32},.86,.12,z+.12,.65*progress,0x52748b);
        for(double side:{-.46,.46}) c.box({p.x+side,p.y},.08,.65,z+.15,.22,0xb9c8ce);
        if(game.astronaut_aboard()) rescue_passenger(c,p,ground+.76,1);
    }
    c.object_origin=previous_origin;c.object_angle=previous_angle;
}
void ship_cargo_arm(Canvas& c,Vec2 p,double time,bool repaired) {
    const auto at=[&](double x,double y,double z){return c.project({p.x+x,p.y+y},z);};
    // Debris stays decorative; the reduced body and arm own the collision.
    for(int i=0;!repaired && i<5;++i) {
        const double x=-.75+i*.19,y=.6+(i%3)*.13;
        c.triangle(at(x-.07,y,.025),at(x+.09,y+.03,.025),at(x-.02,y+.12,.025),i%2 ? 0x5e6567 : 0x333e46);
    }
    for(int i=0;!repaired && i<6;++i) {
        const double age=std::fmod(time*.42+i/6.0,1.0),r=.035+age*.11;
        const double x=-.4+std::sin(age*5+i)*.08-age*.14,z=1.12+age*.85;
        c.blend_quad(at(x-r,.18,z-r),at(x+r,.18,z-r),at(x+r*.7,.18,z+r),
                     at(x-r*.8,.18,z+r),0x53616c,(1-age)*.23);
    }
    struct Joint {double x,y,z;};
    const auto point=[&](Joint q,double y){return at(q.x,q.y+y,q.z);};
    const auto beam=[&](Joint a,Joint b,double radius,double depth,std::uint32_t color) {
        const double length=std::hypot(b.x-a.x,b.z-a.z);
        const double nx=-(b.z-a.z)*radius/length,nz=(b.x-a.x)*radius/length;
        const Joint al{a.x-nx,a.y,a.z-nz},ah{a.x+nx,a.y,a.z+nz},
                    bl{b.x-nx,b.y,b.z-nz},bh{b.x+nx,b.y,b.z+nz};
        c.quad(point(ah,-depth),point(bh,-depth),point(bh,depth),point(ah,depth),color);
        c.quad(point(ah,depth),point(al,depth),point(bl,depth),point(bh,depth),tint(color,.76));
        c.line(point(ah,depth),point(bh,depth),tint(color,1.25));
        c.line(point(al,depth),point(bl,depth),0x25313b);
    };
    const auto joint=[&](Joint q,double radius) {
        const auto radial=[&](double a,double r){return at(q.x+std::cos(a)*r,q.y+.12,q.z+std::sin(a)*r);};
        for(int i=0;i<12;++i) {
            const double a=i*.5235987756,b=(i+1)*.5235987756;
            c.triangle(point(q,.12),radial(a,radius),radial(b,radius),0x26343e);
            c.line(radial(a,radius),radial(b,radius),0x84908f);
        }
        c.line(radial(0,radius*.5),radial(3.14159265,radius*.5),0xc18a39);
    };
    const Joint shoulder{.34,.25,.73},elbow{.06,.22,1.4},wrist{-.44,.19,1.1},claw{-.61,.19,.95};
    c.box({p.x+.34,p.y+.25},.46,.43,.575,.15,0x646c70);
    c.box({p.x+.34,p.y+.25},.27,.29,.725,.06,0xb48a3d);
    beam(shoulder,elbow,.11,.08,0x65717b);
    beam(elbow,wrist,.085,.07,0x596772);
    beam(wrist,claw,.07,.06,0xa47c3e);
    c.line(at(.4,.36,.8),at(.18,.33,1.36),0xa9b7b7);
    c.line(at(.18,.33,1.36),at(-.39,.3,1.17),0x26333e);
    joint(shoulder,.11);joint(elbow,.14);joint(wrist,.08);
    for(double side:{-1.0,1.0}) {
        const Joint knuckle{-.77,.19+side*.13,.85},tip{-.83,.19+side*.16,.67},hook{-.71,.19+side*.07,.73};
        beam(claw,knuckle,.035,.025,0x5b686f);
        beam(knuckle,tip,.025,.02,0x3d4a54);
        beam(tip,hook,.018,.015,0x9c956f);
        if(!repaired) {
            c.line(at(-.42,.28,1.08),at(-.46,.32,.86),0x1c2931);
            c.line(at(-.46,.32,.86),at(-.37,.33,.78),0x78604a);
        }
    }
    // Short electrical arcs and falling sparks are deterministic on the
    // simulation clock, including during redraws and pause.
    if(!repaired && int(time*9)%7<3) {
        c.emissive_line(at(-.34,.29,1.15),at(-.46,.3,1.19),0x87cefc);
        c.emissive_line(at(-.46,.3,1.19),at(-.42,.31,1.06),0xd1f2ff);
        c.emissive_line(at(-.42,.31,1.06),at(-.59,.3,1.01),0x78bfea);
    }
    for(int i=0;!repaired && i<7;++i) {
        const double age=std::fmod(time*.83+i*.143,1.0);
        const double x=-.45+(i-3)*age*.065,z=1.08-age*age*1.04;
        if(age>.88) continue;
        c.emissive_line(at(x,.25+age*.12,z),at(x-.015,.25+age*.12,z+.045),
                        i%3==0 ? 0xf3df9c : 0xd98d45);
    }
}
void ship_forward_wreckage(Canvas& c,const ShipRect& b,double time) {
    const auto at=[&](double x,double y,double z){return c.project({b.x+x,b.y+y},z);};
    const Vec2 center{b.x+b.w*.5,b.y+b.h*.5};
    c.box(center,b.w-.06,b.h-.06,0,.32,0x313d45);
    // A collapsed pressure hatch, twisted hull plating and structural ribs fill
    // the whole passage. Draw the damage even when cosmetic aging is disabled.
    for(int i=0;i<b.w;++i) {
        const double x=i+.5,h=.7+((i*7)%5)*.19;
        const Vec2 p{b.x+x,b.y+.95};
        c.box(p,.96,1.65,.18,h*.62,i%2 ? 0x62646a : 0x55606b);
        c.oriented_box({p.x,p.y+.08},.93,1.3,h*.5,.24,i%2 ? 0x899192 : 0x727f87,
                       .2*(i%3-1),.13,-.1*(i%2));
        c.triangle(at(x-.44,.4,h*.64),at(x+.4,.72,h+.2),at(x-.25,1.55,h*.64),0x3a464e);
        c.line(at(x-.44,.4,h*.64),at(x+.4,.72,h+.2),0xa3a69b);
        c.line(at(x-.25,1.55,h*.64),at(x+.4,.72,h+.2),0x202d36);
        c.line(at(x-.22,1.82,.19),at(x+.15,1.62,.4),0xb68a4e);
    }
    // Broken I-beams bridge the wreckage with a clearly impassable silhouette.
    c.oriented_box({center.x-.08,center.y-.24},.22,5.85,1.02,.3,0x889298,0,.11);
    c.oriented_box({center.x+.08,center.y+.53},.25,5.8,.6,.24,0x606b73,0,-.065);
    for(double x:{.18,5.82}) {
        c.box({b.x+x,b.y+.6},.27,.5,.2,1.85,0x5b6b76);
        c.line(at(x-.1,.88,.5),at(x+.1,.88,1.98),0x9eaaa7);
    }
    // Fractured warning rail and exposed hanging cables face the Crew Room.
    c.box({center.x,center.y+.82},5.8,.09,.53,.18,0xd3a14f);
    for(int i=0;i<14;++i) {
        const double x=.25+i*.4;
        c.line(at(x,1.87,.56),at(x+.17,1.87,.69),0x343b3c);
    }
    for(int i=0;i<5;++i) {
        const double x=1.1+i*.75;
        c.line(at(x,.96,1.55),at(x+.14,1.25,1.03),0x1b2933);
        c.line(at(x+.14,1.25,1.03),at(x-.08,1.41,.75),i%2 ? 0x9b6b43 : 0x293b49);
    }
    for(double x:{.25,5.75}) {
        const auto q=at(x,1.9,1.74);
        c.emissive_rect(int(q.x)-1,int(q.y)-1,3,2,tint(0xf0a15d,.75+.25*std::sin(time*2.5)));
    }
}
void ship_furniture(Canvas& c,const Game& game,const ShipFurniture& prop,int index) {
    const auto b=prop.bounds;const double w=b.w,d=b.h,h=ShipInterior::fixture_height(prop.kind);
    const Vec2 p{b.x+w*.5,b.y+d*.5};const double time=game.presentation().clock;
    const auto at=[&](double x,double y,double z){return c.project({p.x+x,p.y+y},z);};
    const auto panel=[&](Vec2 center,double width,double z,int phase,bool lit=false) {
        const double y=center.y;
        const auto a=c.project({center.x-width/2,y},z),b=c.project({center.x+width/2,y},z),
                   d=c.project({center.x+width/2,y},z+.55),e=c.project({center.x-width/2,y},z+.55);
        if(lit) c.emissive_quad(a,b,d,e,0x123b55);else c.quad(a,b,d,e,0x123b55);
        for(int row=0;row<3;++row) {
            const double start=center.x-width*.42,end=start+width*(.25+.15*std::sin(time*1.6+row+phase));
            const auto a=c.project({start,y},z+.12+row*.15),b=c.project({end,y},z+.12+row*.15);
            if(lit) c.emissive_line(a,b,row==1 ? 0x86e8ed : 0x479ed2);else c.line(a,b,row==1 ? 0x86e8ed : 0x479ed2);
        }
    };
    const auto parts_table=[&]() {
        for(double dx:{-w*.40,w*.40}) for(double dy:{-d*.30,d*.30})
            c.box({p.x+dx,p.y+dy},.12,.12,.04,h-.12,0x58686a);
        c.box(p,w,d,h-.12,.12,0x9b805b);
        c.box({p.x,p.y-d*.36},w,.07,h-.02,.06,0xc1a16c);
        c.line(c.project({b.x+.08,p.y},h+.01),c.project({b.x+w-.08,p.y},h+.01),0xd0b17a);
    };
    switch(prop.kind) {
    case ShipFixture::CollapsedBulkhead:
        ship_forward_wreckage(c,b,time);
        break;
    case ShipFixture::PCDesk: {
        InteractionOutline outline(c,game.mission_number()==3 && !game.records_copied(),time,0x54ff87);
        for(double dx:{-.42,.42}) for(double dy:{-.32,.32}) c.box({p.x+w*dx,p.y+d*dy},.12,.12,0,h-.1,0x667b86);
        c.box(p,w,d,h-.1,.1,0xa9b9bc);
        c.box({p.x+w*.36,p.y},.42,.68,.04,.62,0x314954);
        c.box({p.x,p.y-.25},.38,.3,h,.06,0x536e7d);
        c.box({p.x,p.y-.28},.1,.1,h+.06,.13,0x8caab1);
        c.box({p.x,p.y-.3},1.5,.13,h+.19,.65,0x526f7e);
        panel({p.x,p.y-.22},1.34,h+.24,index,true);
        c.box({p.x-.18,p.y+.22},.9,.26,h,.035,0x385362);
        if(game.mission_number()==3 && !game.usb_obtained()) {
            c.box({p.x+.65,p.y+.18},.32,.16,h+.02,.10,0x418db2);
            c.box({p.x+.87,p.y+.18},.14,.12,h+.025,.08,0xb7c8d0);
        }
        for(int i=0;i<6;++i) c.line(c.project({p.x-.55+i*.13,p.y+.13},h+.04),c.project({p.x-.55+i*.13,p.y+.3},h+.04),0x93bbc4);
        c.box({p.x+.65,p.y+.2},.4,.38,h,.015,0x39566a);
        c.box({p.x+.65,p.y+.2},.13,.2,h+.015,.05,0xa9c4c6);
        break;
    }
    case ShipFixture::Seat:
        c.box(p,.72,.12,.03,.08,0x556975);c.box(p,.12,.72,.03,.08,0x556975);
        c.box(p,.12,.12,.1,.24,0x7d969e);c.box(p,.82,.8,.34,.14,0x547c99);
        c.box({p.x,p.y+.34},.82,.16,.42,.48,0x678da7);
        for(double side:{-.42,.42}) c.box({p.x+side,p.y},.08,.52,.48,.07,0x8ca5af);
        break;
    case ShipFixture::Bunk:
        c.box(p,w,d,0,.16,0x52616c);c.box(p,w-.2,d-.2,.16,.11,0x9caab3);
        c.box({p.x+.3,p.y},w-1,d-.35,.27,.18,0x4e709a);
        c.box({p.x-w*.34,p.y},.75,d-.45,.28,.17,0xc0d3d2);
        break;
    case ShipFixture::MessTable: {
        const double radius=std::min(w,d)*.5;
        const auto ring=[&](double angle,double r,double z) {
            return at(std::cos(angle)*r,std::sin(angle)*r,z);
        };
        const auto cylinder=[&](double r,double bottom,double height,std::uint32_t color) {
            for(int i=0;i<40;++i) {
                const double a=i*6.28318530718/40,b=(i+1)*6.28318530718/40;
                if(std::cos((a+b)*.5)+std::sin((a+b)*.5)>0)
                    c.quad(ring(a,r,bottom+height),ring(b,r,bottom+height),ring(b,r,bottom),ring(a,r,bottom),tint(color,.58));
            }
            for(int i=0;i<40;++i) {
                const double a=i*6.28318530718/40,b=(i+1)*6.28318530718/40;
                c.triangle(at(0,0,bottom+height),ring(a,r,bottom+height),ring(b,r,bottom+height),color);
            }
        };
        // Circular steel top, inset glass surface and a single sculpted pedestal.
        cylinder(.83,.02,.10,0x475b67);
        cylinder(.42,.12,h-.25,0x6c828d);
        cylinder(radius,h-.13,.13,0x82949e);
        cylinder(radius-.10,h,.025,0x354d5d);
        for(int i=0;i<40;++i) {
            const double a=i*6.28318530718/40,b=(i+1)*6.28318530718/40;
            c.line(ring(a,radius-.055,h+.015),ring(b,radius-.055,h+.015),0x77b5be);
        }
        // A modest impact crack and scuffed rim retain the crash aftermath.
        c.line(at(.35,radius-.05,h+.03),at(.24,radius-.43,h+.03),0x152c3b);
        c.line(at(.24,radius-.43,h+.03),at(.45,radius-.65,h+.03),0x152c3b);
        c.line(at(.24,radius-.43,h+.03),at(.04,radius-.5,h+.03),0x152c3b);
        c.line(ring(1.30,radius,h+.012),ring(1.44,radius,h+.012),0xb2bdba);
        // Three discarded TVs lie at different angles on the circular top.
        for(int i=0;i<3;++i) {
            const Vec2 offset=i==0 ? Vec2{-.65,-.55} : i==1 ? Vec2{.72,-.18} : Vec2{-.18,.72};
            const double angle=i==0 ? -.35 : i==1 ? .62 : -.12;
            const double cs=std::cos(angle),sn=std::sin(angle),base=h+.04+i*.025;
            const auto tv=[&](double x,double y,double z) {
                return at(offset.x+x*cs-y*sn,offset.y+x*sn+y*cs,base+z);
            };
            c.quad(tv(-.62,-.4,.1),tv(.62,-.4,.1),tv(.62,.4,.1),tv(-.62,.4,.1),0x4f626e);
            c.quad(tv(-.62,.4,0),tv(.62,.4,0),tv(.62,.4,.1),tv(-.62,.4,.1),0x263742);
            c.quad(tv(-.52,-.31,.105),tv(.52,-.31,.105),tv(.52,.29,.105),tv(-.52,.29,.105),0x0b1520);
            c.line(tv(-.39,-.22,.11),tv(-.08,.03,.11),0x82949e);
            c.line(tv(-.08,.03,.11),tv(.14,-.1,.11),0x82949e);
            c.line(tv(.14,-.1,.11),tv(.46,.21,.11),0x82949e);
            c.line(tv(-.08,.03,.11),tv(-.23,.28,.11),0x657f8c);
            c.line(tv(.14,-.1,.11),tv(.31,-.29,.11),0x657f8c);
            c.line(tv(-.12,.4,.05),tv(.03,.61,.03),0x71828b);
            c.line(tv(.03,.61,.03),tv(.31,.63,.03),0x3d515f);
            c.line(tv(.47,.34,.11),tv(.54,.34,.11),0x8c6452);
        }
        break;
    }
    case ShipFixture::Sofa:
        c.box(p,w,d,0,.36,0x6e513c);c.box(p,w-.2,d-.2,.36,.15,0xb29262);
        c.box({p.x-w*.4,p.y},.3,d,.45,h-.45,0x8c6b4b);
        for(double y:{-.35,0.0,.35}) c.line(at(-w*.25,y*d,.52),at(w*.43,y*d,.52),0x66553e);
        break;
    case ShipFixture::Rover:
        {
        const bool carrier_active=(game.rescue_mission()==7 && game.rescue_stage()==Game::RescueStage::Waiting) || (game.mission2() &&
            (!game.carrier_scanned() || (game.carrier_wheel_collected() && game.carrier_arm_collected() && game.carrier_chip_collected() && !game.carrier_repaired())));
        InteractionOutline outline(c,carrier_active,time,0x54ff87);
        ship_cargo_rover(c,p,game.carrier_repaired());
        if(game.rescue_mission()==7) rescue_transport(c,game,p,0,false);
        break;
        }
    case ShipFixture::RoverArm:
        ship_cargo_arm(c,p,time,game.carrier_repaired());
        break;
    case ShipFixture::Tank:
    case ShipFixture::Reactor: {
        const double r=std::min(w,d)*.37;const bool reactor=prop.kind==ShipFixture::Reactor;
        c.box(p,w,d,0,.18,0x46585a);
        const auto radial=[&](double angle,double radius){return Vec2{p.x+std::cos(angle)*radius,p.y+std::sin(angle)*radius};};
        const auto ring=[&](double z,double height,std::uint32_t color) {
            for(int i=0;i<16;++i) {
                const double a=i*.3926990817,b=(i+1)*.3926990817;
                const auto x=radial(a,r),y=radial(b,r);
                c.quad(c.project(x,z),c.project(y,z),c.project(y,z+height),c.project(x,z+height),
                       tint(color,.7+.3*std::max(0.0,-std::cos(a+.6))));
            }
        };
        ring(.18,h-.18,0x3695b7);ring(.18,.15,0xadb7a9);ring(h-.18,.18,0x98b9bd);
        ring(.35+std::fmod(time*.35,h-.7),.08,0x75dfdf);
        for(int i=0;i<16;++i) {
            const auto x=radial(i*.3926990817,r),y=radial((i+1)*.3926990817,r);
            c.triangle(c.project(p,h),c.project(x,h),c.project(y,h),0x719da7);
        }
        if(reactor) for(double dx:{-.44,.44}) c.box({p.x+w*dx,p.y},.25,d,.18,.25,0xb39d5b);
        break;
    }
    case ShipFixture::Monitor: {
        const bool powered=!ShipInterior::forward_power_outage(b.x,b.y);
        c.box(p,w,d,0,h,powered ? 0x4c606b : 0x1d2832);
        for(int i=0;i<std::max(1,int(w/3));++i) {
            const int count=std::max(1,int(w/3));const double cw=(w-.3)/count;
            const Vec2 screen{p.x-w/2+.15+cw*(i+.5),p.y+d/2+.01};
            if(powered) panel(screen,cw-.15,h-.7,i);
            else c.quad(c.project({screen.x-(cw-.15)/2,screen.y},h-.7),
                        c.project({screen.x+(cw-.15)/2,screen.y},h-.7),
                        c.project({screen.x+(cw-.15)/2,screen.y},h-.15),
                        c.project({screen.x-(cw-.15)/2,screen.y},h-.15),0x080d14);
        }
        break;
    }
    case ShipFixture::ObservedTable:
    case ShipFixture::CraftTable:
        ship_worktable(c,game,b,prop.kind==ShipFixture::ObservedTable);
        break;
    case ShipFixture::CarrierWheelTable: {
        const bool active=game.mission2() && game.carrier_scanned() && !game.carrier_wheel_collected();
        InteractionOutline outline(c,active,time,0x54ff87);
        parts_table();
        if(game.mission2() && !game.carrier_wheel_collected()) {
            for(int i=0;i<20;++i) {
                const double a=i*.3141592654,beta=(i+1)*.3141592654;
                c.line(c.project({p.x+std::cos(a)*.27,p.y+std::sin(a)*.27},1.04),
                       c.project({p.x+std::cos(beta)*.27,p.y+std::sin(beta)*.27},1.04),0x31383d);
                c.line(c.project({p.x+std::cos(a)*.16,p.y+std::sin(a)*.16},1.045),
                       c.project({p.x+std::cos(beta)*.16,p.y+std::sin(beta)*.16},1.045),0xa9b3ad);
            }
            c.emissive_rect(int(c.project(p,1.05).x)-1,int(c.project(p,1.05).y)-1,2,2,0xd3c18a);
        }
        break;
    }
    case ShipFixture::CarrierArmTable: {
        const bool active=game.mission2() && game.carrier_scanned() && !game.carrier_arm_collected();
        InteractionOutline outline(c,active,time,0x54ff87);
        parts_table();
        if(game.mission2() && !game.carrier_arm_collected()) {
            const double z=1.05;
            c.oriented_box({p.x-.19,p.y},.13,.46,z,.08,0x87989a,.34);
            c.oriented_box({p.x+.12,p.y-.08},.12,.39,z+.07,.07,0xb1b4a5,-.62);
            c.line(c.project({p.x-.28,p.y-.17},z+.05),c.project({p.x-.40,p.y-.28},z+.05),0xd5c7a1);
            c.line(c.project({p.x-.40,p.y-.28},z+.05),c.project({p.x-.27,p.y-.35},z+.05),0xd5c7a1);
            c.emissive_rect(int(c.project({p.x+.25,p.y-.22},z+.12).x)-1,int(c.project({p.x+.25,p.y-.22},z+.12).y),2,1,0x78cde0);
        }
        break;
    }
    case ShipFixture::StorageChipTable: {
        const bool active=game.mission2() && game.carrier_scanned() && !game.carrier_chip_collected();
        InteractionOutline outline(c,active,time,0x54ff87);
        parts_table();
        if(game.mission2() && !game.carrier_chip_collected()) {
            c.box(p,.46,.30,1.02,.045,0x315c50);
            c.box(p,.30,.18,1.07,.025,0x6cc69c);
            for(int i=0;i<4;++i) c.emissive_rect(int(c.project({p.x-.11+i*.07,p.y},1.10).x),
                                                   int(c.project({p.x-.11+i*.07,p.y},1.10).y),1,1,0xc5f4bd);
        }
        break;
    }
    case ShipFixture::Galley:
        c.box(p,w,d,0,h,0x82908e);
        for(int i=0;i<int(w);++i) {
            c.box({b.x+i+.5,p.y},.8,d*.8,h,.04,0x415c65);
            c.line(c.project({b.x+i+.35,p.y+d/2+.02},h*.55),c.project({b.x+i+.65,p.y+d/2+.02},h*.55),0xc2b28e);
        }
        break;
    case ShipFixture::FoodSupply: {
        InteractionOutline outline(c,true,time,0x54ff87);
        c.box(p,w,d,0,h,0x263b68);
        c.box({p.x,p.y-.02},w-.12,d-.10,h,.12,0x315aa4);
        c.line(c.project({b.x+.12,b.y+d*.5+.01},h+.14),c.project({b.x+w-.12,b.y+d*.5+.01},h+.14),0xf0e0c0);
        c.line(c.project({b.x+w*.5,b.y+d*.5+.01},h+.14),c.project({b.x+w*.5,b.y+d*.5+.01},h+.28),0xe45858);
        c.line(c.project({b.x+w*.5-.12,b.y+d*.5+.01},h+.21),c.project({b.x+w*.5+.12,b.y+d*.5+.01},h+.21),0xe45858);
        supply_marker(c,p,h+.65);
        break;
    }
    case ShipFixture::Locker:
    case ShipFixture::Crate:
        c.box(p,w,d,0,h,prop.kind==ShipFixture::Crate ? 0x9c805b : 0x576d76);
        for(int i=0;i<int(w);++i) {
            c.line(c.project({b.x+i+.1,p.y+d/2+.01},.12),c.project({b.x+i+.1,p.y+d/2+.01},h-.1),0xb0aba0);
            const auto q=c.project({b.x+i+.5,p.y+d/2+.02},h*.6);
            c.emissive_rect(int(q.x),int(q.y),2,1,0x82c7ca);
        }
        break;
    }
}
}
Vec2 IsometricScene::project(Vec2 point, const WorldGrid& world) const {
    const auto& projection=active_projection();
    const auto p = projection.project(point,surface_height(world,point));
    return p.visible ? p.screen : projection.direction_marker(point);
}
Vec2 IsometricScene::item_position(GridPos cell, const WorldGrid& world) const {
    const Vec2 p{cell.x+0.5,cell.y+0.5};
    const auto& projection=active_projection();
    const auto q = projection.project(p,surface_height(world,p)+world.obstacle_height(cell.x,cell.y)*0.6);
    return q.visible ? q.screen : projection.direction_marker(p);
}
std::array<Vec2,4> IsometricScene::floor_corners(GridPos cell, const WorldGrid& world) const {
    const int x = cell.x, y = cell.y; const bool cliff = cliff_cell(world,x,y);
    std::array<Vec2,4> result{};
    const GridPos corners[] = {{x,y},{x+1,y},{x+1,y+1},{x,y+1}};
    for (int i = 0; i < 4; ++i) {
        const auto p = corners[i];
        result[i] = active_projection().project({double(p.x),double(p.y)},cliff ? world.elevation(x,y) : corner_height(world,p.x,p.y)).screen;
    }
    return result;
}
GridPos IsometricScene::pick(int x, int y) const {
    return x >= 0 && x < width && y >= top && y < bottom && picking_.size() == width*height ? picking_[y*width+x] : GridPos{-1,-1};
}
void IsometricScene::render_ship(std::vector<std::uint32_t>& pixels,const Game& game,bool headlamp,const RobotAppearance& appearance) {
    const auto p=game.unit().center();const double time=game.presentation().clock;
    auto& view=ship_projection_;view.curved=false;
    if(ship_time_<0 || time<ship_time_) {view.anchor=p;view.anchor_altitude=0;}
    const double follow=1-std::exp(-std::max(0.0,time-ship_time_)*12);
    view.anchor.x+=(p.x-view.anchor.x)*follow;view.anchor.y+=(p.y-view.anchor.y)*follow;
    ship_time_=time;
    std::fill(pixels.begin(),pixels.end(),0xff08121c);
    picking_.assign(width*height,{-1,-1});Canvas c{pixels,picking_,view};
    const auto visible=[&](Vec2 point,double margin) {
        const auto q=c.project(point,0);return q.x>-margin && q.x<width+margin && q.y>-margin && q.y<height+margin;
    };
    const auto deck_color=[](double x,double y) {
        // Continuous steel shading avoids visible cell edges or a tiled grid.
        const double wear=PlanetTerrain::noise(x*.22,y*.22,713)*.10;
        double glow=0;
        for(Vec2 lamp:{Vec2{10,16},Vec2{20,23},Vec2{25,34},Vec2{16,45}}) {
            const double dx=x-lamp.x,dy=y-lamp.y;
            glow+=std::exp(-(dx*dx+dy*dy)/15.0);
        }
        const auto channel=[&](double base,double light) {
            return unsigned(std::clamp(base*(.94+wear)+glow*light,0.0,255.0));
        };
        return (channel(48,3)<<16)|(channel(62,17)<<8)|channel(79,20);
    };
    for(int y=0;y<ShipInterior::height;++y) for(int x=0;x<ShipInterior::width;++x) {
        if(!ShipInterior::floor(x,y) || !visible({x+.5,y+.5},55)) continue;
        const int room=ShipInterior::room_at({x+.5,y+.5});
        const auto color=deck_color(x+.5,y+.5);
        const auto a=c.project({double(x),double(y)},0),b=c.project({x+1.0,double(y)},0),
                   d=c.project({x+1.0,y+1.0},0),e=c.project({double(x),y+1.0},0);
        c.terrain_triangle(a,b,d,deck_color(x,y),deck_color(x+1,y),deck_color(x+1,y+1),{x,y});
        c.terrain_triangle(a,d,e,deck_color(x,y),deck_color(x+1,y+1),deck_color(x,y+1),{x,y});
        for(int i=0;i<3;++i) {
            const auto grain=c.project({x+.16+((x*13+y*7+i*23)%67)*.01,y+.18+((x*5+y*11+i*17)%63)*.01},.005);
            c.rect(int(grain.x),int(grain.y),1,1,tint(color,i==0 ? 1.18 : .9));
        }
        if(ship_damage_style_) {
            if(room==2 && (x%3==0 || y%5==0)) {
                const auto lamp=c.project({x+.5,y+.5},.06);
                c.emissive_rect(int(lamp.x)-1,int(lamp.y)-1,3,2,0x4e969e);
            }
            if((x*11+y*5)%23==0) {
                const auto stain=c.project({x+.24,y+.72},.018);
                c.rect(int(stain.x),int(stain.y),2,1,(x+y)%2 ? 0x263742 : 0x3c4953);
            }
        }
    }
    const auto dock=ShipInterior::charger;const bool charging=game.charging();
    c.box(dock,1.1,1.0,.01,.035,0x31565e);
    for(double side:{-.48,.48}) {
        c.box({dock.x+side,dock.y},.08,.95,.05,.035,0x6ee2df);
        const auto q=c.project({dock.x+side,dock.y-.4+std::fmod(time*.5,.8)},.1);
        c.emissive_rect(int(q.x)-1,int(q.y)-1,2,2,charging ? 0xa4fff3 : 0x65b5be);
    }
    struct DeckObject {double depth;int kind,index,x,y;};std::vector<DeckObject> objects;
    for(int y=2;y<ShipInterior::height-1;++y) for(int x=1;x<ShipInterior::width-1;++x) {
        if(ShipInterior::blocked(x,y)) continue;
        bool hallway=false;
        for(const auto& corridor:ShipInterior::corridors) hallway=hallway||corridor.contains(x,y);
        const auto seed=ship_debris_seed(x,y);
        // Sparse irregular piles leave more exposed deck along the passages.
        if(seed%100 >= (hallway ? 24u : 8u)) continue;
        const Vec2 pos{x+.22+((seed>>8)%56)*.01,y+.22+((seed>>18)%56)*.01};
        if(!visible(pos,40) || distance(pos,ShipInterior::exit)<1.7 ||
           distance(pos,ShipInterior::entry)<1.0 || distance(pos,ShipInterior::charger)<1.5 ||
           distance(pos,ShipInterior::astronaut)<1.1 || distance(pos,ShipInterior::records_pc)<1.5 ||
           distance(pos,ShipInterior::storage_door_center)<1.8) continue;
        bool door_clear=true;
        for(const auto& door:ShipInterior::doors) if(distance(pos,door.center)<1.8) door_clear=false;
        if(door_clear) objects.push_back({pos.x+pos.y,5,0,x,y});
    }
    for(int y=0;y<ShipInterior::height;++y) for(int x=0;x<ShipInterior::width;++x) {
        if(ShipInterior::floor(x,y) || !visible({x+.5,y+.5},95)) continue;
        bool edge=false;for(GridPos d:{GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}) edge=edge||ShipInterior::floor(x+d.x,y+d.y);
        if(edge) objects.push_back({x+y+1.0,0,0,x,y});
    }
    objects.push_back({ShipInterior::storage_door_center.x+ShipInterior::storage_door_center.y,3,0,0,0});
    for(int i=0;i<int(ShipInterior::doors.size());++i) {
        const auto p=ShipInterior::doors[i].center;objects.push_back({p.x+p.y,4,i,0,0});
    }
    for(int i=0;i<int(ShipInterior::furniture.size());++i) {
        const auto& prop=ShipInterior::furniture[i];const auto b=prop.bounds;
        const Vec2 center{b.x+b.w*.5,b.y+b.h*.5};
        if(game.carrier_mobile() && (prop.kind==ShipFixture::Rover || prop.kind==ShipFixture::RoverArm)) continue;
        if(prop.kind==ShipFixture::FoodSupply) {
            const int food_index=center.y<30 ? 0 : 1;
            if(game.indoor_food_collected(food_index)) continue;
        }
        if(!visible(center,(b.w+b.h)*22+50)) continue;
        const auto a=c.project({b.x-.05,b.y-.05},.01),bb=c.project({b.x+b.w+.08,b.y-.05},.01),
                   cc=c.project({b.x+b.w+.08,b.y+b.h+.08},.01),d=c.project({b.x-.05,b.y+b.h+.08},.01);
        c.blend_quad(a,bb,cc,d,0x14232b,.16);
        double depth=center.x+center.y;
        if(b.contains(int(p.x),int(p.y)) && game.unit().elevation()>=ShipInterior::fixture_height(prop.kind)-.01) depth=p.x+p.y-.1;
        objects.push_back({depth,1,i,0,0});
    }
    for(int i=0;i<int(ship_toppled_seats.size());++i) {
        const auto seat=ship_toppled_seats[i];
        if(visible(seat,45)) objects.push_back({seat.x+seat.y,6,i,0,0});
    }
    if(game.carrier_mobile()) {
        const auto carrier=game.escort_carrier_position();objects.push_back({carrier.x+carrier.y,7,0,0,0});
    }
    if(game.rescue_mission()==6) for(int i=0;i<3;++i) if(!game.rescue_material_collected(i)) {
        const auto material=ShipInterior::rescue_materials[i];objects.push_back({material.x+material.y+.1,8,i,0,0});
    }
    ship_robot_shadow(c,game);
    objects.push_back({p.x+p.y,2,0,0,0});
    std::stable_sort(objects.begin(),objects.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
    for(const auto& object:objects) {
        if(object.kind==0) {
            ship_wall(c,game,object.x,object.y,ship_damage_style_);
        } else if(object.kind==1) {
            const auto& prop=ShipInterior::furniture[object.index];
            ship_furniture(c,game,prop,object.index);
            ship_robot_shadow(c,game,&prop.bounds,ShipInterior::fixture_height(prop.kind));
        }
        else if(object.kind==2) {
            if(appearance.model==RobotModel::Mark1) robot_mark1(c,game);else robot_mark2(c,game,appearance);
            if(headlamp) cartoon_eye_rays(c,game,appearance.model);
        } else if(object.kind==7) {
            rescue_transport(c,game,game.escort_carrier_position(),0);
        } else if(object.kind==8) {
            const auto material=ShipInterior::rescue_materials[object.index];
            InteractionOutline outline(c,true,time,0x54ff87);
            c.box(material,.64,.48,.9,.28,object.index==0 ? 0x93a6b9 : object.index==1 ? 0xe2b568 : 0x75c6b2);
            supply_marker(c,material,1.5);
        } else if(object.kind==6) {
            ship_toppled_seat(c,object.index);
        } else if(object.kind==5) {
            const auto seed=ship_debris_seed(object.x,object.y);
            const Vec2 pos{object.x+.22+((seed>>8)%56)*.01,object.y+.22+((seed>>18)%56)*.01};
            ship_crash_object(c,pos,.015,seed);
        } else if(object.kind==3) ship_sliding_door(c,ShipInterior::storage_door,game.storage_door_phase(),game.storage_door_open());
        else ship_sliding_door(c,ShipInterior::doors[object.index].bounds,game.ship_door_phase(static_cast<ShipDoor>(object.index)),game.ship_door_open(static_cast<ShipDoor>(object.index)));
    }
    if(!game.astronaut_aboard() && game.rescue_stage()!=Game::RescueStage::Loading) ship_injured_astronaut(c,time);
    if(game.rescue_stage()==Game::RescueStage::Loading) {
        const double t=game.rescue_stage_progress(),smooth=t*t*(3-2*t);
        const Vec2 bed{9.5,17.45},seat=game.escort_carrier_position();
        const Vec2 passenger{bed.x+(seat.x-bed.x)*smooth,bed.y+(seat.y-bed.y)*smooth};
        const double z=.5+.26*smooth+.7*std::sin(t*3.14159265);
        const auto unit=game.unit().center();
        // Two support arms follow the passenger throughout the lift.
        for(double side:{-.24,.24}) {
            c.line(c.project({unit.x,unit.y+side},.7),c.project({passenger.x,passenger.y+side},z+.12),0x92c9d9);
            c.line(c.project({seat.x,seat.y+side},.65),c.project({passenger.x,passenger.y+side},z+.12),0xd5a958);
        }
        rescue_passenger(c,passenger,z,smooth);
    }
    for(const auto& particle:game.presentation().particles()) {
        if(particle.dust) continue;
        const double age=particle.age/particle.lifetime;
        const auto q=c.project(particle.position,.5*(1-age));
        c.rect(int(q.x),int(q.y),particle.size,particle.size,tint(particle.color,1-age));
    }
    std::vector<Canvas::Glow> glows;for(const auto glow:c.glows) if(pixels[glow.pixel]==glow.color) glows.push_back(glow);
    SceneLighting::apply(pixels,picking_,game,{0,0,headlamp,&view,appearance,ship_damage_style_});
    for(const auto glow:glows) pixels[glow.pixel]=glow.color;
}
void IsometricScene::render(std::vector<std::uint32_t>& pixels, const Game& game, bool headlamp, const RobotAppearance& appearance) {
    if (pixels.size() != width*height) return;
    if(ship_scene_!=game.in_spaceship()) { last_time_=-1;ship_time_=-1; }
    ship_scene_=game.in_spaceship();
    if(ship_scene_) { render_ship(pixels,game,headlamp,appearance);return; }
    const auto& world = game.world(); const auto p = game.unit().center();
    const double time = game.presentation().clock;
    // Leave more skyward room for the wreck while the robot is near camp.
    // The blend fades out on the open map and keeps indoor movement centered.
    Vec2 focus = p;
    const double camp_weight = std::clamp((8.0-distance(p,BaseCamp::center))/8.0,0.0,0.47);
    focus.x += (BaseCamp::center.x-p.x)*camp_weight;
    focus.y += (BaseCamp::center.y-p.y)*camp_weight;
    const bool reset = last_time_ < 0 || time < last_time_;
    const double camera_ground = world.pit(int(p.x),int(p.y)) ?
        game.unit().fall_origin_elevation() : surface_height(world,p);
    if (reset) { projection_.anchor = focus; projection_.anchor_altitude = camera_ground; }
    const double smoothing = 1-std::exp(-std::max(0.0,time-last_time_)*12);
    projection_.anchor.x += (focus.x-projection_.anchor.x)*smoothing;
    projection_.anchor.y += (focus.y-projection_.anchor.y)*smoothing;
    projection_.anchor_altitude += (camera_ground-projection_.anchor_altitude)*smoothing;
    last_time_ = time;
    if (projection_.curved) SpaceBackground::draw(pixels,{width,height,top,bottom,0,0,10000,time});
    draw_planet_shell(pixels,projection_,world);
    picking_.assign(width*height,{-1,-1});
    Canvas c{pixels,picking_,projection_};
    const int stride = world.width()+1;
    if (terrain_width_ != world.width() || terrain_height_ != world.height() || terrain_seed_ != world.seed()) {
        terrain_width_ = world.width(); terrain_height_ = world.height(); terrain_seed_ = world.seed();
        ground_colors_.resize(std::size_t(stride)*(world.height()+1));
        for (int y = 0; y <= world.height(); ++y) for (int x = 0; x <= world.width(); ++x) {
            const double dx = corner_height(world,x+1,y)-corner_height(world,x-1,y);
            const double dy = corner_height(world,x,y+1)-corner_height(world,x,y-1);
            const double light = (0.8+dx*0.24+dy*0.32)/std::sqrt(1+dx*dx*0.25+dy*dy*0.25);
            ground_colors_[y*stride+x] = tint(PlanetTerrain::material(x,y,world.seed(),world.width(),world.height()),std::clamp(0.76+light*0.3,0.72,1.12));
        }
    }
    const auto& ground_colors = ground_colors_;
    for (int sum = 0; sum < world.width()+world.height()-1; ++sum) for (int x = 0; x < world.width(); ++x) {
        const int y = sum-x; if (!world.in_bounds(x,y)) continue;
        const double z = world.elevation(x,y); const GridPos id{x,y};
        const auto center = projection_.project({x+0.5,y+0.5},z);
        if (!center.visible || center.depth < 0.035) continue;
        if (center.screen.x < -70 || center.screen.x > width+70 || center.screen.y < top-80 || center.screen.y > bottom+80) continue;
        bool front = true;
        for (Vec2 corner : {Vec2{double(x),double(y)},Vec2{x+1.0,double(y)},Vec2{x+1.0,y+1.0},Vec2{double(x),y+1.0}})
            front = front && projection_.project(corner,z).visible;
        if (!front) continue;
        const bool cliff = cliff_cell(world,x,y);
        const auto a = c.project({double(x),double(y)},cliff ? z : corner_height(world,x,y)),
                   b = c.project({x+1.0,double(y)},cliff ? z : corner_height(world,x+1,y)),
                   d = c.project({double(x),y+1.0},cliff ? z : corner_height(world,x,y+1)),
                   e = c.project({x+1.0,y+1.0},cliff ? z : corner_height(world,x+1,y+1));
        if (e.x < -50 || e.x > width+50 || e.y < top-60 || a.y > bottom+60) continue;
        const auto hash = unsigned(x*317+y*719+world.seed()*31);
        auto color = PlanetTerrain::material(x+0.5,y+0.5,world.seed(),world.width(),world.height());
        const double dx = world.elevation(x+1,y)-world.elevation(x-1,y);
        const double dy = world.elevation(x,y+1)-world.elevation(x,y-1);
        const double normal_light = (0.8+dx*0.24+dy*0.32)/std::sqrt(1+dx*dx*0.25+dy*dy*0.25);
        color = tint(color,std::clamp(0.76+normal_light*0.3,0.72,1.12));
        const bool pad = x >= 7 && x <= 9 && y >= 9 && y <= 11 && (x >= 8 || y <= 10);
        const bool camp_floor = BaseCamp::footprint(x,y), hole = world.pit(x,y);
        if (pad) color = hash%2 ? 0x697c7c : 0x728382;
        if (camp_floor) color = shelter_dust(x+0.5,y+0.5,world.seed());
        if (world.get_tile_type(x,y) == TileType::Scaffold) color = 0x828a83;
        if (hole) c.quad(a,b,e,d,0x000000,id);
        else if (camp_floor) {
            const auto ca = shelter_dust(x,y,world.seed()), cb = shelter_dust(x+1,y,world.seed()),
                       cd = shelter_dust(x,y+1,world.seed()), ce = shelter_dust(x+1,y+1,world.seed());
            c.terrain_triangle(a,b,e,ca,cb,ce,id); c.terrain_triangle(a,e,d,ca,ce,cd,id);
        } else if (pad || world.get_tile_type(x,y) == TileType::Scaffold) c.quad(a,b,e,d,color,id);
        else {
            const auto ca = ground_colors[y*stride+x], cb = ground_colors[y*stride+x+1],
                       cd = ground_colors[(y+1)*stride+x], ce = ground_colors[(y+1)*stride+x+1];
            c.terrain_triangle(a,b,e,ca,cb,ce,id); c.terrain_triangle(a,e,d,ca,ce,cd,id);
        }
        if (pad) c.line(d,e,tint(color,0.86));
        // Grain, exposed stone and narrow tread ruts replace visible checkerboard edges.
        const int grains = hole ? 0 : camp_floor ? 3 : 3+int(hash%3);
        for (int detail = 0; detail < grains; ++detail) {
            const double px = x+0.12+PlanetTerrain::noise(x*9+detail*7,y*13,world.seed())*0.76;
            const double py = y+0.12+PlanetTerrain::noise(x*7,y*11+detail*3,world.seed()+53)*0.76;
            const auto q = c.project({px,py},surface_height(world,{px,py})+0.005);
            c.rect(int(q.x),int(q.y),!camp_floor && detail%3 == 0 ? 2 : 1,1,
                   tint(color,camp_floor ? (detail%2 ? 0.96 : 1.04) : (detail%2 ? 0.86 : 1.09)));
        }
        if (y == 10 && x > 12 && x < world.width()-14) {
            for (double rut : {0.27,0.72}) {
                const double shift = PlanetTerrain::noise(x*0.2,2,world.seed())*0.12;
                c.line(c.project({double(x),y+rut+shift},z+0.005),c.project({x+1.0,y+rut+shift},z+0.005),tint(color,0.83));
            }
        }
        if (world.get_tile_type(x,y) == TileType::Scaffold) {
            c.line(a,e,0xd5bc83); c.line(b,d,0xd5bc83);
        }
        if (!hole && !camp_floor && !pad) {
            settled_dust(c,world,x,y,color);
            surface_scatter(c,world,x,y);
        }
    }
    interior_wreckage_floor(c,game);
    // Marks live in world coordinates, under every rock, building and the robot.
    // Reprojecting them each frame also keeps them attached in the globe view.
    for (const auto& mark : game.presentation().tread_marks()) {
        const double age = time-mark.born;
        const double opacity = Presentation::tread_opacity(age);
        if (opacity <= 0) continue;
        const Vec2 forward{std::cos(mark.heading),std::sin(mark.heading)}, right{forward.y,-forward.x};
        for (int side : {-1,1}) {
            const Vec2 center{mark.position.x+right.x*side*0.23,mark.position.y+right.y*side*0.23};
            const GridPos cell{int(std::floor(center.x)),int(std::floor(center.y))};
            if (!world.in_bounds(cell.x,cell.y) || world.solid(cell.x,cell.y) || world.pit(cell.x,cell.y)) continue;
            const auto projected = projection_.project(center,surface_height(world,center));
            if (!projected.visible || projected.depth < 0.08 || projected.screen.x < -8 || projected.screen.x > width+8 ||
                projected.screen.y < top-8 || projected.screen.y >= bottom+8) continue;
            double lowest = 1e9, highest = -1e9;
            bool front = true;
            for (double across : {-0.105,0.105}) for (double along : {-0.045,0.055}) {
                const Vec2 q{center.x+right.x*across+forward.x*along,center.y+right.y*across+forward.y*along};
                const double z = surface_height(world,q);
                lowest = std::min(lowest,z); highest = std::max(highest,z);
                front = front && projection_.project(q,z).visible;
            }
            if (!front || highest-lowest > 0.25) continue; // Never stretch a decal over a cliff or the horizon.
            const auto ground = [&](double across,double along) {
                const Vec2 q{center.x+right.x*across+forward.x*along,center.y+right.y*across+forward.y*along};
                return c.project(q,surface_height(world,q)+0.006);
            };
            const bool pad = cell.x >= 7 && cell.x <= 10 && cell.y >= 9 && cell.y <= 11;
            const bool camp_dust = BaseCamp::footprint(cell.x,cell.y);
            auto material = camp_dust ? shelter_dust(center.x,center.y,world.seed()) :
                pad ? 0x697c7cu : PlanetTerrain::material(center.x,center.y,world.seed(),world.width(),world.height());
            // Broad, dark transverse tread bars plus a fine raised dust edge.
            const auto ink = tint(material,camp_dust ? 0.78 : 0.57);
            c.blend_quad(ground(-0.105,-0.045),ground(0.105,-0.045),ground(0.105,0.045),ground(-0.105,0.045),ink,opacity);
            c.blend_quad(ground(-0.105,0.045),ground(0.105,0.045),ground(0.105,0.065),ground(-0.105,0.065),tint(material,0.85),opacity*0.7);
        }
    }
    SceneShadows::apply(pixels,picking_,game,projection_);
    // Dust stays behind the robot, rocks and shelter geometry.
    for (const auto& particle : game.presentation().particles()) {
        if (!particle.dust) continue;
        if (world.pit(int(std::floor(particle.position.x)),int(std::floor(particle.position.y)))) continue;
        const double age = particle.age/particle.lifetime;
        const double ground = surface_height(world,particle.position);
        const auto view = projection_.project(particle.position,ground+0.04+age*0.28);
        if (!view.visible || view.depth < 0.08) continue;
        const auto q = view.screen;
        // Soft alpha, expanding edges and upward drift replace dark squares.
        const double radius = 1.8+age*3.8;
        for (int y = -5; y <= 5; ++y) for (int x = -5; x <= 5; ++x) {
            const int px = int(q.x)+x, py = int(q.y)+y;
            if (px < 0 || px >= width || py < top || py >= bottom) continue;
            const double edge = std::max(0.0,1-std::hypot(x,y*1.25)/radius);
            const double alpha = edge*(1-age)*(1-age)*0.62;
            if (alpha <= 0) continue;
            // Foreground structures retain their silhouette.
            const auto cell = picking_[py*width+px];
            if (world.solid(cell.x,cell.y)) continue;
            const auto old = pixels[py*width+px]; std::uint32_t color = 0;
            for (int shift : {0,8,16})
                color |= unsigned(((old>>shift)&255)*(1-alpha)+((particle.color>>shift)&255)*alpha)<<shift;
            c.pixel(px,py,color);
        }
    }

    if (game.placeholder_mission()==4 || game.mission_number()==3) {
        for (int x=1;x<world.width()-1;++x) {
            if (!world.mission4_cliff_cell(x,BaseCamp::mission4_cliff_north)) continue;
            const double height=2.35+((x*13)%5)*0.08;
            const auto front_left=Vec2{double(x),double(BaseCamp::mission4_cliff_north)};
            const auto front_right=Vec2{x+1.0,double(BaseCamp::mission4_cliff_north)};
            const auto back_left=Vec2{double(x),double(BaseCamp::mission4_cliff_south+1)};
            const auto back_right=Vec2{x+1.0,double(BaseCamp::mission4_cliff_south+1)};
            const auto fl=c.project(front_left,surface_height(world,front_left));
            const auto fr=c.project(front_right,surface_height(world,front_right));
            const auto tl=c.project(front_left,surface_height(world,front_left)+height);
            const auto tr=c.project(front_right,surface_height(world,front_right)+height);
            const auto tbl=c.project(back_left,surface_height(world,back_left)+height);
            const auto tbr=c.project(back_right,surface_height(world,back_right)+height);
            c.quad(tl,tr,tbr,tbl,0x626765);
            c.quad(fl,fr,tr,tl,0x4c5352);
            c.line(tl,tr,0x8a8b7b);
            c.line(tl,tbl,0x454b4b);
        }
    }
    rescue_aircraft_lower(c,game);
    struct Object { double depth; int kind, x, y; };
    std::vector<Object> objects;
    for (int y = 0; y < world.height(); ++y) for (int x = 0; x < world.width(); ++x) {
        if (!world.solid(x,y) && !BaseCamp::doorway(x,y)) continue;
        if (world.mission4_cliff_cell(x,y)) continue;
        if ((x == 8 && (y == 9 || y == 10)) || (x == world.width()-14 && (y == 11 || y == 12))) continue;
        const auto visible = projection_.project({x+0.5,y+0.5},world.elevation(x,y));
        if (!visible.visible || visible.depth < 0.08) continue;
        if ((x == 0 || y == 0 || x == world.width()-1 || y == world.height()-1) && distance(p,{x+0.5,y+0.5}) > 7) continue;
        const auto q = visible.screen;
        if (q.x < -80 || q.x > width+80 || q.y < top-20 || q.y > bottom+90) continue;
        objects.push_back({x+y+1.0,BaseCamp::structure(x,y) || BaseCamp::doorway(x,y) ? 4 : 0,x,y});
    }
    if(game.mission1()) for(int i=0;i<3;++i) { const auto& s=game.supplies()[i]; if(!s.collected) objects.push_back({s.position.x+s.position.y,13,i,0}); }
    if (game.survival_enabled())
        objects.push_back({BaseCamp::charger.x+BaseCamp::charger.y-0.05,12,0,0});
    if (game.data_chip_spotted() && !game.data_chip_collected())
        objects.push_back({BaseCamp::data_bank.x+BaseCamp::data_bank.y,14,0,0});
    if (game.placeholder_mission()==4 || game.mission_number()==3)
        objects.push_back({BaseCamp::damaged_shuttle.x+BaseCamp::damaged_shuttle.y,15,0,0});
    if(game.carrier_mobile()) {
        const auto carrier=game.escort_carrier_position();objects.push_back({carrier.x+carrier.y,16,0,0});
    }
    if (!game.unit().fell_into_pit()) objects.push_back({p.x+p.y,1,0,0});
    const auto& creatures = game.danger().creatures();
    for (std::size_t i = 0; i < creatures.size(); ++i)
        if (creatures[i].health > 0 || creatures[i].death_age < 1.1)
            objects.push_back({creatures[i].position.x+creatures[i].position.y,6,int(i),0});
    auto station = game.refinery().position(), ship = game.rocket().ground_socket();
    if (projection_.project(station,0).visible) objects.push_back({station.x+station.y,2,0,0});
    if (projection_.project(ship,0).visible) objects.push_back({ship.x+ship.y,3,0,0});
    const auto camp = projection_.project(BaseCamp::center,0);
    if (camp.visible && camp.screen.x > -400 && camp.screen.x < width+400 && camp.screen.y > top-200 && camp.screen.y < bottom+200) {
        objects.push_back({BaseCamp::right+BaseCamp::south+2.0,5,0,0});
    }
    std::stable_sort(objects.begin(),objects.end(),[](const Object& a, const Object& b) { return a.depth < b.depth; });
    for (const auto& object : objects) {
        if (object.kind == 0) node(c,world,object.x,object.y,time);
        if (object.kind == 1) {
            if (appearance.model == RobotModel::Mark1) robot_mark1(c,game);
            else robot_mark2(c,game,appearance);
            if (headlamp) cartoon_eye_rays(c,game,appearance.model);
        }
        if (object.kind == 2 && !game.campaign()) base(c,game);
        if (object.kind == 3 && !game.campaign()) rocket(c,game);
        if (object.kind == 4) base_camp_cell(c,game,object.x,object.y);
        if (object.kind == 5) rescue_aircraft_upper(c,game);
        if (object.kind == 6) creature(c,game,creatures[std::size_t(object.x)]);
        if (object.kind == 12) charger_pad(c,game);
        if (object.kind == 13) {
            const auto p=game.supplies()[object.x].position; const double z=surface_height(world,p);
            InteractionOutline outline(c,true,time,0x54ff87);
            const bool first_aid=object.x<3;
            // First-aid cases and blue food/water supplement boxes share the mission pickup silhouette.
            c.box(p,0.68,0.46,z,0.17,0x18202a);
            c.box(p,0.66,0.44,z+0.17,0.43,first_aid ? 0xb33d4e : 0x254dc1);
            c.box({p.x,p.y-0.035},0.72,0.48,z+0.60,0.065,first_aid ? 0xd0525b : 0x3765de);
            const auto at=[&](double x,double y,double h){ return c.project({p.x+x,p.y+y},z+h); };
            c.line(at(-0.22,0.23,0.22),at(0.22,0.23,0.22),first_aid ? 0xf0c8ba : 0x5589ed);
            c.line(at(-0.24,-0.04,0.66),at(-0.18,-0.04,0.93),0x234bb7);
            c.line(at(-0.18,-0.04,0.93),at(0.18,-0.04,0.93),0x3765de);
            c.line(at(0.18,-0.04,0.93),at(0.24,-0.04,0.66),0x234bb7);
            const auto badge=at(0.10,0.235,0.29);
            if(first_aid) {
                c.emissive_rect(int(badge.x)-3,int(badge.y)-2,6,5,0xf1d8c8);
                c.rect(int(badge.x)-1,int(badge.y)-3,2,6,0xc93649);
                c.rect(int(badge.x)-3,int(badge.y)-1,6,2,0xc93649);
            } else {
                c.emissive_rect(int(badge.x)-3,int(badge.y)-2,6,5,0x386cce);
                c.rect(int(badge.x)-2,int(badge.y)-1,4,1,0xffffff);
            }
            supply_marker(c,p,z+1.15);
        }
        if (object.kind == 14) data_chip_bank(c,game,time);
        if (object.kind == 15) damaged_shuttle(c,game);
        if (object.kind == 16) {
            const auto carrier=game.escort_carrier_position();
            rescue_transport(c,game,carrier,surface_height(world,carrier));
        }
    }
    if(game.rescue_mission()==8) {
        const auto ship=BaseCamp::damaged_shuttle;
        const double ground=surface_height(world,ship);
        if(game.rescue_stage()==Game::RescueStage::RepairingShip) {
            const auto robot=game.unit().center();
            const auto weld=c.project({ship.x-.5,ship.y+.6},ground+.55);
            c.line(c.project(robot,game.unit().elevation()+.6),weld,0x8de9f6);
            for(int i=0;i<6;++i) {
                const double phase=time*12+i*1.0472;
                c.line(weld,{weld.x+std::cos(phase)*7,weld.y+std::sin(phase)*5},i%2 ? 0xffd68b : 0xb2f5ff);
            }
        }
        if(game.rescue_stage()==Game::RescueStage::LaunchingShip) {
            const double lift=game.rescue_ship_lift();
            for(double x:{-.9,.9}) {
                const auto nozzle=c.project({ship.x+x,ship.y+.28},ground+lift+.05);
                const auto jet=c.project({ship.x+x,ship.y+.28},ground+lift-1.0-.2*std::sin(time*45));
                c.emissive_line({nozzle.x-1,nozzle.y},jet,0xefad63);
                c.emissive_line({nozzle.x+1,nozzle.y},jet,0x80e5ff);
                c.emissive_line(nozzle,{jet.x,jet.y-3},0xe5faff);
            }
        }
        if(game.rescue_stage()==Game::RescueStage::ChargingCarrier || game.rescue_stage()==Game::RescueStage::ChargingUnit) {
            const bool carrier=game.rescue_stage()==Game::RescueStage::ChargingCarrier;
            const auto source=carrier ? game.escort_carrier_position() : game.unit().center();
            const double source_z=surface_height(world,source)+.6;
            const auto a=c.project(source,source_z),b=c.project({ship.x-1,ship.y+.6},ground+.55);
            c.line({a.x,a.y+1},{b.x,b.y+1},0x1c3440);
            c.line(a,b,carrier ? 0x7ad4ec : 0xf2c278);
            for(int i=0;i<4;++i) {
                const double t=std::fmod(time*.7+i*.25,1.0);
                const auto q=between(a,b,t);c.emissive_rect(int(q.x)-1,int(q.y)-1,3,3,0xcefbff);
            }
        }
    }
    if (game.presentation().mining_target) {
        auto target = *game.presentation().mining_target;
        const double z = world.elevation(target.x,target.y);
        const double heading = game.presentation().heading;
        const Vec2 arm{p.x+std::sin(heading)*0.43+std::cos(heading)*0.1,
                       p.y-std::cos(heading)*0.43+std::sin(heading)*0.1};
        const auto a = c.project(arm,game.unit().elevation()+0.6);
        const auto b = c.project({target.x+0.5,target.y+0.5},z+0.5);
        c.line({a.x,a.y-1},{b.x,b.y-1},0x3f8b87); c.line(a,b,0xb5ffe1);
        c.rect(int(b.x)-2,int(b.y)-2,4,4,0xffe6a7);
    }
    if (game.combat_beam()) {
        const auto& beam = *game.combat_beam();
        const double target_z = surface_height(world,beam.endpoint)+(beam.hit ? 0.30 : 0.12);
        const auto a = c.project(beam.origin,game.unit().elevation()+0.72);
        const auto b = c.project(beam.endpoint,target_z);
        c.line({a.x,a.y-1},{b.x,b.y-1},0x9d2638);
        c.line(a,b,beam.age < 0.07 ? 0xffddbf : 0xff4e4e);
        c.line({a.x,a.y+1},{b.x,b.y+1},0xb32339);
        c.emissive_rect(int(a.x)-2,int(a.y)-2,4,4,0xff7668);
        c.emissive_rect(int(b.x)-2,int(b.y)-2,beam.hit ? 5 : 3,beam.hit ? 5 : 3,
                        beam.hit ? 0xffc58b : 0xff5f56);
    }
    for (const auto& particle : game.presentation().particles()) {
        if (particle.dust) continue; // Surface dust is drawn beneath solid objects.
        if (world.pit(int(std::floor(particle.position.x)),int(std::floor(particle.position.y)))) continue;
        const double age = particle.age/particle.lifetime;
        const auto view = projection_.project(particle.position,surface_height(world,particle.position)+0.5*(1-age));
        if (!view.visible || view.depth < 0.08) continue;
        c.rect(int(view.screen.x),int(view.screen.y),particle.size,particle.size,tint(particle.color,1-age));
    }
    const auto valve = game.rocket().launch_valve();
    auto q = project(valve,world);
    if (!game.campaign() && projection_.project(valve,0).visible) {
        c.box(valve,0.5,0.5,world.elevation(int(valve.x),int(valve.y)),0.65,game.rocket().calibrated() ? 0x83d8ac : 0xb39d6e);
        c.rect(int(q.x)-2,int(q.y)-12,4,4,0xe7d7a2);
    }
    if (game.climax().storm().active() && !game.rocket().launched()) {
        for (int i = 0; i < 60; ++i) {
            const int x = (i*97+int(time*110))%width, y = top+(i*31+int(time*150))%(bottom-top);
            c.line({double(x),double(y)},{double(x-5),double(y+9)},0xb77a69);
        }
    }
    // Record only eyes still visible after opaque geometry, preventing their
    // emission from appearing through habitat walls or foreground rocks.
    std::vector<Canvas::Glow> visible_glows;
    for (auto glow : c.glows) if (pixels[glow.pixel] == glow.color) visible_glows.push_back(glow);
    SceneLighting::apply(pixels,picking_,game,{0,0,headlamp,&projection_,appearance});
    for (auto glow : visible_glows) pixels[glow.pixel] = glow.color;
}
}

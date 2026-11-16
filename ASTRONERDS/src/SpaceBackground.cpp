#include "horizon/SpaceBackground.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace horizon {
namespace {
struct Point { double x, y; };
struct Vertex { double x, y, z; };
std::uint32_t shade(std::uint32_t color, double gain) {
    const auto channel = [&](int shift) { return unsigned(std::clamp(((color>>shift)&255)*gain,0.0,255.0)); };
    return (channel(16)<<16)|(channel(8)<<8)|channel(0);
}
struct Surface {
    std::vector<std::uint32_t>& pixels;
    const BackgroundView& view;
    void pixel(int x, int y, std::uint32_t color) {
        if (x >= 0 && x < view.width && y >= view.top && y < view.bottom)
            pixels[std::size_t(y)*view.width+x] = 0xff000000|color;
    }
    void blend(int x, int y, std::uint32_t color, double alpha) {
        if (x < 0 || x >= view.width || y < view.top || y >= view.bottom) return;
        const auto old = pixels[std::size_t(y)*view.width+x];
        std::uint32_t result = 0;
        for (int shift : {0,8,16}) result |= unsigned(((old>>shift)&255)*(1-alpha)+((color>>shift)&255)*alpha)<<shift;
        pixel(x,y,result);
    }
    void triangle(Point a, Point b, Point c, std::uint32_t color) {
        const auto edge = [](Point p, Point q, Point r) { return (r.x-p.x)*(q.y-p.y)-(r.y-p.y)*(q.x-p.x); };
        const double area = edge(a,b,c);
        if (std::abs(area) < 0.001) return;
        const int left = std::max(0,int(std::floor(std::min({a.x,b.x,c.x}))));
        const int right = std::min(view.width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
        const int top = std::max(view.top,int(std::floor(std::min({a.y,b.y,c.y}))));
        const int bottom = std::min(view.bottom-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
        for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
            const Point p{x+0.5,y+0.5};
            const double u = edge(a,b,p)/area, v = edge(b,c,p)/area, w = edge(c,a,p)/area;
            if (u >= -0.0001 && v >= -0.0001 && w >= -0.0001) pixel(x,y,color);
        }
    }
};
void planet(Surface& screen) {
    const auto& v = screen.view;
    const int cx = int(v.width*0.58-v.camera_x*0.035), cy = int(v.top+55+(v.surface_y-180)*0.045);
    constexpr int radius = 53;
    if (cx+radius+6 < 0 || cx-radius-6 >= v.width || cy+radius+6 < v.top || cy-radius-6 >= v.bottom) return;
    for (int y = -radius-6; y <= radius+6; ++y) for (int x = -radius-6; x <= radius+6; ++x) {
        const double rr = double(x*x+y*y);
        if (rr > (radius+6)*(radius+6)) continue;
        const double radial = std::sqrt(rr);
        if (radial > radius) {
            const double alpha = std::pow(1-(radial-radius)/6,2)*0.3;
            screen.blend(cx+x,cy+y,0x88c1d8,alpha);
            continue;
        }
        const double nx = double(x)/radius, ny = double(y)/radius, nz = std::sqrt(std::max(0.0,1-nx*nx-ny*ny));
        const double longitude = std::atan2(nx,nz)+v.time*0.025, latitude = std::asin(ny);
        const double terrain = std::sin(longitude*4+std::sin(latitude*8))*std::sin(latitude*5+longitude*2)+0.2*std::sin(longitude*13+latitude*19);
        std::uint32_t albedo = terrain > 0.1 ? 0xa77464 : terrain > -0.35 ? 0x7e555b : 0x4e4c60;
        if (std::abs(ny) > 0.86+0.025*std::sin(longitude*9)) albedo = 0xb1c3cb;
        const double cloud = std::sin(longitude*8+latitude*21-v.time*0.04);
        if (cloud > 0.93 && terrain > -0.2) albedo = 0xb29b8b;
        const double diffuse = std::max(0.0,-nx*0.65-ny*0.4+nz*0.65);
        screen.pixel(cx+x,cy+y,shade(albedo,0.11+0.89*diffuse));
        screen.blend(cx+x,cy+y,0x8fbbd7,std::pow(1-nz,4)*0.38);
    }
}
void asteroid(Surface& screen, int index) {
    const auto& v = screen.view;
    const double angle = v.time*(0.13+index*0.017)+index*1.4;
    const double tilt = 0.3+std::sin(v.time*0.11+index)*0.5;
    const double cx = std::fmod(index*137.0+40-v.camera_x*(0.08+index*0.025)+v.time*(index%2 ? 0.9 : -0.5)+100000,v.width+140)-70;
    const double cy = v.top+30+(index*43)%96+std::sin(v.time*0.35+index)*5+(v.surface_y-180)*0.06;
    const double scale = 16+index*7;
    const std::array<Vertex,8> mesh{{{-1,-0.65,-0.7},{0.7,-0.9,-0.7},{0.9,0.65,-0.8},{-0.8,0.9,-0.65},
        {-0.65,-0.8,0.8},{0.9,-0.55,0.65},{0.65,0.8,0.8},{-0.95,0.5,0.6}}};
    const std::array<std::array<int,3>,12> faces{{{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
        {2,3,7},{2,7,6},{1,2,6},{1,6,5},{3,0,4},{3,4,7}}};
    std::array<Vertex,8> transformed{};
    std::array<Point,8> projected{};
    for (std::size_t i = 0; i < mesh.size(); ++i) {
        const auto p = mesh[i];
        const double x = p.x*std::cos(angle)+p.z*std::sin(angle);
        const double z = -p.x*std::sin(angle)+p.z*std::cos(angle);
        transformed[i] = {x,p.y*std::cos(tilt)-z*std::sin(tilt),p.y*std::sin(tilt)+z*std::cos(tilt)};
        const auto q = transformed[i];
        projected[i] = {cx+q.x*scale/(q.z+3.8),cy+q.y*scale/(q.z+3.8)};
    }
    std::array<int,12> order{};
    for (int i = 0; i < 12; ++i) order[i] = i;
    const auto depth = [&](int i) { const auto f = faces[i]; return transformed[f[0]].z+transformed[f[1]].z+transformed[f[2]].z; };
    std::sort(order.begin(),order.end(),[&](int a, int b) { return depth(a) > depth(b); });
    for (int i : order) {
        const auto f = faces[i];
        const auto a = transformed[f[0]], b = transformed[f[1]], c = transformed[f[2]];
        const Vertex u{b.x-a.x,b.y-a.y,b.z-a.z}, w{c.x-a.x,c.y-a.y,c.z-a.z};
        const Vertex n{u.y*w.z-u.z*w.y,u.z*w.x-u.x*w.z,u.x*w.y-u.y*w.x};
        const double length = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        const double light = 0.24+0.68*std::max(0.0,(-n.x*0.6-n.y*0.5-n.z*0.6)/length);
        screen.triangle(projected[f[0]],projected[f[1]],projected[f[2]],shade(index%2 ? 0x839295 : 0x9c8a81,light));
    }
}
}
bool SpaceBackground::draw(std::vector<std::uint32_t>& pixels, const BackgroundView& view) {
    if (view.width <= 0 || view.height <= 0 || view.width > 8192 || view.height > 8192 ||
        pixels.size() != std::size_t(view.width)*view.height || view.top < 0 || view.bottom > view.height || view.top > view.bottom ||
        !std::isfinite(view.time) || std::abs(view.time) > 1e9 || !std::isfinite(view.camera_x) || std::abs(view.camera_x) > 1e7 ||
        !std::isfinite(view.camera_y) || std::abs(view.camera_y) > 1e7 || !std::isfinite(view.surface_y) || std::abs(view.surface_y) > 1e7) return false;
    Surface screen{pixels,view};
    for (int y = view.top; y < view.bottom; ++y) {
        const unsigned shade = unsigned((y-view.top)/24);
        for (int x = 0; x < view.width; ++x) screen.pixel(x,y,0x081426+shade*0x010101);
    }
    for (int i = 0; i < 135; ++i) {
        const int x = int(std::fmod(i*139.0+17-view.camera_x/(15+i%3*12)+100000,view.width));
        const int y = view.top+(i*67+11)%std::max(1,view.bottom-view.top);
        const auto color = std::sin(view.time*0.7+i*4.7) > 0.4 ? 0x879cae : 0x3a526b;
        screen.pixel(x,y,color);
        if (i%13 == 0) screen.pixel(x+1,y,color);
    }
    planet(screen);
    for (int i = 0; i < 6; ++i) asteroid(screen,i);
    if (!view.terrain) return true;
    // Three differently spaced ridge meshes provide atmospheric perspective and
    // camera parallax. A dark faceted back wall also remains behind excavated caves.
    for (int layer = 0; layer < 3; ++layer) {
        const double base = view.surface_y+8+layer*5;
        const double span = 140-layer*22, scroll = view.camera_x*(0.07+layer*0.09);
        const int first = int(std::floor(scroll/span))-2;
        for (int segment = first; segment < first+int(view.width/span)+5; ++segment) {
            const double x = segment*span-scroll;
            const double peak = base-(30+layer*6+std::abs(std::sin(segment*1.79+layer*2.8))*45);
            const Point a{x,base}, b{x+span*0.42,peak}, c{x+span,base}, ridge{x+span*0.63,base};
            const std::uint32_t colors[] = {0x273c51,0x33434f,0x454a50};
            screen.triangle(a,b,ridge,colors[layer]); screen.triangle(b,c,ridge,shade(colors[layer],0.64));
        }
    }
    for (int y = std::max(view.top,int(view.surface_y)+12); y < view.bottom; ++y)
        for (int x = 0; x < view.width; ++x) screen.pixel(x,y,0x122331);
    for (int row = 0; row < 7; ++row) for (int col = -1; col < 12; ++col) {
        const double x = col*72-std::fmod(view.camera_x*0.25,72.0), y = view.surface_y+12+row*56;
        screen.triangle({x,y},{x+72,y},{x+46,y+56},row%2 ? 0x152a37 : 0x192d39);
    }
    return true;
}
}

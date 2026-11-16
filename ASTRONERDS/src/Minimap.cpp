#include "horizon/Minimap.hpp"
#include "horizon/BaseCamp.hpp"
#include "horizon/Game.hpp"
#include "horizon/RobotAppearance.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace horizon {
namespace {
constexpr int screen_width = 640, screen_height = 360;
constexpr int field_left = Minimap::left+8, field_top = Minimap::top+17;
constexpr int field_width = Minimap::width-16, field_height = Minimap::height-27;
struct Layout {
    double scale, x, y, w, h;
};
Layout layout(const WorldGrid& world) {
    const double scale = std::min(double(field_width)/world.width(),double(field_height)/world.height());
    const double w = world.width()*scale, h = world.height()*scale;
    return {scale,field_left+(field_width-w)*0.5,field_top+(field_height-h)*0.5,w,h};
}
std::uint32_t tint(std::uint32_t color, double amount) {
    std::uint32_t result = 0;
    for (int shift : {0,8,16}) result |= unsigned(std::clamp(((color>>shift)&255)*amount,0.0,255.0))<<shift;
    return result;
}
struct Canvas {
    std::vector<std::uint32_t>& pixels;
    void pixel(int x,int y,std::uint32_t color) {
        if (Minimap::contains(x,y)) pixels[std::size_t(y)*screen_width+x] = 0xff000000|color;
    }
    void rect(int x,int y,int w,int h,std::uint32_t color) {
        for (int yy = std::max(y,Minimap::top); yy < std::min(y+h,Minimap::top+Minimap::height); ++yy)
            for (int xx = std::max(x,Minimap::left); xx < std::min(x+w,Minimap::left+Minimap::width); ++xx) pixel(xx,yy,color);
    }
    void line(int x0,int y0,int x1,int y1,std::uint32_t color) {
        const int dx = std::abs(x1-x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1-y0), sy = y0 < y1 ? 1 : -1;
        int error = dx+dy;
        for (;;) {
            pixel(x0,y0,color);
            if (x0 == x1 && y0 == y1) break;
            const int twice = 2*error;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    }
    void outline(int x,int y,int w,int h,std::uint32_t color) {
        line(x,y,x+w-1,y,color); line(x,y+h-1,x+w-1,y+h-1,color);
        line(x,y,x,y+h-1,color); line(x+w-1,y,x+w-1,y+h-1,color);
    }
    static std::array<unsigned,5> glyph(char c) {
        switch (c) {
            case 'A': return {{2,5,7,5,5}}; case 'B': return {{6,5,6,5,6}};
            case 'C': return {{3,4,4,4,3}}; case 'K': return {{5,5,6,5,5}};
            case 'D': return {{6,5,5,5,6}}; case 'E': return {{7,4,6,4,7}};
            case 'F': return {{7,4,6,4,4}}; case 'I': return {{7,2,2,2,7}};
            case 'L': return {{4,4,4,4,7}}; case 'M': return {{5,7,7,5,5}};
            case 'N': return {{5,7,7,7,5}}; case 'P': return {{6,5,6,4,4}};
            case 'S': return {{3,4,2,1,6}}; case 'X': return {{5,5,2,5,5}};
            case 'Y': return {{5,5,2,2,2}};
            case '0': return {{7,5,5,5,7}}; case '1': return {{2,6,2,2,7}};
            case '2': return {{6,1,7,4,7}}; case '3': return {{6,1,3,1,6}};
            case '4': return {{5,5,7,1,1}}; case '5': return {{7,4,6,1,6}};
            case '6': return {{3,4,7,5,7}}; case '7': return {{7,1,2,2,2}};
            case '8': return {{7,5,7,5,7}}; case '9': return {{7,5,7,1,6}};
            default: return {{0,0,0,0,0}};
        }
    }
    void text(int x,int y,const std::string& label,std::uint32_t color) {
        for (char c : label) {
            const auto rows = glyph(c);
            for (int yy = 0; yy < 5; ++yy) for (int xx = 0; xx < 3; ++xx)
                if (rows[yy] & (1u<<(2-xx))) pixel(x+xx,y+yy,color);
            x += 4;
        }
    }
};
std::uint32_t terrain_color(const WorldGrid& world,int x,int y) {
    if (world.pit(x,y)) return 0x000000;
    const double elevation = std::clamp((world.elevation(x,y)+2)/5.5,0.0,1.0);
    const auto type = world.get_tile_type(x,y);
    std::uint32_t color = (unsigned(65+elevation*39)<<16)|(unsigned(66+elevation*31)<<8)|unsigned(58+elevation*22);
    if (std::abs(y-10) <= 1 || std::abs(x-(world.width()-16)) <= 1) color = tint(color,1.18);
    if (type == TileType::Bedrock) color = 0x343e43;
    else if (type == TileType::Scaffold) color = 0x899487;
    else if (type == TileType::WaterIce) color = 0x8ab9c4;
    else if (type == TileType::HydratedMinerals) color = 0x547e74;
    else if (type == TileType::IronNickelOre) color = 0x9d8064;
    if (BaseCamp::footprint(x,y)) color = BaseCamp::wall(x,y) ? 0x7a9ba0 : 0x445e69;
    return color;
}
Vec2 safe_icon(Vec2 point) {
    return {std::clamp(point.x,double(Minimap::left+9),double(Minimap::left+Minimap::width-10)),
            std::clamp(point.y,double(Minimap::top+24),double(Minimap::top+Minimap::height-14))};
}
void rescue_aircraft(Canvas& c,Vec2 point) {
    const int x = int(std::lround(point.x)), y = int(std::lround(point.y));
    c.rect(x-6,y-3,13,7,0x10232c);
    c.line(x-6,y,x-2,y-2,0xdedee5); c.line(x-6,y,x-2,y+2,0xa7a7b5);
    c.rect(x-2,y-2,7,5,0xdedee5);
    c.line(x,y-2,x+3,y-5,0xd7d7df); c.line(x+3,y-5,x+5,y-2,0xa1a0af);
    c.line(x,y+2,x+3,y+5,0xd7d7df); c.line(x+3,y+5,x+5,y+2,0x888797);
    c.rect(x-3,y-1,4,2,0x20214a);
    c.pixel(x+3,y-5,0xa31355); c.pixel(x+3,y+5,0xa31355);
    c.line(x-1,y+2,x+5,y+2,0xa31355);
    c.pixel(x+6,y-1,0x43ff79); c.pixel(x+6,y+1,0x43ff79);
}
void rocket(Canvas& c,Vec2 point,bool launched) {
    const int x = int(std::lround(point.x)), y = int(std::lround(point.y));
    c.rect(x-3,y-6,7,12,0x12242e);
    c.pixel(x,y-7,0xe4e9dc); c.rect(x-1,y-6,3,2,0xe4e9dc);
    c.rect(x-2,y-4,5,6,0xb6c6ca); c.rect(x-1,y-4,2,3,0xe0e7db);
    c.rect(x-2,y,5,1,0x657b86); c.line(x-3,y+1,x-4,y+4,0x91a5aa); c.line(x+3,y+1,x+4,y+4,0x91a5aa);
    c.rect(x-1,y+2,3,2,0x536b76); c.pixel(x,y+4,launched ? 0xffd394 : 0x9cbbb9);
}
void data_chip_marker(Canvas& c,Vec2 point) {
    const int x=int(std::lround(point.x)),y=int(std::lround(point.y));
    c.rect(x-2,y-2,5,5,0x10232c);
    c.rect(x-1,y-1,3,3,0x70d6f1);
    c.pixel(x,y,0xe3fbff);
}
void damaged_shuttle_marker(Canvas& c,Vec2 point) {
    const int x=int(std::lround(point.x)),y=int(std::lround(point.y));
    c.rect(x-3,y-3,7,7,0x10232c);
    c.rect(x-1,y-3,3,6,0xc3d1c9);
    c.rect(x-3,y-1,7,2,0x95acb1);
    c.pixel(x+1,y+2,0xe47e62);
}
void robot_head(Canvas& c,Vec2 point,const RobotAppearance& appearance,bool eyes_open,bool alive) {
    const int x = int(std::lround(point.x)), y = int(std::lround(point.y));
    const bool classic = appearance.model == RobotModel::Mark1;
    const auto metal = classic ? 0xc6a363u : appearance.armor;
    const auto lens = !alive ? 0x263c45u : classic ? 0x49baffu : appearance.optics;
    c.rect(x-4,y-4,9,8,0x10242a);
    c.line(x-2,y-6,x-2,y-4,0xa0b4ac); c.pixel(x-2,y-6,lens);
    c.rect(x-3,y-3,7,5,metal);
    c.rect(x-4,y-1,1,2,tint(metal,0.65)); c.rect(x+4,y-1,1,2,tint(metal,0.65));
    c.rect(x-3,y-1,7,2,0x263c45);
    if (eyes_open) { c.pixel(x-2,y-1,lens); c.pixel(x+2,y-1,lens); }
    c.rect(x-1,y+2,3,1,classic ? 0x947943 : appearance.trim);
    c.line(x-3,y-3,x+3,y-3,tint(metal,1.15));
}
}
Vec2 Minimap::location(Vec2 point,const WorldGrid& world) const {
    const auto map = layout(world);
    if (!std::isfinite(point.x)) point.x = world.width()*0.5;
    if (!std::isfinite(point.y)) point.y = world.height()*0.5;
    return {map.x+std::clamp(point.x,0.0,double(world.width()))*map.scale,
            map.y+std::clamp(point.y,0.0,double(world.height()))*map.scale};
}
void Minimap::render(std::vector<std::uint32_t>& pixels,const Game& game,const RobotAppearance& appearance) {
    if (pixels.size() != screen_width*screen_height || !game.world().planar()) return;
    Canvas c{pixels}; const auto& world = game.world(); const auto map = layout(world);
    c.rect(left,top,width,height,0x0b1622); c.outline(left,top,width,height,0x617f85);
    c.outline(left+2,top+2,width-4,height-4,0x253f4c); c.rect(left+3,top+3,width-6,11,0x172c38);
    c.text(left+8,top+6,"MAP",0xc0d6c9); c.text(left+width-20,top+5,"N",0x98bfc0);
    c.line(left+width-9,top+10,left+width-9,top+5,0x98bfc0);
    c.line(left+width-11,top+7,left+width-9,top+5,0x98bfc0); c.line(left+width-7,top+7,left+width-9,top+5,0x98bfc0);
    c.rect(field_left,field_top,field_width,field_height,0x12232d);
    for (int y = int(std::floor(map.y)); y < int(std::ceil(map.y+map.h)); ++y)
        for (int x = int(std::floor(map.x)); x < int(std::ceil(map.x+map.w)); ++x) {
            const int wx = std::clamp(int((x+0.5-map.x)/map.scale),0,world.width()-1);
            const int wy = std::clamp(int((y+0.5-map.y)/map.scale),0,world.height()-1);
            if(game.in_spaceship()) {
                const int room=ShipInterior::room_at({wx+.5,wy+.5});
                c.pixel(x,y,!ShipInterior::floor(wx,wy) ? 0x0a1420 : world.solid(wx,wy) ? 0x536470 :
                        room<0 ? 0x8c8b7a : ShipInterior::rooms[room].light);
            } else c.pixel(x,y,terrain_color(world,wx,wy));
        }
    if(game.in_spaceship()) {
        for(const auto point:{ShipInterior::charger,ShipInterior::table,ShipInterior::astronaut,ShipInterior::exit}) {
            const auto q=location(point,world);c.rect(int(q.x)-1,int(q.y)-1,3,3,0xb9f5d6);
        }
        const auto player=game.unit().center(),q=location(player,world);
        robot_head(c,safe_icon(q),appearance,game.presentation().eye_openness()>.25,game.unit().alive());
        c.rect(left+4,top+height-9,width-8,6,0x122632);
        c.text(left+8,top+height-8,"X "+std::to_string(int(player.x))+" Y "+std::to_string(int(player.y)),0x829fa8);
        c.text(left+width-25,top+height-8,"DECK",0xa1ddbc);
        return;
    }
    const auto base = safe_icon(location(BaseCamp::center,world));
    const auto ship = safe_icon(location(game.rocket().ground_socket(),world));
    const auto actual = location(game.unit().center(),world);
    Vec2 head = safe_icon(actual);
    const auto clear = [&](Vec2 candidate) {
        // The head is intentionally displaced when locations are close. A fine
        // leader and exact ground point preserve location without hiding a base.
        return !(std::abs(candidate.x-base.x) < 10 && std::abs(candidate.y-base.y) < 11) &&
               !(std::abs(candidate.x-ship.x) < 10 && std::abs(candidate.y-ship.y) < 11);
    };
    if (!clear(head)) {
        const std::array<Vec2,12> offsets{{{14,9},{-14,9},{14,-10},{-14,-10},{20,0},{-20,0},
            {0,16},{0,-17},{22,14},{-22,14},{22,-16},{-22,-16}}};
        for (auto offset : offsets) {
            const auto candidate = safe_icon({actual.x+offset.x,actual.y+offset.y});
            if (clear(candidate)) { head = candidate; break; }
        }
    }
    if (game.survival_enabled() && game.danger().active()) for (const auto& creature : game.danger().creatures()) {
        const auto threat = location(creature.position,world);
        c.rect(int(std::lround(threat.x))-1,int(std::lround(threat.y))-1,3,3,0xe16b62);
    }
    rescue_aircraft(c,base);
    if (game.data_chip_spotted() && !game.data_chip_collected()) data_chip_marker(c,location(BaseCamp::data_bank,world));
    if ((game.placeholder_mission()==4 || (game.mission_number()==3 && game.usb_obtained()) || game.rescue_mission()) && !game.rescue_launch_complete())
        damaged_shuttle_marker(c,location(BaseCamp::damaged_shuttle,world));
    if(game.carrier_mobile()) {
        const auto carrier=location(game.escort_carrier_position(),world);
        c.rect(int(carrier.x)-2,int(carrier.y)-2,5,5,0xe9b65f);
        c.pixel(int(carrier.x),int(carrier.y),0xe8f1ef);
    }
    if(!game.campaign()) rocket(c,ship,game.rocket().launched());
    if (!game.unit().fell_into_pit()) {
        const auto player_color = appearance.model == RobotModel::Mark1 ? 0x49baffu : appearance.optics;
        const int px = int(std::lround(actual.x)), py = int(std::lround(actual.y));
        if (distance(head,actual) > 2) c.line(px,py,int(std::lround(head.x)),int(std::lround(head.y)),0x92b7ac);
        const double heading = game.presentation().heading;
        c.line(px,py,px+int(std::lround(std::cos(heading)*5)),py+int(std::lround(std::sin(heading)*5)),player_color);
        robot_head(c,head,appearance,game.presentation().eye_openness() > 0.25,game.unit().alive());
        if (distance(head,actual) > 2) {
            c.rect(px-1,py-1,3,3,0x142b30); c.pixel(px,py,0xd4fff0);
        }
    }
    c.rect(left+4,top+height-9,width-8,6,0x122632);
    const auto player = game.unit().center();
    c.text(left+8,top+height-8,"X "+std::to_string(int(player.x))+" Y "+std::to_string(int(player.y)),0x829fa8);
    c.text(left+width-29,top+height-8,BaseCamp::inside(player) ? "SHIP" : "FIELD",BaseCamp::inside(player) ? 0xa1ddbc : 0x829fa8);
}
}

#include "horizon/Renderer.hpp"
#include <algorithm>

namespace horizon {
void PixelRenderer::draw_ship_interior(const Game& game) {
    // The same projected scene, robot, camera scale and HUD as the surface.
    isometric_.render(pixels_,game,headlamp_on_,robot_);
    if(!modal_open()) {
        const auto label=[&](Vec2 point,const char* title,std::uint32_t color) {
            const auto q=isometric_.project(point,game.world());
            const int w=int(std::string(title).size())*6+12,x=int(q.x)-w/2,y=int(q.y)-42;
            if(x<8 || x+w>632 || y<38 || y>278) return;
            glass_panel(x,y,w,15,5);text(x+6,y+4,title,color);
        };
        label(ShipInterior::charger,"CHARGE",0x8be5e5);
        if(!game.astronaut_aboard()) label(ShipInterior::astronaut,"ASTRONERD",0xe4cd98);
        if(!game.near_observed_table()) label(ShipInterior::table,"OBSERVED TABLE",0x9befff);
        label(ShipInterior::craft_table,"CRAFT TABLE / C",0x7aef9f);
        label(ShipInterior::exit,"AIRLOCK / E",0x8ae9b8);
        if(game.mission_number()==3 && !game.records_copied())
            label(ShipInterior::records_pc,"PC / E / COLLECT USB",0x8ae9b8);
        if(game.rescue_mission()==6) for(int i=0;i<3;++i) if(!game.rescue_material_collected(i))
            label(ShipInterior::rescue_materials[i],ShipInterior::rescue_material_names[i],0x8ae9b8);
        if(game.rescue_mission()==7 && game.rescue_stage()==Game::RescueStage::Waiting)
            label(ShipInterior::carrier_robot,"C-02 / E TO COMMUNICATE",0x8ae9b8);
        if(game.carrier_mobile()) label(game.escort_carrier_position(),game.astronaut_aboard() ? "C-02 / ASTRONERD SECURE" : "C-02 / SEAT READY",0x8ae9b8);
        if(game.mission2()) {
            if(!game.carrier_scanned()) label(ShipInterior::carrier_robot,"CARRIER / SCAN WITH E",0x8ae9b8);
            else {
                if(!game.carrier_wheel_collected()) label(ShipInterior::carrier_wheel,"WHEEL / E",0x8ae9b8);
                if(!game.carrier_arm_collected()) label(ShipInterior::carrier_arm,"ROBOT ARM / E",0x8ae9b8);
                if(!game.carrier_chip_collected()) label(ShipInterior::carrier_chip,"CONTROL CHIP / E",0x8ae9b8);
                if(game.carrier_wheel_collected() && game.carrier_arm_collected() && game.carrier_chip_collected())
                    label(ShipInterior::carrier_robot,game.carrier_repairing() ? "REPAIRING..." : "CARRIER / REPAIR E",0x8ae9b8);
            }
        }
    }
    draw_hud(game);
    if(!modal_open()) {
        const auto wreck=ShipInterior::forward_wreckage;const auto p=game.unit().center();
        if(!game.paused() && !game.finished() && p.y>=wreck.y+wreck.h && p.y<wreck.y+wreck.h+3 &&
           p.x>=wreck.x-1 && p.x<wreck.x+wreck.w+1) {
            glass_panel(205,235,230,27,6);
            text(217,245,"HULL DAMAGE / PASSAGE BLOCKED",0xf2c18b);
        }
        for(int i=0;i<int(ShipInterior::doors.size());++i) {
            const auto id=static_cast<ShipDoor>(i);
            if(!game.near_ship_door(id) || game.ship_door_open(id)) continue;
            const auto name=std::string(ShipInterior::doors[i].name);
            glass_panel(211,235,218,27,6);
            text(223,245,game.ship_door_opening(id) ? name+" / OPENING..." : name+" / AUTOMATIC DOOR",0xf2d3a0);
        }
        const int room=ShipInterior::room_at(game.unit().center());
        const std::string title=std::string("MAP2 / ")+(room<0 ? "ACCESS PASSAGE" : ShipInterior::rooms[room].name);
        glass_panel(12,292,int(title.size())*6+16,17,5);text(20,298,title,0xbad9ec);
    }
    if(game.transitioning()) for(auto& pixel:pixels_) {
        const double keep=1-game.transition_alpha();
        pixel=0xff000000|(unsigned(((pixel>>16)&255)*keep)<<16)|(unsigned(((pixel>>8)&255)*keep)<<8)|unsigned((pixel&255)*keep);
    }
}
} // namespace horizon

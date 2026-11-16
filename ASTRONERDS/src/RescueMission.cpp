#include "horizon/Game.hpp"
#include <algorithm>
#include <queue>

namespace horizon {
double Game::rescue_stage_progress() const {
    const double duration=rescue_stage_==RescueStage::Communicating ? 6.0 :
        rescue_stage_==RescueStage::InstallingSeat || rescue_stage_==RescueStage::CarrierOffline ? 3.0 :
        rescue_stage_==RescueStage::ChargingUnit ? 7.0 : rescue_stage_==RescueStage::ClosingDoor ? 2.0 :
        rescue_stage_==RescueStage::RepairingShip || rescue_stage_==RescueStage::ChargingCarrier || rescue_stage_==RescueStage::LaunchingShip ? 6.0 : 4.0;
    return std::clamp(rescue_stage_time_/duration,0.0,1.0);
}
double Game::rescue_door_phase() const {
    if(rescue_stage_==RescueStage::Docking) return std::clamp(rescue_stage_time_/.8,0.0,1.0);
    if(rescue_stage_==RescueStage::Unloading) return 1;
    if(rescue_stage_==RescueStage::ClosingDoor) return 1-rescue_stage_progress();
    return 0;
}
Vec2 Game::rescue_passenger_position() const {
    if(rescue_stage_!=RescueStage::Unloading) return RescueShip::passenger_seat;
    const double t=rescue_stage_progress();
    const auto from=t<.45 ? escort_carrier_ : RescueShip::ramp_entry;
    const auto to=t<.45 ? RescueShip::ramp_entry : RescueShip::passenger_seat;
    const double progress=t<.45 ? t/.45 : (t-.45)/.55;
    const double ease=progress*progress*(3-2*progress);
    return {from.x+(to.x-from.x)*ease,from.y+(to.y-from.y)*ease};
}
double Game::rescue_passenger_height() const {
    return RescueShip::passenger_hip_height;
}
const char* Game::rescue_dialogue() const {
    switch(rescue_stage_) {
        case RescueStage::Communicating:
            return rescue_stage_time_<3 ? "U-01: C-02, HELP ME CARRY THE ASTRONERD TO S-01." :
                "C-02: CONFIRMED. FIT A SEAT, THEN WE WILL COLLECT THEM.";
        case RescueStage::InstallingSeat: return "U-01: INSTALLING A SECURE TRANSPORT SEAT ON C-02.";
        case RescueStage::ToBed: return "C-02: SEAT READY. MOVING TO THE ASTRONERD'S BED.";
        case RescueStage::Loading: return "U-01: LIFTING ASTRONERD INTO THE SEAT. SECURING HARNESS.";
        case RescueStage::Departing: return "C-02: PASSENGER SECURE. READY TO FOLLOW YOU TO S-01.";
        case RescueStage::Escort: return "LEAD U-01 TO S-01 (119,60). C-02 WILL FOLLOW.";
        case RescueStage::Complete: return "MISSION 7 COMPLETE / ASTRONERD AND C-02 HAVE REACHED S-01.";
        case RescueStage::AwaitingRepair: return "MISSION 8 / APPROACH S-01 AND PRESS E TO REPAIR IT.";
        case RescueStage::RepairingShip: return "U-01: REPAIRING S-01. STAY CLOSE TO THE SPACECRAFT.";
        case RescueStage::Docking: return "C-02: S-01 IS REPAIRED. MOVING TO THE BOARDING RAMP.";
        case RescueStage::Unloading: return "C-02: TRANSFERRING ASTRONERD INTO S-01. U-01, WE STAY HERE.";
        case RescueStage::ClosingDoor: return "ASTRONERD INSIDE THE CABIN / CLOSING S-01 BOARDING DOOR.";
        case RescueStage::Rescued: return "ASTRONERD SECURE. S-01 NEEDS POWER TO LAUNCH.";
        case RescueStage::ChargingCarrier: return "C-02: TRANSFERRING ALL BATTERY POWER TO S-01.";
        case RescueStage::CarrierOffline: return "C-02 OFFLINE. S-01 AT 45%. U-01: I WILL SUPPLY THE REST.";
        case RescueStage::ChargingUnit: return "U-01: TRANSFERRING ALL REMAINING POWER. GET ASTRONERD HOME.";
        case RescueStage::LaunchingShip: return "S-01 FULLY CHARGED / LAUNCHING. U-01 AND C-02: POWER OFF.";
        case RescueStage::ShipDeparted: return "ASTRONERD ESCAPED. U-01 AND C-02 REMAIN ON THE PLANET, POWERED OFF.";
        default: return "CARGO HANGAR / APPROACH C-02 AND PRESS E TO COMMUNICATE.";
    }
}
void Game::begin_rescue_material_mission() {
    if(rescue_mission_) return;
    placeholder_mission_=4;
    rescue_mission_=6;
    carrier_repaired_=true;
    last_message_="MISSION 4 COMPLETE / MISSION 6: GATHER S-01 REPAIR MATERIALS IN STORAGE";
    presentation_.notify(last_message_);
    if(!in_spaceship_) begin_transfer();
}
// Both the cinematic and escort use the collision map, including the cliff gap.
// Four-way waypoints keep the actors from cutting through corners or bulkheads.
std::vector<Vec2> Game::rescue_path(Vec2 from,Vec2 to) const {
    const int width=world_->width(),height=world_->height();
    const auto clear=[&](int x,int y) {
        return world_->in_bounds(x,y) && !world_->solid(x,y) && !world_->pit(x,y) &&
            (in_spaceship_ || rescue_mission_<7 || !RescueShip::blocks_carrier({x+.5,y+.5}));
    };
    const int sx=int(from.x),sy=int(from.y),tx=int(to.x),ty=int(to.y);
    if(!world_->in_bounds(sx,sy) || !world_->in_bounds(tx,ty)) return {};
    const int start=sy*width+sx,end=ty*width+tx;
    std::vector<int> previous(width*height,-1);std::queue<int> queue;
    previous[start]=start;queue.push(start);
    int closest=start;double best=distance(from,to);
    while(!queue.empty() && previous[end]<0) {
        const int at=queue.front();queue.pop();
        const double remaining=distance({at%width+.5,at/width+.5},to);
        if(clear(at%width,at/width) && remaining<best) {best=remaining;closest=at;}
        for(GridPos d:{GridPos{1,0},GridPos{0,1},GridPos{-1,0},GridPos{0,-1}}) {
            const int x=at%width+d.x,y=at/width+d.y;
            if(!clear(x,y)) continue;
            if(!in_spaceship_ && rescue_mission_>=7 && !escort_clear_route(
                at==start ? from : Vec2{at%width+.5,at/width+.5},{x+.5,y+.5})) continue;
            const int next=y*width+x;
            if(previous[next]>=0) continue;
            previous[next]=at;queue.push(next);
        }
    }
    const bool exact=previous[end]>=0;
    const int goal=exact ? end : closest;
    if(goal==start && !exact) return {};
    std::vector<Vec2> path;
    if(exact) path.push_back(to);
    for(int at=goal;at!=start;at=previous[at]) path.push_back({at%width+.5,at/width+.5});
    // Stored backwards so the next waypoint is available in constant time.
    if(in_spaceship_ && start!=goal) path.push_back({sx+.5,sy+.5});
    return path;
}
bool Game::move_rescue_actor(Vec2& position,std::vector<Vec2>& path,double amount) {
    while(!path.empty() && amount>0) {
        const auto target=path.back();const double length=distance(position,target);
        if(length<=amount+1e-8) {position=target;path.pop_back();amount-=length;}
        else {
            position.x+=(target.x-position.x)*amount/length;
            position.y+=(target.y-position.y)*amount/length;
            amount=0;
        }
    }
    return path.empty();
}
bool Game::escort_clear_route(Vec2 from,Vec2 to) const {
    const int steps=std::max(1,int(std::ceil(distance(from,to)/.12)));
    for(int i=0;i<=steps;++i) {
        const double t=double(i)/steps;
        const Vec2 p{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t};
        if(!in_spaceship_ && rescue_mission_>=7 && RescueShip::blocks_carrier(p)) return false;
        for(double x:{-.32,.32}) for(double y:{-.32,.32}) {
            const int cx=int(std::floor(p.x+x)),cy=int(std::floor(p.y+y));
            if(!world_->in_bounds(cx,cy) || world_->solid(cx,cy) || world_->pit(cx,cy)) return false;
        }
    }
    return true;
}
void Game::update_escort_follow(double dt) {
    const auto player=unit_->center();
    const double separation=distance(escort_carrier_,player);
    escort_repath_time_-=dt;
    Vec2 target=player;
    bool route=escort_clear_route(escort_carrier_,player);
    if(!route && separation>1.8) {
        if(escort_repath_time_<=0 || escort_path_.empty()) {
            escort_path_=rescue_path(escort_carrier_,player);escort_repath_time_=.4;
        }
        // Look ahead along the route: pursue the furthest visible point instead
        // of snapping to each cell centre or returning to it on every replan.
        for(std::size_t i=0;i<escort_path_.size();++i) {
            if(distance(escort_carrier_,escort_path_[i])>.12 && escort_clear_route(escort_carrier_,escort_path_[i])) {
                target=escort_path_[i];route=true;break;
            }
        }
    }
    Vec2 desired{};
    const double length=distance(escort_carrier_,target);
    if(route && length>1e-6 && separation>1.9) {
        const double speed=std::min(4.8,(separation-1.8)*2.6);
        desired={(target.x-escort_carrier_.x)*speed/length,(target.y-escort_carrier_.y)*speed/length};
    }
    const Vec2 change{desired.x-escort_velocity_.x,desired.y-escort_velocity_.y};
    const double magnitude=std::hypot(change.x,change.y);
    const double acceleration=std::hypot(desired.x,desired.y)<.1 ? 10.0 : 7.0;
    const double factor=magnitude>1e-9 ? std::min(1.0,acceleration*dt/magnitude) : 0;
    escort_velocity_.x+=change.x*factor;escort_velocity_.y+=change.y*factor;
    const Vec2 next{escort_carrier_.x+escort_velocity_.x*dt,escort_carrier_.y+escort_velocity_.y*dt};
    if(escort_clear_route(escort_carrier_,next)) {
        escort_wheel_phase_+=distance(escort_carrier_,next)/.25;
        escort_carrier_=next;
    } else escort_velocity_={};
    if(std::hypot(escort_velocity_.x,escort_velocity_.y)>.05) {
        const double heading=std::atan2(escort_velocity_.y,escort_velocity_.x);
        const double turn=std::remainder(heading-escort_heading_,6.28318530718);
        escort_heading_+=std::clamp(turn,-3.5*dt,3.5*dt);
        escort_heading_=std::remainder(escort_heading_,6.28318530718);
    }
}
void Game::update_rescue(double dt) {
    if(!unit_->alive() && !rescue_power_sequence()) return;
    if(rescue_mission_!=7 && rescue_mission_!=8) return;
    rescue_stage_time_+=dt;
    switch(rescue_stage_) {
        case RescueStage::Communicating:
            if(rescue_stage_time_>=6) {rescue_stage_=RescueStage::InstallingSeat;rescue_stage_time_=0;}
            break;
        case RescueStage::InstallingSeat:
            if(rescue_stage_time_>=3) {
                world_->release_ship_carrier();
                // Keep the hangar door open throughout the assisted transfer.
                auto& door=room_doors_[static_cast<int>(ShipDoor::Cargo)];
                door.open=door.requested=true;door.phase=1;
                world_->set_ship_room_door(ShipDoor::Cargo,true);
                escort_path_=rescue_path(escort_carrier_,ShipInterior::rescue_carrier_bedside);
                cinematic_unit_path_=rescue_path(unit_->center(),ShipInterior::rescue_unit_bedside);
                rescue_stage_=RescueStage::ToBed;rescue_stage_time_=0;
            }
            break;
        case RescueStage::ToBed: {
            auto position=unit_->center();const auto previous=position;
            move_rescue_actor(position,cinematic_unit_path_,2.2*dt);
            unit_->transfer_to(position);
            if(distance(previous,position)>1e-8) presentation_.face_target(previous,position);
            const auto previous_carrier=escort_carrier_;
            move_rescue_actor(escort_carrier_,escort_path_,2.0*dt);
            if(distance(previous_carrier,escort_carrier_)>1e-8) {
                const double heading=std::atan2(escort_carrier_.y-previous_carrier.y,escort_carrier_.x-previous_carrier.x);
                escort_heading_+=std::clamp(std::remainder(heading-escort_heading_,6.28318530718),-3.5*dt,3.5*dt);
                escort_wheel_phase_+=distance(previous_carrier,escort_carrier_)/.25;
            }
            if(distance(position,ShipInterior::rescue_unit_bedside)<.1 &&
               distance(escort_carrier_,ShipInterior::rescue_carrier_bedside)<.1) {
                rescue_stage_=RescueStage::Loading;rescue_stage_time_=0;
            }
            break;
        }
        case RescueStage::Loading:
            if(rescue_stage_time_>=4) {rescue_stage_=RescueStage::Departing;rescue_stage_time_=0;}
            break;
        case RescueStage::Departing:
            if(in_spaceship_ && rescue_stage_time_>=2) begin_transfer();
            else if(!in_spaceship_) {
                rescue_stage_=RescueStage::Escort;rescue_stage_time_=0;escort_repath_time_=0;
                last_message_="MISSION 7 / LEAD C-02 AND ASTRONERD TO S-01 (119,60)";
                presentation_.notify(last_message_);
            }
            break;
        case RescueStage::Escort: {
            update_escort_follow(dt);
            if(unit_->near(BaseCamp::damaged_shuttle,2.5) && distance(escort_carrier_,BaseCamp::damaged_shuttle)<=4.5) {
                rescue_mission_=8;rescue_stage_=RescueStage::AwaitingRepair;rescue_stage_time_=0;
                escort_velocity_={};escort_path_.clear();
                last_message_="MISSION 7 COMPLETE / MISSION 8: REPAIR S-01 / E";presentation_.notify(last_message_);
                presentation_.sound(SoundCue::Pickup);
            }
            break;
        }
        case RescueStage::AwaitingRepair:
            break;
        case RescueStage::RepairingShip:
            if(!unit_->near(BaseCamp::damaged_shuttle,2.5) || !unit_->grounded()) {
                rescue_stage_=RescueStage::AwaitingRepair;rescue_stage_time_=0;
                last_message_="REPAIR CANCELLED / RETURN TO S-01 AND PRESS E";presentation_.notify(last_message_);
            } else if(rescue_stage_time_>=rescue_ship_repair_seconds-1e-9) {
                if(!unit_->inventory().exchange({{Item::StructuralPlates,4},{Item::ScrapAlloys,1000},{Item::Polyethylene,500}},{})) {
                    rescue_stage_=RescueStage::AwaitingRepair;rescue_stage_time_=0;
                    last_message_="REPAIR NEEDS 4 PLATES, 1 KG WIRING AND 500 G SEALANT";presentation_.notify(last_message_);
                    break;
                }
                rescue_stage_=RescueStage::Docking;rescue_stage_time_=0;
                escort_path_=rescue_path(escort_carrier_,rescue_boarding_point);
                last_message_=rescue_dialogue();presentation_.notify(last_message_);presentation_.sound(SoundCue::Interface);
            }
            break;
        case RescueStage::Docking: {
            const auto previous=escort_carrier_;
            // Reject unsafe segments rather than moving the rover through a
            // decorative hull. All route nodes include its full body clearance.
            if(!escort_path_.empty() && !escort_clear_route(previous,escort_path_.back())) {
                escort_path_=rescue_path(previous,rescue_boarding_point);
            }
            move_rescue_actor(escort_carrier_,escort_path_,1.5*dt);
            if(distance(previous,escort_carrier_)>1e-8) {
                escort_heading_+=std::clamp(std::remainder(std::atan2(escort_carrier_.y-previous.y,escort_carrier_.x-previous.x)-escort_heading_,6.28318530718),-3.5*dt,3.5*dt);
                escort_wheel_phase_+=distance(previous,escort_carrier_)/.25;
            }
            const bool parked=escort_path_.empty() && distance(escort_carrier_,rescue_boarding_point)<1e-6;
            if(parked) escort_heading_+=std::clamp(std::remainder(-escort_heading_,6.28318530718),-3.5*dt,3.5*dt);
            if(parked && std::abs(std::remainder(escort_heading_,6.28318530718))<1e-6 && rescue_door_phase()>=1) {
                rescue_stage_=RescueStage::Unloading;rescue_stage_time_=0;
            }
            break;
        }
        case RescueStage::Unloading:
            if(rescue_stage_time_>=4) {
                rescue_stage_=RescueStage::ClosingDoor;rescue_stage_time_=0;
                last_message_=rescue_dialogue();presentation_.notify(last_message_);presentation_.sound(SoundCue::Pickup);
            }
            break;
        case RescueStage::ClosingDoor:
            if(rescue_stage_time_>=2) {rescue_stage_=RescueStage::Rescued;rescue_stage_time_=0;}
            break;
        case RescueStage::Rescued:
            if(rescue_stage_time_>=2) {rescue_stage_=RescueStage::ChargingCarrier;rescue_stage_time_=0;}
            break;
        case RescueStage::ChargingCarrier: {
            const double fraction=rescue_stage_progress();
            carrier_battery_=100*(1-fraction);rescue_ship_battery_=45*fraction;
            if(fraction>=1) {
                carrier_battery_=0;rescue_ship_battery_=45;
                rescue_stage_=RescueStage::CarrierOffline;rescue_stage_time_=0;
                last_message_=rescue_dialogue();presentation_.notify(last_message_);presentation_.sound(SoundCue::LowBattery);
            }
            break;
        }
        case RescueStage::CarrierOffline:
            if(rescue_stage_time_>=3) {
                unit_transfer_battery_=unit_->battery_percent();
                rescue_stage_=RescueStage::ChargingUnit;rescue_stage_time_=0;
            }
            break;
        case RescueStage::ChargingUnit: {
            const double fraction=rescue_stage_progress();
            const double remaining=unit_transfer_battery_*(1-fraction);
            unit_->drain_battery(std::max(0.0,unit_->battery_percent()-remaining));
            rescue_ship_battery_=45+55*fraction;
            if(fraction>=1) {
                unit_->drain_battery(unit_->battery_percent());
                rescue_ship_battery_=100;rescue_stage_=RescueStage::LaunchingShip;rescue_stage_time_=0;
                last_message_=rescue_dialogue();presentation_.sound(SoundCue::Launch);
            }
            break;
        }
        case RescueStage::LaunchingShip:
            if(rescue_stage_time_>=6) {
                rescue_stage_=RescueStage::ShipDeparted;rescue_stage_time_=0;
                last_message_=rescue_dialogue();presentation_.notify(last_message_);
            }
            break;
        default: break;
    }
}
}

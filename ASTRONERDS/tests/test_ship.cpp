#include "horizon/Renderer.hpp"
#include <algorithm>
#include <iostream>
#include <queue>
#include <stdexcept>

using namespace horizon;
namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void wait(Game& game,int ticks) {for(int i=0;i<ticks;++i) game.advance({},1.0/60);}
void drive(Game& game,Vec2 target) {
    int tick=0;
    while(distance(game.unit().center(),target)>.12 && ++tick<600) {
        const auto p=game.unit().center();
        const int dx=target.x-p.x>.05 ? 1 : target.x-p.x<-.05 ? -1 : 0;
        const int dy=target.y-p.y>.05 ? 1 : target.y-p.y<-.05 ? -1 : 0;
        InputState input;
        input.right=dx-dy>0;input.left=dx-dy<0;input.down=dx+dy>0;input.up=dx+dy<0;
        game.advance(input,1.0/60);
    }
    check(tick<600 && game.unit().alive(),"robot cannot follow the deck route");
}
void walk(Game& game,GridPos target) {
    const auto& world=game.world();const int width=world.width();
    const auto p=game.unit().center();const int start=int(p.y)*width+int(p.x),end=target.y*width+target.x;
    std::vector<int> previous(width*world.height(),-1);std::queue<int> queue;
    queue.push(start);previous[start]=start;
    while(!queue.empty() && previous[end]<0) {
        const int at=queue.front();queue.pop();
        for(GridPos d:{GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}) {
            const int x=at%width+d.x,y=at/width+d.y,next=y*width+x;
            if(!world.in_bounds(x,y) || (world.solid(x,y) && !ShipInterior::monitor_door.contains(x,y) && !ShipInterior::cargo_door.contains(x,y) && !ShipInterior::storage_door.contains(x,y)) || previous[next]>=0) continue;
            previous[next]=at;queue.push(next);
        }
    }
    if(previous[end]<0) throw std::runtime_error("ship station disconnected at "+std::to_string(target.x)+","+std::to_string(target.y));
    std::vector<int> path;for(int at=end;at!=start;at=previous[at]) path.push_back(at);
    std::reverse(path.begin(),path.end());
    for(int at:path) {
        drive(game,{at%width+.5,at/width+.5});
    }
    wait(game,12);
}
}
int main(int argc,char** argv) {try {
    GameOptions mission4_options;mission4_options.placeholder_mission=4;mission4_options.survival_enabled=false;
    Game mission4(WorldGrid::generate_isometric(),mission4_options);
    check(mission4.mission_number()==4 && !mission4.mission1() && !mission4.data_chip_spotted() && !mission4.data_chip_collected(),
        "Mission 4 starts with the chip already discovered or inherits Mission 1 state");
    check(mission4.tasks()[0].label=="RECOVER MARS DATA CHIP" && mission4.tasks()[0].active &&
        distance(mission4.tasks()[0].target,BaseCamp::data_bank)<.01,
        "Mission 4 does not start with the data-chip objective");
    check(mission4.world().mission4_cliff_cell(109,BaseCamp::mission4_cliff_north) &&
        !mission4.world().mission4_cliff_cell(BaseCamp::mission4_cliff_gap_left,BaseCamp::mission4_cliff_north) &&
        !mission4.world().mission4_cliff_cell(116,BaseCamp::mission4_cliff_north) &&
        mission4.world().get_tile_type(109,BaseCamp::mission4_cliff_north)==TileType::Bedrock,
        "Mission 4 cliff does not block the route or preserve the opening beside the chip");
    GameOptions options;options.mission1=true;options.initial_battery=65;options.danger_seed=47;
    Game game(WorldGrid::generate_isometric(),options);
    drive(game,{7,10.5});drive(game,{7,9.4});wait(game,12);
    check(game.interaction_label()=="ENTER SHIP","surface entrance lost its interaction");
    check(game.interact().ok,"cannot enter the replacement ship");wait(game,90);
    check(game.in_spaceship() && !game.transitioning(),"airlock did not transfer into Map2");
    check(distance(game.unit().center(),ShipInterior::entry)<.1,"spawn missed the boarding ramp");
    check(distance(game.tasks()[1].target,ShipInterior::astronaut)<.01 && distance(game.tasks()[2].target,ShipInterior::table)<.01,"mission targets still point to the old ship layout");
    check(game.world().width()==32 && game.world().height()==57,"ship is not two times narrower");
    check(!game.place_tile({16,53}).ok,"interior permits terrain placement");
    walk(game,{16,10});
    const auto wreckage=ShipInterior::forward_wreckage;
    for(int x=wreckage.x;x<wreckage.x+wreckage.w;++x) {
        Unit7Entity jumper({x+.15,10.075});jumper.move_on_plane({0,-1},true);
        for(int i=0;i<240;++i) jumper.update(1.0/120,game.world());
        check(jumper.center().y>=10.4 && jumper.elevation()<ShipInterior::wall_height,
              "robot can jump through or land on the collapsed forward bulkhead");
    }
    const auto bridge_reachable=[&]() {
        const auto& w=game.world();const int width=w.width();std::vector<bool> seen(width*w.height());
        std::queue<GridPos> queue;queue.push({16,10});seen[10*width+16]=true;
        while(!queue.empty()) {
            const auto p=queue.front();queue.pop();if(p.y<wreckage.y) return true;
            for(GridPos d:{GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}) {
                const int x=p.x+d.x,y=p.y+d.y;
                if(!w.in_bounds(x,y)||w.solid(x,y)||seen[y*width+x]) continue;
                seen[y*width+x]=true;queue.push({x,y});
            }
        }
        return false;
    };
    check(!bridge_reachable(),"forward room has a route around the wreckage");
    check(!game.interact().ok,"damaged forward passage can be opened with E");
    const auto before_mining=game.unit().battery_percent();
    InputState wreckage_laser;wreckage_laser.mining_target=GridPos{16,9};game.advance(wreckage_laser,.25);
    check(game.world().solid(16,9)&&game.unit().battery_percent()>=before_mining-.01,"forward wreckage can be mined or consumes mining power");
    for(const auto id:{ShipDoor::Monitor,ShipDoor::Cargo}) {
        const auto b=ShipInterior::doors[static_cast<int>(id)].bounds;
        const double y=b.y+1.5;
        walk(game,{b.x-4,b.y+1});wait(game,30);
        check(!game.ship_door_open(id) && game.world().solid(b.x,b.y+1),"room door opens while robot is far away");
        check(!game.open_ship_door(id).ok,"room door opens remotely");
        const auto reachable=[&]() {
            const auto& w=game.world();const int width=w.width();
            std::vector<bool> seen(width*w.height());std::queue<GridPos> q;
            q.push({b.x-1,b.y+1});seen[(b.y+1)*width+b.x-1]=true;
            while(!q.empty()) {
                const auto p=q.front();q.pop();if(p.x==b.x+1 && p.y==b.y+1) return true;
                for(GridPos d:{GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}) {
                    const int x=p.x+d.x,y=p.y+d.y;
                    if(!w.in_bounds(x,y) || w.solid(x,y) || seen[y*width+x]) continue;
                    seen[y*width+x]=true;q.push({x,y});
                }
            }
            return false;
        };
        check(!reachable(),"closed room door can be bypassed");
        Unit7Entity door_jump({b.x-.85,y-.425});door_jump.move_on_plane({1,0},true);
        for(int i=0;i<240;++i) door_jump.update(1.0/120,game.world());
        check(door_jump.center().x<b.x,"robot jumps through closed room door");
        drive(game,{b.x-1.85,y});wait(game,1);
        check(game.ship_door_opening(id) && game.ship_door_phase(id)>0 && game.world().solid(b.x,b.y+1),"door does not automatically start its opening animation");
        const double phase=game.ship_door_phase(id);game.set_paused(true);wait(game,30);
        check(game.ship_door_phase(id)==phase && game.world().solid(b.x,b.y+1),"paused door moves or admits entry");
        game.set_paused(false);wait(game,30);
        check(game.ship_door_open(id) && reachable(),"room door does not automatically open from outside");
        drive(game,{b.x-2.3,y});wait(game,30);
        check(game.ship_door_open(id),"door chatters inside its release buffer");
        walk(game,{b.x-4,b.y+1});wait(game,30);
        check(!game.ship_door_open(id) && game.ship_door_phase(id)==0,"door stays open after approach is abandoned");
        // Approach the end of the opening, not just its center.
        walk(game,{b.x-1,b.y});wait(game,30);
        check(game.ship_door_open(id),"door sensor misses the end panel");
        walk(game,{b.x,b.y+1});wait(game,45);
        check(game.ship_door_open(id),"door closes on robot in threshold");
        walk(game,{b.x+2,b.y+1});wait(game,30);
        check(game.ship_door_open(id),"door closes while robot remains near inside sensor");
        // Leave room for the wheel table at (25..26,17) in the Cargo Hangar.
        walk(game,{b.x+(id==ShipDoor::Cargo ? 3 : 2),b.y-3});wait(game,30);
        check(!game.ship_door_open(id) && game.world().solid(b.x,b.y+1),"door does not close after entering room");
        walk(game,{b.x+1,b.y+1});wait(game,30);
        check(game.ship_door_open(id),"room door does not reopen automatically from inside");
        walk(game,{b.x-4,b.y+1});wait(game,30);
        check(!game.ship_door_open(id),"door does not close after leaving room");
        check(!game.ship_door_open(id==ShipDoor::Cargo ? ShipDoor::Monitor : ShipDoor::Cargo),"room doors do not operate independently");
    }
    check(game.world().solid(26,32) && game.world().solid(26,34),"wall PC desk or seat has no collision");
    walk(game,{24,36});walk(game,{26,38});walk(game,{22,35});
    check(!game.world().solid(22,35),"removed Monitor Room desk still blocks the floor");
    walk(game,{22,37});check(game.charging(),"PC setup blocks the Monitor Room charger");
    PixelRenderer tv_view;tv_view.render(game);const auto tv_frame=tv_view.pixels();wait(game,30);tv_view.render(game);
    check(tv_frame!=tv_view.pixels(),"Monitor Room TVs do not animate");
    game.set_paused(true);tv_view.render(game);const auto tv_paused=tv_view.pixels();wait(game,30);tv_view.render(game);
    check(tv_paused==tv_view.pixels(),"wall TVs advance while paused");game.set_paused(false);
    check(!game.storage_door_open() && game.world().solid(7,15),"storage door opened while robot was far away");
    walk(game,{9,15});wait(game,30);
    check(game.storage_door_open() && !game.world().solid(7,15) && game.storage_door_phase()>.9,"storage door did not automatically slide clear");
    check(game.interaction_label().empty(),"automatic door still requests manual interaction");
    drive(game,{10.3,15.5});wait(game,30);
    check(game.storage_door_open(),"door chatters inside its release buffer");
    walk(game,{11,15});wait(game,30);
    check(!game.storage_door_open() && game.world().solid(7,15) && game.storage_door_phase()<.01,"door did not close after leaving crew-side sensor");
    walk(game,{7,15});wait(game,30);
    check(game.storage_door_open() && !game.world().solid(7,14) && !game.world().solid(7,16),"door closed on robot in threshold");
    walk(game,{6,21});wait(game,30);
    check(!game.storage_door_open() && game.world().solid(7,15),"door stayed open deep inside storage");
    walk(game,{5,15});wait(game,30);
    check(game.storage_door_open() && game.storage_door_phase()>.9,"door did not reopen from storage side");
    game.set_paused(true);const double door_phase=game.storage_door_phase();wait(game,30);
    check(game.storage_door_phase()==door_phase,"paused door continued animating");game.set_paused(false);
    walk(game,{19,17});wait(game,30);
    check(!game.storage_door_open() && game.world().solid(7,15),"door did not close after leaving storage");
    walk(game,{11,19});
    check(game.unit().near(game.survivor_position(),2.4),"astronaut bunk cannot be approached for delivery");
    walk(game,{5,17});check(game.near_observed_table(),"storage-room observation desk is out of reach");
    walk(game,{5,23});check(game.refinery().can_operate(game.unit()),"storage-room craft table is not usable");
    PixelRenderer craft_ui;craft_ui.toggle_crafting(game);check(craft_ui.modal_open(),"craft table did not open the crafting menu");
    walk(game,{19,24});const double old_charge=game.unit().battery_percent();wait(game,120);
    check(!game.charging() && game.unit().battery_percent()<=old_charge,"former Crew Room dock still charges");
    walk(game,{22,37});const double charge=game.unit().battery_percent();wait(game,120);
    check(game.charging() && game.unit().battery_percent()>charge,"Monitor Room charger does not charge");
    walk(game,{19,17});
    PixelRenderer tracking;tracking.render(game);
    const auto ground=tracking.tile_to_screen(game.unit().center(),game.world());
    const auto east=tracking.tile_to_screen({game.unit().center().x+1,game.unit().center().y},game.world());
    check(std::abs(east.x-ground.x-22)<.01 && std::abs(east.y-ground.y-11)<.01,"interior does not share Map 1 projection scale");
    const auto start=game.unit().center();InputState right;right.right=true;
    for(int i=0;i<36;++i) game.advance(right,1.0/60);
    tracking.render(game);const auto moved=game.unit().center();
    check(moved.x>start.x+.5 && moved.y<start.y-.5,"indoor WASD is not screen-relative like Map 1");
    check(distance(tracking.tile_to_screen(moved,game.world()),{320,200})<1,"interior camera does not follow the player");
    InputState jump;jump.jump=true;game.advance(jump,.15);
    check(game.unit().jump_height()>.2,"Map 2 disables the Map 1 jump");wait(game,90);
    Unit7Entity bunk_jump({11.15,17.075});bunk_jump.move_on_plane({-1,0},true);
    for(int i=0;i<120;++i) bunk_jump.update(1.0/120,game.world());
    check(bunk_jump.center().x<10 && std::abs(bunk_jump.elevation()-.45)<.01,"furniture collision does not match its visible height");
    check(game.world().obstacle_height(19,9)==ShipInterior::wall_collision_height,
          "ship wall is still a jumpable platform");
    Unit7Entity wall_jump({19.15,10.075});wall_jump.move_on_plane({0,-1},true);
    for(int i=0;i<240;++i) wall_jump.update(1.0/120,game.world());
    check(wall_jump.center().y>=10.4 && ShipInterior::floor(int(wall_jump.center().x),int(wall_jump.center().y)),
          "robot jumped through the spaceship wall");
    check(wall_jump.elevation()<ShipInterior::wall_height,
          "robot landed on top of the spaceship wall");
    PixelRenderer renderer;renderer.render(game);const auto still=renderer.pixels();
    const auto clock=game.simulated_seconds();renderer.render(game);
    check(renderer.pixels()==still && game.simulated_seconds()==clock,"ship rendering changes simulation state");
    wait(game,20);renderer.render(game);check(renderer.pixels()!=still,"ship machinery does not animate");
    game.set_paused(true);renderer.render(game);const auto paused=renderer.pixels();wait(game,30);renderer.render(game);
    check(renderer.pixels()==paused,"ship animation advances while paused");game.set_paused(false);
    walk(game,{16,55});check(game.interaction_label()=="EXIT SHIP","boarding ramp cannot be used to exit");
    const auto count=game.supply_count();check(renderer.click(300,250,game),"relocated airlock button did not consume its click");
    wait(game,90);check(!game.in_spaceship() && game.supply_count()==count,"exit lost the surface or mission state");
    check(distance(game.tasks()[1].target,BaseCamp::entrance)<.01,"surface mission marker does not lead to the airlock");
    check(distance(game.unit().center(),BaseCamp::approach)<.2,"exit did not return to the exterior approach");
    std::cout<<"PASS ship: automatic doors, PC setup, stations, camera and airlock round trip\n";
} catch(const std::exception& e) {std::cerr<<"FAIL ship: "<<e.what()<<'\n';return 1;}}

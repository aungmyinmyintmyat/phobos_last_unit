#include "horizon/Renderer.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <queue>
#include <stdexcept>
using namespace horizon;
namespace {
int diagonal_follow_steps=0;
double largest_follow_turn=0;
bool routed_around_hull=false;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void tick(Game& game,const InputState& input={}) {
    const auto before=game.escort_carrier_position();const double heading=game.escort_carrier_heading();
    const bool following=game.rescue_stage()==Game::RescueStage::Escort;
    game.advance(input,1.0/60);
    if(following && game.rescue_stage()==Game::RescueStage::Escort) {
        const auto after=game.escort_carrier_position();
        check(distance(before,after)<=4.8/60+1e-6,"C-02 snapped to a route waypoint");
        const double turn=std::abs(std::remainder(game.escort_carrier_heading()-heading,6.28318530718));
        check(turn<=3.5/60+1e-6,"C-02 heading snapped instead of turning smoothly");
        largest_follow_turn=std::max(largest_follow_turn,turn);
        if(std::abs(after.x-before.x)>.005 && std::abs(after.y-before.y)>.005) ++diagonal_follow_steps;
    }
    if(game.carrier_mobile() && !game.transitioning()) {
        const auto p=game.escort_carrier_position();
        check(!game.world().solid(int(p.x),int(p.y)) && !game.world().pit(int(p.x),int(p.y)),"C-02 crossed a wall or pit");
        if(!game.in_spaceship()) check(!RescueShip::blocks_carrier(p),"C-02's body crossed S-01's hull or wings");
        if(game.rescue_stage()==Game::RescueStage::Docking && p.y<57.2) routed_around_hull=true;
    }
    if(game.rescue_stage()==Game::RescueStage::Unloading) {
        const auto passenger=game.rescue_passenger_position();
        check(game.rescue_door_phase()==1 && std::abs(passenger.y-RescueShip::door.y)<1e-6,"passenger does not travel through the open door");
        check(game.rescue_passenger_height()<=.76,"passenger lifted onto the roof");
        if(passenger.x>=RescueShip::door.x) check(game.rescue_passenger_height()+.89<RescueShip::roof_height,"passenger is above the cabin roof at the door");
    }
}
void wait(Game& game,int ticks) {for(int i=0;i<ticks;++i) tick(game);}
void walk(Game& game,GridPos target) {
    const auto& w=game.world();const int width=w.width();
    const auto p=game.unit().center();const int start=int(p.y)*width+int(p.x),end=target.y*width+target.x;
    std::vector<int> previous(width*w.height(),-1);std::queue<int> q;previous[start]=start;q.push(start);
    while(!q.empty() && previous[end]<0) {
        const int at=q.front();q.pop();
        for(GridPos d:{GridPos{1,0},GridPos{0,1},GridPos{-1,0},GridPos{0,-1}}) {
            const int x=at%width+d.x,y=at/width+d.y;
            if(!w.in_bounds(x,y) || w.pit(x,y)) continue;
            const bool door=game.in_spaceship() && (ShipInterior::storage_door.contains(x,y) || ShipInterior::cargo_door.contains(x,y) || ShipInterior::monitor_door.contains(x,y));
            const int next=y*width+x;
            if(previous[next]>=0 || (w.solid(x,y) && !door)) continue;
            previous[next]=at;q.push(next);
        }
    }
    check(previous[end]>=0,"rescue objective has no walking route");
    std::vector<int> path;for(int at=end;at!=start;at=previous[at]) path.push_back(at);
    std::reverse(path.begin(),path.end());
    for(int cell:path) {
        const Vec2 target_point{cell%width+.5,cell/width+.5};int count=0;
        while(distance(game.unit().center(),target_point)>.13 && !game.finished() && ++count<600) {
            const auto at=game.unit().center();
            const int dx=target_point.x-at.x>.05 ? 1 : target_point.x-at.x<-.05 ? -1 : 0;
            const int dy=target_point.y-at.y>.05 ? 1 : target_point.y-at.y<-.05 ? -1 : 0;
            InputState input;input.right=dx-dy>0;input.left=dx-dy<0;input.down=dx+dy>0;input.up=dx+dy<0;
            tick(game,input);
        }
        check(count<600 && game.unit().alive(),"rescue walking route blocked");
    }
    wait(game,12);
}
void snapshot(Game& game,const char* directory,const char* name) {
    if(!directory) return;
    std::filesystem::create_directories(directory);
    PixelRenderer renderer;renderer.render(game);
    check(renderer.write_ppm((std::filesystem::path(directory)/name).string()),"rescue preview failed");
}
void verify_mission4_quiz_and_handoff() {
    GameOptions options;options.placeholder_mission=4;options.survival_enabled=false;
    Game game(WorldGrid::generate_isometric(),options);
    check(!game.data_chip_spotted() && !game.data_chip_collected() && !game.mission4_quiz_started() &&
        !game.mission4_quiz_complete() && !game.mission5_transition_requested(),
        "fresh Mission 4 retained chip, quiz or handoff state");
    check(!game.start_mission4_quiz().ok,"quiz starts without the data chip at the PC");

    auto& robot=const_cast<Unit7Entity&>(game.unit());
        robot.transfer_to(BaseCamp::entrance);wait(game,90);
        check(game.in_spaceship() && !game.transitioning(),"Mission 4 cannot enter the rescue ship");
        robot.transfer_to({26.5,33.5});wait(game,30);
        check(game.near_records_pc() && game.interaction_label().empty() && !game.start_mission4_quiz().ok,
            "Records Room PC scans data before chip recovery");
        robot.transfer_to(ShipInterior::exit);
        check(game.interaction_label()=="EXIT SHIP" && game.interact().ok,"Mission 4 cannot leave the rescue ship");
        wait(game,90);check(!game.in_spaceship(),"airlock did not return Unit-01 to the surface");

        robot.transfer_to(BaseCamp::data_bank);wait(game,30);
    check(game.data_chip_spotted() && game.near_data_chip(),"approaching the chip did not reveal its signal");
    check(game.interaction_label()=="COLLECT DATA CHIP" && game.interact().ok && game.data_chip_collected(),
          "discovered chip cannot be collected");
    check(!game.near_data_chip() && !game.interact().ok,"collected chip can be collected twice");
    check(!game.start_mission4_quiz().ok,"chip starts the quiz remotely without the Monitor Room PC");

    robot.transfer_to(BaseCamp::entrance);wait(game,90);
    check(game.in_spaceship() && !game.transitioning(),"collected chip cannot be returned to the rescue ship");
    robot.transfer_to({26.5,33.5});wait(game,30);
    check(game.near_records_pc() && game.interaction_label()=="SCAN DATA CHIP" && game.interact().ok &&
          game.mission4_quiz_started() && game.mission4_quiz_reading(),"Monitor Room PC did not start the briefing");

    int passage_count=0,previous_passage=-1;
    for(int question=0;question<Mission4::question_count;++question) {
        check(game.mission4_question()==question,"quiz advanced past or skipped a question");
        const int passage=Mission4::question_passages[question];
        if(game.mission4_quiz_reading()) {
            check(question==0 || passage!=previous_passage,"same passage was reopened without a passage change");
            ++passage_count;game.mission4_read_passage();
        }
        check(!game.mission4_quiz_reading(),"question appeared before its passage was read");
        const int correct=Mission4::questions[question].correct;
        if(question==0) {
            check(!game.mission4_answer((correct+1)%4) && game.mission4_question()==question &&
                game.mission4_quiz_wrong() && !game.mission4_quiz_answered(),"wrong answer advanced or lost retry state");
            game.mission4_read_passage();
            check(game.mission4_quiz_reading() && game.mission4_quiz_rereading() &&
                game.mission4_question()==question,"rereading reset question progress");
            game.mission4_read_passage();
            check(!game.mission4_quiz_reading() && game.mission4_question()==question,"reread did not return to the same question");
        }
        check(game.mission4_answer(correct) && game.mission4_quiz_answered() &&
            !game.mission4_answer((correct+1)%4),"correct answer was not locked for confirmation");
        game.mission4_advance_quiz();
        previous_passage=passage;
        if(question+1<Mission4::question_count && Mission4::question_passages[question+1]!=passage)
            check(game.mission4_quiz_reading(),"next passage was skipped");
    }
    check(passage_count==4 && game.mission4_quiz_complete() && !game.mission4_quiz_started() &&
        !game.finished() && game.tasks()[0].complete && game.tasks()[0].progress=="ALL QUESTIONS CORRECT" &&
        game.last_message()=="MISSION 4 COMPLETE / MARS DATA VERIFIED / MISSION 5 AVAILABLE",
        "all-nine-answer completion state or feedback is incorrect");
    check(!game.mission5_transition_requested(),"Mission 5 starts before leaving the Records Room");
    robot.transfer_to({18.5,36.5});tick(game);
    check(game.mission5_transition_requested(),"leaving the Records Room does not request Mission 5");
}
}
int main(int argc,char** argv) {try {
    verify_mission4_quiz_and_handoff();
    const char* previews=argc>1 ? argv[1] : nullptr;
    GameOptions options;options.placeholder_mission=4;options.survival_enabled=false;
    Game game(WorldGrid::generate_isometric(),options);
    check(game.mission_number()==4 && !game.finished(),"Mission 4 entry is broken");
    auto& quiz_robot=const_cast<Unit7Entity&>(game.unit());
    check(!game.continue_mission()&&!game.start_mission4_quiz().ok,"fresh Mission 4 can skip chip recovery");
    quiz_robot.transfer_to({119,60});wait(game,12);
    check(game.mission_number()==4&&!game.finished()&&!game.rescue_mission(),"S-01 arrival incorrectly skips the quiz");
    quiz_robot.transfer_to({100,48});wait(game,12);
    check(game.data_chip_spotted()&&!game.data_chip_collected(),"data signal is not discovered");
    quiz_robot.transfer_to({112,48});wait(game,12);
    check(game.interaction_label()=="COLLECT DATA CHIP"&&game.interact().ok,"chip pickup failed");
    check(!game.interact().ok&&game.tasks()[0].progress=="RETURN TO THE RESCUE SHIP AIRLOCK","chip repeats or airlock objective missing");
    quiz_robot.transfer_to({7,9.7});wait(game,12);check(game.interact().ok,"quiz mission cannot enter airlock");wait(game,90);
    check(game.in_spaceship()&&game.tasks()[0].label=="SCAN CHIP IN RECORDS ROOM","terminal objective not updated on entry");
    check(!game.start_mission4_quiz().ok,"remote PC scan allowed");
    walk(game,{26,33});check(game.interaction_label()=="SCAN DATA CHIP"&&game.interact().ok,"PC cannot open quiz with chip");
    check(game.mission4_quiz_reading()&&game.mission4_question()==0,"first reading passage missing");
    const auto quiz_position=game.unit().center();InputState quiz_move;quiz_move.right=quiz_move.jump=true;
    for(int i=0;i<30;++i) tick(game,quiz_move);
    check(distance(quiz_position,game.unit().center())<1e-9&&!game.interact().ok&&!game.place_tile({16,53}).ok,"quiz does not block gameplay");
    PixelRenderer terminal;terminal.render(game);check(terminal.modal_open(),"quiz not shown as modal");
    snapshot(game,previews,"mission4-reading.ppm");
    for(int q=0;q<Mission4::question_count;++q) {
        check(game.mission4_question()==q&&!game.mission4_quiz_complete(),"quiz skipped an unanswered question");
        const bool new_passage=q==0 || Mission4::question_passages[q]!=Mission4::question_passages[q-1];
        check(game.mission4_quiz_reading()==new_passage,"relevant passage did not appear before its first question");
        if(new_passage) game.mission4_read_passage();
        game.mission4_advance_quiz();check(game.mission4_question()==q,"quiz advances without an answer");
        const int answer=Mission4::questions[q].correct;
        check(!game.mission4_answer((answer+1)%4)&&game.mission4_quiz_wrong(),"wrong answer was accepted");
        game.mission4_advance_quiz();check(game.mission4_question()==q,"wrong answer advances question");
        game.mission4_read_passage();check(game.mission4_quiz_rereading()&&game.mission4_question()==q,"reread resets progress");
        game.mission4_read_passage();check(!game.mission4_quiz_reading(),"back to question failed");
        game.set_paused(true);check(!game.mission4_answer(answer),"paused quiz accepts answers");game.set_paused(false);
        check(game.mission4_answer(answer)&&!game.mission4_answer(answer),"correct answer missing or can be submitted twice");
        if(q==0) snapshot(game,previews,"mission4-question.ppm");
        game.mission4_advance_quiz();
    }
    check(game.mission4_quiz_complete()&&!game.mission4_quiz_started()&&game.finished()&&game.tasks()[0].progress=="ALL QUESTIONS CORRECT","all nine answers do not complete Mission 4");
    check(game.unit().inventory().count(Item::MarsDataChip)==0&&game.unit().inventory().count(Item::USBDrive)==0,"standalone quiz does not consume its data chip");
    terminal.render(game);check(terminal.completion_screen(game),"Mission 4 continuation panel missing");
    terminal.completion_move(1);terminal.completion_activate(game);
    check(terminal.take_menu_command()==PixelRenderer::MenuCommand::ContinueMission&&game.continue_mission(),"quiz completion cannot continue directly to Mission 6");
    check(game.in_spaceship()&&game.mission_number()==6&&!game.finished()&&!game.transitioning(),"Mission 6 did not retain gameplay inside Map2");
    // No old Mission 5 entry remains; Mission 6 can also be selected directly.
    GameOptions direct;direct.placeholder_mission=6;direct.survival_enabled=false;
    Game selected(WorldGrid::generate_isometric(),direct);wait(selected,90);
    check(selected.in_spaceship()&&selected.mission_number()==6,"Mission 6 menu entry does not reach storage map");
    check(!game.interact().ok,"repair supplies can be gathered remotely");
    walk(game,{4,18});snapshot(game,previews,"mission6-storage.ppm");
    check(game.interact().ok && game.rescue_material_count()==1,"hull plate pickup failed");
    check(!game.interact().ok,"repair cache can be collected twice");
    walk(game,{4,22});check(game.interact().ok && game.rescue_material_count()==2,"wiring pickup failed");
    walk(game,{4,25});check(game.interact().ok && game.mission_number()==7,"sealant does not complete Mission 6");
    check(game.unit().inventory().count(Item::StructuralPlates)==4 && game.unit().inventory().count(Item::ScrapAlloys)==1000 &&
        game.unit().inventory().count(Item::Polyethylene)==500,"gathered repair materials were lost");
    check(!game.interact().ok,"C-02 communication starts outside the hangar");
    walk(game,{29,19});check(game.interaction_label()=="TALK TO C-02" && game.interact().ok,"hangar communication is unreachable");
    wait(game,60);const auto progress=game.rescue_stage_progress();const auto position=game.unit().center();
    game.set_paused(true);wait(game,300);
    check(game.rescue_stage_progress()==progress,"pause advances the rescue cinematic");game.set_paused(false);
    InputState attempted;attempted.left=attempted.jump=true;
    for(int i=0;i<30;++i) tick(game,attempted);
    check(distance(position,game.unit().center())<.05,"player input interrupts communication");
    snapshot(game,previews,"mission7-communication.ppm");
    int limit=0;while(game.rescue_stage()!=Game::RescueStage::InstallingSeat && ++limit<600) tick(game);
    wait(game,90);snapshot(game,previews,"mission7-seat.ppm");
    limit=0;while(game.rescue_stage()!=Game::RescueStage::Loading && ++limit<3600) tick(game);
    check(limit<3600 && game.carrier_seat_installed(),"robots did not walk from hangar to bed");
    check(distance(game.unit().center(),ShipInterior::rescue_unit_bedside)<.15 &&
        distance(game.escort_carrier_position(),ShipInterior::rescue_carrier_bedside)<.15,"pickup starts away from bed");
    wait(game,120);snapshot(game,previews,"mission7-pickup.ppm");
    limit=0;while(game.rescue_stage()!=Game::RescueStage::Escort && ++limit<900) tick(game);
    check(limit<900 && !game.in_spaceship() && game.astronaut_aboard() && !game.rescue_cinematic(),"pickup did not resume escort outside the base");
    snapshot(game,previews,"mission7-escort.ppm");
    const auto carrier=game.escort_carrier_position();game.set_paused(true);wait(game,120);
    check(distance(carrier,game.escort_carrier_position())<1e-9,"paused C-02 keeps moving");game.set_paused(false);
    const auto outside=game.unit().center();
    const_cast<Unit7Entity&>(game.unit()).transfer_to({119,60});tick(game);
    check(!game.finished(),"Mission 7 completes when only U-01 reaches S-01");
    const_cast<Unit7Entity&>(game.unit()).transfer_to(outside);
    walk(game,{18,12});walk(game,{18,18});walk(game,{13,18});wait(game,300);
    check(diagonal_follow_steps>30 && largest_follow_turn>.01,"C-02 still follows on fixed axes without steering");
    const auto parked=game.escort_carrier_position();const double wheels=game.escort_wheel_phase();wait(game,120);
    check(distance(parked,game.escort_carrier_position())<.01 && game.escort_wheel_phase()==wheels,"C-02 creeps or spins its wheels while parked");
    snapshot(game,previews,"mission7-flexible-follow.ppm");
    walk(game,{113,51});wait(game,240);
    check(distance(game.escort_carrier_position(),game.unit().center())<2.2,"C-02 cannot keep up with U-01");
    walk(game,{116,52});walk(game,{116,56});walk(game,{124,56});wait(game,240);
    walk(game,{120,58});wait(game,600);
    check(game.mission7_complete() && game.mission_number()==8 && !game.finished(),"escort did not start Mission 8 with both robots at S-01");
    snapshot(game,previews,"mission7-arrival.ppm");
    check(game.astronaut_aboard() && !game.rescue_ship_repaired(),"passenger unloaded before S-01 was repaired");
    auto& robot=const_cast<Unit7Entity&>(game.unit());const auto repair_position=robot.center();
    robot.transfer_to({113,60});tick(game);check(!game.interact().ok,"S-01 can be repaired remotely");
    robot.transfer_to(repair_position);tick(game);
    robot.inventory().consume(Item::StructuralPlates,1);
    check(!game.interact().ok,"S-01 repair starts without all required materials");robot.inventory().add(Item::StructuralPlates,1);
    check(game.interact().ok,"S-01 repair could not start");wait(game,120);
    const double repair_progress=game.rescue_stage_progress();game.set_paused(true);wait(game,120);
    check(game.rescue_stage_progress()==repair_progress,"paused S-01 repair advances");game.set_paused(false);
    robot.transfer_to({113,60});tick(game);
    check(game.rescue_stage()==Game::RescueStage::AwaitingRepair && robot.inventory().count(Item::StructuralPlates)==4,"cancelled repair consumes materials");
    robot.transfer_to(repair_position);tick(game);check(game.interact().ok,"cancelled repair cannot restart");
    wait(game,180);snapshot(game,previews,"mission8-repair.ppm");wait(game,179);
    check(!game.rescue_ship_repaired() && game.astronaut_aboard(),"repair finishes before six seconds");tick(game);
    check(game.rescue_ship_repaired() && !game.finished(),"repair did not start the passenger transfer");
    check(robot.inventory().count(Item::StructuralPlates)==0 && robot.inventory().count(Item::ScrapAlloys)==0 && robot.inventory().count(Item::Polyethylene)==0,"repair materials not consumed exactly once");
    check(!game.interact().ok,"repair can be repeated during transfer");
    limit=0;while(game.rescue_stage()!=Game::RescueStage::Unloading && ++limit<600) tick(game);
    check(limit<600,"C-02 cannot reach the S-01 boarding ramp");
    check(routed_around_hull,"C-02 did not route around S-01 from the far side");
    wait(game,120);snapshot(game,previews,"mission8-unloading.ppm");
    const auto unloading_progress=game.rescue_stage_progress();game.set_paused(true);wait(game,120);
    check(game.rescue_stage_progress()==unloading_progress,"paused passenger transfer advances");game.set_paused(false);
    wait(game,72);snapshot(game,previews,"mission8-through-door.ppm");wait(game,108);
    check(!game.finished() && game.astronaut_in_rescue_ship() && !game.astronaut_aboard(),"Astronerd transfer did not continue into launch preparation");
    snapshot(game,previews,"mission8-cabin-seated.ppm");
    check(!game.in_spaceship() && distance(robot.center(),repair_position)<.1 && distance(game.escort_carrier_position(),Game::rescue_boarding_point)<.1,"robots did not stay on the planet");
    limit=0;while(game.rescue_stage()!=Game::RescueStage::ChargingCarrier && ++limit<240) tick(game);
    const double unit_power=robot.battery_percent();
    check(game.rescue_door_phase()==0 && distance(game.rescue_passenger_position(),RescueShip::passenger_seat)<1e-6,"cabin door did not close after boarding");
    check(limit<240,"C-02 charging did not begin after boarding");wait(game,180);
    check(game.carrier_battery_percent()>40 && game.carrier_battery_percent()<60 && game.rescue_ship_battery_percent()>20 && game.rescue_ship_battery_percent()<30,"C-02 battery does not transfer progressively");
    check(std::abs(robot.battery_percent()-unit_power)<1e-6,"U-01 drains before C-02 finishes charging");
    snapshot(game,previews,"mission8-c02-charging.ppm");
    const double carrier_power=game.carrier_battery_percent(),ship_power=game.rescue_ship_battery_percent();
    game.set_paused(true);wait(game,180);check(game.carrier_battery_percent()==carrier_power && game.rescue_ship_battery_percent()==ship_power,"pause does not freeze battery transfer");game.set_paused(false);
    limit=0;while(game.rescue_stage()!=Game::RescueStage::CarrierOffline && ++limit<300) tick(game);
    check(limit<300 && game.carrier_battery_percent()==0 && game.rescue_ship_battery_percent()==45 && robot.alive(),"C-02 does not power down with insufficient launch energy");
    snapshot(game,previews,"mission8-c02-offline.ppm");
    limit=0;while(game.rescue_stage()!=Game::RescueStage::ChargingUnit && ++limit<240) tick(game);
    check(limit<240,"U-01 transfer did not follow C-02 shutdown");wait(game,210);
    check(robot.battery_percent()>unit_power*.45 && robot.battery_percent()<unit_power*.55 && game.rescue_drain_effect()>.45,"U-01 battery drain and screen effect do not progress");
    snapshot(game,previews,"mission8-u01-charging.ppm");
    const double paused_power=robot.battery_percent();game.set_paused(true);wait(game,120);
    check(robot.battery_percent()==paused_power,"U-01 battery drains while paused");game.set_paused(false);
    limit=0;while(game.rescue_stage()!=Game::RescueStage::LaunchingShip && ++limit<300) tick(game);
    check(limit<300 && robot.battery_percent()==0 && !robot.alive() && game.carrier_battery_percent()==0 && game.rescue_ship_battery_percent()==100,"both robots did not sacrifice all battery power");
    check(!game.finished() && game.climax().state()!=ClimaxState::Failed,"intentional shutdown triggers failure or stops launch");
    wait(game,120);check(game.rescue_ship_lift()>1,"S-01 does not lift off");snapshot(game,previews,"mission8-launch.ppm");
    const double lift=game.rescue_ship_lift();game.set_paused(true);wait(game,120);
    check(game.rescue_ship_lift()==lift,"launch moves while paused");game.set_paused(false);
    wait(game,400);
    check(game.finished() && game.rescue_launch_complete() && game.astronaut_in_rescue_ship() && game.climax().state()!=ClimaxState::Failed,"launch does not complete after U-01 powers off");
    check(robot.battery_percent()==0 && game.carrier_battery_percent()==0 && !game.in_spaceship() && distance(robot.center(),repair_position)<.1 && distance(game.escort_carrier_position(),Game::rescue_boarding_point)<.1,"powered-off robots were not left on planet");
    snapshot(game,previews,"mission8-complete.ppm");
    std::cout<<"PASS rescue: Missions 4-8, escort, repairs, boarding, sequential full battery transfers, pause, launch after shutdown and both robots left behind\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

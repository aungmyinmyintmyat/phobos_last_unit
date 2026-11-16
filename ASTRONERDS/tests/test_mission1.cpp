#include "horizon/Renderer.hpp"
#include <iostream>
#include <filesystem>
#include <queue>
#include <stdexcept>
using namespace horizon;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void wait(Game& game,int ticks){for(int i=0;i<ticks;++i)game.advance({},1.0/60);}
bool barrier(TileType t){return t==TileType::SupplyBarrier||t==TileType::SilicateBarrier||t==TileType::IceBarrier;}
bool mineral(TileType t){return t==TileType::IronNickelOre||t==TileType::HydratedMinerals||t==TileType::WaterIce;}
std::vector<std::uint32_t> inventory_slot(const PixelRenderer& renderer,int slot) {
 std::vector<std::uint32_t> result;
 for(int y=318;y<346;++y) for(int x=185+slot*30;x<213+slot*30;++x) result.push_back(renderer.pixels()[y*PixelRenderer::width+x]);
 return result;
}
void go(Game& g,GridPos target){
 const auto& w=g.world();const int width=w.width(),start=int(g.unit().center().y)*width+int(g.unit().center().x),end=target.y*width+target.x;
 std::vector<int> prev(width*w.height(),-1);std::queue<int> q;q.push(start);prev[start]=start;
 while(!q.empty()&&prev[end]<0){int at=q.front();q.pop();for(GridPos d:{GridPos{1,0},GridPos{-1,0},GridPos{0,1},GridPos{0,-1}}){int x=at%width+d.x,y=at/width+d.y;if(!w.in_bounds(x,y))continue;int next=y*width+x;
 // Surface routes stop at the approach; a map transfer is requested separately.
 if(!g.in_spaceship() && distance({x+.5,y+.5},BaseCamp::entrance)<.65) continue;
 const bool doorway=g.in_spaceship()&&(ShipInterior::storage_door.contains(x,y)||ShipInterior::monitor_door.contains(x,y)||ShipInterior::cargo_door.contains(x,y));
 if(prev[next]>=0||(w.solid(x,y)&&!barrier(w.get_tile_type(x,y))&&!doorway)||std::abs(w.elevation(x,y)-w.elevation(at%width,at/width))>.65) { continue; }
 prev[next]=at;q.push(next);}}
 if(prev[end]<0)throw std::runtime_error("no route to "+std::to_string(target.x)+","+std::to_string(target.y));
 std::vector<GridPos> path;for(int n=end;n!=start;n=prev[n])path.push_back({n%width,n/width});std::reverse(path.begin(),path.end());
 for(auto cell:path){if(barrier(w.get_tile_type(cell.x,cell.y))){InputState mine;mine.mining_target=cell;for(int t=0;t<140&&w.solid(cell.x,cell.y);++t)g.advance(mine,1.0/60);check(!w.solid(cell.x,cell.y),"laser cannot clear supply barrier");}
 int t=0;while(distance(g.unit().center(),{cell.x+.5,cell.y+.5})>.13&&++t<500){auto p=g.unit().center();int dx=cell.x+.5-p.x>.06?1:cell.x+.5-p.x<-.06?-1:0,dy=cell.y+.5-p.y>.06?1:cell.y+.5-p.y<-.06?-1:0;InputState i;i.right=dx-dy>0;i.left=dx-dy<0;i.down=dx+dy>0;i.up=dx+dy<0;g.advance(i,1.0/60);}check(t<500&&g.unit().alive(),"walking route blocked");}
 for(int i=0;i<8;++i)g.advance({},1.0/60);
}
int main(int argc,char** argv){try{
 Game game;check(game.mission1(),"new game does not start Mission 1");check(game.supply_count()==0,"mission did not reset");check(!game.interact().ok,"remote collection allowed");
 check(game.mission_number()==1&&!game.start_copying_records().ok,"record copy unlocks before supply recovery");
 for(int x:{6,7}) check(game.world().solid(x,BaseCamp::south) && game.world().obstacle_height(x,BaseCamp::south)>=100,"campaign airlock is not sealed against entry");
 Unit7Entity gate_jumper({6.65,9.275});gate_jumper.move_on_plane({0,-1},true);
 for(int tick=0;tick<120;++tick)gate_jumper.update(1.0/120,game.world());
 check(gate_jumper.center().y>9.0,"robot jumped through the sealed campaign airlock");
 int barriers=0,minerals=0,west=0,east=0;std::array<int,3> material_counts{};
 auto repeated=WorldGrid::generate_isometric();repeated.setup_mission1();
 for(int y=1;y<game.world().height()-1;++y) for(int x=1;x<game.world().width()-1;++x) {
   const auto type=game.world().get_tile_type(x,y);barriers+=barrier(type);
   check(type==repeated.get_tile_type(x,y),"campaign mineral scatter changes between identical seeds");
   if(mineral(type)) {
     ++minerals;(x<game.world().width()/2 ? west : east)++;
     ++material_counts[type==TileType::IronNickelOre ? 0 : type==TileType::HydratedMinerals ? 1 : 2];
     check(std::abs(y-10)>2&&!game.world().pit(x,y),"minerals obstruct the hub road or spawn in pits");
     check(distance({x+.5,y+.5},BaseCamp::data_bank)>=5&&distance({x+.5,y+.5},BaseCamp::damaged_shuttle)>=6,"mineral scatter obstructs later mission landmarks");
   }
 }
 check(barriers==24,"three barrier enclosures required");
 check(minerals>=20&&minerals<150&&west>0&&east>0&&material_counts[0]>0&&material_counts[1]>0&&material_counts[2]>0,"Map1 does not have sparse, varied mineral deposits across both halves");
 check(!game.perform(Action::Smelt).ok,"old crafting enabled");
 auto slope_world=WorldGrid::generate_isometric(72,64);slope_world.setup_mission1();
 bool slope_checked=false;
 for(int y=13;y<slope_world.height()-1&&!slope_checked;++y) for(int x=20;x<slope_world.width()-4&&!slope_checked;++x) {
   const auto& w=slope_world;
   if(w.elevation(x+1,y)-w.elevation(x,y)<0.67 || w.solid(x,y)||w.solid(x+1,y)||w.solid(x+2,y)||w.solid(x+3,y)) continue;
   Unit7Entity climber({x+0.15,y+0.075}); climber.move_on_plane({1,0},false);
   for(int tick=0;tick<60;++tick) climber.update(1.0/60,w);
   check(climber.center().x>x+1.6 && climber.alive(),"robot stuck on continuous uphill slope");
   slope_checked=true;
 }
 check(slope_checked,"missing steep slope regression fixture");
 Unit7Entity jumper({25.15,12.075}); jumper.move_on_plane({0,1},true);
 for(int tick=0;tick<120;++tick) jumper.update(1.0/120,game.world());
 check(jumper.center().y<13,"robot jumped over the supply enclosure");
 for(int supply_index=0;supply_index<3;++supply_index){
  const auto& supply=game.supplies()[supply_index];
  auto p=supply.position;int before=game.supply_count();go(game,{int(p.x),int(p.y)});check(game.supply_count()==before,"automatic collection");check(game.interaction_label()=="COLLECT","collect button unavailable");
  PixelRenderer ui;ui.render(game);check(ui.click(300,250,game),"collect click not consumed");check(game.supply_count()==before+1,"click did not collect");check(!game.interact().ok,"supply collected twice");
 }
 for(int barrier_index=3;barrier_index<5;++barrier_index) go(game,{int(Mission1::supplements[barrier_index].x),int(Mission1::supplements[barrier_index].y)});
 int remaining=0;for(int y=1;y<63;++y)for(int x=1;x<71;++x)remaining+=barrier(game.world().get_tile_type(x,y));
 check(remaining>0 && remaining<24,"collection required destroying every barrier");
 check(game.unit().inventory().count(Item::FirstAidKit)==3,"inventory missing three first-aid kits");
 check(game.unit().inventory().count(Item::FoodWaterSupply)==0,"food/water boxes appeared on the surface");
 for(auto item:Game::sample_items) check(game.unit().inventory().count(item)==1,"sample missing or count exceeds one");
 check(!game.tasks()[0].complete&&!game.tasks()[1].active,"task two unlocked before indoor food boxes");
 go(game,{7,9});check(game.interaction_label()=="ENTER SHIP","airlock unavailable after collection");check(game.interact().ok,"cannot enter the rescue ship");
 for(int tick=0;tick<90;++tick)game.advance({},1.0/60);
 check(game.in_spaceship(),"delivery route did not transfer to Map2");
 check(distance(game.tasks()[0].target,ShipInterior::food_supplies[1])<.01,"indoor task does not guide the player to food supplies");
 go(game,{2,19});check(game.interaction_label()=="COLLECT","storage food box is not collectible");
 PixelRenderer supplies_ui;supplies_ui.render(game);
 const auto medical_before=inventory_slot(supplies_ui,1),food_before=inventory_slot(supplies_ui,2),sample_before=inventory_slot(supplies_ui,3);
 check(game.interact().ok,"storage food box collection failed");supplies_ui.render(game);
 check(game.unit().inventory().count(Item::FirstAidKit)==3&&game.unit().inventory().count(Item::FoodWaterSupply)==1,"food pickup changes the wrong inventory item");
 check(inventory_slot(supplies_ui,1)==medical_before&&inventory_slot(supplies_ui,2)!=food_before&&inventory_slot(supplies_ui,3)==sample_before,"food pickup updates the medical or mineral sample box instead of the food box");
 if(argc==2) {
   std::filesystem::create_directories(argv[1]);
   check(supplies_ui.write_ppm((std::filesystem::path(argv[1])/"mission1-food-counter.ppm").string()),"food counter preview failed");
   supplies_ui.toggle_inventory();supplies_ui.render(game);
   check(supplies_ui.write_ppm((std::filesystem::path(argv[1])/"mission1-supplies.ppm").string()),"supply inventory preview failed");
 }
 check(game.supply_count()==4&&!game.tasks()[0].complete,"storage food box is missing from mission progress");
 go(game,{1,19});check(!game.interact().ok,"storage food box can be collected twice");
 go(game,{23,47});check(game.interaction_label()=="COLLECT","engine food box is not collectible");check(game.interact().ok,"engine food box collection failed");
 go(game,{24,47});check(!game.interact().ok,"engine food box can be collected twice");
 check(game.tasks()[0].complete&&game.tasks()[1].active,"task two did not unlock after indoor boxes");
 check(!game.mission1_complete()&&game.mission_number()==1&&!game.mission2_complete()&&!game.start_copying_records().ok,"supply collection skips delivery and observations");
 go(game,{26,33});check(game.near_records_pc()&&game.interaction_label()!="COLLECT USB"&&!game.start_copying_records().ok,"PC copy unlocks before delivery when approached");
 go(game,{11,19});
 check(game.interaction_label()=="DELIVER","astronaut delivery unavailable");check(game.interact().ok,"delivery failed");
 check(!game.mission2_complete()&&game.mission_number()==1&&!game.finished(),"delivery skips the carrier repair mission");
 check(game.observation_count()==0&&game.tasks()[2].active,"delivery does not unlock observations");
 check(game.unit().inventory().count(Item::FirstAidKit)==0&&game.unit().inventory().count(Item::FoodWaterSupply)==0,"delivery did not transfer mission supplies");
 check(!game.start_copying_records().ok,"copy starts remotely after delivery");
 go(game,{9,15});check(game.storage_door_open(),"storage door did not open automatically");
 for(int tick=0;tick<30;++tick)game.advance({},1.0/60);
 go(game,{5,17});
 for(int sample=0;sample<3;++sample) {
   check(game.near_observed_table(),"cannot approach Observed Table");
   PixelRenderer ui;ui.render(game);check(ui.click(300,175+sample*18,game),"observation click not consumed");
   check(game.observing()==sample,"sample did not start observing");
   check(game.unit().inventory().count(Game::sample_items[sample])==0,"sample was not deposited on table");
   check(!game.start_observation((sample+1)%3).ok,"concurrent observation allowed");
   for(int tick=0;tick<23;++tick) game.advance({},.25);
   check(!game.sample_observed(sample),"observation finishes before six seconds");
   double before=game.observation_progress();game.set_paused(true);game.advance({},.25);
   check(game.observation_progress()==before,"observation continues while paused");game.set_paused(false);
   game.advance({},.25);check(game.sample_observed(sample)&&game.observation_result()==sample,"observation did not finish after six seconds");
   ui.render(game);check(ui.click(425,250,game),"result close click failed");check(game.observation_result()<0,"knowledge panel stays open");
   check(game.unit().inventory().count(Game::sample_items[sample])==1,"observation duplicates or loses sample");
 }
 check(game.mission1_complete()&&game.mission2()&&game.mission_number()==2&&!game.finished(),"final observation does not start carrier repair");
 check(!game.start_copying_records().ok,"records unlock before carrier repair");
 go(game,{26,33});check(game.interaction_label()!="COLLECT USB","Mission 2 PC bypasses carrier repair");
 go(game,{29,19});check(game.near_carrier_robot()&&game.interaction_label()=="SCAN CARRIER ROBOT","carrier scan is unreachable");
 check(game.interact().ok&&game.carrier_scanned(),"carrier scan failed");
 check(!game.interact().ok,"carrier repair starts without replacement parts");
 go(game,{26,18});check(game.interaction_label()=="COLLECT WHEEL"&&game.interact().ok&&game.carrier_wheel_collected(),"carrier wheel cannot be collected");
 check(!game.interact().ok,"wheel collected twice");
 go(game,{26,22});check(game.interaction_label()=="COLLECT ROBOT ARM"&&game.interact().ok&&game.carrier_arm_collected(),"carrier arm cannot be collected");
 go(game,{4,18});check(game.interaction_label()=="COLLECT CONTROL CHIP"&&game.interact().ok&&game.carrier_chip_collected(),"storage chip cannot be collected");
 go(game,{29,19});check(game.interaction_label()=="REPAIR CARRIER ROBOT"&&game.interact().ok,"carrier repair cannot start");
 wait(game,60);const double repair_progress=game.carrier_repair_progress();
 game.set_paused(true);wait(game,60);check(game.carrier_repair_progress()==repair_progress,"carrier repair advances while paused");game.set_paused(false);
 wait(game,299);check(game.mission2()&&!game.mission2_complete(),"carrier repair finishes before six seconds");
 wait(game,1);check(game.carrier_repaired()&&game.mission2_complete()&&game.mission_number()==3&&!game.finished(),"carrier repair does not start Mission 3");
 if(argc==2) {
     std::filesystem::create_directories(argv[1]);PixelRenderer repaired_view;repaired_view.render(game);
     check(repaired_view.write_ppm((std::filesystem::path(argv[1])/"carrier-repaired.ppm").string()),"repaired carrier preview write failed");
 }
 check(game.tasks()[0].label=="COLLECT USB"&&distance(game.tasks()[0].target,ShipInterior::records_pc)<.01,"repaired carrier does not unlock PC task");
 go(game,{26,33});
 check(game.near_records_pc()&&game.interaction_label()=="COLLECT USB","PC cannot be approached or has no E interaction");
 wait(game,30);check(!game.copying_records()&&game.records_copy_progress()==0,"PC starts copying automatically");
 game.set_paused(true);check(!game.start_copying_records().ok,"copy starts while paused");game.set_paused(false);
 check(game.interact().ok&&game.copying_records(),"E does not start record copy");
 check(!game.interact().ok&&!game.start_copying_records().ok,"duplicate E restarts copying");wait(game,60);
 const double copy_progress=game.records_copy_progress();check(std::abs(copy_progress-.25)<1e-8,"one second is not 25 percent of copying");
 game.set_paused(true);wait(game,60);check(game.records_copy_progress()==copy_progress,"copying advances while paused");game.set_paused(false);
 go(game,{24,36});check(!game.copying_records()&&game.records_copy_progress()==0&&!game.mission3_complete(),"leaving PC does not cancel copy");
 go(game,{26,33});check(game.interact().ok,"cancelled copy cannot restart");
 InputState copy_jump;copy_jump.jump=true;game.advance(copy_jump,1.0/60);
 check(!game.copying_records(),"jumping does not cancel copy");wait(game,90);
 check(game.interact().ok,"copy cannot restart after landing");
 PixelRenderer records_view;records_view.render(game);const auto render_progress=game.records_copy_progress();
 records_view.render(game);check(game.records_copy_progress()==render_progress,"rendering advances copying");
 if(argc==2){std::filesystem::create_directories(argv[1]);check(records_view.write_ppm((std::filesystem::path(argv[1])/"records-start.ppm").string()),"record preview write failed");}
 wait(game,120);records_view.render(game);
 if(argc==2)check(records_view.write_ppm((std::filesystem::path(argv[1])/"records-copying.ppm").string()),"copying preview write failed");
 wait(game,119);check(!game.mission3_complete()&&game.copying_records(),"copy finishes before four seconds");
 wait(game,1);check(game.records_copied()&&!game.mission3_complete()&&!game.finished()&&game.records_copy_progress()==1,"record copy must unlock S-01 scanning before completion");
 check(game.usb_obtained()&&!game.usb_scan_recorded()&&game.unit().inventory().count(Item::USBDrive)==1&&game.unit().inventory().count(Item::MarsDataChip)==0,"Monitor Room did not supply a USB distinct from the Mars chip");
 check(game.tasks()[0].label=="SCAN S-01 TO USB"&&game.tasks()[0].active&&!game.start_copying_records().ok,"record copy did not unlock S-01 objective");
 auto& scanner=const_cast<Unit7Entity&>(game.unit());scanner.transfer_to(ShipInterior::exit);wait(game,2);
 check(game.interact().ok,"cannot leave ship to scan S-01");wait(game,90);
 scanner.transfer_to({116,60});wait(game,12);check(!game.interact().ok,"remote S-01 scan allowed");
 scanner.transfer_to({119,60});wait(game,12);check(game.interaction_label()=="SCAN SPACESHIP"&&game.interact().ok,"E scan unavailable at S-01");
 check(!game.interact().ok,"scan can be restarted while recording to USB");
 wait(game,60);const double scan_progress=game.spaceship_scan_progress();game.set_paused(true);wait(game,60);
 check(game.spaceship_scan_progress()==scan_progress,"paused spaceship scan advances");game.set_paused(false);
 scanner.transfer_to({115,60});wait(game,2);check(!game.scanning_spaceship()&&game.spaceship_scan_progress()==0,"leaving S-01 did not cancel scan");
 check(!game.usb_scan_recorded(),"cancelled scan left a completed record on USB");
 check(scanner.inventory().consume(Item::USBDrive,1),"test USB removal failed");
 scanner.transfer_to({119,60});wait(game,12);check(!game.interact().ok,"spaceship can be scanned without USB");
 scanner.inventory().add(Item::USBDrive,1);
 scanner.transfer_to({119,60});wait(game,12);check(game.interact().ok,"cancelled scan cannot restart");
 wait(game,239);check(!game.mission3_complete()&&game.scanning_spaceship(),"S-01 scan finishes before four seconds");
 wait(game,1);check(game.mission3_complete()&&game.finished(),"S-01 scan does not finish Mission 3");
 check(game.usb_scan_recorded()&&scanner.inventory().count(Item::USBDrive)==1,"S-01 result was not recorded onto the USB");
 const auto finished_time=game.simulated_seconds();game.advance({},.25);
 check(game.simulated_seconds()==finished_time,"completed mission keeps advancing");
 check(game.continue_mission()&&game.mission_number()==4&&!game.finished()&&!game.data_chip_spotted()&&!game.data_chip_collected()&&!game.mission4_quiz_started()&&!game.mission4_quiz_complete(),"Mission 3 does not start a clean Mission 4");
 check(game.usb_scan_recorded()&&scanner.inventory().count(Item::USBDrive)==1,"USB recording lost when the return journey begins");
 go(game,{112,48});check(game.data_chip_spotted()&&game.interaction_label()=="COLLECT DATA CHIP"&&game.interact().ok,"Mars chip not found on return route");
 check(scanner.inventory().count(Item::MarsDataChip)==1&&!game.interact().ok&&scanner.inventory().count(Item::USBDrive)==1,"Mars chip repeats or replaces the USB");
 go(game,{7,9});check(game.interact().ok,"cannot return to base carrying both data items");wait(game,90);
 go(game,{26,33});
 if(argc==2) {
     PixelRenderer usb_view;usb_view.toggle_inventory();usb_view.render(game);
     check(usb_view.write_ppm((std::filesystem::path(argv[1])/"usb-and-mars-chip.ppm").string()),"USB inventory preview failed");
 }
 check(game.interaction_label()=="SCAN DATA CHIP"&&game.interact().ok&&game.usb_record_uploaded(),"PC did not upload USB record and open the Mars quiz");
 for(int q=0;q<Mission4::question_count;++q) {
     check(scanner.inventory().count(Item::USBDrive)==1&&scanner.inventory().count(Item::MarsDataChip)==1,"quiz consumes USB/chip before the last answer is confirmed");
     if(game.mission4_quiz_reading()) game.mission4_read_passage();
     check(game.mission4_answer(Mission4::questions[q].correct),"return-journey quiz answer failed");
     check(scanner.inventory().count(Item::USBDrive)==1&&scanner.inventory().count(Item::MarsDataChip)==1,"answer selection consumes items before confirmation");
     game.mission4_advance_quiz();
 }
 check(game.finished()&&scanner.inventory().count(Item::USBDrive)==0&&scanner.inventory().count(Item::MarsDataChip)==0,"quiz completion did not use up USB and Mars chip immediately");
 game.mission4_advance_quiz();
 check(game.continue_mission()&&game.mission_number()==6&&game.usb_record_uploaded()&&scanner.inventory().count(Item::USBDrive)==0&&scanner.inventory().count(Item::MarsDataChip)==0,"Mission 6 continuation restores used items or loses verified data");
 Game reset;check(reset.supply_count()==0&&!reset.mission1_complete()&&!reset.mission2_complete()&&!reset.mission3_complete()&&reset.records_copy_progress()==0,"new campaign not reset");
 GameOptions repair_entry;repair_entry.placeholder_mission=2;
 Game direct_repair(WorldGrid::generate_isometric(),repair_entry);
 go(direct_repair,{7,9});check(direct_repair.interact().ok,"Mission 2 menu entry cannot board ship");wait(direct_repair,90);
 go(direct_repair,{29,19});check(direct_repair.interact().ok&&direct_repair.carrier_scanned(),"Mission 2 menu entry cannot scan carrier");
 go(direct_repair,{26,18});check(direct_repair.interact().ok,"Mission 2 menu entry cannot collect wheel");
 go(direct_repair,{26,22});check(direct_repair.interact().ok,"Mission 2 menu entry cannot collect arm");
 go(direct_repair,{4,18});check(direct_repair.interact().ok,"Mission 2 menu entry cannot collect chip");
 go(direct_repair,{29,19});check(direct_repair.interact().ok,"Mission 2 menu entry cannot repair carrier");wait(direct_repair,360);
 check(direct_repair.mission2_complete()&&direct_repair.mission_number()==3,"direct Mission 2 entry requires unrelated supply delivery to unlock Mission 3");
 std::cout<<"PASS campaign: supplies, observations, carrier scan and parts, six-second repair, automatic Mission 3, four-second PC copy and direct Mission 2 entry\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

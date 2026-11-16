#pragma once
#include "horizon/ClimaxEngine.hpp"
#include "horizon/NASA_ISRU_Refinery.hpp"
#include "horizon/Presentation.hpp"
#include "horizon/BaseCamp.hpp"
#include "horizon/Mission1.hpp"
#include "horizon/Mission4.hpp"
#include "horizon/ShipInterior.hpp"
#include "horizon/RescueShip.hpp"
#include "horizon/PhobosClock.hpp"
#include "horizon/DangerSystem.hpp"
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>

namespace horizon {
struct InputState {
    bool left = false, right = false, jump = false, climb = false;
    bool up = false, down = false;
    std::optional<GridPos> mining_target;
    std::optional<Vec2> combat_target;
};
struct CombatBeam {
    Vec2 origin, endpoint;
    double age = 0, duration = 0.15;
    bool hit = false, killed = false;
};
enum class Action { Extract, MeltIce, Electrolyze, Liquefy, PackageFuel, Smelt,
                    UpgradeDrill, UpgradeTreads, UpgradeShield, WeldHull,
                    HookupLines, SocketCore, Calibrate, ActivateValve };
struct MissionTask {
    std::string label, progress;
    bool complete = false, active = false;
    Vec2 target;
};
struct GameOptions {
    std::uint32_t danger_seed = 0; // Zero chooses a fresh hazard schedule per mission.
    DangerConfig danger;
    double starting_solar_phase = PhobosClock::default_starting_phase;
    double initial_battery = 100;
    bool survival_enabled = true;
    bool mission1 = false;
    bool start_mission3 = false;
    int placeholder_mission = 0; // Standalone mission entry point.
};
class Game {
public:
    enum class RescueStage { Waiting, Communicating, InstallingSeat, ToBed, Loading, Departing, Escort, Complete, AwaitingRepair, RepairingShip, Docking, Unloading, ClosingDoor, Rescued, ChargingCarrier, CarrierOffline, ChargingUnit, LaunchingShip, ShipDeparted };
    int rescue_mission() const { return rescue_mission_; }
    RescueStage rescue_stage() const { return rescue_stage_; }
    bool rescue_cinematic() const { return (rescue_mission_==7 && rescue_stage_>=RescueStage::Communicating && rescue_stage_<=RescueStage::Departing) || (rescue_mission_==8 && rescue_stage_>=RescueStage::Docking); }
    bool rescue_material_collected(int i) const { return i>=0 && i<3 && repair_materials_[i]; }
    int rescue_material_count() const { return int(repair_materials_[0])+int(repair_materials_[1])+int(repair_materials_[2]); }
    bool carrier_mobile() const { return rescue_mission_>=7 && rescue_stage_>=RescueStage::ToBed; }
    bool carrier_seat_installed() const { return rescue_stage_>=RescueStage::ToBed; }
    bool astronaut_aboard() const { return rescue_stage_>=RescueStage::Departing && rescue_stage_<RescueStage::Unloading; }
    bool mission7_complete() const { return rescue_mission_>=8; }
    bool rescue_ship_repaired() const { return rescue_stage_>=RescueStage::Docking; }
    bool astronaut_in_rescue_ship() const { return rescue_stage_>=RescueStage::ClosingDoor; }
    double rescue_door_phase() const;
    Vec2 rescue_passenger_position() const;
    double rescue_passenger_height() const;
    bool rescue_power_sequence() const { return rescue_mission_==8 && rescue_stage_>=RescueStage::Rescued; }
    bool rescue_launch_complete() const { return rescue_stage_==RescueStage::ShipDeparted; }
    double carrier_battery_percent() const { return carrier_battery_; }
    double rescue_ship_battery_percent() const { return rescue_ship_battery_; }
    double rescue_ship_lift() const { const double t=rescue_stage_progress();return rescue_stage_==RescueStage::LaunchingShip ? 1.5*t+18*t*t : 0; }
    double rescue_drain_effect() const { return rescue_stage_==RescueStage::ChargingUnit ? 1-unit_->battery_percent()/std::max(1.0,unit_transfer_battery_) : rescue_stage_>=RescueStage::LaunchingShip ? 1.0 : rescue_stage_==RescueStage::ChargingCarrier ? .35*(1-carrier_battery_/100) : 0; }
    static constexpr double rescue_ship_repair_seconds=6.0;
    static constexpr Vec2 rescue_boarding_point=RescueShip::carrier_stop;
    Vec2 escort_carrier_position() const { return escort_carrier_; }
    double escort_carrier_heading() const { return escort_heading_; }
    double escort_wheel_phase() const { return escort_wheel_phase_; }
    Vec2 escort_carrier_velocity() const { return escort_velocity_; }
    double rescue_stage_progress() const;
    const char* rescue_dialogue() const;
    Game();
    bool campaign() const { return mission1_; }
    bool in_spaceship() const { return in_spaceship_; }
    bool storage_door_open() const { return storage_door_open_; }
    double storage_door_phase() const { return storage_door_phase_; }
    const char* map_name() const { return in_spaceship_ ? "Map2" : "Map1"; }
    bool transitioning() const { return transition_time_>0; }
    double transition_alpha() const;
    Vec2 observation_table_position() const { return in_spaceship_ ? ShipInterior::table : BaseCamp::observed_table; }
    Vec2 survivor_position() const { return in_spaceship_ ? ShipInterior::astronaut : astronaut_position; }
    bool mission1() const { return mission1_ && !mission2_active_ && !mission2_completed_ && placeholder_mission_ != 4 && !rescue_mission_; }
    int placeholder_mission() const { return placeholder_mission_; }
    bool data_chip_spotted() const { return data_chip_spotted_; }
    bool data_chip_collected() const { return data_chip_collected_; }
    bool near_data_chip() const;
    bool mission4_quiz_started() const { return mission4_quiz_started_; }
    bool mission4_quiz_reading() const { return mission4_quiz_reading_; }
    bool mission4_quiz_answered() const { return mission4_quiz_answered_; }
    bool mission4_quiz_wrong() const { return mission4_quiz_wrong_; }
    bool mission4_quiz_rereading() const { return mission4_quiz_rereading_; }
    int mission4_question() const { return mission4_question_; }
    int mission4_selected_answer() const { return mission4_selected_answer_; }
    bool mission4_quiz_complete() const { return mission4_quiz_complete_; }
    bool mission5_transition_requested() const { return mission5_transition_requested_; }
    ActionResult start_mission4_quiz();
    void mission4_read_passage();
    bool mission4_answer(int choice);
    void mission4_advance_quiz();
    struct Supply { Vec2 position; bool collected = false; };
    const std::array<Supply,5>& supplies() const { return supplies_; }
    int supply_count() const;
    bool mission1_complete() const { return campaign() && delivered_ && observation_count()==3; }
    bool mission2() const { return campaign() && mission2_active_ && !mission2_completed_; }
    bool mission2_complete() const { return mission2_completed_; }
    bool carrier_scanned() const { return carrier_scanned_; }
    bool carrier_wheel_collected() const { return carrier_wheel_collected_; }
    bool carrier_arm_collected() const { return carrier_arm_collected_; }
    bool carrier_chip_collected() const { return carrier_chip_collected_; }
    bool carrier_repaired() const { return carrier_repaired_; }
    bool carrier_repairing() const { return carrier_repairing_; }
    double carrier_repair_progress() const { return carrier_repair_time_/ShipInterior::carrier_repair_seconds; }
    bool near_carrier_robot() const;
    bool near_carrier_wheel() const;
    bool near_carrier_arm() const;
    bool near_carrier_chip() const;
    bool mission3_complete() const { return campaign() && ship_scanned_; }
    bool records_copied() const { return records_copied_; }
    bool usb_obtained() const { return records_copied_ && unit_->inventory().count(Item::USBDrive)>0; }
    bool usb_scan_recorded() const { return ship_scanned_ && usb_obtained(); }
    bool usb_record_uploaded() const { return usb_record_uploaded_; }
    bool scanning_spaceship() const { return scanning_spaceship_; }
    double spaceship_scan_progress() const { return spaceship_scan_time_/4.0; }
    bool near_rescue_spaceship() const;
    bool continue_mission();
    int mission_number() const { return rescue_mission_ ? rescue_mission_ : placeholder_mission_ == 4 ? 4 : !campaign() ? 0 : mission2() ? 2 : mission2_complete() ? 3 : 1; }
    static constexpr double records_copy_seconds=4.0;
    bool near_records_pc() const;
    bool copying_records() const { return copying_records_; }
    double records_copy_progress() const { return records_copy_time_/records_copy_seconds; }
    ActionResult start_copying_records();
    static constexpr Item mission_supply_item = Item::FirstAidKit;
    static constexpr Item supply_item(int index) { return index < 3 ? Item::FirstAidKit : Item::FoodWaterSupply; }
    static constexpr std::array<Item,3> sample_items{{Item::SilicateSample,Item::IceSample,Item::RockSample}};
    static constexpr std::array<const char*,3> sample_names{{"SILICATE","WATER ICE","ROCK FRAGMENTS"}};
    int observation_count() const { return int(observed_[0])+int(observed_[1])+int(observed_[2]); }
    bool sample_obtained(int i) const { return i>=0 && i<3 && obtained_[i]; }
    bool sample_observed(int i) const { return i>=0 && i<3 && observed_[i]; }
    bool indoor_food_collected(int i) const { return i>=0 && i<2 && indoor_food_collected_[i]; }
    int observing() const { return observing_; }
    double observation_progress() const { return observation_time_/Mission1::observation_seconds; }
    int observation_result() const { return observation_result_; }
    void close_observation_result();
    bool review_observation(int sample) {
        if(sample<0 || sample>=int(observed_.size()) || !observed_[sample] || observation_result_>=0) return false;
        observation_result_=sample; return true;
    }
    bool near_observed_table() const { return mission1() && inside_base() && unit_->near(observation_table_position(),2.3); }
    ActionResult start_observation(int sample);
    std::string interaction_label() const;
    ActionResult interact();
    bool near_ship_door(ShipDoor door) const;
    ActionResult open_ship_door(ShipDoor door);
    ActionResult enter_nearby_room();
    bool ship_door_open(ShipDoor door) const { return room_doors_[static_cast<int>(door)].open; }
    bool ship_door_opening(ShipDoor door) const { const auto& d=room_doors_[static_cast<int>(door)];return d.requested && !d.open; }
    double ship_door_phase(ShipDoor door) const { return room_doors_[static_cast<int>(door)].phase; }
    bool near_cargo_door() const { return near_ship_door(ShipDoor::Cargo); }
    ActionResult enter_cargo_hangar() { return open_ship_door(ShipDoor::Cargo); }
    bool cargo_door_open() const { return ship_door_open(ShipDoor::Cargo); }
    bool cargo_door_opening() const { return ship_door_opening(ShipDoor::Cargo); }
    double cargo_door_phase() const { return ship_door_phase(ShipDoor::Cargo); }
    static constexpr Vec2 astronaut_position = Mission1::astronaut;
    explicit Game(WorldGrid world, GameOptions options = {});
    const WorldGrid& world() const { return *world_; }
    const Unit7Entity& unit() const { return *unit_; }
    const NASA_ISRU_Refinery& refinery() const { return *refinery_; }
    const RocketRepairSystem& rocket() const { return *rocket_; }
    const ClimaxEngine& climax() const { return *climax_; }
    const std::string& last_message() const { return last_message_; }
    double simulated_seconds() const { return simulated_seconds_; }
    const Presentation& presentation() const { return presentation_; }
    std::array<MissionTask,6> tasks() const;
    const char* chapter_title() const;
    // A bounded accumulator provides the same 60 Hz simulation for either renderer.
    // Single frame deltas over 0.25 s are capped to avoid a spiral after window stalls.
    void advance(const InputState& input, double frame_seconds);
    void set_paused(bool value) { paused_ = value; if (value) { accumulator_ = 0; pending_jump_ = false; } }
    bool paused() const { return paused_; }
    ActionResult perform(Action action);
    ActionResult place_tile(GridPos position);
    bool finished() const;
    bool inside_base() const { return in_spaceship_ || (!campaign() && world_->planar() && BaseCamp::inside(unit_->center())); }
    const PhobosClock& phobos() const { return phobos_; }
    const DangerSystem& danger() const { return danger_; }
    bool survival_enabled() const { return survival_enabled_; }
    bool charging() const { return charging_; }
    bool at_charger() const;
    double attack_flash() const { return attack_flash_; }
    double last_bite_loss() const { return last_bite_loss_; }
    double battery_hit_from() const { return battery_hit_from_; }
    const std::optional<CombatBeam>& combat_beam() const { return combat_beam_; }
    static constexpr double combat_battery_cost = 0.6, combat_interval = 0.3;
    static double action_battery_cost(Action action);
private:
    bool scanning_spaceship_=false,ship_scanned_=false;
    bool usb_record_uploaded_=false;
    double spaceship_scan_time_=0;
    void update_spaceship_scan(double dt);
    int rescue_mission_=0;
    double carrier_battery_=100, rescue_ship_battery_=0, unit_transfer_battery_=0;
    RescueStage rescue_stage_=RescueStage::Waiting;
    std::array<bool,3> repair_materials_{{false,false,false}};
    Vec2 escort_carrier_=ShipInterior::carrier_robot;
    Vec2 escort_velocity_{};
    double escort_heading_=0, escort_wheel_phase_=0;
    bool escort_clear_route(Vec2 from,Vec2 to) const;
    void update_escort_follow(double dt);
    double rescue_stage_time_=0, escort_repath_time_=0;
    std::vector<Vec2> escort_path_, cinematic_unit_path_;
    void begin_rescue_material_mission();
    void update_rescue(double dt);
    std::vector<Vec2> rescue_path(Vec2 from,Vec2 to) const;
    bool move_rescue_actor(Vec2& position,std::vector<Vec2>& path,double amount);
    struct RoomDoorState { bool open=false,requested=false;double phase=0; };
    std::array<RoomDoorState,2> room_doors_{};
    void update_room_doors(double dt);
    bool in_spaceship_=false, transfer_swapped_=false;
    bool storage_door_open_=false;
    double storage_door_phase_=0;
    double transition_time_=0;
    std::unique_ptr<WorldGrid> other_map_;
    void begin_transfer();
    void update_transfer(double dt);
    bool mission1_ = false, delivered_ = false;
    int placeholder_mission_ = 0;
    bool mission2_active_=false, mission2_completed_=false;
    bool data_chip_spotted_=false, data_chip_collected_=false;
    bool mission4_quiz_started_=false, mission4_quiz_reading_=false, mission4_quiz_answered_=false,
         mission4_quiz_wrong_=false, mission4_quiz_complete_=false, mission4_quiz_rereading_=false;
    bool mission5_transition_requested_=false;
    int mission4_question_=0, mission4_selected_answer_=-1;
    bool carrier_scanned_=false, carrier_wheel_collected_=false, carrier_arm_collected_=false,
         carrier_chip_collected_=false, carrier_repairing_=false, carrier_repaired_=false;
    double carrier_repair_time_=0;
    void update_carrier_repair(double dt);
    void begin_carrier_mission_if_ready();
    void on_mission2_completed();
    bool copying_records_=false,records_copied_=false;
    double records_copy_time_=0;
    void update_record_copy(double dt);
    std::array<bool,3> obtained_{{false,false,false}}, observed_{{false,false,false}};
    int observing_=-1, observation_result_=-1;
    double observation_time_=0;
    std::array<Supply,5> supplies_{{{Mission1::supplements[0],false},{Mission1::supplements[1],false},{Mission1::supplements[2],false},{{-100,-100},false},{{-100,-100},false}}};
    std::array<bool,2> indoor_food_collected_{{false,false}};
    bool paused_ = false;
    void fixed_update(const InputState& input);
    void battery_feedback(double previous);
    std::unique_ptr<WorldGrid> world_;
    std::unique_ptr<Unit7Entity> unit_;
    std::unique_ptr<NASA_ISRU_Refinery> refinery_;
    std::unique_ptr<RocketRepairSystem> rocket_;
    std::unique_ptr<ClimaxEngine> climax_;
    double accumulator_ = 0, dust_accumulator_ = 0, simulated_seconds_ = 0;
    bool pending_jump_ = false;
    Presentation presentation_;
    PhobosClock phobos_;
    DangerSystem danger_;
    bool survival_enabled_ = true, charging_ = false;
    double attack_flash_ = 0;
    double last_bite_loss_ = 0, battery_hit_from_ = 0, combat_cooldown_ = 0;
    std::optional<CombatBeam> combat_beam_;
    std::int64_t ore_mined_ = 0, dust_mined_ = 0, plates_crafted_ = 0, fuel_crafted_ = 0;
    bool core_recovered_ = false;
    std::string last_message_ = "Unit-01 online. Mine resources; return Earth's last data core.";
};
using DemoObserver = std::function<void(const Game&,const char* stage)>;
bool run_mission_demo(std::ostream& output, const DemoObserver& observe = {});
bool run_isometric_demo(std::ostream& output, const DemoObserver& observe = {});
}

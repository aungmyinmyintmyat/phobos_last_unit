#include "horizon/Game.hpp"
#include "horizon/PlanetTerrain.hpp"
#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <random>

namespace horizon {
namespace {
std::uint32_t hazard_seed(std::uint32_t requested) {
    if (requested) return requested;
    try { std::random_device entropy; return entropy(); }
    catch (const std::exception&) { return std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()); }
}
}
Game::Game() : Game(WorldGrid::generate_isometric(),[] { GameOptions o; o.mission1=true; return o; }()) {}
Game::Game(WorldGrid world, GameOptions options) : world_(std::make_unique<WorldGrid>(std::move(world))),
    phobos_(options.starting_solar_phase), danger_(hazard_seed(options.danger_seed),options.danger),
    survival_enabled_(options.survival_enabled && world_->planar()) {
    if (!world_->planar() && (world_->width() < 64 || world_->surface_y() < 20)) throw std::invalid_argument("mission world too small");
    if (!std::isfinite(options.initial_battery) || options.initial_battery <= 0 || options.initial_battery > 100)
        throw std::invalid_argument("initial battery must be greater than zero and at most 100");
    const double surface = world_->surface_y();
    unit_ = std::make_unique<Unit7Entity>(world_->planar() ? Vec2{10.15,10.075} : Vec2{12,surface-0.85});
    refinery_ = std::make_unique<NASA_ISRU_Refinery>(world_->planar() ? BaseCamp::craft_bench : Vec2{10.5,surface-0.5});
    if (world_->planar()) refinery_->set_indoor_only();
    rocket_ = std::make_unique<RocketRepairSystem>(world_->planar() ? Vec2{world_->width()-13.5,12.5} : Vec2{world_->width()-19.5,surface-0.5},
                                                 world_->planar() ? Vec2{world_->width()-8.5,14.5} : Vec2{world_->width()-21.5,surface-17.0});
    climax_ = std::make_unique<ClimaxEngine>();
    if (options.placeholder_mission != 0 && (options.placeholder_mission < 2 || options.placeholder_mission > 6 || options.placeholder_mission == 5))
        throw std::invalid_argument("standalone mission must be Mission 2, 3, 4 or 6");
    mission2_active_ = !options.mission1 && options.placeholder_mission == 2 && world_->planar();
    placeholder_mission_ = options.mission1 || mission2_active_ || options.start_mission3 ? 0 : options.placeholder_mission;
    mission2_completed_ = options.start_mission3 && world_->planar();
    mission1_ = (options.mission1 || mission2_active_ || mission2_completed_ || (placeholder_mission_ == 4 || placeholder_mission_ == 6)) && world_->planar();
    if (mission1_ || placeholder_mission_ == 3 || placeholder_mission_ == 4) world_->setup_mission1();
    if (placeholder_mission_ == 4 || placeholder_mission_ == 6 || mission2_completed_) world_->setup_mission4();
    if (mission2_active_) last_message_="MISSION 2 / SCAN THE CARRIER ROBOT IN THE CARGO HANGAR";
    else if (mission_number()==3) last_message_="MISSION 3 / COLLECT THE USB AND S-01 COORDINATES AT THE MONITOR ROOM PC";
    else if (placeholder_mission_ == 4) last_message_="MISSION 4 / RECOVER THE MARS DATA CHIP";
    else if (placeholder_mission_ == 6) last_message_="MISSION 6 / GATHER S-01 REPAIR MATERIALS";
    else if (placeholder_mission_) last_message_="MISSION "+std::to_string(placeholder_mission_)+" / TEST ENTRY / NO TASKS IMPLEMENTED";
    else if (mission1_) last_message_="MISSION 1 / ACT 1 / FIRST-AID KIT RECOVERY";
    // Crashed lander's salvage cache supplies manufactured materials unavailable
    // from raw soil. This is the ordinary starting loadout, not a hidden craft reward.
    if (!mission1_) unit_->inventory().add(Item::ScrapAlloys,2000);
    if (!mission1_) unit_->inventory().add(Item::Polyethylene,1000);
    unit_->drain_battery(100-options.initial_battery);
    if(placeholder_mission_==6) begin_rescue_material_mission();
}
double Game::action_battery_cost(Action action) {
    switch (action) {
        case Action::Extract: return 4;
        case Action::MeltIce: return 2;
        case Action::Electrolyze: return 5;
        case Action::Liquefy: return 6;
        case Action::PackageFuel: return 1;
        case Action::Smelt: return 2;
        case Action::UpgradeDrill: case Action::UpgradeTreads: case Action::UpgradeShield: return 1.5;
        case Action::WeldHull: return 2.5;
        case Action::HookupLines: return 1.5;
        case Action::SocketCore: return 1;
        case Action::Calibrate: return 0.5;
        case Action::ActivateValve: return 0;
    }
    return 0;
}
bool Game::at_charger() const {
    const auto velocity = unit_->velocity();
    return world_->planar() && inside_base() && unit_->alive() && unit_->grounded() &&
        distance(unit_->center(),in_spaceship_ ? ShipInterior::charger : BaseCamp::charger) <= 0.65 && std::hypot(velocity.x,velocity.y) < 0.12;
}
double Game::transition_alpha() const {
    if(transition_time_<=0) return 0;
    const double elapsed=1.2-transition_time_;
    return std::clamp(elapsed<0.45 ? elapsed/0.45 : elapsed<0.75 ? 1.0 : transition_time_/0.45,0.0,1.0);
}
void Game::begin_transfer() {
    if(transitioning() || !unit_->alive() || paused_) return;
    transition_time_=1.2; transfer_swapped_=false; pending_jump_=false;
    combat_beam_.reset(); charging_=false;
    presentation_.sound(SoundCue::Interface);
}
void Game::update_transfer(double dt) {
    transition_time_=std::max(0.0,transition_time_-dt);
    if(!transfer_swapped_ && transition_time_<=0.6) {
        if(!other_map_) other_map_=std::make_unique<WorldGrid>(WorldGrid::spaceship_interior());
        world_.swap(other_map_); in_spaceship_=!in_spaceship_;
        if(in_spaceship_) refinery_->set_secondary_bench(ShipInterior::craft_table,false);
        else refinery_->clear_secondary_bench();
        unit_->transfer_to(in_spaceship_ ? ShipInterior::entry : BaseCamp::approach);
        unit_->update(dt,*world_);
        presentation_=Presentation{};
        last_message_=rescue_mission_ ? chapter_title() : in_spaceship_ ? "Map2 / RESCUE SHIP / EXPLORE THE CONNECTED ROOMS" : "Map1 / SURFACE";
        if(rescue_mission_==7 && !in_spaceship_) {
            escort_carrier_={BaseCamp::approach.x+2,BaseCamp::approach.y+1};
            escort_path_.clear();unit_->recharge_battery(100);
        }
        transfer_swapped_=true;
    }
}
void Game::battery_feedback(double previous) {
    const double battery = unit_->battery_percent();
    if (battery <= 0) return;
    if ((previous > 20 && battery <= 20) || (previous > 10 && battery <= 10)) {
        presentation_.sound(SoundCue::LowBattery);
        presentation_.notify(battery <= 10 ? "CRITICAL BATTERY: return to the aircraft charger." : "LOW BATTERY: recharge inside the rescue aircraft.");
    }
}
void Game::advance(const InputState& input, double frame_seconds) {
    if (paused_ || mission4_quiz_started_ || !std::isfinite(frame_seconds) || frame_seconds <= 0) return;
    if (finished()) {
        if (climax_->state() == ClimaxState::Failed) {
            const double dt = std::min(0.25,frame_seconds);
            unit_->update_wreck(dt,*world_);
            presentation_.update(dt,unit_->position(),{},unit_->grounded(),false,world_->planar(),
                                 unit_->vertical_velocity(),unit_->battery_percent(),unit_->integrity(),
                                 unit_->elevation()+(world_->continuous_ground() ? 0 : PlanetTerrain::surface_height(*world_,unit_->center())-
                                 world_->elevation(int(unit_->center().x),int(unit_->center().y))),unit_->fell_into_pit());
        }
        return;
    }
    pending_jump_ = pending_jump_ || input.jump;
    accumulator_ += std::min(0.25,frame_seconds);
    constexpr double fixed_dt = 1.0/60.0;
    while (accumulator_+1e-10 >= fixed_dt) {
        fixed_update(input);
        accumulator_ = std::max(0.0,accumulator_-fixed_dt);
        if (finished()) { accumulator_ = 0; break; }
    }
}
void Game::fixed_update(const InputState& requested_input) {
    const InputState input=rescue_cinematic() ? InputState{} : requested_input;
    constexpr double dt = 1.0/60.0;
    if(transitioning()) { update_transfer(dt); return; }
    // The deliberate final power transfer is a successful rescue, and must keep
    // animating after U-01 has no battery without triggering ordinary failure.
    if(rescue_power_sequence()) {
        pending_jump_=false;unit_->stop_moving();
        update_rescue(dt);
        if(!unit_->alive()) unit_->update_wreck(dt,*world_);
        presentation_.update(dt,unit_->position(),{},unit_->grounded(),unit_->alive(),true,
            unit_->vertical_velocity(),unit_->battery_percent(),unit_->integrity(),unit_->elevation());
        simulated_seconds_+=dt;
        return;
    }
    if(in_spaceship_) {
        const auto p=unit_->center(); const auto door=ShipInterior::storage_door;
        // Sense the whole opening from either side, including its end panels.
        // A wider release distance prevents chatter and keeps the threshold clear.
        const Vec2 nearest{std::clamp(p.x,double(door.x),double(door.x+door.w)),
                           std::clamp(p.y,double(door.y),double(door.y+door.h))};
        const bool open=distance(p,nearest)<=(storage_door_open_ ? 2.6 : 2.0);
        if(open!=storage_door_open_) {
            storage_door_open_=open;
            world_->set_ship_storage_door(open);
            presentation_.sound(SoundCue::Interface);
        }
    }
    const double door_target=storage_door_open_ ? 1.0 : 0.0;
    storage_door_phase_+=std::clamp(door_target-storage_door_phase_,-dt*3.5,dt*3.5);
    const double previous_battery = unit_->battery_percent();
    const double previous_integrity = unit_->integrity();
    const bool previously_charging = charging_;
    attack_flash_ = std::max(0.0,attack_flash_-dt);
    combat_cooldown_ = std::max(0.0,combat_cooldown_-dt);
    if (combat_beam_) {
        combat_beam_->age += dt;
        if (combat_beam_->age >= combat_beam_->duration) combat_beam_.reset();
    }
    phobos_.advance(dt);
    if(mission1() && observing_>=0 && unit_->alive()) {
        observation_time_=std::min(Mission1::observation_seconds,observation_time_+dt);
        if(observation_time_>=Mission1::observation_seconds-1e-9) {
            observation_time_=Mission1::observation_seconds;
            unit_->inventory().add(sample_items[observing_],1);
            observed_[observing_]=true; observation_result_=observing_; observing_=-1;
            last_message_=std::string(sample_names[observation_result_])+" OBSERVED / SAMPLE RETURNED";
            presentation_.sound(SoundCue::Pickup);
        }
    }
    if (world_->planar()) {
        const double x = int(input.right)-int(input.left), y = int(input.down)-int(input.up);
        if(rescue_cinematic()) pending_jump_=false;
        unit_->move_on_plane({x+y,y-x},pending_jump_); pending_jump_ = false;
    } else {
        if (input.left == input.right) unit_->stop_moving();
        else if (input.left) unit_->move_left(); else unit_->move_right();
        if (pending_jump_) { unit_->jump(); pending_jump_ = false; }
        unit_->set_climbing(input.climb);
    }
    unit_->update(dt,*world_);
    if (placeholder_mission_ == 4 && mission4_quiz_complete_ && in_spaceship_ &&
        ShipInterior::room_at(unit_->center()) != 3)
        mission5_transition_requested_ = true;
    if (placeholder_mission_ == 4 && !in_spaceship_ && !data_chip_spotted_ && unit_->alive() &&
        distance(unit_->center(),BaseCamp::data_bank) <= 12.0) {
        data_chip_spotted_ = true;
        last_message_ = "MARS EXPLORATION SIGNAL / DATA CHIP LOCATED";
        presentation_.notify(last_message_);
        presentation_.sound(SoundCue::Interface);
    }
    update_carrier_repair(dt);
    update_record_copy(dt);
    update_spaceship_scan(dt);
    update_rescue(dt);
    if(in_spaceship_) update_room_doors(dt);
    const bool can_enter_rescue_ship = !placeholder_mission_ || (placeholder_mission_ == 4 ? data_chip_collected_ : true);
    if(campaign() && !in_spaceship_ && can_enter_rescue_ship && distance(unit_->center(),BaseCamp::entrance)<0.65) {
        begin_transfer(); return;
    }
    if (!unit_->alive()) unit_->update_wreck(dt,*world_);
    presentation_.update(dt,unit_->position(),unit_->velocity(),unit_->grounded(),unit_->alive(),world_->planar(),
                         world_->planar() ? unit_->vertical_velocity() : -unit_->velocity().y,
                         unit_->battery_percent(),unit_->integrity(),world_->planar() ?
                         unit_->elevation()+(world_->continuous_ground() ? 0 : PlanetTerrain::surface_height(*world_,unit_->center())-
                         world_->elevation(int(unit_->center().x),int(unit_->center().y))) :
                         std::numeric_limits<double>::quiet_NaN(),unit_->fell_into_pit());
    if (unit_->fell_into_pit()) combat_beam_.reset();
    if(in_spaceship_) {
        charging_=at_charger();
        if(charging_) unit_->recharge_battery(4*dt);
        climax_->update(dt,*unit_,*world_,*rocket_);
        battery_feedback(previous_battery); simulated_seconds_+=dt;
        return;
    }
    if (survival_enabled_ && unit_->alive() && input.combat_target && combat_cooldown_ <= 1e-9 &&
        unit_->battery_percent() >= combat_battery_cost && !rocket_->launched()) {
        const auto origin = unit_->center();
        const auto strike = danger_.fire_laser(origin,*input.combat_target,*world_);
        if (strike.valid) {
            unit_->spend_battery(combat_battery_cost);
            combat_cooldown_ = combat_interval;
            combat_beam_ = CombatBeam{origin,strike.endpoint,0,0.15,strike.hit,strike.killed};
            presentation_.face_target(origin,*input.combat_target);
            presentation_.sound(SoundCue::CombatFire);
            if (strike.hit) presentation_.sound(SoundCue::CombatImpact);
            if (strike.killed) presentation_.notify("CREATURE NEUTRALIZED");
        }
    }
    // Mission 1 has no creature hunts.
    if (survival_enabled_ && !mission1() && placeholder_mission_ != 4 && unit_->alive() && !rocket_->launched()) {
        const auto danger = danger_.update(dt,*world_,unit_->center(),inside_base());
        if (danger.beep) presentation_.sound(SoundCue::Alarm);
        if (danger.warning_started) presentation_.notify("DANGER WARNING: return to the rescue aircraft before the hunt.");
        if (danger.active_started) presentation_.notify(inside_base() ? "HUNT ACTIVE: remain inside the aircraft." : "HUNT ACTIVE: reach the rescue aircraft!");
        if (danger.cleared) presentation_.notify("ALL CLEAR: the creatures have withdrawn.");
        if (danger.battery_damage > 0) {
            const double before_bite = unit_->battery_percent();
            unit_->drain_battery(danger.battery_damage);
            const double actual_loss = before_bite-unit_->battery_percent();
            if (attack_flash_ <= 0) last_bite_loss_ = 0;
            last_bite_loss_ += actual_loss;
            battery_hit_from_ = std::max(battery_hit_from_,before_bite);
            attack_flash_ = 0.6;
            presentation_.damage_flash = 0.4;
            presentation_.sound(SoundCue::BiteCrack);
        }
    }
    charging_ = at_charger();
    if (charging_) {
        unit_->recharge_battery(4*dt);
        if (!previously_charging) presentation_.sound(SoundCue::Charge);
    }
    if (input.mining_target && !input.combat_target) {
        const auto p = *input.mining_target;
        const auto type = world_->get_tile_type(p.x,p.y);
        const auto inventory_before = unit_->inventory();
        const auto result = unit_->use_mining_laser(p.x,p.y,*world_,dt);
        if (result.message != "This tile cannot be mined.") last_message_ = result.message;
        if (result.ok) {
            presentation_.face_target(unit_->center(),{p.x+0.5,p.y+0.5});
            const bool broke = world_->get_tile_type(p.x,p.y) == TileType::VacuumAir;
            presentation_.mining(p,broke ? 1.0 : world_->mining_progress(p.x,p.y),broke,tile_definition(type).color);
            const bool specimen = type==TileType::SupplyBarrier || type==TileType::SilicateBarrier || type==TileType::IceBarrier;
            if(mission1() && broke && specimen) {
                int sample=type==TileType::SilicateBarrier ? 0 : type==TileType::IceBarrier ? 1 : 2;
                if(!obtained_[sample] && unit_->inventory().add(sample_items[sample],1)) {
                    obtained_[sample]=true; last_message_=std::string(sample_names[sample])+" SAMPLE COLLECTED";
                    presentation_.activity({p.x+0.5,p.y+0.5},last_message_,false,sample_items[sample],1);
                    presentation_.notify(last_message_+" / TAKE TO OBSERVED TABLE");
                }
            }
            if (broke && !specimen) {
                if (type == TileType::IronNickelOre) ore_mined_ += 1000;
                if (type == TileType::Regolith) dust_mined_ += 1000;
                if (type == TileType::NASADataCore) core_recovered_ = true;
                const char* pickup = type == TileType::IronNickelOre ? "ORE +1 KG" :
                    type == TileType::Regolith ? "DUST +1 KG" :
                    type == TileType::HydratedMinerals ? "MINERALS +20 KG" :
                    type == TileType::NASADataCore ? "NASA DATA CORE RECOVERED" : "RESOURCE RECOVERED";
                Item recovered = Item::Count;
                std::int64_t amount = 0;
                for (auto stack : unit_->inventory().stacks()) {
                    if (stack.quantity > inventory_before.count(stack.item)) {
                        recovered = stack.item; amount = stack.quantity-inventory_before.count(stack.item); break;
                    }
                }
                presentation_.activity({p.x+0.5,p.y+0.5},pickup,false,recovered,amount);
            }
        }
    }
    dust_accumulator_ += dt;
    if (dust_accumulator_+1e-10 >= 0.125) {
        world_->apply_gravity_to_loose_regolith(unit_->body());
        dust_accumulator_ -= 0.125;
    }
    const auto before = climax_->state();
    climax_->update(dt,*unit_,*world_,*rocket_);
    if (!unit_->fell_into_pit() && unit_->integrity() < previous_integrity-0.01 && presentation_.damage_flash <= 0)
        presentation_.damage(unit_->position(),previous_integrity-unit_->integrity());
    if (before != climax_->state()) last_message_ = unit_->battery_percent() <= 0 ? "BATTERY EMPTY. Unit-01 shut down. Mission failed." : state_name(climax_->state());
    if (unit_->fell_into_pit()) last_message_ = "FELL INTO PIT. Unit-01 lost below the surface. Mission failed.";
    battery_feedback(previous_battery);
    simulated_seconds_ += dt;
}
ActionResult Game::perform(Action action) {
    if (mission1_) return ActionResult::failure("Crafting and rocket repair are unavailable during Mission 1.");
    if (paused_ || mission4_quiz_started_) return ActionResult::failure("Mission paused or terminal active.");
    if (!unit_->alive() || finished()) return ActionResult::failure("Unit-01 is offline.");
    const double battery_cost = action_battery_cost(action), previous_battery = unit_->battery_percent();
    if (previous_battery < battery_cost) {
        last_message_ = "Not enough battery for this task. Recharge inside the rescue aircraft.";
        presentation_.notify(last_message_);
        return ActionResult::failure(last_message_);
    }
    ActionResult result{false,"Unknown action."};
    const auto inventory_before = unit_->inventory();
    switch (action) {
        case Action::Extract: result = refinery_->thermal_extraction(*unit_); break;
        case Action::MeltIce: result = refinery_->melt_ice(*unit_); break;
        case Action::Electrolyze: result = refinery_->electrolysis(*unit_); break;
        case Action::Liquefy: result = refinery_->liquefy_propellant(*unit_); break;
        case Action::PackageFuel: result = refinery_->package_refined_fuel(*unit_); break;
        case Action::Smelt: result = refinery_->alloy_smelting(*unit_); break;
        case Action::UpgradeDrill: result = refinery_->craft_upgrade(*unit_,Upgrade::SolarThermalDrill); break;
        case Action::UpgradeTreads: result = refinery_->craft_upgrade(*unit_,Upgrade::DustRepellentTreads); break;
        case Action::UpgradeShield: result = refinery_->craft_upgrade(*unit_,Upgrade::RadiationShielding); break;
        case Action::WeldHull: result = rocket_->weld_hull(*unit_); break;
        case Action::HookupLines: result = rocket_->hookup_fuel_lines(*unit_); break;
        case Action::SocketCore: result = rocket_->socket_data_core(*unit_); break;
        case Action::Calibrate: result = rocket_->calibrate_launch_control_valve(*unit_); break;
        case Action::ActivateValve: result = climax_->activate_launch_valve(*unit_,*rocket_); break;
    }
    last_message_ = result.message;
    if (!result.ok) presentation_.notify(result.message); else presentation_.notice.reset();
    if (result.ok) {
        if (battery_cost > 0) unit_->spend_battery(battery_cost);
        const bool crafting = action <= Action::UpgradeShield;
        static const char* labels[] = {"WATER EXTRACTED", "ICE PURIFIED", "GASES SEPARATED", "CRYOGENICS READY",
            "FUEL KIT +1", "STRUCTURAL PLATES +2", "SOLAR DRILL INSTALLED", "DUST TREADS INSTALLED",
            "RADIATION SHIELD FITTED", "HULL WELDED", "FUEL LINES CONNECTED", "DATA CORE SECURED",
            "LAUNCH VALVE ARMED", "EARTH RETURN COMMITTED"};
        Item output = Item::Count;
        std::int64_t amount = 0;
        for (auto stack : unit_->inventory().stacks()) {
            if (stack.item != Item::Slag && stack.quantity > inventory_before.count(stack.item)) {
                output = stack.item; amount = stack.quantity-inventory_before.count(stack.item); break;
            }
        }
        presentation_.activity(crafting ? refinery_->operating_position(*unit_) : rocket_->ground_socket(),
                               labels[static_cast<std::size_t>(action)],crafting,output,amount);
        if (action == Action::Smelt) plates_crafted_ += 2;
        if (action == Action::PackageFuel) ++fuel_crafted_;
        if (action == Action::ActivateValve) presentation_.sound(SoundCue::Launch);
    }
    battery_feedback(previous_battery);
    if (!unit_->alive() && !rocket_->launched()) {
        climax_->update(1.0/60,*unit_,*world_,*rocket_);
        if (unit_->battery_percent() <= 0) last_message_ = "BATTERY EMPTY. Unit-01 shut down. Mission failed.";
    }
    return result;
}
ActionResult Game::place_tile(GridPos position) {
    if (mission1_) return ActionResult::failure("Placement is unavailable during Mission 1.");
    if (paused_ || mission4_quiz_started_) return ActionResult::failure("Mission paused or terminal active.");
    auto result = unit_->place_tile(position.x,position.y,*world_);
    last_message_ = result.message;
    if (!result.ok) presentation_.notify(result.message);
    if (result.ok) presentation_.placement(position);
    if (!unit_->alive() && !rocket_->launched()) {
        climax_->update(1.0/60,*unit_,*world_,*rocket_);
        if (unit_->battery_percent() <= 0) last_message_ = "BATTERY EMPTY. Unit-01 shut down. Mission failed.";
    }
    return result;
}
bool Game::near_records_pc() const {
    return in_spaceship_ && !transitioning() && unit_->alive() && unit_->grounded() &&
        ShipInterior::room_at(unit_->center())==3 && unit_->near(ShipInterior::records_pc,1.8) &&
        world_->line_of_sight(unit_->center(),{int(ShipInterior::records_pc.x),int(ShipInterior::records_pc.y)});
}
void Game::close_observation_result() {
    observation_result_=-1;
    begin_carrier_mission_if_ready();
}
void Game::begin_carrier_mission_if_ready() {
    if(!mission1() || !mission1_complete() || observation_result_>=0 || !unit_->alive()) return;
    mission2_active_=true;
    last_message_="MISSION 2 / SCAN THE CARRIER ROBOT IN THE CARGO HANGAR";
    presentation_.notify(last_message_);presentation_.sound(SoundCue::Interface);
}
bool Game::near_carrier_robot() const {
    return mission2() && in_spaceship_ && !transitioning() && unit_->alive() && unit_->grounded() &&
        ShipInterior::room_at(unit_->center())==2 && unit_->near(ShipInterior::carrier_robot,2.3);
}
bool Game::near_data_chip() const {
    return placeholder_mission_ == 4 && data_chip_spotted_ && !data_chip_collected_ && !in_spaceship_ &&
        !transitioning() && unit_->alive() && unit_->grounded() && unit_->near(BaseCamp::data_bank,1.5) &&
        world_->line_of_sight(unit_->center(),{int(BaseCamp::data_bank.x),int(BaseCamp::data_bank.y)});
}
ActionResult Game::start_mission4_quiz() {
    if(placeholder_mission_!=4 || !data_chip_collected_ || unit_->inventory().count(Item::MarsDataChip)<1 || (ship_scanned_ && !usb_obtained()) || mission4_quiz_started_ ||
       mission4_quiz_complete_ || paused_ || !near_records_pc())
        return ActionResult::failure("Bring the data chip to the Monitor Room PC and press E.");
    mission4_quiz_started_=true;
    mission4_quiz_reading_=true;
    mission4_quiz_rereading_=false;
    mission4_question_=0;
    mission4_selected_answer_=-1;
    mission4_quiz_answered_=false;
    mission4_quiz_wrong_=false;
    usb_record_uploaded_=usb_scan_recorded();
    last_message_=usb_record_uploaded_ ? "USB SCAN RECORD UPLOADED / MARS CHIP SCANNED / READ THE BRIEFING" :
        "MARS DATA CHIP SCANNED / READ THE MARS EXPLORATION BRIEFING";
    presentation_.sound(SoundCue::Interface);
    return ActionResult::success(last_message_);
}
void Game::mission4_read_passage() {
    if(paused_ || !mission4_quiz_started_ || mission4_quiz_complete_) return;
    if(mission4_quiz_reading_) {
        mission4_quiz_reading_=false;
        mission4_quiz_rereading_=false;
    } else {
        mission4_quiz_reading_=true;
        mission4_quiz_rereading_=true;
    }
}
bool Game::mission4_answer(int choice) {
    if(paused_ || !mission4_quiz_started_ || mission4_quiz_reading_ || mission4_quiz_answered_ ||
       choice<0 || choice>=4 || mission4_question_<0 || mission4_question_>=Mission4::question_count) return false;
    mission4_selected_answer_=choice;
    mission4_quiz_wrong_=choice!=Mission4::questions[mission4_question_].correct;
    mission4_quiz_answered_=!mission4_quiz_wrong_;
    if(mission4_quiz_wrong_) last_message_="NOT QUITE / REREAD THE PASSAGE OR CHOOSE AGAIN";
    else last_message_="CORRECT / CONTINUE";
    return mission4_quiz_answered_;
}
void Game::mission4_advance_quiz() {
    if(paused_ || !mission4_quiz_started_ || !mission4_quiz_answered_ || mission4_quiz_reading_) return;
    const int previous_passage=Mission4::question_passages[mission4_question_];
    if(++mission4_question_>=Mission4::question_count) {
        // These quest items are used up only when the last correct answer is
        // confirmed. Standalone Mission 4 can start without a Mission 3 USB.
        unit_->inventory().consume(Item::USBDrive,1);
        unit_->inventory().consume(Item::MarsDataChip,1);
        mission4_quiz_complete_=true;
        mission4_quiz_started_=false;
        last_message_="MISSION 4 COMPLETE / MARS DATA VERIFIED / MISSION 6 AVAILABLE";
        presentation_.notify(last_message_);presentation_.sound(SoundCue::Pickup);
        return;
    }
    mission4_quiz_answered_=false;
    mission4_quiz_wrong_=false;
    mission4_selected_answer_=-1;
    if(Mission4::question_passages[mission4_question_]!=previous_passage) {
        mission4_quiz_reading_=true;
        mission4_quiz_rereading_=false;
    }
}
bool Game::near_carrier_wheel() const {
    return mission2() && carrier_scanned_ && in_spaceship_ && !transitioning() && unit_->alive() &&
        ShipInterior::room_at(unit_->center())==2 && unit_->near(ShipInterior::carrier_wheel,1.6);
}
bool Game::near_carrier_arm() const {
    return mission2() && carrier_scanned_ && in_spaceship_ && !transitioning() && unit_->alive() &&
        ShipInterior::room_at(unit_->center())==2 && unit_->near(ShipInterior::carrier_arm,1.6);
}
bool Game::near_carrier_chip() const {
    return mission2() && carrier_scanned_ && in_spaceship_ && !transitioning() && unit_->alive() &&
        ShipInterior::room_at(unit_->center())==1 && unit_->near(ShipInterior::carrier_chip,1.6);
}
void Game::update_carrier_repair(double dt) {
    if(!carrier_repairing_) return;
    if(!near_carrier_robot()) {
        carrier_repairing_=false;carrier_repair_time_=0;
        last_message_="REPAIR PAUSED / RETURN TO THE CARRIER ROBOT";
        presentation_.notify(last_message_);return;
    }
    carrier_repair_time_=std::min(ShipInterior::carrier_repair_seconds,carrier_repair_time_+dt);
    if(carrier_repair_time_>=ShipInterior::carrier_repair_seconds-1e-9) {
        carrier_repair_time_=ShipInterior::carrier_repair_seconds;
        carrier_repairing_=false;carrier_repaired_=true;
        presentation_.sound(SoundCue::Pickup);
        on_mission2_completed();
    }
}
void Game::on_mission2_completed() {
    if(!mission2() || !carrier_repaired_ || mission2_completed_ || !unit_->alive() ||
       climax_->state()==ClimaxState::Failed) return;
    mission2_completed_=true;
    (in_spaceship_ ? other_map_.get() : world_.get())->setup_mission4();
    last_message_="MISSION 3 / COLLECT THE USB AND S-01 COORDINATES AT THE MONITOR ROOM PC";
    presentation_.notify(last_message_);presentation_.sound(SoundCue::Interface);
}
ActionResult Game::start_copying_records() {
    if(paused_ || finished() || mission_number()!=3 || !mission2_complete() || observation_result_>=0 ||
       copying_records_ || records_copied_ || !near_records_pc())
        return ActionResult::failure("Complete Mission 2, approach the Monitor Room PC and press E.");
    copying_records_=true;records_copy_time_=0;
    last_message_="PREPARING USB / DOWNLOADING S-01 COORDINATES / 4 SECONDS";
    presentation_.sound(SoundCue::Interface);
    return ActionResult::success(last_message_);
}
void Game::update_record_copy(double dt) {
    if(!copying_records_) return;
    if(!near_records_pc()) {
        copying_records_=false;records_copy_time_=0;
        last_message_="USB PREPARATION CANCELLED / RETURN TO THE PC AND PRESS E";
        return;
    }
    records_copy_time_=std::min(records_copy_seconds,records_copy_time_+dt);
    if(records_copy_time_>=records_copy_seconds-1e-9) {
        if(!unit_->inventory().add(Item::USBDrive,1)) {
            copying_records_=false;records_copy_time_=0;
            last_message_="CANNOT COLLECT USB / INVENTORY FULL";presentation_.notify(last_message_);return;
        }
        records_copy_time_=records_copy_seconds;copying_records_=false;records_copied_=true;
        last_message_="USB COLLECTED / S-01 COORDINATES (119,60) RECEIVED / TAKE USB TO S-01";
        presentation_.notify(last_message_);
        presentation_.sound(SoundCue::Pickup);
    }
}
ActionResult Game::start_observation(int sample) {
    if(paused_ || finished() || !unit_->alive() || !near_observed_table() || observing_>=0 || observation_result_>=0 || sample<0 || sample>=3 || !obtained_[sample] || observed_[sample])
        return ActionResult::failure("Choose one unobserved sample at the Observed Table.");
    if(!unit_->inventory().consume(sample_items[sample],1)) return ActionResult::failure("Sample unavailable.");
    observing_=sample; observation_time_=0; presentation_.sound(SoundCue::Pickup);
    last_message_=std::string("OBSERVING ")+sample_names[sample]+" / 6 SECONDS";
    return ActionResult::success(last_message_);
}
int Game::supply_count() const {
    int n=0;for(const auto& s:supplies_) n+=s.collected;
    for(bool collected:indoor_food_collected_) n+=collected;
    return n;
}
bool Game::near_ship_door(ShipDoor id) const {
    const auto p=unit_->center();const auto& plan=ShipInterior::doors[static_cast<int>(id)];const auto b=plan.bounds;
    return in_spaceship_ && !transitioning() && !paused_ && !mission4_quiz_started_ && unit_->alive() && !finished() && observation_result_<0 &&
        distance(p,{std::clamp(p.x,double(b.x),double(b.x+b.w)),
                    std::clamp(p.y,double(b.y),double(b.y+b.h))})<=2;
}
ActionResult Game::open_ship_door(ShipDoor id) {
    auto& state=room_doors_[static_cast<int>(id)];const auto& plan=ShipInterior::doors[static_cast<int>(id)];
    if(!near_ship_door(id) || state.requested || state.open)
        return ActionResult::failure("Approach the room door to open it automatically.");
    state.requested=true;
    presentation_.sound(SoundCue::Interface);last_message_=std::string(plan.name)+" / DOOR OPENING";
    return ActionResult::success(last_message_);
}
ActionResult Game::enter_nearby_room() {
    for(int i=0;i<int(ShipInterior::doors.size());++i) {
        const auto id=static_cast<ShipDoor>(i);if(near_ship_door(id)) return open_ship_door(id);
    }
    return ActionResult::failure("Approach a room door to open it automatically.");
}
void Game::update_room_doors(double dt) {
    for(int i=0;i<int(room_doors_.size());++i) {
        auto& state=room_doors_[i];const auto& plan=ShipInterior::doors[i];const auto id=static_cast<ShipDoor>(i);
        const auto p=unit_->center();const auto b=plan.bounds;
        const Vec2 nearest{std::clamp(p.x,double(b.x),double(b.x+b.w)),
                           std::clamp(p.y,double(b.y),double(b.y+b.h))};
        const Aabb threshold{{b.x-.1,b.y-.1},b.w+.2,b.h+.2};
        // Sense both sides and the full opening; a wider release range avoids chatter.
        const bool requested=(rescue_cinematic() && id==ShipDoor::Cargo) || distance(p,nearest)<=(state.requested ? 2.6 : 2.0) ||
                             overlaps(unit_->body(),threshold);
        if(requested!=state.requested) {
            state.requested=requested;presentation_.sound(SoundCue::Interface);
        }
        state.phase=std::clamp(state.phase+(state.requested ? dt/.3 : -dt/.3),0.0,1.0);
        const bool open=state.requested && state.phase>=1-1e-9;
        if(open!=state.open) {
            state.open=open;world_->set_ship_room_door(id,open);
        }
    }
}
std::string Game::interaction_label() const {
    if ((!mission1_ && placeholder_mission_ != 4) || paused_ || !unit_->alive() || finished() || observation_result_>=0) return {};
    if(transitioning() || rescue_cinematic() || mission4_quiz_started_) return {};
    if(rescue_mission_) {
        if(rescue_mission_==8 && rescue_stage_==RescueStage::AwaitingRepair && !in_spaceship_ &&
           unit_->grounded() && unit_->near(BaseCamp::damaged_shuttle,2.5)) return "REPAIR S-01";
        if(rescue_mission_==6 && in_spaceship_ && ShipInterior::room_at(unit_->center())==1) {
            for(int i=0;i<3;++i) if(!repair_materials_[i] && unit_->near(ShipInterior::rescue_materials[i],1.7))
                return std::string("COLLECT ")+ShipInterior::rescue_material_names[i];
        }
        if(rescue_mission_==7 && rescue_stage_==RescueStage::Waiting && in_spaceship_ &&
           ShipInterior::room_at(unit_->center())==2 && unit_->near(ShipInterior::carrier_robot,2.5)) return "TALK TO C-02";
        if(rescue_mission_==6 && !in_spaceship_ && unit_->near(BaseCamp::entrance,2)) return "ENTER SHIP";
        if(rescue_mission_==6 && in_spaceship_ && unit_->near(ShipInterior::exit,1.5)) return "EXIT SHIP";
        return {};
    }
    if(!in_spaceship_ && unit_->near(BaseCamp::entrance,2)) return "ENTER SHIP";
    if(in_spaceship_ && unit_->near(ShipInterior::exit,1.5)) return "EXIT SHIP";
    if (placeholder_mission_ == 4) {
        if(!in_spaceship_ && data_chip_collected_ && unit_->near(BaseCamp::entrance,2)) return "ENTER SHIP";
        if(!in_spaceship_) return near_data_chip() ? "COLLECT DATA CHIP" : std::string{};
        if(data_chip_collected_ && !mission4_quiz_complete_ && !mission4_quiz_started_ && near_records_pc()) return "SCAN DATA CHIP";
        return {};
    }
    if(mission_number()==3 && usb_obtained() && !ship_scanned_ && !scanning_spaceship_ && near_rescue_spaceship()) return "SCAN SPACESHIP";
    if(mission2()) {
        if(carrier_repairing_) return {};
        if(!carrier_scanned_) return near_carrier_robot() ? "SCAN CARRIER ROBOT" : std::string{};
        if(!carrier_wheel_collected_ && near_carrier_wheel()) return "COLLECT WHEEL";
        if(!carrier_arm_collected_ && near_carrier_arm()) return "COLLECT ROBOT ARM";
        if(!carrier_chip_collected_ && near_carrier_chip()) return "COLLECT CONTROL CHIP";
        if(carrier_wheel_collected_ && carrier_arm_collected_ && carrier_chip_collected_ && near_carrier_robot()) return "REPAIR CARRIER ROBOT";
        return {};
    }
    if(mission2_complete() && !records_copied_ && !copying_records_ && near_records_pc()) return "COLLECT USB";
    if(in_spaceship_) {
        const auto& food_positions=ShipInterior::food_supplies;
        for(int i=0;i<2;++i) if(!indoor_food_collected_[i] && unit_->near(food_positions[i],1.25)) return "COLLECT";
    }
    if(near_observed_table() && observing_<0 && observation_result_<0) for(int i=0;i<3;++i) if(obtained_[i]&&!observed_[i]) return "OBSERVE";
    if (!delivered_ && supply_count()==5 && inside_base() && unit_->near(survivor_position(),2.4)) return "DELIVER";
    if(in_spaceship_) return {};
    for(int i=0;i<3;++i) { const auto& s=supplies_[i]; if(!s.collected && unit_->near(s.position,1.1) &&
        world_->line_of_sight(unit_->center(),{int(s.position.x),int(s.position.y)})) return "COLLECT"; }
    return {};
}
ActionResult Game::interact() {
    const auto label=interaction_label();
    if(label.empty()) return ActionResult::failure("Approach a supply box, the astronaut or the Observed Table.");
    if(label=="ENTER SHIP" || label=="EXIT SHIP") { begin_transfer(); return ActionResult::success("AIRLOCK / TRANSFERRING MAP"); }
    if(label=="REPAIR S-01") {
        const auto& inventory=unit_->inventory();
        if(inventory.count(Item::StructuralPlates)<4 || inventory.count(Item::ScrapAlloys)<1000 || inventory.count(Item::Polyethylene)<500) {
            last_message_="REPAIR NEEDS THE PLATES, WIRING AND SEALANT FROM MISSION 6";
            presentation_.notify(last_message_);return ActionResult::failure(last_message_);
        }
        rescue_stage_=RescueStage::RepairingShip;rescue_stage_time_=0;
        last_message_=rescue_dialogue();presentation_.sound(SoundCue::Interface);return ActionResult::success(last_message_);
    }
    if(label=="SCAN SPACESHIP") {
        scanning_spaceship_=true;spaceship_scan_time_=0;
        last_message_="SCANNING THE SPACESHIP / STAY CLOSE / 4 SECONDS";
        presentation_.sound(SoundCue::Interface);return ActionResult::success(last_message_);
    }
    if(rescue_mission_==6) {
        static constexpr Item items[]{Item::StructuralPlates,Item::ScrapAlloys,Item::Polyethylene};
        static constexpr int amounts[]{4,1000,500};
        for(int i=0;i<3;++i) if(label==std::string("COLLECT ")+ShipInterior::rescue_material_names[i]) {
            if(!unit_->inventory().add(items[i],amounts[i])) return ActionResult::failure("Inventory full.");
            repair_materials_[i]=true;presentation_.sound(SoundCue::Pickup);
            last_message_=std::string(ShipInterior::rescue_material_names[i])+" COLLECTED / "+std::to_string(rescue_material_count())+" OF 3";
            if(rescue_material_count()==3) {
                rescue_mission_=7;rescue_stage_=RescueStage::Waiting;
                last_message_="MISSION 6 COMPLETE / MISSION 7: COMMUNICATE WITH C-02 IN THE CARGO HANGAR";
            }
            presentation_.notify(last_message_);return ActionResult::success(last_message_);
        }
    }
    if(label=="TALK TO C-02") {
        rescue_stage_=RescueStage::Communicating;rescue_stage_time_=0;
        pending_jump_=false;unit_->stop_moving();
        last_message_=rescue_dialogue();presentation_.sound(SoundCue::Interface);
        return ActionResult::success(last_message_);
    }
    if(label=="COLLECT DATA CHIP") {
        if(!unit_->inventory().add(Item::MarsDataChip,1)) return ActionResult::failure("Inventory full.");
        data_chip_collected_=true;
        last_message_="MARS EXPLORATION CHIP RECOVERED / RETURN TO BASE WITH THE USB AND CHIP";
        presentation_.notify(last_message_);presentation_.sound(SoundCue::Pickup);
        return ActionResult::success(last_message_);
    }
    if(label=="SCAN DATA CHIP") return start_mission4_quiz();
    if(label=="SCAN CARRIER ROBOT") {
        carrier_scanned_=true;last_message_="CARRIER SCANNED / FIND A WHEEL, ROBOT ARM AND CONTROL CHIP";
        presentation_.notify(last_message_);presentation_.sound(SoundCue::Interface);return ActionResult::success(last_message_);
    }
    if(label=="COLLECT WHEEL") carrier_wheel_collected_=true;
    else if(label=="COLLECT ROBOT ARM") carrier_arm_collected_=true;
    else if(label=="COLLECT CONTROL CHIP") carrier_chip_collected_=true;
    if(label=="COLLECT WHEEL" || label=="COLLECT ROBOT ARM" || label=="COLLECT CONTROL CHIP") {
        last_message_=label.substr(8)+" COLLECTED";presentation_.notify(last_message_);
        presentation_.sound(SoundCue::Pickup);return ActionResult::success(last_message_);
    }
    if(label=="REPAIR CARRIER ROBOT") {
        carrier_repairing_=true;carrier_repair_time_=0;
        last_message_="REPAIRING CARRIER ROBOT / STAY CLOSE / 6 SECONDS";
        presentation_.sound(SoundCue::Interface);return ActionResult::success(last_message_);
    }
    if(label=="OBSERVE") { for(int i=0;i<3;++i) if(obtained_[i]&&!observed_[i]) return start_observation(i); }
    if(label=="COLLECT USB") return start_copying_records();
    if(label=="DELIVER") {
        if(unit_->inventory().count(Item::FirstAidKit)<3 || unit_->inventory().count(Item::FoodWaterSupply)<2)
            return ActionResult::failure("Three first-aid kits and two food/water boxes required.");
        unit_->inventory().consume(Item::FirstAidKit,3);
        unit_->inventory().consume(Item::FoodWaterSupply,2);
        delivered_=true;
        last_message_="SUPPLIES DELIVERED / OBSERVE ALL THREE MATERIALS";
        begin_carrier_mission_if_ready();
        presentation_.sound(SoundCue::Pickup); return ActionResult::success(last_message_);
    }
    if(in_spaceship_) {
        const auto& food_positions=ShipInterior::food_supplies;
        for(int i=0;i<2;++i) if(!indoor_food_collected_[i] && unit_->near(food_positions[i],1.25)) {
            if(!unit_->inventory().add(Item::FoodWaterSupply,1)) return ActionResult::failure("Inventory full.");
            indoor_food_collected_[i]=true;world_->remove_ship_food(i);presentation_.sound(SoundCue::Pickup);
            last_message_="FOOD AND WATER BOX COLLECTED / "+std::to_string(supply_count())+" OF 5";
            return ActionResult::success(last_message_);
        }
    }
    for(auto& s:supplies_) if(!s.collected && unit_->near(s.position,1.1) &&
        world_->line_of_sight(unit_->center(),{int(s.position.x),int(s.position.y)})) {
        const auto item=supply_item(int(&s-supplies_.data()));
        if(!unit_->inventory().add(item,1)) return ActionResult::failure("Inventory full.");
        s.collected=true; presentation_.sound(SoundCue::Pickup);
        last_message_=std::string(item==Item::FirstAidKit ? "FIRST-AID KIT" : "FOOD AND WATER BOX")+" COLLECTED / "+std::to_string(supply_count())+" OF 5";
        return ActionResult::success(last_message_);
    }
    return ActionResult::failure("Supply unavailable.");
}
std::array<MissionTask,6> Game::tasks() const {
    if(rescue_mission_==8) {
        std::array<MissionTask,6> tasks{};
        tasks[0]={"REPAIR S-01",rescue_ship_repaired() ? "SPACECRAFT REPAIRED" : rescue_stage_==RescueStage::RepairingShip ?
            std::to_string(int(rescue_stage_progress()*100))+"% / STAY CLOSE" : "E / USE STORED MATERIALS",rescue_ship_repaired(),!rescue_ship_repaired(),BaseCamp::damaged_shuttle};
        tasks[1]={"TRANSFER ASTRONERD",astronaut_in_rescue_ship() ? "SAFE ABOARD S-01" : "C-02 / BOARDING RAMP",astronaut_in_rescue_ship(),rescue_ship_repaired()&&!astronaut_in_rescue_ship(),rescue_boarding_point};
        tasks[2]={"POWER S-01 / LAUNCH",rescue_launch_complete() ? "BOTH ROBOTS POWERED OFF" :
            "S-01 BATTERY / "+std::to_string(int(rescue_ship_battery_percent()))+"%",rescue_launch_complete(),astronaut_in_rescue_ship()&&!rescue_launch_complete(),BaseCamp::damaged_shuttle};
        return tasks;
    }
    if(rescue_mission_==6) {
        std::array<MissionTask,6> tasks{};
        for(int i=0;i<3;++i) tasks[i]={ShipInterior::rescue_material_names[i],repair_materials_[i] ? "COLLECTED" : "STORAGE ROOM / E TO COLLECT",
            repair_materials_[i],false,in_spaceship_ ? ShipInterior::rescue_materials[i] : BaseCamp::entrance};
        for(auto& task:tasks) if(!task.complete && !task.label.empty()) {task.active=true;break;}
        return tasks;
    }
    if(rescue_mission_==7) {
        std::array<MissionTask,6> tasks{};
        const bool talked=rescue_stage_!=RescueStage::Waiting;
        tasks[0]={"COMMUNICATE WITH C-02",talked ? "TRANSPORT AGREED" : "CARGO HANGAR / E",talked,!talked,ShipInterior::carrier_robot};
        tasks[1]={"SEAT AND PICKUP",astronaut_aboard() ? "ASTRONERD SECURED" : "FIT SEAT / PICK UP",
            astronaut_aboard(),talked&&!astronaut_aboard(),ShipInterior::rescue_unit_bedside};
        tasks[2]={"ESCORT C-02 TO S-01",rescue_stage_==RescueStage::Complete ? "ASTRONERD ARRIVED" : "C-02 FOLLOWS / (119,60)",
            rescue_stage_==RescueStage::Complete,rescue_stage_==RescueStage::Escort,BaseCamp::damaged_shuttle};
        return tasks;
    }
    if (placeholder_mission_ == 4) {
        std::array<MissionTask,6> tasks{};
        if(!data_chip_collected_) {
            tasks[0]={"RECOVER MARS DATA CHIP",near_data_chip() ? "PRESS E TO COLLECT THE CHIP" :
                data_chip_spotted_ ? "FOLLOW THE SIGNAL TO THE DATA CHIP" : "SEARCH THE SURFACE FOR THE DATA SIGNAL",
                false,true,BaseCamp::data_bank};
        } else if(!in_spaceship_) {
            tasks[0]={"DELIVER CHIP TO RECORDS ROOM", "RETURN TO THE RESCUE SHIP AIRLOCK",false,true,BaseCamp::entrance};
        } else if(!mission4_quiz_started_ && !mission4_quiz_complete_) {
            tasks[0]={"SCAN CHIP IN RECORDS ROOM",near_records_pc() ? "PRESS E AT THE MONITOR ROOM PC" :
                (usb_scan_recorded() ? "UPLOAD USB AND MARS CHIP AT PC" : "TAKE THE CHIP TO THE MONITOR ROOM PC"),false,true,ShipInterior::records_pc};
        } else {
            tasks[0]={"COMPLETE MARS DATA QUIZ",mission4_quiz_complete_ ? "ALL QUESTIONS CORRECT" :
                "READ EACH PASSAGE AND ANSWER EVERY QUESTION",mission4_quiz_complete_,!mission4_quiz_complete_,ShipInterior::records_pc};
        }
        return tasks;
    }
    if (placeholder_mission_) return {};
    if(mission2()) {
        std::array<MissionTask,6> tasks{};
        if(!carrier_scanned_) {
            tasks[0]={"SCAN CARRIER ROBOT","APPROACH THE BROKEN CARRIER / E",false,true,
                      in_spaceship_ ? ShipInterior::carrier_robot : BaseCamp::entrance};
            return tasks;
        }
        tasks[0]={"REPLACE CARRIER WHEEL",carrier_wheel_collected_ ? "WHEEL COLLECTED" : "FIND THE WHEEL ON A HANGAR TABLE",
                  carrier_wheel_collected_,false,ShipInterior::carrier_wheel};
        tasks[1]={"INSTALL ROBOT ARM",carrier_arm_collected_ ? "ROBOT ARM COLLECTED" : "FIND THE ARM ON THE OTHER HANGAR TABLE",
                  carrier_arm_collected_,false,ShipInterior::carrier_arm};
        tasks[2]={"INSTALL CONTROL CHIP",carrier_chip_collected_ ? "CHIP COLLECTED" : "FIND THE CHIP IN STORAGE",
                  carrier_chip_collected_,false,ShipInterior::carrier_chip};
        const bool parts_ready=carrier_wheel_collected_&&carrier_arm_collected_&&carrier_chip_collected_;
        if(parts_ready) tasks[3]={"REPAIR CARRIER ROBOT",carrier_repaired_ ? "CARRIER REPAIRED" : carrier_repairing_ ?
                  std::to_string(int(carrier_repair_progress()*100+1e-7))+"% / REPAIRING" : "RETURN TO THE CARGO HANGAR",
                  carrier_repaired_,true,ShipInterior::carrier_robot};
        for(auto& task:tasks) if(!task.complete) { task.active=true;break; }
        return tasks;
    }
    if(mission1_) {
        if(mission_number()==3) {
            std::array<MissionTask,6> tasks{};
            if(records_copied_) {
                tasks[0]={"SCAN S-01 TO USB",ship_scanned_ ? "SCAN SAVED ON USB" : scanning_spaceship_ ?
                    std::to_string(int(spaceship_scan_progress()*100))+"% / SCANNING" : "USB READY / S-01 AT (119,60)",
                    ship_scanned_,!ship_scanned_,in_spaceship_ ? ShipInterior::exit : BaseCamp::damaged_shuttle};
                return tasks;
            }
            tasks[0]={"COLLECT USB",records_copied_ ? "USB AND COORDINATES READY" : copying_records_ ?
                std::to_string(int(records_copy_progress()*100+1e-7))+"% / PREPARING USB" : "MONITOR ROOM PC / E",
                records_copied_,!records_copied_,in_spaceship_ ? ShipInterior::records_pc : BaseCamp::entrance};
            return tasks;
        }
        Vec2 target=in_spaceship_ ? ShipInterior::exit : BaseCamp::entrance;double best=1e9;
        if(in_spaceship_) {
            for(int i=0;i<int(ShipInterior::food_supplies.size());++i) {
                const auto p=ShipInterior::food_supplies[i];const double d=distance(unit_->center(),p);
                if(!indoor_food_collected_[i] && d<best) {best=d;target=p;}
            }
        } else {
            for(int i=0;i<3;++i) { const auto& s=supplies_[i];const double d=distance(unit_->center(),s.position);
                if(!s.collected && d<best) {best=d;target=s.position;}
            }
        }
        std::array<MissionTask,6> tasks{};
        tasks[0]={"RECOVER SUPPLIES",std::to_string(supply_count())+"/5 SUPPLIES",supply_count()==5,supply_count()<5,target};
        tasks[1]={"HELP ASTRONAUT",delivered_ ? "SUPPLIES DELIVERED" : "3 KITS + 2 FOOD BOXES",delivered_,supply_count()==5&&!delivered_,in_spaceship_ ? survivor_position() : BaseCamp::entrance};
        tasks[2]={"OBSERVE MATERIALS",std::to_string(observation_count())+"/3 MATERIALS OBSERVED",observation_count()==3,delivered_&&observation_count()<3,in_spaceship_ ? ShipInterior::table : BaseCamp::entrance};
        for(int i=3;i<6;++i) tasks[i].complete=true;
        return tasks;
    }
    const auto kg = [](std::int64_t grams) { return std::to_string(std::min<std::int64_t>(3,grams/1000)); };
    Vec2 resource{18.5,world_->surface_y()+0.5}, core{};
    double best = 1e9;
    for (int y = 0; y < world_->height(); ++y) {
        for (int x = 0; x < world_->width(); ++x) {
            const auto tile = world_->get_tile_type(x,y);
            if (tile == TileType::NASADataCore) core = {x+0.5,y+0.5};
            const auto wanted = ore_mined_ < 3000 ? TileType::IronNickelOre : TileType::Regolith;
            const double d = distance(unit_->center(),{x+0.5,y+0.5});
            if (tile == wanted && d < best) { best = d; resource = {x+0.5,y+0.5}; }
        }
    }
    std::array<MissionTask,6> result{{
        {"GATHER MATERIALS",kg(ore_mined_)+"/3 ORE  "+kg(dust_mined_)+"/3 DUST",ore_mined_ >= 3000 && dust_mined_ >= 3000,false,resource},
        {"FORGE PLATES",std::to_string(std::min<std::int64_t>(6,plates_crafted_))+"/6 PLATES",plates_crafted_ >= 6,false,refinery_->position()},
        {"REFINE PROPELLANT",std::to_string(std::min<std::int64_t>(2,fuel_crafted_))+"/2 FUEL KITS",fuel_crafted_ >= 2,false,refinery_->position()},
        {"RECOVER DATA CORE",core_recovered_ ? "CORE RECOVERED" : "FOLLOW THE BEACON",core_recovered_,false,core},
        {"RESTORE ROCKET",std::to_string(int(rocket_->hull_welded())+int(rocket_->fuel_lines_connected())+
            int(rocket_->data_core_socketed())+int(rocket_->calibrated()))+"/4 REPAIRS",rocket_->ready(),false,rocket_->ground_socket()},
        {"MANUAL LAUNCH",rocket_->launched() ? "EARTH RETURN CONFIRMED" : "SUMMIT VALVE",rocket_->launched(),false,rocket_->launch_valve()}
    }};
    for (auto& task : result) if (!task.complete) { task.active = true; break; }
    return result;
}
const char* Game::chapter_title() const {
    if(rescue_mission_==8) return rescue_launch_complete() ? "MISSION 8 / COMPLETE" : "MISSION 8 / REPAIR, POWER AND LAUNCH S-01";
    if(rescue_mission_==6) return "MISSION 6 / GATHER S-01 REPAIR MATERIALS";
    if(rescue_mission_==7) return rescue_stage_==RescueStage::Complete ? "MISSION 7 / COMPLETE" : "MISSION 7 / TRANSPORT THE ASTRONERD";
    if(mission2()) return "MISSION 2 / REPAIR THE CARRIER ROBOT";
    if(placeholder_mission_ == 4) return mission4_quiz_complete_ ? "MISSION 4 / COMPLETE" : "MISSION 4 / MARS DATA CHIP";

    if(campaign()) return mission3_complete() ? "MISSION 3 / COMPLETE" : mission_number()==3 ?
        (records_copied_ ? "MISSION 3 / SCAN S-01" : "MISSION 3 / COLLECT USB") : mission1_complete() ? "MISSION 1 / COMPLETE" : "MISSION 1 / ACT 1";
    if(mission1_) return mission1_complete() ? "MISSION 1 / COMPLETE" : "MISSION 1 / ACT 1";
    if(placeholder_mission_ == 2) return "MISSION 2 / REPAIR THE CARRIER ROBOT";
    if(placeholder_mission_ == 3) return "MISSION 3 / TEST ENTRY";
    switch (climax_->state()) {
        case ClimaxState::SolarParticleEvent: return "ACT III / SACRIFICE";
        case ClimaxState::Launching: return "ACT III / EARTH RETURN";
        case ClimaxState::Complete: return "GAME END / THANK YOU";
        case ClimaxState::Failed: return "MISSION FAILED / SIGNAL LOST";
        default: break;
    }
    return plates_crafted_ >= 6 && fuel_crafted_ >= 2 && core_recovered_ ? "ACT II / REPAIR" : "ACT I / DISCOVERY";
}
bool Game::finished() const {
    if(rescue_launch_complete()) return true;
    if(!rescue_mission_ && (mission_number()==3 ? mission3_complete() : placeholder_mission_==4 && mission4_quiz_complete_)) return true;
    return climax_->state() == ClimaxState::Complete || climax_->state() == ClimaxState::Failed;
}
bool run_mission_demo(std::ostream& output, const DemoObserver& observe) {
    // A small hand-authored test asteroid exposes the whole resource chain in one
    // deterministic run. No teleportation, free fuel or debug launch bypass is used.
    WorldGrid world(64,72);
    const int surface = world.surface_y();
    for (int x = 0; x < world.width(); ++x) world.place_tile(x,surface,TileType::Bedrock);
    for (int y = surface+1; y < world.height(); ++y)
        for (int x = 0; x < world.width(); ++x)
            world.place_tile(x,y,(x*13+y*7)%5 ? TileType::HydratedMinerals : TileType::Bedrock);
    for (int x = 14; x < 20; ++x) world.place_tile(x,surface-1,TileType::IronNickelOre);
    for (int x = 22; x < 24; ++x) world.place_tile(x,surface-1,TileType::HydratedMinerals);
    for (int x = 26; x < 32; ++x) world.place_tile(x,surface-1,TileType::Regolith);
    world.place_tile(34,surface-1,TileType::NASADataCore);
    for (int y = surface-18; y < surface; ++y) world.place_tile(42,y,TileType::Scaffold);
    Game game(std::move(world));
    const char* stage = "overview";
    if (observe) observe(game,stage);
    const auto step = [&](InputState input, double seconds) {
        for (int i = 0; i < int(std::ceil(seconds*60)); ++i) {
            game.advance(input,1.0/60.0);
            if (observe) observe(game,stage);
            input.jump = false;
        }
    };
    const auto walk_to = [&](double x) {
        int attempts = 0;
        while (std::abs(game.unit().center().x-x) > 0.09 && ++attempts < 12000) {
            InputState input;
            input.left = game.unit().center().x > x;
            input.right = !input.left;
            game.advance(input,1.0/60.0);
            if (observe) observe(game,stage);
        }
        step({},0.2);
        return attempts < 12000;
    };
    const auto act = [&](Action action) {
        const auto result = game.perform(action);
        output << (result.ok ? "  OK: " : "  FAIL: ") << result.message << '\n';
        if (observe && result.ok && action != Action::ActivateValve) step({},1.8);
        return result.ok;
    };
    output << "PHOBOS / deterministic mission fixture\nMining through the resource lane...\n";
    for (int x = 14; x <= 34; ++x) {
        if (!tile_definition(game.world().get_tile_type(x,surface-1)).mineable) continue;
        stage = "movement";
        if (!walk_to(x-1.0)) return false;
        stage = "mining";
        InputState laser; laser.mining_target = GridPos{x,surface-1};
        step(laser,2.2);
        if (game.world().solid(x,surface-1)) { output << "Mining failed at " << x << '\n'; return false; }
    }
    stage = "movement";
    if (!walk_to(10.5)) return false;
    stage = "crafting";
    output << "Base refinery and upgrades:\n";
    for (int i = 0; i < 6; ++i) if (!act(Action::Smelt)) return false;
    for (Action action : {Action::UpgradeDrill,Action::UpgradeTreads,Action::UpgradeShield}) if (!act(action)) return false;
    for (int i = 0; i < 2; ++i)
        for (Action action : {Action::Extract,Action::Electrolyze,Action::Liquefy,Action::PackageFuel}) if (!act(action)) return false;
    stage = "movement";
    if (!walk_to(game.rocket().ground_socket().x)) return false;
    stage = "tasks";
    output << "Rocket repair:\n";
    for (Action action : {Action::WeldHull,Action::HookupLines,Action::SocketCore,Action::Calibrate}) if (!act(action)) return false;
    step({},0.1);
    stage = "climb";
    if (!walk_to(game.rocket().launch_valve().x)) return false;
    InputState climbing; climbing.climb = true;
    int attempts = 0;
    while (game.unit().center().y > game.rocket().launch_valve().y+0.5 && ++attempts < 1200 && game.unit().alive()) {
        game.advance(climbing,1.0/60.0);
        if (observe) observe(game,stage);
    }
    output << "SPE climb: integrity " << game.unit().integrity() << "% / dosimeter "
           << game.unit().accumulated_dose_msv() << " mSv\n";
    if (attempts >= 1200 || !act(Action::ActivateValve)) return false;
    stage = "launch";
    step({},5.2);
    output << game.climax().outcome_text() << '\n';
    return game.climax().state() == ClimaxState::Complete && game.rocket().launched() && !game.unit().alive();
}
}

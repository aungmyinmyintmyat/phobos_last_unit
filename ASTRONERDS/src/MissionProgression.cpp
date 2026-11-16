#include "horizon/Game.hpp"

namespace horizon {
bool Game::near_rescue_spaceship() const {
    return !in_spaceship_ && !transitioning() && unit_->alive() && unit_->grounded() &&
        unit_->near(BaseCamp::damaged_shuttle,2.5) && world_->line_of_sight(unit_->center(),
            {int(BaseCamp::damaged_shuttle.x),int(BaseCamp::damaged_shuttle.y)});
}
void Game::update_spaceship_scan(double dt) {
    if(!scanning_spaceship_) return;
    if(mission_number()!=3 || !usb_obtained() || !near_rescue_spaceship()) {
        scanning_spaceship_=false;spaceship_scan_time_=0;
        last_message_="SCAN CANCELLED / RETURN TO S-01 AND PRESS E";presentation_.notify(last_message_);return;
    }
    spaceship_scan_time_=std::min(4.0,spaceship_scan_time_+dt);
    if(spaceship_scan_time_>=4.0-1e-9) {
        spaceship_scan_time_=4;scanning_spaceship_=false;ship_scanned_=true;
        last_message_="MISSION 3 COMPLETE / S-01 SCAN SAVED TO USB / RETURN TO BASE";
        presentation_.notify(last_message_);presentation_.sound(SoundCue::Pickup);
    }
}
bool Game::continue_mission() {
    if(!unit_->alive() || transitioning()) return false;
    if(mission_number()==3 && mission3_complete()) {
        placeholder_mission_=4;
        data_chip_spotted_=data_chip_collected_=false;
        mission4_quiz_started_=mission4_quiz_reading_=mission4_quiz_answered_=false;
        mission4_quiz_wrong_=mission4_quiz_complete_=mission4_quiz_rereading_=false;
        mission4_question_=0;mission4_selected_answer_=-1;
        last_message_="RETURN TO BASE WITH THE USB / SEARCH FOR THE MARS EXPLORATION CHIP ON THE WAY";
        presentation_.notify(last_message_);presentation_.sound(SoundCue::Interface);
        return true;
    }
    if(mission_number()==4 && mission4_quiz_complete_) {
        begin_rescue_material_mission();return true;
    }
    return false;
}
}

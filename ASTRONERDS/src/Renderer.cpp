#include "horizon/Renderer.hpp"
#include "horizon/SpaceBackground.hpp"
#include "horizon/BaseCamp.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace horizon {
namespace {
constexpr int dock_x = 78, dock_y = 318, dock_width = 484, dock_height = 28;
constexpr int slot_x = 158, slot_pitch = 38, slot_width = 35, inspection_y = 347;
constexpr int header_x = 12, header_y = 6, header_width = 616, header_height = 22;
constexpr int observe_x=228, observe_y=170, observe_w=184, observe_pitch=18;
constexpr int observe_header_y=150;
constexpr int result_close_x=402, result_close_y=242, result_close_w=72, result_close_h=18;
bool over_observation(int x,int y) {
    return x>=observe_x && x<observe_x+observe_w && y>=observe_header_y && y<observe_y+3*observe_pitch;
}
bool over_header(int x, int y) {
    return x >= header_x && x < header_x+header_width && y >= header_y && y < header_y+header_height;
}
bool over_dock(int x, int y) {
    return x >= dock_x && x < dock_x+dock_width && y >= dock_y && y < dock_y+dock_height;
}
std::uint32_t blend(std::uint32_t base, std::uint32_t tint, double alpha) {
    std::uint32_t result = 0xff000000;
    for (int shift : {0,8,16})
        result |= unsigned(((base>>shift)&255)*(1-alpha)+((tint>>shift)&255)*alpha)<<shift;
    return result;
}
std::string objective(const Game& game) {
    if(game.rescue_mission()==6) return "MISSION 6 / STORAGE: HULL PLATES, WIRING KIT AND SEALANT / E";
    if(game.rescue_mission()>=7) return game.rescue_dialogue();
    if (game.placeholder_mission()==4) {
        if (game.mission4_quiz_complete()) return "MISSION 4 COMPLETE / MARS DATA VERIFIED / MISSION 5 AVAILABLE.";
        if (game.mission4_quiz_started()) return "MISSION 4 / READ EACH PASSAGE AND ANSWER ALL QUESTIONS.";
        if (game.data_chip_collected()) return "MISSION 4 / RETURN TO THE SHIP AND SCAN THE CHIP AT THE MONITOR ROOM PC.";
        return game.data_chip_spotted() ? "MISSION 4 / FOLLOW THE SIGNAL TO THE DATA CHIP." :
            "MISSION 4 / SEARCH THE SURFACE FOR THE DATA SIGNAL.";
    }
    if(game.mission_number()==2) {
        if(!game.carrier_scanned()) return "MISSION 2 / SCAN THE CARRIER ROBOT IN THE CARGO HANGAR";
        if(game.carrier_repairing()) return "MISSION 2 / REPAIRING CARRIER ROBOT / STAY CLOSE";
        if(game.carrier_wheel_collected() && game.carrier_arm_collected() && game.carrier_chip_collected())
            return "MISSION 2 / RETURN TO THE CARRIER ROBOT AND REPAIR IT";
        return "MISSION 2 / FIND A WHEEL, ROBOT ARM AND CONTROL CHIP";
    }
    if(game.mission_number()==3 && game.records_copied()) return game.mission3_complete() ?
        "MISSION 3 COMPLETE / S-01 SCAN SAVED ON USB / N TO RETURN" : game.scanning_spaceship() ?
        "SCANNING S-01 / RECORDING TO USB / 4 SECONDS" : "USB READY / S-01 COORDINATES (119,60) / E TO SCAN";
    if(game.mission_number()==3) return game.mission3_complete() ? "MISSION 3 COMPLETE / S-01 SCANNED" :
        game.copying_records() ? "MISSION 3 / PREPARING USB / STAY NEAR THE PC" :
        "MISSION 3 / MONITOR ROOM PC / E TO COLLECT USB";
    if(game.mission1()) return game.supply_count()<5 ? "MISSION 1 / FIND THREE KITS + TWO FOOD BOXES" :
        !game.tasks()[1].complete ? "MISSION 1 / DELIVER SUPPLIES TO THE ASTRONAUT" :
        "MISSION 1 / OBSERVE SILICATE, WATER ICE AND ROCK FRAGMENTS";
    if (game.climax().state() == ClimaxState::Complete) return "MISSION COMPLETE. EARTH SAVED.";
    if (game.climax().state() == ClimaxState::Failed)
        return game.unit().battery_percent() <= 0 ? "BATTERY EMPTY. MISSION FAILED. RESTART TO TRY AGAIN." : "SIGNAL LOST. RESTART TO TRY AGAIN.";
    if (game.climax().state() == ClimaxState::Launching) return "EARTH RETURN CONFIRMED. THANK YOU, UNIT-01.";
    if (game.climax().state() == ClimaxState::SolarParticleEvent) return "CLIMB THE ROCKET SCAFFOLD. OPEN THE SUMMIT VALVE.";
    for (const auto& task : game.tasks()) if (task.active) {
        if (task.label == "GATHER MATERIALS") return "MINE 3 KG ORE AND 3 KG DUST. FOLLOW THE GOLD BEACON.";
        if (task.label == "FORGE PLATES") return "RETURN TO BASE. M SMELTS ORE + DUST INTO PLATES.";
        if (task.label == "REFINE PROPELLANT") return "AT BASE: E EXTRACT, O ELECTROLYZE, L LIQUEFY, F FUEL.";
        if (task.label == "RECOVER DATA CORE") return "FOLLOW THE DATA BEACON. RECOVER THE NASA CORE.";
        if (!game.rocket().hull_welded()) return "AT THE ROCKET: R WELDS THE HULL. NEED 4 PLATES.";
        if (!game.rocket().fuel_lines_connected()) return "AT THE ROCKET: R CONNECTS FUEL. NEED 2 PLATES + 2 KITS.";
        if (!game.rocket().data_core_socketed()) return "AT THE ROCKET: R SOCKETS THE RECOVERED DATA CORE.";
        return "R CALIBRATES THE VALVE. PREPARE FOR THE SOLAR STORM.";
    }
    return "EARTH RETURN CONFIRMED.";
}
std::array<unsigned char,7> glyph(char c) {
    switch (c) {
        case 'a': return {0,0,14,1,15,17,15}; case 'r': return {0,0,22,25,16,16,16};
        case 'h': return {16,16,22,25,17,17,17}; case 'g': return {0,14,17,17,15,1,14};
        case 'e': return {0,0,14,17,31,16,14}; case 'f': return {6,9,8,28,8,8,8};
        case 't': return {8,8,28,8,8,9,6};
        case 'A': return {14,17,17,31,17,17,17}; case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,15}; case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {31,4,4,4,4,4,31}; case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17}; case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17}; case 'N': return {17,25,25,21,19,19,17};
        case 'O': return {14,17,17,17,17,17,14}; case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13}; case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14}; case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10}; case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4}; case 'Z': return {31,1,2,4,8,16,31};
        case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
        case '.': return {0,0,0,0,0,6,6}; case ':': return {0,6,6,0,6,6,0};
        case '-': return {0,0,0,31,0,0,0}; case '+': return {0,4,4,31,4,4,0};
        case '/': return {1,1,2,4,8,16,16}; case '%': return {17,2,4,4,8,16,17};
        case '(': return {2,4,8,8,8,4,2}; case ')': return {8,4,2,2,2,4,8};
        case '!': return {4,4,4,4,4,0,4}; case ',': return {0,0,0,0,0,4,8};
        default: return {};
    }
}
std::string quantity(Item item, std::int64_t count) {
    if (!measured_in_grams(item) || count == 0) return std::to_string(count);
    std::ostringstream out;
    if (count < 1000) out << count << "G";
    else if (count%1000 == 0) out << count/1000 << "KG";
    else out << std::fixed << std::setprecision(1) << count/1000.0 << "KG";
    return out.str();
}
struct RecipeHint {
    const char* key; const char* label; Action action;
    std::array<ItemStack,2> inputs, outputs;
};
std::array<RecipeHint,9> recipes(const Game& game) {
    const auto& config = game.refinery().config();
    const auto water = static_cast<std::int64_t>(std::llround(20000*config.hydrated_water_mass_fraction*config.recovery_efficiency));
    const ItemStack empty{Item::Count,0};
    return {{
        {"M","PLATES",Action::Smelt,{{{Item::IronNickelOre,1000},{Item::Regolith,1000}}},{{{Item::StructuralPlates,2},empty}}},
        {"E","EXTRACT",Action::Extract,{{{Item::HydratedMinerals,20000},empty}},{{{Item::Water,water},empty}}},
        {"I","MELT ICE",Action::MeltIce,{{{Item::WaterIce,1800},empty}},{{{Item::Water,1800},empty}}},
        {"O","SPLIT WATER",Action::Electrolyze,{{{Item::Water,1800},empty}},{{{Item::HydrogenGas,200},{Item::OxygenGas,1600}}}},
        {"L","COOL GASES",Action::Liquefy,{{{Item::HydrogenGas,200},{Item::OxygenGas,1600}}},{{{Item::LiquidHydrogen,200},{Item::LiquidOxygen,1600}}}},
        {"F","FUEL KIT",Action::PackageFuel,{{{Item::LiquidHydrogen,200},{Item::LiquidOxygen,1600}}},{{{Item::RefinedFuel,1},empty}}},
        {"1","DRILL",Action::UpgradeDrill,{{{Item::StructuralPlates,2},{Item::ScrapAlloys,1000}}},{{empty,empty}}},
        {"2","TREADS",Action::UpgradeTreads,{{{Item::StructuralPlates,2},{Item::ScrapAlloys,1000}}},{{empty,empty}}},
        {"3","SHIELD",Action::UpgradeShield,{{{Item::StructuralPlates,2},{Item::Polyethylene,1000}}},{{empty,empty}}}
    }};
}
bool ready(const RecipeHint& recipe, const Game& game) {
    if (!game.refinery().can_operate(game.unit())) return false;
    if (game.unit().battery_percent() < Game::action_battery_cost(recipe.action)) return false;
    if (recipe.action == Action::UpgradeDrill && game.unit().has_upgrade(Upgrade::SolarThermalDrill)) return false;
    if (recipe.action == Action::UpgradeTreads && game.unit().has_upgrade(Upgrade::DustRepellentTreads)) return false;
    if (recipe.action == Action::UpgradeShield && game.unit().has_upgrade(Upgrade::RadiationShielding)) return false;
    for (auto stack : recipe.inputs)
        if (stack.quantity > 0 && game.unit().inventory().count(stack.item) < stack.quantity) return false;
    return true;
}
std::string countdown(double seconds) {
    const int total = std::max(0,int(std::ceil(seconds)));
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << total/60 << ':' << std::setw(2) << total%60;
    return out.str();
}

}
void TerminalRenderer::render(const Game& game) {
    const auto& world = game.world();
    const auto center = game.unit().center();
    const int columns = std::min(61,world.width()), rows = std::min(19,world.height());
    const int left = std::clamp(int(center.x)-columns/2,0,world.width()-columns);
    const int top = std::clamp(int(center.y)-rows/2,0,world.height()-rows);
    output_ << "\nPHOBOS: LAST LIGHT | " << state_name(game.climax().state()) << '\n';
    output_ << "Viewport (" << left << ',' << top << ") | U Unit-7  B bench  R rocket  V valve\n+" << std::string(columns,'-') << "+\n";
    for (int y = top; y < top+rows; ++y) {
        output_ << '|';
        for (int x = left; x < left+columns; ++x) {
            char ch = tile_definition(world.get_tile_type(x,y)).glyph;
            auto at = [&](Vec2 p) { return int(std::floor(p.x)) == x && int(std::floor(p.y)) == y; };
            if (at(game.refinery().position())) ch = 'B';
            if (at(game.rocket().ground_socket())) ch = 'R';
            if (at(game.rocket().launch_valve())) ch = 'V';
            if (at(center)) ch = game.unit().alive() ? 'U' : 'u';
            output_ << ch;
        }
        output_ << "|\n";
    }
    output_ << '+' << std::string(columns,'-') << "+\n" << std::fixed << std::setprecision(1)
            << "Position " << center.x << ',' << center.y << " | integrity " << game.unit().integrity()
            << "% | battery " << game.unit().battery_percent() << "% | dose " << game.unit().accumulated_dose_msv()
            << " mSv | sim " << game.simulated_seconds() << " s\n";
    if (game.survival_enabled())
        output_ << "Phobos local " << game.phobos().local_time() << (game.phobos().night() ? " NIGHT" : " DAY")
                << " | real time 1x | " << (game.charging() ? "CHARGING" : game.inside_base() ? "SHELTERED" : "OUTSIDE")
                << (game.danger().warning() ? " | DANGER IN " : game.danger().active() ? " | DANGER CLEARS IN " : "")
                << (game.danger().warning() || game.danger().active() ? countdown(game.danger().seconds_remaining()) : "") << '\n';
    output_ << "Upgrades: drill " << game.unit().has_upgrade(Upgrade::SolarThermalDrill)
            << " / dust treads " << game.unit().has_upgrade(Upgrade::DustRepellentTreads)
            << " / shield " << game.unit().has_upgrade(Upgrade::RadiationShielding) << '\n';
    for (auto stack : game.unit().inventory().stacks())
        output_ << "  " << item_name(stack.item) << ": " << stack.quantity << (measured_in_grams(stack.item) ? " g" : " units") << '\n';
    output_ << objective(game) << '\n' << game.last_message() << '\n';
    for (const auto& task : game.tasks())
        output_ << (task.complete ? "[x] " : task.active ? "[>] " : "[ ] ") << task.label << " / " << task.progress << '\n';
    if (game.finished()) output_ << game.climax().outcome_text() << '\n';
}
PixelRenderer::PixelRenderer() : pixels_(width*height,0xff090e20) {}
void PixelRenderer::open_startup() {
    close_overlays();
    startup_visible_ = true;
    startup_page_ = StartupPage::Main;
    startup_selection_ = 0;
    startup_command_ = StartupCommand::None;
}
void PixelRenderer::open_mission_select() {
    close_overlays();
    startup_visible_=true;
    startup_page_=StartupPage::Missions;
    startup_selection_=0;
    startup_command_=StartupCommand::None;
}
bool PixelRenderer::completion_screen(const Game& game) const {
    return game.observation_result()<0 && game.finished() &&
        (game.mission1_complete() || game.mission3_complete() || game.placeholder_mission()==4);
}
void PixelRenderer::completion_move(int direction) {
    if (direction) completion_selection_=direction>0 ? 1 : 0;
}
void PixelRenderer::completion_activate(const Game& game) {
    if (!completion_screen(game)) return;
    menu_command_=completion_selection_==0 ? MenuCommand::MissionSelect : MenuCommand::ContinueMission;
}
void PixelRenderer::render_startup(double time) {
    draw_startup_menu(time);
}
void PixelRenderer::startup_move(int direction) {
    if (!startup_visible_ || direction == 0 || startup_page_ == StartupPage::Options) return;
    const int count = startup_page_ == StartupPage::Missions ? (enable_upcoming_missions ? 6 : 3) : 4;
    startup_selection_ = (startup_selection_+(direction > 0 ? 1 : count-1))%count;
}
void PixelRenderer::startup_activate() {
    if (!startup_visible_) return;
    if (startup_page_ == StartupPage::Main) {
        if (startup_selection_ == 0) { startup_command_ = StartupCommand::StartMission1; startup_visible_ = false; }
        else if (startup_selection_ == 1) { startup_page_ = StartupPage::Missions; startup_selection_ = 0; }
        else if (startup_selection_ == 2) { startup_page_ = StartupPage::Options; startup_selection_ = 0; }
        else startup_command_ = StartupCommand::Quit;
    } else if (startup_page_ == StartupPage::Missions) {
        if (startup_selection_ == 0) { startup_command_ = StartupCommand::StartMission1; startup_visible_ = false; }
        else if (startup_selection_ == 1) { startup_command_ = StartupCommand::StartMission2; startup_visible_ = false; }
        else if (enable_upcoming_missions && startup_selection_ >= 2 && startup_selection_ <= 4) {
            startup_command_ = static_cast<StartupCommand>(static_cast<int>(StartupCommand::StartMission1)+startup_selection_);
            startup_visible_ = false;
        }
        else if (startup_selection_ == (enable_upcoming_missions ? 5 : 2)) { startup_page_ = StartupPage::Main; startup_selection_ = 1; }
    } else startup_back();
}
void PixelRenderer::startup_back() {
    if (!startup_visible_) return;
    if (startup_page_ == StartupPage::Main) startup_command_ = StartupCommand::Quit;
    else {
        startup_selection_ = startup_page_ == StartupPage::Missions ? (enable_upcoming_missions ? 5 : 2) : 2;
        startup_page_ = StartupPage::Main;
    }
}
bool PixelRenderer::startup_click(int x,int y) {
    if (!startup_visible_) return false;
    if (startup_page_ == StartupPage::Options) return sound_settings_click(x,y,true);
    if (x < 180 || x >= 460) return true;
    if (startup_page_ == StartupPage::Main) {
        for (int row=0;row<4;++row) if (y >= 146+row*36 && y < 174+row*36) {
            startup_selection_=row; startup_activate(); break;
        }
    } else {
        for (int row=0;row<(enable_upcoming_missions ? 5 : 2);++row) if (y >= 108+row*39 && y < 144+row*39) {
            startup_selection_=row; startup_activate(); break;
        }
        if (y >= 306 && y < 334) { startup_selection_=enable_upcoming_missions ? 5 : 2; startup_activate(); }
    }
    return true;
}
void PixelRenderer::draw_startup_menu(double time) {
    SpaceBackground::draw(pixels_,{width,height,0,height,time*7,time*2,180,time,false});
    for (auto& pixel : pixels_) pixel=blend(pixel,0x000106,0.34);
    // A slow diagonal meteor gives the otherwise calm star field a clear pulse.
    const double travel=std::fmod(std::max(0.0,time)*34.0,850.0);
    const int meteor_x=int(720-travel), meteor_y=int(44+travel*0.18);
    for (int i=0;i<20;++i) rectangle(meteor_x+i,meteor_y-int(i*0.18),i<3 ? 2 : 1,1,
                                      i<3 ? 0xd9f4ff : (i<9 ? 0x52778e : 0x263d50));
    text(132,48,"PHOBOS",0xbde8e7,3);
    text(238,82,"LAST LIGHT",0x789cae);
    rectangle(213,102,214,1,0x4d7588);
    const auto button=[&](int y,const std::string& label,int selection,bool enabled=true) {
        const bool hover=pointer_x_>=180 && pointer_x_<460 && pointer_y_>=y && pointer_y_<y+28;
        const bool active=startup_selection_==selection || hover;
        glass_panel(180,y,280,28,7);
        if (active) outline(183,y+3,274,22,enabled ? 0x7fc9c3 : 0x596875);
        const auto color=enabled ? (active ? 0xd9f4e9 : 0xa7c4ca) : 0x657782;
        text(196,y+10,std::to_string(selection+1)+".",color);
        text(224,y+10,label,color);
    };
    if (startup_page_ == StartupPage::Main) {
        button(146,"START THE GAME",0);
        button(182,"MISSIONS",1);
        button(218,"OPTIONS",2);
        button(254,"QUIT",3);
        text(185,310,"ARROWS / ENTER     MOUSE SELECT",0x6f8c9b);
    } else if (startup_page_ == StartupPage::Missions) {
        text(234,92,"SELECT MISSION",0xd8ede8,2);
        const auto mission=[&](int y,const char* title,const char* detail,int selection,bool enabled) {
            const bool hover=pointer_x_>=180 && pointer_x_<460 && pointer_y_>=y && pointer_y_<y+36;
            glass_panel(180,y,280,36,7);
            if (startup_selection_==selection || hover) outline(183,y+3,274,30,enabled ? 0x7fc9c3 : 0x596875);
            text(198,y+9,title,enabled ? 0xccebe2 : 0x768691);
            text(198,y+25,detail,enabled ? 0x829faa : 0x566773);
        };
        mission(108,"MISSION 1","OBTAIN VITAL SUPPLIES",0,true);
        mission(147,"MISSION 2","REPAIR THE CARRIER ROBOT",1,true);
        if (enable_upcoming_missions) {
            mission(186,"MISSION 3","USB / COORDINATES / SCAN S-01",2,true);
            mission(225,"MISSION 4","MARS RESEARCH DATA QUIZ",3,true);
            mission(264,"MISSION 6","GATHER S-01 REPAIR MATERIALS",4,true);
        }
        button(306,"BACK",enable_upcoming_missions ? 5 : 1);
    } else draw_sound_settings(true);
}
void PixelRenderer::rectangle(int x, int y, int w, int h, std::uint32_t rgb) {
    const int right = std::clamp(x+w,0,width), bottom = std::clamp(y+h,0,height);
    for (int row = std::max(0,y); row < bottom; ++row)
        for (int col = std::max(0,x); col < right; ++col) pixels_[row*width+col] = 0xff000000|rgb;
}
void PixelRenderer::line(int x0, int y0, int x1, int y1, std::uint32_t rgb) {
    const int dx = std::abs(x1-x0), dy = -std::abs(y1-y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx+dy;
    while (true) {
        rectangle(x0,y0,1,1,rgb);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2*error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}
void PixelRenderer::text(int x, int y, const std::string& value, std::uint32_t rgb, int scale) {
    for (unsigned char c : value) {
        const auto bits = glyph(char(std::toupper(c)));
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (bits[row] & (1 << (4-col))) rectangle(x+col*scale,y+row*scale,scale,scale,rgb);
        x += 6*scale;
    }
}
void PixelRenderer::outline(int x, int y, int w, int h, std::uint32_t color) {
    rectangle(x,y,w,1,color); rectangle(x,y+h-1,w,1,color);
    rectangle(x,y,1,h,color); rectangle(x+w-1,y,1,h,color);
}
void PixelRenderer::panel(int x, int y, int w, int h, std::uint32_t accent) {
    rectangle(x+2,y+3,w,h,0x060c17);
    rectangle(x,y,w,h,0x111f30); outline(x,y,w,h,accent);
    rectangle(x+1,y+1,w-2,1,0x26384a);
}
void PixelRenderer::glass_panel(int x, int y, int w, int h, int radius) {
    constexpr int blur = 4, samples = blur*2+1;
    // Blur only the backdrop beneath this panel, keeping software rendering cheap.
    std::vector<std::uint32_t> horizontal(w*(h+blur*2));
    for (int row = -blur; row < h+blur; ++row) for (int col = 0; col < w; ++col) {
        unsigned red = 0, green = 0, blue = 0;
        for (int k = -blur; k <= blur; ++k) {
            const auto p = pixels_[std::clamp(y+row,0,height-1)*width+std::clamp(x+col+k,0,width-1)];
            red += (p>>16)&255; green += (p>>8)&255; blue += p&255;
        }
        horizontal[(row+blur)*w+col] = (red/samples<<16)|(green/samples<<8)|(blue/samples);
    }
    const auto edge_distance = [&](double col,double row) {
        const double dx = std::abs(col-(w-1)*0.5)-(w*0.5-radius);
        const double dy = std::abs(row-(h-1)*0.5)-(h*0.5-radius);
        return std::hypot(std::max(0.0,dx),std::max(0.0,dy))+std::min(0.0,std::max(dx,dy))-radius;
    };
    for (int row = -3; row < h+6; ++row) for (int col = -4; col < w+4; ++col) {
        const int px = x+col, py = y+row;
        if (px < 0 || px >= width || py < 0 || py >= height) continue;
        if (y >= inspection_y && py < inspection_y) continue;
        auto& pixel = pixels_[py*width+px];
        const double shadow = std::clamp((4-edge_distance(col,row-2))/7,0.0,1.0)*0.28;
        pixel = blend(pixel,0x07101b,shadow);
        if (row < 0 || row >= h || col < 0 || col >= w) continue;
        const double edge = edge_distance(col,row), coverage = std::clamp(0.5-edge,0.0,1.0);
        if (coverage <= 0) continue;
        unsigned red = 0, green = 0, blue = 0;
        for (int k = 0; k < samples; ++k) {
            const auto p = horizontal[(row+k)*w+col];
            red += (p>>16)&255; green += (p>>8)&255; blue += p&255;
        }
        auto frost = blend((red/samples<<16)|(green/samples<<8)|(blue/samples),0x172c3c,0.62);
        const double sheen = 0.10*(1.0-double(row)/h)+0.025*std::sin(double(col)/w*3.1415926536);
        frost = blend(frost,0xe0f5ff,sheen);
        const double rim = std::clamp(edge+1.35,0.0,1.0)*(row < h/2 ? 0.36 : 0.14);
        pixel = blend(pixel,blend(frost,0xd7edf5,rim),coverage);
    }
}
PixelRenderer::Icon PixelRenderer::item_icon(Item item) {
    static constexpr std::array<Icon,item_count> icons{{Icon::Dust,Icon::Mineral,Icon::Ore,Icon::Ice,Icon::Water,Icon::Scrap,
        Icon::Hydrogen,Icon::Oxygen,Icon::LH2,Icon::LOX,Icon::Fuel,Icon::Plate,Icon::Polymer,Icon::Core,Icon::Slag,
        Icon::FoodWater,Icon::Health,Icon::Silicate,Icon::Ice,Icon::RockSample,Icon::USB,Icon::Chip}};
    const auto i = static_cast<std::size_t>(item);
    return i < icons.size() ? icons[i] : Icon::Bench;
}
void PixelRenderer::keycap(int x, int y, const std::string& key) {
    const int w = std::max(13,int(key.size())*6+6);
    rectangle(x,y,w,13,0x25394b); outline(x,y,w,13,0x506c7c);
    text(x+(w-int(key.size())*6+1)/2,y+3,key,0xc1d4df);
}
void PixelRenderer::tooltip(int x, int y, const std::string& label) {
    const int w = int(label.size())*6+14;
    x = std::clamp(x-w/2,4,width-w-4); y = std::clamp(y,scene_top+2,height-20);
    panel(x,y,w,17,0x516c76); text(x+7,y+5,label,0xd7e7e2);
}
void PixelRenderer::icon(int x, int y, Icon kind, int scale) {
    const auto r = [&](int a,int b,int w,int h,std::uint32_t color) { rectangle(x+a*scale,y+b*scale,w*scale,h*scale,color); };
    const auto l = [&](int a,int b,int c,int d,std::uint32_t color) { line(x+a*scale,y+b*scale,x+c*scale,y+d*scale,color); };
    switch (kind) {
        case Icon::USB:
            r(5,1,6,5,0xb7c8d0);r(6,2,1,2,0x3d4e59);r(9,2,1,2,0x3d4e59);
            r(3,6,10,9,0x2e6e91);r(4,7,8,6,0x418db2);r(6,10,4,2,0x8ee8e1);break;
        case Icon::Silicate:
            r(2,10,12,4,0x756453); r(4,6,9,6,0xa58d70); r(6,3,5,5,0xc9ae88);
            l(3,11,13,8,0x5d4d40); l(5,8,12,6,0xe6d4af); l(7,4,10,3,0xf1dfbd); break;
        case Icon::RockSample:
            r(1,10,8,4,0x565b60); r(3,7,6,5,0x74777a); r(9,8,7,6,0x4b5055);
            r(10,5,5,5,0x85827c); l(3,11,8,8,0x34383c); l(11,6,14,9,0xb0aaa0); break;
        case Icon::Observe:
            l(4,2,10,2,0x9befff); l(10,2,13,6,0x9befff); l(13,6,10,10,0x9befff); l(10,10,4,10,0x9befff); l(4,10,1,6,0x9befff); l(1,6,4,2,0x9befff); l(11,10,16,15,0xb3edf5); break;
        case Icon::FoodWater:
            r(0,6,7,7,0xd6a25e); r(1,4,5,2,0xe8b972); r(2,7,1,1,0x70492f); r(5,9,1,1,0x70492f); r(2,11,1,1,0x70492f);
            r(11,3,2,2,0x8ad8f5); r(10,5,4,2,0x72c9ed); r(9,7,6,5,0x54afe1); r(10,12,4,2,0x54afe1); r(10,8,1,3,0xb5efff); break;
        case Icon::Ore:
            r(3,7,11,7,0x563f38); r(5,4,7,9,0xa86d45); r(2,9,5,4,0xce995f);
            r(7,5,3,3,0xf0c48b); r(10,10,3,2,0xdfaa72); r(4,11,2,2,0x754d3a); break;
        case Icon::Dust: case Icon::Slag: {
            const auto c = kind == Icon::Dust ? 0xae9676u : 0x65707au;
            r(2,11,13,3,c); r(4,8,10,4,c); r(7,6,4,3,c); r(4,11,2,1,0xe0c49c);
            r(10,9,1,1,0x493c37); r(1,14,2,1,c); break;
        }
        case Icon::Mineral:
            r(4,5,8,9,0x348d88); r(7,2,3,12,0x70d5bf); r(3,8,3,5,0x4ab7a0);
            r(8,3,1,9,0xb5f5dd); r(11,7,3,6,0x2b6b72); l(5,4,3,8,0x82deca); break;
        case Icon::Ice:
            r(2,9,12,5,0x4c91ad); r(4,5,9,7,0x75bdd0); r(7,2,5,7,0xa7e1eb);
            l(8,3,5,10,0xe5fbff); l(5,10,10,7,0xcff4f8); l(10,7,13,12,0x4f99b5); break;
        case Icon::Water:
            r(7,1,2,3,0x82d9fa); r(5,4,6,3,0x67bee8); r(3,7,10,6,0x3e8fc2);
            r(5,13,6,2,0x3e8fc2); r(5,7,2,4,0xc7f5ff); break;
        case Icon::Hydrogen: case Icon::Oxygen: {
            const auto c = kind == Icon::Hydrogen ? 0x85cfe3u : 0xe79787u;
            r(4,2,8,12,0x334b60); r(2,5,12,6,0x334b60);
            text(x+5*scale,y+4*scale,kind == Icon::Hydrogen ? "H" : "O",c,scale);
            r(1,2,2,2,c); r(12,12,2,2,c); break;
        }
        case Icon::LH2: case Icon::LOX:
            r(5,1,6,2,0xa7b9c8); r(3,3,10,11,0x798c9c); r(4,5,8,8,kind == Icon::LH2 ? 0x407b9a : 0xa56b66);
            text(x+6*scale,y+5*scale,kind == Icon::LH2 ? "H" : "O",0xe4f3f2,scale); r(5,14,6,1,0x263b50); break;
        case Icon::Fuel:
            r(2,4,5,10,0xb4c9d1); r(9,4,5,10,0xc78a71); r(3,2,3,3,0x73b5ca); r(10,2,3,3,0xe3b18b);
            r(3,7,3,5,0x457c9b); r(10,7,3,5,0x9f5b55); r(6,8,4,3,0xe4cf91); break;
        case Icon::Plate:
            r(2,8,12,6,0x526b82); r(3,5,12,6,0x8fa6b9); r(1,3,12,6,0xb5c8d5);
            r(2,3,10,1,0xebf6f7); r(3,5,2,2,0x657f94); break;
        case Icon::Scrap:
            r(2,3,4,11,0x7d8e9c); r(5,9,9,4,0xa6b5ba); r(10,2,3,10,0x5d7386);
            r(3,4,2,2,0xe4bf7b); r(10,10,2,2,0xe4bf7b); break;
        case Icon::Polymer:
            r(3,2,10,12,0x9eb8b6); r(4,3,8,9,0xe2ebe0); r(5,6,6,1,0x95beb0);
            r(5,8,6,1,0x95beb0); r(5,10,4,1,0x95beb0); break;
        case Icon::Core:
            r(3,3,10,10,0x51786f); r(5,5,6,6,0x9ef2be); r(7,6,2,4,0xd9ffe2);
            for (int i = 0; i < 3; ++i) { r(1,4+i*3,2,1,0xe5c78a); r(13,4+i*3,2,1,0xe5c78a); } break;
        case Icon::Wheel:
            r(2,2,12,12,0x303a43); r(4,4,8,8,0x9aaba9); r(6,6,4,4,0x40515b);
            l(8,2,8,5,0xc6d3c5); l(8,11,8,14,0x6b7f84); break;
        case Icon::RobotArm:
            l(3,12,6,9,0xabb9b4); l(6,9,9,7,0xc3b27f); l(9,7,12,4,0xabb9b4);
            r(1,11,4,4,0x657b81); r(5,7,4,4,0x876e51); r(11,2,4,4,0x657b81); break;
        case Icon::Chip:
            r(4,4,8,8,0x397b69); r(6,6,4,4,0xa5edbd);
            for(int i=0;i<3;++i) { r(2+i*4,1,1,3,0xb8c9b8); r(2+i*4,12,1,3,0xb8c9b8); }
            r(1,5,3,1,0xb8c9b8); r(12,5,3,1,0xb8c9b8); r(1,10,3,1,0xb8c9b8); r(12,10,3,1,0xb8c9b8); break;
        case Icon::Drill: case Icon::Laser:
            r(2,7,8,5,0xd3ac5d); r(5,10,3,5,0x6d8193); r(9,6,3,5,0x9eb7c5);
            r(12,7,3,3,kind == Icon::Drill ? 0xe6cf9d : 0x8ef5d4); r(3,6,4,1,0xf2d38e); break;
        case Icon::Treads:
            r(1,6,14,8,0x526c80); r(3,5,10,10,0x526c80); r(3,8,10,4,0x142638);
            for (int i = 0; i < 3; ++i) { r(3+i*4,8,2,3,0x9bb7c3); }
            break;
        case Icon::Shield:
            r(2,2,12,7,0x6bba9c); r(4,9,8,3,0x6bba9c); r(6,12,4,2,0x6bba9c);
            r(4,3,8,5,0xb4e8cc); r(7,4,2,7,0x3d7a70); break;
        case Icon::Bench:
            r(1,7,14,3,0xb6c2ca); r(3,10,2,5,0x617e91); r(11,10,2,5,0x617e91);
            r(4,2,8,5,0x425e72); r(5,3,6,3,0x86d9bb); break;
        case Icon::Rocket:
            r(6,1,4,3,0xd1e0e5); r(4,4,8,8,0x9fb5c5); r(6,5,4,3,0x5fa7bd);
            r(2,10,3,4,0x68879d); r(11,10,3,4,0x68879d); r(6,12,4,3,0xe4ad69); break;
        case Icon::Valve:
            r(7,2,2,12,0xdcc286); r(2,7,12,2,0xdcc286); r(3,3,10,1,0xdcc286);
            r(3,12,10,1,0xdcc286); r(3,3,1,10,0xdcc286); r(12,3,1,10,0xdcc286); break;
        case Icon::Jump:
            r(7,4,2,10,0xb7d8e4); l(2,7,8,1,0xb7d8e4); l(8,1,14,7,0xb7d8e4); break;
        case Icon::Climb:
            r(3,1,2,14,0xc7af7c); r(11,1,2,14,0xc7af7c);
            for (int i = 0; i < 4; ++i) { r(4,3+i*3,8,1,0xc7af7c); }
            break;
        case Icon::Health:
            r(1,4,13,8,0x7fcbb1); r(14,6,2,4,0x7fcbb1); r(3,6,9,4,0x203b40);
            r(7,5,1,6,0xd0f4df); r(5,7,5,1,0xd0f4df); break;
        case Icon::Radiation:
            r(6,1,4,5,0xebbc75); r(1,9,5,5,0xebbc75); r(10,9,5,5,0xebbc75); r(6,7,4,4,0xebbc75); break;
        case Icon::Tasks:
            for (int i = 0; i < 3; ++i) { r(1,2+i*5,3,3,0x8db9a9); r(6,3+i*5,9,1,0x7b99ad); } break;
        case Icon::Help:
            text(x+5*scale,y+3*scale,"H",0xa6bfce,scale); break;
    }
}
void PixelRenderer::toggle_inventory() {
    const bool next = !inventory_visible_; close_overlays(); inventory_visible_ = next;
}
void PixelRenderer::toggle_help() {
    const bool next = !help_visible_; close_overlays(); help_visible_ = next;
}
void PixelRenderer::toggle_crafting(const Game& game) {
    if(game.campaign() && !game.in_spaceship()) return;
    const bool next = !crafting_visible_ && game.refinery().can_operate(game.unit());
    close_overlays(); crafting_visible_ = next;
}
void PixelRenderer::toggle_pause() {
    if (mission4_quiz_visible_) return;
    const bool next = !pause_visible_;
    close_overlays(); pause_visible_ = next;
}
void PixelRenderer::escape_menu() {
    if (mission4_quiz_visible_) return;
    if (sound_settings_visible_) sound_settings_visible_ = false;
    else if (pause_visible_) close_overlays();
    else if (modal_open()) close_overlays();
    else toggle_pause();
}
bool PixelRenderer::sound_settings_click(int x,int y,bool startup) {
    for (int row = 0; row < 4; ++row) {
        const int top = 98+row*36;
        if (y < top || y >= top+22) continue;
        double* levels[] = {&audio_settings_.master,&audio_settings_.effects,&audio_settings_.machinery,&audio_settings_.chatter};
        auto& value = *levels[row];
        if (x >= 263 && x < 284) value = std::clamp(value-0.1,0.0,1.0);
        else if (x >= 423 && x < 444) value = std::clamp(value+0.1,0.0,1.0);
        else if (x >= 288 && x <= 416) value = std::clamp((x-288)/128.0,0.0,1.0);
    }
    if (y >= 242 && y < 264) {
        if (x >= 168 && x < 312) audio_settings_.muted = !audio_settings_.muted;
        else if (x >= 320 && x < 472) {
            if (startup) { startup_page_=StartupPage::Main; startup_selection_=2; }
            else sound_settings_visible_=false;
        }
    }
    return true;
}
bool PixelRenderer::pause_click(int x,int y) {
    if (sound_settings_visible_) return sound_settings_click(x,y,false);
    if (x >= 192 && x < 448) {
        if (y >= 104 && y < 132) close_overlays();
        else if (y >= 140 && y < 168) sound_settings_visible_ = true;
        else if (y >= 176 && y < 204) menu_command_ = MenuCommand::Restart;
        else if (y >= 212 && y < 240) menu_command_ = MenuCommand::Quit;
    }
    return true;
}
void PixelRenderer::draw_sound_settings(bool startup) {
    glass_panel(152,48,336,268,13);
    text(224,66,"SOUND SETTINGS",0xdceeea,2);
    const char* labels[] = {"MASTER","EFFECTS","MACHINERY","ROBOT TALK"};
    const double levels[] = {audio_settings_.master,audio_settings_.effects,audio_settings_.machinery,audio_settings_.chatter};
    const auto button = [&](int x,int y,int w,const std::string& label,std::uint32_t color = 0xc7e4de) {
        const bool hover = pointer_x_ >= x && pointer_x_ < x+w && pointer_y_ >= y && pointer_y_ < y+26;
        glass_panel(x,y,w,26,7);
        if (hover) outline(x+3,y+3,w-6,20,0x7db4bf);
        text(x+(w-int(label.size())*6)/2,y+10,label,color);
    };
    for (int row = 0; row < 4; ++row) {
        const int y = 98+row*36;
        text(168,y+6,labels[row],0xb2cdd2);
        glass_panel(263,y,20,20,5); text(270,y+6,"-",0xe1e9dd);
        rectangle(288,y+8,128,5,0x243c4b);
        rectangle(288,y+8,int(128*levels[row]),5,0x82cdb9);
        rectangle(286+int(128*levels[row]),y+4,4,13,0xd0e9e3);
        glass_panel(423,y,20,20,5); text(430,y+6,"+",0xe1e9dd);
        text(450,y+6,std::to_string(int(std::lround(levels[row]*100)))+"%",0x9cbcc7);
    }
    button(168,242,144,audio_settings_.muted ? "SOUND OFF / U" : "SOUND ON / U");
    button(320,242,152,"BACK");
    text(202,288,startup ? "F11 FULLSCREEN / ESC BACK" : "CLICK OR DRAG / ESC BACK",0x7b9da9);
}
void PixelRenderer::draw_pause_menu() {
    for (auto& pixel : pixels_) pixel = blend(pixel,0x07101b,0.60);
    if (sound_settings_visible_) draw_sound_settings(false);
    else {
        const auto button = [&](int x,int y,int w,const std::string& label,std::uint32_t color = 0xc7e4de) {
            const bool hover = pointer_x_ >= x && pointer_x_ < x+w && pointer_y_ >= y && pointer_y_ < y+26;
            glass_panel(x,y,w,26,7);
            if (hover) outline(x+3,y+3,w-6,20,0x7db4bf);
            text(x+(w-int(label.size())*6)/2,y+10,label,color);
        };
        glass_panel(168,52,304,238,13);
        text(248,72,"PAUSED",0xdceeea,2);
        button(192,104,256,"RESUME");
        button(192,140,256,"SOUND SETTINGS");
        button(192,176,256,"RESTART MISSION");
        button(192,212,256,"QUIT GAME",0xe3ada1);
        text(239,268,"ESC / P RESUME",0x819fab);
    }
}
std::optional<Action> PixelRenderer::action_at(int x, int y, const Game& game) const {
    if (!crafting_visible_) return std::nullopt;
    const auto cards = recipes(game);
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const int cx = 40+int(i%3)*188, cy = 76+int(i/3)*76;
        if (x >= cx && x < cx+184 && y >= cy && y < cy+70 && ready(cards[i],game)) return cards[i].action;
    }
    return std::nullopt;
}
bool PixelRenderer::click(int x, int y, Game& game) {
    if (completion_screen(game)) {
        close_overlays();
        tasks_visible_=false;
        if (y>=180 && y<212) {
            if (x>=126 && x<306) { completion_selection_=0;completion_activate(game); }
            else if (x>=334 && x<514) { completion_selection_=1;completion_activate(game); }
        }
        return true;
    }
    if (pause_visible_) return pause_click(x,y);
    if(game.paused() && game.mission4_quiz_started()) return true;
    if(game.mission4_quiz_started()) {
        if(game.mission4_quiz_reading()) {
            if(x>=425 && x<590 && y>=270 && y<310) game.mission4_read_passage();
            return true;
        }
        if(game.mission4_quiz_answered()) {
            if(x>=430 && x<590 && y>=270 && y<310) game.mission4_advance_quiz();
            else if(x>=50 && x<205 && y>=270 && y<310) game.mission4_read_passage();
            return true;
        }
        if(x>=50 && x<205 && y>=270 && y<310) { game.mission4_read_passage(); return true; }
        if(y>=125 && y<269) {
            const int row=(y-125)/36;
            if(row>=0 && row<4 && x>=56 && x<584) game.mission4_answer(row);
        }
        return true;
    }
    if(game.observation_result()>=0) {
        if(x>=result_close_x && x<result_close_x+result_close_w && y>=result_close_y && y<result_close_y+result_close_h)
            game.close_observation_result();
        return true;
    }
    if(!modal_open() && game.near_observed_table() && game.observing()<0 && over_observation(x,y)) {
        if(y>=observe_y && (y-observe_y)%observe_pitch<observe_pitch-2) {
            const int sample=(y-observe_y)/observe_pitch;
            if(game.sample_observed(sample)) game.review_observation(sample);
            else if(!game.finished()) game.start_observation(sample);
        }
        return true;
    }
    const int interact_x=250,interact_y=240,interact_w=140;
    if(game.campaign() && !modal_open() && x>=interact_x && x<interact_x+interact_w && y>=interact_y && y<interact_y+25 && !game.interaction_label().empty()) { game.interact(); return true; }
    if (modal_open()) {
        if ((crafting_visible_ && x >= 475 && x < 607 && y >= 44 && y < 72) ||
            (inventory_visible_ && x >= 451 && x < 596 && y >= 44 && y < 72) ||
            (help_visible_ && x >= 491 && x < 585 && y >= 44 && y < 72)) close_overlays();
        else if (const auto action = action_at(x,y,game)) game.perform(*action);
        return true;
    }
    if (over_header(x,y)) {
        if (x < 66 && game.world().planar()) toggle_robot_model();
        else if (x >= 600) toggle_help();
        else if (x >= 250) toggle_tasks();
        return true;
    }
    if (game.campaign() ? (x>=185 && x<455 && y>=318 && y<346) : over_dock(x,y)) {
        if (game.campaign() || x >= 506) toggle_inventory();
        return true;
    }
    return !world_input_at(x,y,game);
}
void PixelRenderer::draw_overlays(const Game& game) {
    if ((!modal_open() && !game.mission4_quiz_started()) || pause_visible_) return;
    // Details appear only on demand. Menus use the same material sprites as tiles
    // and pickups, so players can follow recipes without reading a manual.
    for (auto& p : pixels_) p = 0xff000000|((p & 0xfefefe)>>1);
    std::string hover;
    if(game.mission4_quiz_started()) {
        panel(34,39,572,282,0x426b70);
        const int question=std::clamp(game.mission4_question(),0,Mission4::question_count-1);
        const int passage=Mission4::question_passages[question];
        text(52,51,"MARS EXPLORATION / DATA CHIP QUIZ",0xb7e1cc,2);
        text(52,76,"QUESTION "+std::to_string(question+1)+" OF "+std::to_string(Mission4::question_count),0xa9cbd5);
        if(game.mission4_quiz_reading()) {
            text(52,94,Mission4::passage_titles[passage],0xf0dca5);
            std::istringstream words(Mission4::passages[passage]);
            std::string word,line_text;
            int y=117;
            while(words>>word) {
                if(line_text.size()+word.size()+1>79) {
                    text(54,y,line_text,0xc2d7df); y+=15; line_text.clear();
                }
                if(!line_text.empty()) line_text+=' ';
                line_text+=word;
            }
            if(!line_text.empty()) text(54,y,line_text,0xc2d7df);
            glass_panel(422,270,168,34,7);outline(425,273,162,28,0x8fcdb6);
            text(441,283,game.mission4_quiz_rereading() ? "BACK TO QUESTION" : "START QUESTIONS",0xc1f5df);
        } else {
            const auto& q=Mission4::questions[question];
            if(std::string(q.prompt).size()>86) {
                const std::string prompt=q.prompt;const auto split=prompt.rfind(' ',86);
                text(52,91,prompt.substr(0,split),0xe4d7b8);text(52,104,prompt.substr(split+1),0xe4d7b8);
            } else text(52,96,q.prompt,0xe4d7b8);
            for(int choice=0;choice<4;++choice) {
                const int y=124+choice*36;
                const bool selected=game.mission4_selected_answer()==choice;
                const bool correct=selected && game.mission4_quiz_answered();
                const bool wrong=selected && game.mission4_quiz_wrong();
                panel(56,y,528,31,correct ? 0x649b7e : wrong ? 0x815b5b : 0x344b5b);
                text(70,y+11,std::string(1,char('A'+choice))+". "+q.choices[choice],correct ? 0xa8f0be : wrong ? 0xf0aaa0 : 0xc2d7df);
            }
            if(game.mission4_quiz_answered()) {
                glass_panel(50,270,155,34,7);text(67,283,"READ PASSAGE AGAIN",0xc1f5df);
                glass_panel(430,270,160,34,7);outline(433,273,154,28,0x8fcdb6);text(470,283,"CORRECT / CONTINUE",0xc1f5df);
            } else {
                glass_panel(50,270,155,34,7);text(67,283,"READ PASSAGE AGAIN",0xc1f5df);
                text(226,283,game.mission4_quiz_wrong() ? "NOT QUITE / REREAD THE PASSAGE OR CHOOSE AGAIN" : "SELECT AN ANSWER",game.mission4_quiz_wrong() ? 0xf0aaa0 : 0x9db7bf);
            }
        }
    } else if (crafting_visible_) {
        panel(26,43,588,271,0x426b70);
        icon(38,51,Icon::Bench); text(61,56,"CRAFT",0xb7e1cc);
        text(475,56,"C / ESC CLOSE",0x7998a7);
        const auto cards = recipes(game);
        for (std::size_t i = 0; i < cards.size(); ++i) {
            const auto& card = cards[i];
            const bool can_make = ready(card,game);
            const int x = 40+int(i%3)*188, y = 76+int(i/3)*76;
            const bool hovered = pointer_x_ >= x && pointer_x_ < x+184 && pointer_y_ >= y && pointer_y_ < y+70;
            panel(x,y,184,70,can_make ? hovered ? 0x9de7c8 : 0x547f75 : 0x2b4051);
            text(x+9,y+9,card.label,can_make ? 0xdce8d5 : 0x718b9e); keycap(x+158,y+5,card.key);
            for (int j = 0; j < 2; ++j) {
                const auto input = card.inputs[j], output = card.outputs[j];
                if (input.quantity > 0) {
                    const int ix = x+10+j*31;
                    icon(ix+3,y+29,item_icon(input.item));
                    text(ix,y+52,quantity(input.item,input.quantity),
                        game.unit().inventory().count(input.item) >= input.quantity ? 0x9fbeae : 0xbd8579);
                    if (hovered && pointer_x_ >= ix && pointer_x_ < ix+31)
                        hover = std::string(item_name(input.item))+" / NEED "+quantity(input.item,input.quantity);
                }
                if (output.quantity > 0) {
                    const int ox = x+109+j*34;
                    icon(ox+3,y+29,item_icon(output.item));
                    text(ox,y+52,quantity(output.item,output.quantity),0x8fc5ae);
                    if (hovered && pointer_x_ >= ox && pointer_x_ < ox+34)
                        hover = std::string(item_name(output.item))+" / +"+quantity(output.item,output.quantity);
                }
            }
            line(x+79,y+39,x+97,y+39,can_make ? 0xa5d5b7 : 0x506677);
            line(x+93,y+35,x+97,y+39,0x73998c); line(x+97,y+39,x+93,y+43,0x73998c);
            const double cost = Game::action_battery_cost(card.action);
            std::ostringstream energy; energy << '-' << cost << '%';
            text(x+76,y+52,energy.str(),game.unit().battery_percent() >= cost ? 0xc8a975 : 0xe68d7f);
            if (i >= 6) {
                const auto upgrade = i == 6 ? Upgrade::SolarThermalDrill : i == 7 ? Upgrade::DustRepellentTreads : Upgrade::RadiationShielding;
                icon(x+122,y+27,i == 6 ? Icon::Drill : i == 7 ? Icon::Treads : Icon::Shield,2);
                if (game.unit().has_upgrade(upgrade)) { line(x+160,y+52,x+163,y+55,0x98e4c0); line(x+163,y+55,x+170,y+46,0x98e4c0); }
            }
        }
        if (game.presentation().active_activity && game.presentation().active_activity->crafting) {
            const auto& activity = *game.presentation().active_activity;
            rectangle(61,69,int(536*activity.age/activity.duration),2,0x8bd9b5);
        }
    } else if (inventory_visible_) {
        panel(36,43,568,270,0x426b70);
        icon(48,51,Icon::Tasks); text(72,56,game.mission1() ? "SUPPLIES" : game.mission2() ? "CARRIER PARTS" : game.mission_number()==3 ? "RECORDS" : "MATERIALS",0xb7e1cc); text(451,56,"TAB / ESC CLOSE",0x7998a7);
        if(game.mission2()) {
            panel(52,82,536,70,0x5b8490);
            icon(70,92,Icon::RobotArm,3);
            text(130,94,"CARRIER REPAIR PARTS",0xb1f5c8,2);
            text(130,123,"WHEEL + ROBOT ARM + CONTROL CHIP",0x9ebdca);
            const int found=int(game.carrier_wheel_collected())+int(game.carrier_arm_collected())+int(game.carrier_chip_collected());
            text(455,102,std::to_string(found)+" / 3",found==3 ? 0xa4efbd : 0xf2d497,2);
            const char* names[]={"WHEEL","ROBOT ARM","CONTROL CHIP"};
            const Icon icons[]={Icon::Wheel,Icon::RobotArm,Icon::Chip};
            const bool owned[]={game.carrier_wheel_collected(),game.carrier_arm_collected(),game.carrier_chip_collected()};
            for(int i=0;i<3;++i) {
                const int x=52+i*179;
                panel(x,164,168,125,owned[i] ? 0x5a8d78 : 0x3c4b56);
                icon(x+12,177,icons[i],2); text(x+52,181,names[i],owned[i] ? 0xd7eef1 : 0x849496);
                text(x+12,222,owned[i] ? "COLLECTED" : "NOT FOUND",owned[i] ? 0x9cf0b8 : 0x7f9098);
                text(x+12,250,owned[i] ? "READY TO INSTALL" : i==2 ? "STORAGE ROOM" : "CARGO HANGAR",0xaac8d2);
                rectangle(x+12,273,144,3,0x263a43);
                if(owned[i]) rectangle(x+12,273,144,3,0x83ddb0);
            }
            return;
        }
        if(game.mission_number()==3 || game.mission_number()==4) {
            if(game.mission4_quiz_complete()) {
                panel(52,82,536,90,0x5a8d78);
                text(72,102,"USB AND MARS CHIP USED",0xb1f5c8,2);
                text(72,136,"VERIFIED DATA ARCHIVED AT THE MONITOR ROOM PC",0xaac8d2);
                return;
            }
            panel(52,82,536,70,0x5b8490); icon(70,92,Icon::Core,3);
            text(130,94,"USB + MARS DATA CHIP",0xb1f5c8,2);
            text(130,123,game.usb_obtained() ? "S-01 COORDINATES: (119,60)" : "COLLECT THE USB AT THE MONITOR ROOM PC",0x9ebdca);
            text(455,102,game.records_copied() ? "1 / 1" : "0 / 1",game.records_copied() ? 0xa4efbd : 0xf2d497,2);
            panel(52,164,260,125,game.usb_obtained() ? 0x5a8d78 : 0x3c4b56);
            icon(68,180,Icon::USB,2);text(116,184,"USB DRIVE",0xd7eef1);
            text(68,220,game.usb_record_uploaded() ? "SCAN RECORD UPLOADED" : game.usb_scan_recorded() ? "S-01 SCAN SAVED ON USB" : game.usb_obtained() ? "COORDINATES READY" : "COLLECT FROM THE PC",0xaac8d2);
            text(68,246,game.usb_obtained() ? "USB / 1" : "USB / 0",0x9cf0b8);
            panel(322,164,260,125,game.data_chip_collected() ? 0x5a8d78 : 0x3c4b56);
            icon(338,180,Icon::Chip,2);text(386,184,"MARS CHIP",0xd7eef1);
            text(338,220,game.data_chip_collected() ? "MARS EXPLORATION DATA" : "FIND ON THE RETURN ROUTE",0xaac8d2);
            text(338,246,game.data_chip_collected() ? "CHIP / 1" : "CHIP / 0",0x9cf0b8);
            return;
        }
        if(game.mission1()) {
            panel(52,82,536,70,0x5b8490);
            icon(70,92,Icon::Health,3);
            text(130,94,"MISSION SUPPLIES",0xb1f5c8,2);
            text(130,123,"FIRST AID "+std::to_string(game.unit().inventory().count(Item::FirstAidKit))+"/3",0x9ebdca);
            icon(267,118,Icon::FoodWater);
            text(290,123,"FOOD/WATER "+std::to_string(game.unit().inventory().count(Item::FoodWaterSupply))+"/2",0x9ebdca);
            const auto boxes=game.unit().inventory().count(Game::mission_supply_item)+game.unit().inventory().count(Item::FoodWaterSupply);
            text(455,102,std::to_string(boxes)+" / 5",boxes==5 ? 0xa4efbd : 0xf2d497,2);
            for(int sample=0;sample<3;++sample) {
                const int x=52+sample*179;
                const bool owned=game.sample_obtained(sample), done=game.sample_observed(sample);
                panel(x,164,168,125,owned ? (done ? 0x5a8d78 : 0x607f91) : 0x3c4b56);
                icon(x+12,177,sample==0 ? Icon::Silicate : sample==1 ? Icon::Ice : Icon::RockSample,2);
                text(x+52,181,Game::sample_names[sample],owned ? 0xd7eef1 : 0x849496);
                text(x+12,222,game.observing()==sample ? "ON TABLE" : owned ? "1 SAMPLE" : "NOT FOUND",owned ? 0xf0dca5 : 0x7f9098);
                text(x+12,250,done ? "OBSERVED" : game.observing()==sample ? "OBSERVING..." : "PENDING",done ? 0x9cf0b8 : 0xaac8d2);
                rectangle(x+12,273,144,3,0x263a43);
                if(done) rectangle(x+12,273,144,3,0x83ddb0);
            }
            const Item minerals[]{Item::IronNickelOre,Item::HydratedMinerals,Item::WaterIce};
            const char* mineral_names[]{"ORE","MINERALS","ICE"};
            for(int i=0;i<3;++i) {
                const int x=52+i*179;glass_panel(x,291,168,20,4);
                icon(x+4,293,item_icon(minerals[i]));
                text(x+27,298,std::string(mineral_names[i])+" / "+quantity(minerals[i],game.unit().inventory().count(minerals[i])),0xaac8d2);
            }
            return;
        }
        static const char* names[] = {"DUST","MINERALS","ORE","ICE","WATER","SCRAP","H2 GAS","O2 GAS","LH2","LOX","FUEL","PLATES","POLYMER","CORE","SLAG"};
        for (std::size_t i = 0; i < static_cast<std::size_t>(Item::FoodWaterSupply); ++i) {
            const int x = 48+int(i%5)*110, y = 79+int(i/5)*73;
            const auto item = static_cast<Item>(i);
            const bool hovered = pointer_x_ >= x && pointer_x_ < x+104 && pointer_y_ >= y && pointer_y_ < y+66;
            rectangle(x,y,104,66,hovered ? 0x283e4e : 0x182b3d);
            icon(x+6,y+10,item_icon(item),2);
            text(x+43,y+13,names[i],0x87a1b3);
            text(x+43,y+30,quantity(item,game.unit().inventory().count(item)),0xcae4d8);
            if (hovered) hover = item_name(item);
        }
    } else if (help_visible_) {
        if(game.rescue_mission()) {
            if(game.rescue_mission()==8) {
                panel(48,43,544,270,0x426b70);text(72,62,"MISSION 8 / REPAIR AND RESCUE",0xb1f5c8,2);
                text(72,114,"APPROACH S-01 / PRESS E TO REPAIR",0xe4d7b8);
                text(72,146,"USES THE MATERIALS GATHERED IN MISSION 6",0xa9cbd5);
                text(72,178,"STAY CLOSE FOR SIX SECONDS / PAUSE FREEZES REPAIR",0xa9cbd5);
                text(72,210,"C-02 THEN TRANSFERS ASTRONERD INTO S-01",0x87dba4);
                text(72,242,"C-02 THEN U-01 GIVE ALL POWER / S-01 LAUNCHES",0x87dba4);
                text(72,278,"H / ESC CLOSE",0x7998a7);return;
            }
            panel(48,43,544,270,0x426b70);text(72,62,game.rescue_mission()==6 ? "MISSION 6 / REPAIR SUPPLIES" : "MISSION 7 / ASTRONERD TRANSPORT",0xb1f5c8,2);
            if(game.rescue_mission()==6) {
                text(72,114,"GO TO THE STORAGE ROOM ON MAP2",0xe4d7b8);
                text(72,146,"E COLLECTS HULL PLATES, WIRING AND SEALANT",0xa9cbd5);
                text(72,178,"GREEN MARKERS SHOW THE THREE SUPPLY CACHES",0x87dba4);
            } else {
                text(72,114,"CARGO HANGAR / E TO COMMUNICATE WITH C-02",0xe4d7b8);
                text(72,146,"WATCH SEAT INSTALLATION AND THE ASTRONERD PICKUP",0xa9cbd5);
                text(72,178,"CONTROL RESUMES OUTSIDE THE BASE ON MAP1",0xa9cbd5);
                text(72,210,"LEAD U-01 TO S-01 AT (119,60) / C-02 FOLLOWS",0x87dba4);
                text(72,242,"USE THE CLIFF GAP / WAIT FOR C-02 AT S-01",0xa9cbd5);
            }
            text(72,278,"H / ESC CLOSE",0x7998a7);return;
        }
        if(game.placeholder_mission()==4) {
            panel(48,43,544,270,0x426b70);text(72,62,"MISSION 4 / MARS DATA CHIP",0xb1f5c8,2);
            text(72,114,"RECOVER THE DATA CHIP ON THE SURFACE",0xe4d7b8);
            text(72,146,"RETURN THROUGH THE AIRLOCK TO THE SHIP",0xa9cbd5);
            text(72,178,"TAKE THE CHIP TO THE MONITOR ROOM PC",0x87dba4);
            text(72,210,"READ EACH PASSAGE / ANSWER ALL QUESTIONS",0xa9cbd5);
            text(72,242,"WRONG ANSWERS CAN BE TRIED AGAIN",0xa9cbd5);
            text(72,278,"H / ESC CLOSE",0x7998a7);return;
        }
        if(game.mission_number()==3) {
            panel(48,43,544,270,0x426b70);text(72,62,"MISSION 3 / USB AND COORDINATES",0xb1f5c8,2);
            text(72,114,"GET USB AND COORDINATES AT THE MONITOR ROOM PC",0xe4d7b8);
            text(72,146,"PRESS E TO PREPARE AND COLLECT THE USB",0xa9cbd5);
            text(72,178,"THEN USB READY / S-01 COORDINATES (119,60) / E TO SCAN",0xa9cbd5);
            text(72,210,"SCAN FOR FOUR SECONDS / WALKING AWAY CANCELS",0xa9cbd5);
            text(72,242,"PAUSING FREEZES COPY PROGRESS",0xa9cbd5);
            text(72,278,"H / ESC CLOSE",0x7998a7);return;
        }
        if(game.mission2()) { panel(48,43,544,270,0x426b70); text(72,62,"MISSION 2 / CARRIER REPAIR",0xb1f5c8,2); text(72,106,"SCAN THE BROKEN CARRIER IN THE CARGO HANGAR",0xe4d7b8); text(72,138,"COLLECT THE WHEEL AND ROBOT ARM FROM HANGAR TABLES",0xa9cbd5); text(72,170,"FIND THE CONTROL CHIP ON THE STORAGE ROOM TABLE",0xa9cbd5); text(72,202,"RETURN TO THE CARRIER AND PRESS E TO REPAIR",0xa9cbd5); text(72,234,"STAY CLOSE FOR SIX SECONDS",0x87dba4); text(72,278,"H / ESC CLOSE",0x7998a7); return; }
        if(game.mission1()) { panel(48,43,544,270,0x426b70); text(72,62,"MISSION 1 / ACT 1",0xb1f5c8,2); text(72,106,"FIND FIVE FOOD AND WATER SUPPLEMENT BOXES",0xe4d7b8); text(72,132,"GREEN OUTLINES MARK SUPPLIES",0x87dba4); text(72,158,"J / LEFT CLICK BREAKS MATERIAL BARRIERS",0xa9cbd5); text(72,184,"COLLECT NEAR A BOX / DELIVER TO ASTRONAUT",0xa9cbd5); text(72,210,"TAKE EACH SAMPLE TO THE OBSERVED TABLE",0xa9cbd5); text(72,236,"ONE SAMPLE AT A TIME / OBSERVE FOR 6 SEC",0xa9cbd5); text(72,278,"H / ESC CLOSE",0x7998a7); return; }
        panel(48,43,544,270,0x426b70); text(66,56,"UNIT-01 / FIELD GUIDE",0xd6dcb9);
        text(491,56,"H / ESC CLOSE",0x7998a7);
        struct Control { Icon icon; const char* key; const char* label; };
        const Control controls[] = {{Icon::Treads,"A D","MOVE"},{Icon::Jump,"SPACE","JUMP"},
            {Icon::Climb,"W","CLIMB"},{Icon::Laser,"J/LMB","HOLD TO MINE"},{Icon::Dust,"J/RMB","PLACE DUST"},
            {Icon::Bench,"C","CRAFT AT BASE"},{Icon::Tasks,"TAB","MATERIALS"},{Icon::Rocket,"R","REPAIR ROCKET"}};
        for (int i = 0; i < 8; ++i) {
            const int x = 64+(i%4)*130, y = 83+(i/4)*71;
            icon(x+6,y,game.world().planar() && i == 2 ? Icon::Tasks : controls[i].icon,2);
            keycap(x+45,y+9,game.world().planar() && i == 0 ? "WASD" : game.world().planar() && i == 2 ? "T" : controls[i].key);
            const char* label = game.world().planar() && i == 0 ? "WASD MOVE" : game.world().planar() && i == 1 ? "JUMP" :
                game.world().planar() && i == 2 ? "QUESTS" : controls[i].label;
            text(x+1,y+42,label,0x8eacb8);
        }
        text(67,239,"FUEL",0xc9d5bd);
        const Icon flow[] = {Icon::Mineral,Icon::Water,Icon::Hydrogen,Icon::LH2,Icon::Fuel};
        const char* keys[] = {"E","O","L","F"};
        for (int i = 0; i < 5; ++i) {
            const int x = 117+i*87;
            icon(x,235,flow[i],2);
            if (i == 2) icon(x+24,252,Icon::Oxygen);
            if (i == 3) icon(x+24,252,Icon::LOX);
            if (i < 4) { line(x+37,250,x+69,250,0x7eaa9b); keycap(x+46,271,keys[i]); }
        }
        text(66,297,"X FIRE  SHIFT+LMB FIRE  K ROBOT  B LIGHT  U SOUND  F9 SHIP STYLE  F11 FULLSCREEN",0x66869b);
    }
    if (!hover.empty()) inspection_text_ = hover;
}
GridPos PixelRenderer::screen_to_tile(int x, int y) const {
    if (planar_) return isometric_.pick(x,y);
    return {int(std::floor((x+camera_x_)/double(tile_size))),int(std::floor((y-scene_top+camera_y_)/double(tile_size)))};
}
Vec2 PixelRenderer::tile_to_screen(Vec2 point, const WorldGrid& world) const {
    if (world.planar()) return isometric_.project(point,world);
    return {point.x*tile_size-camera_x_,point.y*tile_size-camera_y_+scene_top};
}
Vec2 PixelRenderer::item_to_screen(GridPos cell, const WorldGrid& world) const {
    return world.planar() ? isometric_.item_position(cell,world) : tile_to_screen({cell.x+0.5,cell.y+0.5},world);
}
Vec2 PixelRenderer::combat_target_at(int x, int y, const Game& game) const {
    double closest = 1e9;
    std::optional<Vec2> chosen;
    for (const auto& creature : game.danger().creatures()) {
        if (creature.health <= 0) continue;
        const auto screen = tile_to_screen(creature.position,game.world());
        const double dx = (screen.x-x)/13.0, dy = (screen.y-5-y)/11.0;
        const double score = dx*dx+dy*dy;
        if (score < 1 && score < closest) { closest = score; chosen = creature.position; }
    }
    if (chosen) return *chosen;
    const auto tile = screen_to_tile(x,y);
    return {tile.x+0.5,tile.y+0.5};
}
bool PixelRenderer::world_input_at(int x, int y, const Game& game) const {
    if(game.in_spaceship() || game.transitioning()) return false;
    if (x < 0 || x >= width || y < 0 || y >= scene_bottom || game.finished() || modal_open() || game.observation_result()>=0) return false;
    if(game.near_observed_table() && game.observing()<0 && over_observation(x,y)) return false;
    if(game.campaign() && x>=250 && x<390 && y>=240 && y<265 && !game.interaction_label().empty()) return false;
    if (over_header(x,y) || (y < scene_top && !game.world().planar())) return false;
    if ((game.campaign() ? (x>=185 && x<455 && y>=318 && y<346) : over_dock(x,y)) || y >= inspection_y) return false;
    if (game.world().planar() && Minimap::contains(x,y)) return false;
    if (game.survival_enabled()) {
        if (x >= 12 && x < 208 && y >= 46 && y < 90) return false;
        if ((game.unit().battery_percent() < 40 || game.unit().integrity() < 70) && x >= 12 && x < 208 && y >= 162 && y < 196) return false;
        if ((game.danger().warning() || game.danger().active()) && x >= 12 && x < 220 && y >= 98 && y < 156) return false;
        if (game.inside_base() && x >= 196 && x < 404 && y >= 276 && y < 307) return false;
    }
    if (tasks_visible_) return !(x >= 446 && x < 628 && y >= 46 && y < 206);
    return !(x >= 430 && x < 628 && y >= 46 && y < 94);
}
void PixelRenderer::render(const Game& game) {
    inspection_text_.clear();
    planar_ = game.world().planar();
    mission4_quiz_visible_=game.mission4_quiz_started();
    if(game.in_spaceship()) { draw_ship_interior(game); return; }
    if (planar_) {
        isometric_.render(pixels_,game,headlamp_on_,robot_);
        draw_hud(game);
        if(game.campaign()) { glass_panel(12,292,69,17,5); text(23,298,game.map_name(),0xbad9ec); }
        if(game.transitioning()) for(auto& p:pixels_) p=blend(p,0,game.transition_alpha());
        return;
    }
    rectangle(0,0,width,height,0x090e20);
    const auto& world = game.world();
    const auto center = game.unit().center();
    const auto& animation = game.presentation();
    const double time = animation.clock;
    const double target_x = std::clamp(int(center.x*tile_size)-width/2,0,std::max(0,world.width()*tile_size-width));
    const double target_y = std::clamp(int(center.y*tile_size)-140,0,std::max(0,world.height()*tile_size-(scene_bottom-scene_top)));
    if (last_clock_ < 0 || time < last_clock_) { smooth_x_ = target_x; smooth_y_ = target_y; }
    const double smoothing = 1-std::exp(-std::max(0.0,time-last_clock_)*10);
    smooth_x_ += (target_x-smooth_x_)*smoothing;
    smooth_y_ += (target_y-smooth_y_)*smoothing;
    last_clock_ = time;
    camera_x_ = int(std::round(smooth_x_)); camera_y_ = int(std::round(smooth_y_));
    auto sx = [&](double x) { return int(x*tile_size)-camera_x_; };
    auto sy = [&](double y) { return int(y*tile_size)-camera_y_+scene_top; };
    SpaceBackground::draw(pixels_,{width,height,scene_top,scene_bottom,double(camera_x_),double(camera_y_),
                                  double(sy(world.surface_y())),time});
    for (int y = std::max(0,camera_y_/tile_size); y < std::min(world.height(),(camera_y_+scene_bottom-scene_top)/tile_size+1); ++y) {
        for (int x = camera_x_/tile_size; x < std::min(world.width(),(camera_x_+width)/tile_size+1); ++x) {
            const auto type = world.get_tile_type(x,y);
            if (type == TileType::VacuumAir) continue;
            int px = sx(x), py = sy(y);
            if (type == TileType::Scaffold) {
                rectangle(px+1,py,2,tile_size,0x776b48); rectangle(px+13,py,2,tile_size,0x776b48);
                rectangle(px+1,py+7,14,1,0xb4a16c); line(px+2,py+1,px+13,py+14,0x5f5740);
                continue;
            }
            auto color = tile_definition(type).color;
            if (type == TileType::HydratedMinerals) color = 0x344d53;
            if (type == TileType::IronNickelOre) color = 0x49464b;
            if (type == TileType::Regolith) color = 0x675d59;
            rectangle(px,py,tile_size,tile_size,color);
            rectangle(px,py+tile_size-1,tile_size,1,(color & 0xfefefe)>>1);
            rectangle(px+2+(x*7+y*3)%9,py+3,2,2,0x30323c);
            rectangle(px+2,py+10,3,1,0x3a3940);
            if (type == TileType::HydratedMinerals || type == TileType::IronNickelOre || type == TileType::WaterIce)
            {
                const auto vein = type == TileType::IronNickelOre ? 0xd8a36c : 0x75bcb9;
                rectangle(px+5,py+5,4,3,vein); rectangle(px+9,py+3,2,2,vein);
                rectangle(px+3,py+8,2,2,vein);
            }
            if (type == TileType::NASADataCore) {
                rectangle(px+3,py+3,10,10,0x172c33); rectangle(px+6,py+4,4,7,0x88ffc3);
            }
            if (y == world.surface_y()) rectangle(px,py,tile_size,2,0xa09178);
            const double progress = world.mining_progress(x,y);
            if (progress > 0) {
                line(px+7,py+2,px+6,py+7,0x182331);
                line(px+6,py+7,px+10,py+11,0x182331);
                if (progress > 0.35) line(px+6,py+7,px+2,py+10,0x182331);
                if (progress > 0.7) line(px+6,py+7,px+13,py+4,0x182331);
            }
        }
    }
    // Base station: solar panels, powered cryocooler and crafting console.
    const auto base = game.refinery().position();
    const int bx = sx(base.x), by = sy(world.surface_y());
    rectangle(bx-28,by-31,23,12,0x2c4d78);
    for (int i = 0; i < 4; ++i) rectangle(bx-26+i*6,by-30,1,10,0x68a0c7);
    rectangle(bx-17,by-19,2,19,0x89929b);
    rectangle(bx-7,by-21,20,21,0x35465e); rectangle(bx-5,by-19,16,9,0x75d9d1);
    rectangle(bx+13,by-13,9,13,0x8595a0); rectangle(bx-5,by-8,3,3,0xeec66d);
    const bool processing = animation.active_activity && animation.active_activity->crafting;
    const int scan = int(time*18)%7;
    rectangle(bx-4,by-18+scan,14,1,processing ? 0xf8d586 : 0xc0f9df);
    rectangle(bx+15,by-10,5,2,processing && int(time*14)%2 ? 0xffce7e : 0x54887f);
    text(bx-20,by-42,"ISRU",0x96ded8);
    // Decorative rocket body; interaction/collision uses explicit scaffold tiles.
    const auto socket = game.rocket().ground_socket();
    const double lift = game.rocket().launched() ? game.climax().launch_elapsed()*game.climax().launch_elapsed()*5 : 0;
    const int rocket_height = 16*tile_size;
    const int rx = sx(socket.x), bottom = sy(world.surface_y()-lift), rocket_top = bottom-rocket_height;
    rectangle(rx-10,rocket_top+19,20,rocket_height-30,game.rocket().hull_welded() ? 0xc3cdd5 : 0x7b7884);
    for (int i = 0; i < 10; ++i) rectangle(rx-i,rocket_top+2*i,2*i+1,2,0xc3cdd5);
    rectangle(rx-8,rocket_top+42,16,14,0x2e4c68); rectangle(rx-5,rocket_top+45,10,8,0x74c8da);
    rectangle(rx-10,rocket_top+81,20,5,0xbd6d55); rectangle(rx-10,rocket_top+137,20,5,0xbd6d55);
    rectangle(rx-7,bottom-11,14,11,0x364152);
    line(rx-10,bottom-40,rx-21,bottom-3,0x8a9dae); line(rx+10,bottom-40,rx+21,bottom-3,0x8a9dae);
    if (game.rocket().launched()) {
        const int flicker = int(time*41)%9;
        rectangle(rx-8,bottom,16,20+flicker,0xf9a263); rectangle(rx-5,bottom,10,15+flicker,0xffedb3);
        rectangle(rx-2,bottom+3,4,30+flicker,0xffd57e);
    }
    const auto valve = game.rocket().launch_valve();
    rectangle(sx(valve.x)-3,sy(valve.y)-3,7,7,game.rocket().calibrated() ? 0x83ffbd : 0xd5a853);
    // Independent presentation poses keep the physics body stable during bobbing,
    // landing squash, eye blinking and the mining/crafting arm movement.
    const auto pos = game.unit().position();
    const int squash = std::clamp(int(std::lround(-animation.body_bounce*12)),0,2);
    const int ux = sx(pos.x)-2, uy = sy(pos.y)-3;
    const int body_y = uy-int(std::lround(animation.body_bounce*tile_size));
    const int head_y = uy-int(std::lround(animation.head_bounce*tile_size));
    const int track_frame = int(animation.tread_phase)%4;
    rectangle(ux,uy+12,15,4,0x111d2b); rectangle(ux+1,uy+12,13,1,0x596477);
    for (int i = 0; i < 3; ++i) rectangle(ux+1+(i*4+track_frame)%12,uy+14,2,1,0x8a98a6);
    rectangle(ux+3,body_y+6,9,7-squash,game.unit().alive() ? 0xd6a950 : 0x736951);
    rectangle(ux+4,body_y+7,7,1,0xffd57d); rectangle(ux+6,body_y+10,3,2,0x876842);
    rectangle(ux+6,head_y+3,3,3,0x798b9c);
    const int look = animation.mining_target ? (animation.mining_target->x+0.5 > center.x ? 1 : -1) : animation.facing;
    const int eye_x = ux+2+look;
    rectangle(eye_x,head_y,12,5,0x99aebb); rectangle(eye_x,head_y,12,1,0xc7d9df);
    const double eye_open = animation.eye_openness();
    const int eye_height = std::max(1,int(std::lround(2*eye_open)));
    for (int offset : {1,7}) {
        const int x = eye_x+offset;
        if (game.unit().alive() && eye_open > 0.08) {
            for (int yy = head_y+1; yy < head_y+5; ++yy) for (int xx = x-1; xx < x+5; ++xx)
                if (xx >= 0 && xx < width && yy >= 0 && yy < height)
                    pixels_[yy*width+xx] = blend(pixels_[yy*width+xx],0x49baff,0.17*eye_open);
            rectangle(x,head_y+2,4,eye_height,0x49baff);
            if (eye_open > 0.65) rectangle(x+1,head_y+2,1,1,0xc0eaff);
        } else rectangle(x,head_y+2,4,1,0x243447);
    }
    const int arm_x = look > 0 ? ux+13 : ux-2;
    const int arm_y = body_y+7+(processing ? int(std::sin(time*16)*2) : 0);
    rectangle(arm_x,arm_y,4,3,0x859aa8); rectangle(arm_x+(look > 0 ? 3 : -1),arm_y,2,2,0xf6cc7a);
    if (game.unit().has_upgrade(Upgrade::RadiationShielding)) rectangle(ux+2,body_y+6,1,6,0xa5e0be);
    if (animation.mining_target) {
        const auto target = *animation.mining_target;
        const int tx = sx(target.x+0.5), ty = sy(target.y+0.5);
        const int ax = arm_x+2, ay = arm_y+1;
        line(ax,ay-1,tx,ty-1,0x35686c); line(ax,ay+1,tx,ty+1,0x35686c);
        line(ax,ay,tx,ty,int(time*24)%2 ? 0xb9fff0 : 0x73e6d7);
        rectangle(tx-2,ty-2,5,5,0xffd697); rectangle(tx,ty,2,2,0xffffff);
        outline(sx(target.x)-1,sy(target.y)-1,tile_size+2,tile_size+2,0x87ebd4);
        rectangle(sx(target.x),sy(target.y)+tile_size+2,tile_size,2,0x1b2939);
        rectangle(sx(target.x),sy(target.y)+tile_size+2,int(tile_size*animation.mining_progress),2,0x8df3d6);
    }
    for (const auto& p : animation.particles()) {
        const double fade = 1-p.age/p.lifetime;
        const auto r = unsigned(((p.color>>16)&255)*fade), g = unsigned(((p.color>>8)&255)*fade), b = unsigned((p.color&255)*fade);
        rectangle(sx(p.position.x),sy(p.position.y),p.size,p.size,(r<<16)|(g<<8)|b);
    }
    if (animation.active_activity) {
        const auto& activity = *animation.active_activity;
        const int cx = sx(activity.position.x), cy = sy(activity.position.y)-25;
        const double phase = activity.age/activity.duration;
        for (int i = 0; i < 6; ++i) {
            const double angle = time*4+i*1.0472;
            const int radius = 12+int(phase*12);
            rectangle(cx+int(std::cos(angle)*radius),cy+int(std::sin(angle)*radius*0.55),2,2,0x8df3d6);
        }
        const int label_x = std::clamp(cx-20,8,width-66);
        const int label_y = std::clamp(cy-22-int(phase*9),scene_top+4,scene_bottom-25);
        Icon output = activity.item != Item::Count ? item_icon(activity.item) :
            activity.label.find("DRILL") != std::string::npos ? Icon::Drill :
            activity.label.find("TREADS") != std::string::npos ? Icon::Treads :
            activity.label.find("SHIELD") != std::string::npos ? Icon::Shield : Icon::Rocket;
        const std::string value = activity.quantity > 0 ? "+"+quantity(activity.item,activity.quantity) : "+";
        panel(label_x-3,label_y-2,26+int(value.size())*6,21,0x557f78);
        icon(label_x,label_y,output);
        text(label_x+20,label_y+5,value,0xb5f5da);
    }
    if (game.climax().storm().active() && !game.rocket().launched()) {
        const int front = sx(game.climax().storm().front_x(world.width()));
        for (int i = 0; i < 100; ++i) {
            const int x = (i*97+int(game.simulated_seconds()*90))%width;
            const int y = scene_top+(i*31+int(game.simulated_seconds()*140))%(scene_bottom-scene_top);
            if (x < front && y < sy(world.surface_y())) line(x,y,x-3,y+7,0xb66366);
        }
    }
    draw_hud(game);
}
void PixelRenderer::draw_hud(const Game& game) {
    if (completion_screen(game)) {
        close_overlays();
        tasks_visible_=false;
    }
    const auto& world = game.world();
    const auto& animation = game.presentation();
    const int task_count=game.rescue_mission() ? 3 : game.placeholder_mission()==4 ? 1 : game.mission2() ? (!game.carrier_scanned() ? 1 :
        game.carrier_wheel_collected() && game.carrier_arm_collected() && game.carrier_chip_collected() ? 4 : 3) :
        game.campaign() ? (game.mission_number()==3 ? 1 : 3) : 6;
    if(game.campaign() && !modal_open() && !game.interaction_label().empty()) {
        panel(250,240,140,25,0x74d69b);
        const auto prompt=std::string("E / ")+game.interaction_label();
        text(std::max(12,(width-int(prompt.size())*6)/2),249,prompt,0xb1f5c8);
    }
    const auto center = game.unit().center();
    const double time = animation.clock;
    if (game.survival_enabled() && !game.finished() && (game.danger().warning() || game.danger().active())) {
        // A slow 0.6 Hz breathing rim leaves the center of the playfield clear.
        const double pulse = 0.5+0.5*std::sin(time*3.7699111843);
        const double intensity = (game.danger().active() ? 0.23 : 0.12)+pulse*0.15+game.attack_flash()*0.45;
        for (int y = scene_top; y < scene_bottom; ++y) for (int x = 0; x < width; ++x) {
            const int edge = std::min({x,width-1-x,y-scene_top,scene_bottom-1-y});
            if (edge >= 12) continue;
            const double alpha = intensity*(1-edge/12.0);
            const auto old = pixels_[y*width+x]; std::uint32_t color = 0;
            for (int shift : {0,8,16}) color |= unsigned(((old>>shift)&255)*(1-alpha)+((0xed3f3du>>shift)&255)*alpha)<<shift;
            pixels_[y*width+x] = 0xff000000|color;
        }
    }
    // Structural hits and terminal collapse receive their own sharper red rim;
    // this remains distinct from the slower creature-warning pulse.
    const double integrity_warning = game.unit().integrity() < 25 ?
        (0.04+0.04*(0.5+0.5*std::sin(time*6.0)))*(1-game.unit().integrity()/25.0) : 0;
    const double damage_rim = std::clamp(animation.damage_flash/0.4*0.38+integrity_warning+animation.collapse*0.16,0.0,0.48);
    if (damage_rim > 0) for (int y = scene_top; y < scene_bottom; ++y) for (int x = 0; x < width; ++x) {
        const int edge = std::min({x,width-1-x,y-scene_top,scene_bottom-1-y});
        if (edge >= 8) continue;
        const double alpha = damage_rim*(1-edge/8.0);
        const auto old = pixels_[y*width+x]; std::uint32_t color = 0;
        for (int shift : {0,8,16})
            color |= unsigned(((old>>shift)&255)*(1-alpha)+((0xff493fu>>shift)&255)*alpha)<<shift;
        pixels_[y*width+x] = 0xff000000|color;
    }
    const auto project = [&](Vec2 point) { return tile_to_screen(point,world); };
    const auto base = game.refinery().operating_position(game.unit()), valve = game.rocket().launch_valve();
    const auto bp = project(base), rp = project(game.rocket().ground_socket());
    const int bx = int(bp.x), by = int(bp.y)+8, rx = int(rp.x), bottom = int(rp.y)+8;
    if (!game.campaign() && world.planar() && !game.finished() &&
        (game.inside_base() || distance(center,BaseCamp::entrance) < 1.4)) {
        const auto label = [&](Vec2 point,const std::string& value,std::uint32_t accent,int lift,double phase) {
            const auto q = project(point);
            const int w = int(value.size())*6+11;
            const int x = int(q.x)-w/2, y = int(q.y)-lift-int(std::lround(std::sin(time*1.8+phase)*2));
            if (x < 4 || x+w >= width-4 || y < 32 || y+18 >= 315) return;
            glass_panel(x,y,w,15,5);
            rectangle(x+4,y+6,2,2,accent);
            // Preserve the requested title case in these small floating labels.
            int cursor = x+9;
            for (char letter : value) {
                const auto bits = glyph(letter);
                for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
                    if (bits[row] & (1 << (4-col))) rectangle(cursor+col,y+4+row,1,1,accent);
                cursor += 6;
            }
            line(int(q.x)-2,y+17,int(q.x)+2,y+17,accent);
        };
        label(BaseCamp::craft_bench,"Craft",0xffd49a,49,0.7);
        if (game.survival_enabled()) label(BaseCamp::charger,"Charge",0x9eeafa,39,0);
    }
    if (game.mission1() && !game.finished() && game.near_observed_table()) {
        const auto q=project(game.observation_table_position());
        const int x=int(q.x)-48, y=int(q.y)-58-int(std::lround(std::sin(time*1.8)*2));
        glass_panel(x,y,96,15,5); rectangle(x+5,y+6,2,2,0x9befff);
        text(x+11,y+4,"OBSERVED TABLE",0x9befff);
    }
    if (world.planar() && animation.active_activity) {
        const auto& activity = *animation.active_activity;
        const auto q = project(activity.position);
        const bool beside_rocket = !activity.crafting && distance(activity.position,game.rocket().ground_socket()) < 1;
        const int x = std::clamp(int(q.x)+(beside_rocket ? 32 : -20),8,width-90);
        const int y = std::clamp(int(q.y)-(beside_rocket ? 70 : 48),scene_top+4,scene_bottom-25);
        const auto output = activity.item != Item::Count ? item_icon(activity.item) : Icon::Bench;
        const auto value = activity.quantity > 0 ? "+"+quantity(activity.item,activity.quantity) : "+";
        panel(x-3,y-2,26+int(value.size())*6,21,0x557f78); icon(x,y,output); text(x+20,y+5,value,0xb5f5da);
    }
    const auto tasks = game.tasks();
    const Icon task_icons[] = {game.placeholder_mission()==4 ? Icon::Core : game.mission_number()==3 ? Icon::Core : game.mission2() ? (game.carrier_scanned() ? Icon::Wheel : Icon::Tasks) : game.mission1() ? Icon::FoodWater : Icon::Ore,
        game.mission2() ? Icon::RobotArm : game.mission1() ? Icon::Health : Icon::Bench,
        game.mission2() ? Icon::Chip : game.mission1() ? Icon::Observe : Icon::Fuel,
        game.mission2() ? Icon::Rocket : Icon::Core,Icon::Rocket,Icon::Valve};
    int completed = 0, active = 0;
    Icon active_icon = Icon::Ore;
    for (int i = 0; i < task_count; ++i) {
        const auto& task = tasks[i];
        if (!task.label.empty() && task.complete) ++completed;
        if (!task.active || game.finished()) continue;
        active = int(i);
        active_icon = task_icons[i];
        if (i == 0 && world.get_tile_type(int(task.target.x),int(task.target.y)) == TileType::Regolith)
            active_icon = Icon::Dust;
        const auto tp = project(task.target);
        const int actual_x = int(tp.x)+(world.planar() && i == 4 ? 42 : 0), actual_y = int(tp.y)-24;
        const int mx = std::clamp(actual_x,18,width-18);
        const int marker_bottom=dock_y-25;
        const int my = std::clamp(actual_y,scene_top+24,marker_bottom)+int(std::sin(time*3)*2);
        const bool offscreen = actual_x != std::clamp(actual_x,18,width-18) || actual_y != std::clamp(actual_y,scene_top+24,marker_bottom);
        if (!modal_open() && !game.rescue_cinematic()) {
            panel(mx-10,my-10,22,22,0x9a8657);
            icon(mx-7,my-7,active_icon);
            if (offscreen) {
                text(mx-8,my+15,std::to_string(int(distance(center,task.target))),0xeac880);
                if (actual_x < 18 || actual_x > width-18) {
                    const int d = actual_x < 18 ? -1 : 1;
                    line(mx+d*13,my-3,mx+d*16,my,0xeac880); line(mx+d*16,my,mx+d*13,my+3,0xeac880);
                } else {
                    const int d = actual_y > marker_bottom ? 1 : -1;
                    line(mx-3,my+d*13,mx,my+d*16,0xeac880); line(mx,my+d*16,mx+3,my+d*13,0xeac880);
                }
            }
        }
    }
    // Tiny contextual key badges replace the persistent recipe/controls paragraphs.
    if (!game.campaign() && !modal_open() && !game.finished()) {
        if (game.refinery().can_operate(game.unit())) {
            keycap(bx+25,by-26,"C"); icon(bx+42,by-28,Icon::Bench);
        }
        if (game.unit().near(game.rocket().ground_socket()) && !game.rocket().ready()) {
            keycap(rx+22,bottom-26,"R"); icon(rx+39,bottom-28,Icon::Plate);
            text(rx+57,bottom-22,game.rocket().hull_welded() ? "2" : "4",0xeac880);
        }
        if (game.climax().state() == ClimaxState::SolarParticleEvent && game.unit().near(valve,1.5))
            keycap(int(project(valve).x)+13,int(project(valve).y)-6,"V");
    }
    // A single floating glass row leaves the scenery visible around the HUD.
    glass_panel(header_x,header_y,header_width,header_height,9);
    if (world.planar() && over_header(pointer_x_,pointer_y_) && pointer_x_ < 66)
        glass_panel(16,8,48,18,6);
    text(20,14,world.planar() ? (robot_.model == RobotModel::Mark1 ? "U01 MK1" : "U01 MK2") : "UNIT-01",0xe8d4a7);
    const double battery = game.unit().battery_percent();
    const double integrity = game.unit().integrity();
    const auto battery_color = battery <= 15 ? 0xf48b78u : battery < 40 ? 0xe8bd78u : 0x7ed9b5u;
    const auto integrity_color = integrity <= 20 ? 0xff6f62u : integrity < 50 ? 0xe99566u : 0x73c7d0u;
    icon(68,8,Icon::Health); rectangle(89,13,42,5,0x253c48);
    if (game.attack_flash() > 0 && game.battery_hit_from() > battery) {
        const int from = int(42*battery/100), to = int(42*game.battery_hit_from()/100);
        rectangle(89+from,13,std::max(0,to-from),5,game.attack_flash() > 0.27 ? 0xff745f : 0x9c4b4c);
    }
    rectangle(89,13,int(42*battery/100),5,battery_color);
    text(136,14,std::to_string(int(std::ceil(battery)))+"%",game.attack_flash() > 0 ? 0xff7569 : battery_color);
    text(170,14,"HP",integrity_color);
    rectangle(188,13,39,5,0x3c292d);
    if (animation.damage_flash > 0)
        rectangle(188,13,39,5,animation.damage_flash > 0.22 ? 0x8f3c3c : 0x5c3033);
    rectangle(188,13,int(39*integrity/100),5,integrity_color);
    text(232,14,std::to_string(int(std::ceil(integrity))),integrity_color);
    line(262,12,262,22,0x526670);
    const char* verbs[] = {"MINE","CRAFT","REFINE","RECOVER","REPAIR","LAUNCH"};
    if(game.placeholder_mission()) {
        if(game.climax().storm().active()) icon(270,8,Icon::Radiation);
    } else icon(270,8,game.climax().storm().active() ? Icon::Radiation : active_icon);
    const auto status = game.rescue_mission()==6 ? "GATHER PARTS" : game.rescue_mission()>=7 ? "RESCUE S-01" : game.scanning_spaceship() ? "SCANNING S-01" : game.placeholder_mission()==4 ? game.mission4_quiz_complete() ? "DATA VERIFIED" : game.mission4_quiz_started() ? "MARS DATA QUIZ" : game.data_chip_collected() ? "DELIVER CHIP" : "FIND DATA CHIP" :
        game.placeholder_mission() ? "TEST ENTRY" : game.mission2() ? (game.carrier_repairing() ? "REPAIRING" : "CARRIER TASK") : game.climax().storm().active() ? std::to_string(int(game.unit().accumulated_dose_msv()))+" MSV" :
        game.charging() ? battery >= 99.99 ? "FULL / DOCKED" : "CHARGING" :
        battery < 40 ? battery <= 0 ? "EMPTY" : "LOW POWER" :
        game.copying_records() ? "COPYING" : game.inside_base() ? "RESCUE SHIP" : game.mission1() ? active==0 ? "EXPLORE" : active==1 ? "DELIVER" : "OBSERVE" : verbs[active];
    text(291,14,status,game.charging() ? 0x8ee9c3 : integrity < 30 ? integrity_color : battery <= 15 ? battery_color : 0xe5cc97);
    if(game.campaign()) text(505,14,"MISSION "+std::to_string(game.mission_number()),0xb1e6cb);
    else if(game.placeholder_mission()) text(505,14,"MISSION "+std::to_string(game.placeholder_mission()),0xb1e6cb);
    if(!game.placeholder_mission() || game.placeholder_mission()==4) text(395,14,"T",0xadc7d1);
    const int header_task_count=game.mission2() ? std::min(task_count,3) : task_count;
    for (int i = 0; i < (!game.placeholder_mission() || game.placeholder_mission()==4 ? header_task_count : 0); ++i) {
        const int x = 414+i*29;
        if (tasks[i].active) glass_panel(x-2,8,21,18,5);
        icon(x,8,task_icons[i]);
        if (tasks[i].complete) { line(x+10,21,x+12,23,0x9aebc9); line(x+12,23,x+16,18,0x9aebc9); }
    }
    line(597,12,597,22,0x526670); text(610,14,"H",0xc5dddf);
    if(game.mission1() || game.mission2() || game.mission_number()==3 || game.placeholder_mission()==4) {
        for(int slot=0;slot<9;++slot) {
            int x=185+slot*30;
            rectangle(x,318,28,28,0x252a30); outline(x,318,28,28,slot==0 ? 0xe7d8b2 : 0x82868b);
            outline(x+2,320,24,24,0x474c51);
            if(slot==0) { icon(x+5,323,Icon::Laser); }
            if(slot==1 && game.usb_obtained()) {
                icon(x+5,322,Icon::USB);text(x+19,337,"1",0xffffff);
                rectangle(x+3,340,20,2,game.usb_scan_recorded() ? 0x8fe3a9 : 0x70cbe8);
            } else if(slot==2 && game.unit().inventory().count(Item::MarsDataChip)>0) {
                icon(x+5,322,Icon::Chip);text(x+19,337,"1",0xffffff);
            } else if(game.mission2() && slot>=1 && slot<=3) {
                const bool found=slot==1 ? game.carrier_wheel_collected() : slot==2 ? game.carrier_arm_collected() : game.carrier_chip_collected();
                if(found) {
                    icon(x+5,322,slot==1 ? Icon::Wheel : slot==2 ? Icon::RobotArm : Icon::Chip);
                    text(x+19,337,"1",0xffffff); rectangle(x+3,340,20,2,0x8fe3a9);
                }

            } else if(game.mission1() && slot==1) {
                icon(x+4,321,Icon::Health);
                text(x+19,337,std::to_string(game.unit().inventory().count(Item::FirstAidKit)),0xffffff);
            } else if(game.mission1() && slot==2) {
                icon(x+4,321,Icon::FoodWater);
                text(x+19,337,std::to_string(game.unit().inventory().count(Item::FoodWaterSupply)),0xffffff);
            }
            if(game.mission1() && slot>=3 && slot<=5 && game.sample_obtained(slot-3)) {
                const int sample=slot-3;
                icon(x+5,322,sample==0 ? Icon::Silicate : sample==1 ? Icon::Ice : Icon::RockSample);
                text(x+19,337,game.observing()==sample ? "T" : "1",game.observing()==sample ? 0x8de9ff : 0xffffff);
                rectangle(x+3,340,game.sample_observed(sample) ? 20 : 9,2,game.sample_observed(sample) ? 0x8fe3a9 : 0xd6b879);
            }
        }
        const int parts_found=int(game.carrier_wheel_collected())+int(game.carrier_arm_collected())+int(game.carrier_chip_collected());
        if(game.mission2()) {
            text(148,329,std::to_string(parts_found)+"/3",0xe3cd97);
        } else if(game.mission_number()==3 || game.placeholder_mission()==4) {
            text(148,329,std::to_string(game.unit().inventory().count(Item::USBDrive)+game.unit().inventory().count(Item::MarsDataChip)),0xe3cd97);
        } else {
            text(148,329,std::to_string(game.unit().inventory().count(Item::FirstAidKit)+game.unit().inventory().count(Item::FoodWaterSupply))+"/5",0xe3cd97);
        }
        line(503,325,503,339,0x526670); icon(511,323,Icon::Tasks); text(533,329,"TAB",0xc5dddf);
    } else {
    glass_panel(dock_x,dock_y,dock_width,dock_height,10);
    icon(89,323,Icon::Laser); text(110,329,"J",0xc5dddf);
    if(!game.campaign()) icon(129,323,Icon::Dust);
    line(150,325,150,339,0x526670);
    const Item slots[] = {Item::IronNickelOre,Item::Regolith,Item::HydratedMinerals,Item::WaterIce,
        Item::Water,Item::StructuralPlates,Item::RefinedFuel,Item::DataCore,Item::ScrapAlloys};
    for (int i = 0; i < (game.mission1() ? 0 : 9); ++i) {
        const int x = slot_x+i*slot_pitch;
        const auto item = slots[i];
        const bool hover = pointer_x_ >= x && pointer_x_ < x+slot_width && pointer_y_ >= dock_y && pointer_y_ < dock_y+dock_height;
        if (hover) glass_panel(x,320,slot_width,24,6);
        if (game.unit().inventory().count(item)) rectangle(x+14,320,7,1,0x9bdbc5);
        icon(x+10,321,item_icon(item));
        const auto count = quantity(item,game.unit().inventory().count(item));
        text(x+(slot_width-int(count.size())*6)/2,337,count,game.unit().inventory().count(item) ? 0xdaeee8 : 0x8aa6af);
        if (hover) inspection_text_ = std::string(item_name(item))+" / "+count;
    }
    line(503,325,503,339,0x526670);
    icon(511,323,Icon::Tasks); text(533,329,"TAB",0xc5dddf);
    }
    if (!game.campaign() && !modal_open() && over_dock(pointer_x_,pointer_y_)) {
        if (pointer_x_ < 124) inspection_text_ = "MINE / J OR LEFT CLICK";
        else if (pointer_x_ < 150) inspection_text_ = "PLACE DUST / RIGHT CLICK";
        else if (pointer_x_ >= 506) inspection_text_ = "MATERIALS / TAB";
    }

    if (!modal_open() && over_header(pointer_x_,pointer_y_)) {
        if (world.planar() && pointer_x_ < 66)
            inspection_text_ = std::string("UNIT-01 / ")+robot_model_name(robot_.model)+" / K OR CLICK TO SWITCH";
        else if (pointer_x_ < 167)
            inspection_text_ = "BATTERY / "+std::to_string(int(std::ceil(battery)))+"% / RECHARGE IN BROKEN BOX";
        else if (pointer_x_ < 262)
            inspection_text_ = "ROBOT HEALTH / "+std::to_string(int(std::ceil(integrity)))+"% / IMPACT DAMAGE";
        else if (pointer_x_ < 410)
            inspection_text_ = tasks[active].label+" / "+tasks[active].progress;
        else if (pointer_x_ < 590) {
            const auto index = std::size_t(std::clamp((pointer_x_-412)/29,0,5));
            inspection_text_ = tasks[index].label+" / "+tasks[index].progress;
        } else if (pointer_x_ >= 600) inspection_text_ = "FIELD GUIDE / H";
    }
    if (!modal_open() && world_input_at(pointer_x_,pointer_y_,game)) {
        const auto cell = screen_to_tile(pointer_x_,pointer_y_);
        if (world.in_bounds(cell.x,cell.y)) {
            const auto type = world.get_tile_type(cell.x,cell.y);
            if (tile_definition(type).mineable) inspection_text_ = tile_definition(type).name;
            if (!game.campaign() && type == TileType::VacuumAir && game.unit().near({cell.x+0.5,cell.y+0.5},3.1)) {
                const bool valid = game.unit().inventory().count(Item::Regolith) >= 1000 &&
                    (!world.planar() || !BaseCamp::footprint(cell.x,cell.y)) &&
                    !overlaps(game.unit().body(),{{double(cell.x),double(cell.y)},1,1}) &&
                    world.line_of_sight(game.unit().center(),cell);
                const auto q = project({cell.x+0.5,cell.y+0.5});
                if (world.planar()) {
                    const auto color = valid ? 0x93d9be : 0x9c605a;
                    const auto corners = isometric_.floor_corners(cell,world);
                    for (int i = 0; i < 4; ++i) {
                        const auto a = corners[i], b = corners[(i+1)%4];
                        line(int(a.x),int(a.y),int(b.x),int(b.y),color);
                    }
                } else outline(int(q.x)-8,int(q.y)-8,tile_size,tile_size,valid ? 0x93d9be : 0x9c605a);
                icon(int(q.x)-7,int(q.y)-7,Icon::Dust);
                keycap(int(q.x)+14,int(q.y)-6,"J");
            }
        }
    }
    if ((!game.placeholder_mission() || game.placeholder_mission()==4) && !tasks_visible_ && !modal_open() && !game.finished()) {
        panel(430,46,198,48,0x4a615b);
        icon(441,57,active_icon);
        text(465,56,tasks[active].label,0xe5cc97);
        text(465,72,tasks[active].progress,0x86b6aa);
        keycap(607,52,"T");
    }
    if ((!game.placeholder_mission() || game.placeholder_mission()==4) && tasks_visible_ && !modal_open() && !game.finished()) {
        panel(446,46,182,160);
        text(456,56,(game.campaign() ? "MISSION "+std::to_string(game.mission_number())+" / " : "MISSION / ")+
            std::to_string(completed)+" OF "+std::to_string(task_count),0xd9e4ea);
        for (int i = 0; i < task_count; ++i) {
            const int y = 77+int(i)*21;
            icon(454,y,task_icons[i]);
            text(475,y,tasks[i].label,tasks[i].complete ? 0x78c4ac : tasks[i].active ? 0xf3d594 : 0x8298ab);
            text(475,y+10,tasks[i].progress,0x627e92);
        }
    }
    if (game.survival_enabled() && !game.finished()) {
        panel(12,46,196,44,0x47666f);
        if(game.in_spaceship()) {
            text(22,55,"INTERIOR / SHELTERED",0xcce0d8);
            const int room=ShipInterior::room_at(center);
            text(22,72,room<0 ? "ACCESS PASSAGE" : ShipInterior::rooms[room].name,0x99c8d4);
        } else {
            text(22,55,std::string(game.phobos().night() ? "DAWN IN " : "DUSK IN ")+countdown(game.phobos().seconds_to_transition()),0xcce0d8);
            text(22,72,game.phobos().night() ? "NIGHT" : "DAY",game.phobos().night() ? 0x99b4df : 0xe8c992);
            text(70,72,"60X",0x799ba7);
            text(129,72,"DAY "+std::to_string(game.phobos().day_number()),0x799ba7);
        }
        if (battery < 40 || game.unit().integrity() < 70) {
            panel(12,162,196,34,0x946d5c);
            text(22,170,battery < 40 ? "LOW POWER / DAMAGED GAIT" : "STRUCTURAL DAMAGE",0xe5ad86);
            text(22,184,battery < 40 ? "RETURN TO CHARGER" : "AVOID HARD LANDINGS",0xb49c91);
        }
        if (game.danger().warning() || game.danger().active()) {
            const bool active_danger = game.danger().active();
            panel(12,98,208,58,active_danger ? 0xe97568 : 0xaf655e);
            text(22,108,active_danger ? "THREAT ACTIVE" : "THREAT INCOMING",0xf0a295);
            text(176,108,countdown(game.danger().seconds_remaining()),0xffc5a9);
            text(22,125,game.inside_base() ? "SHELTERED / STAY INSIDE" : "RETURN TO CRASHED SHIP",game.inside_base() ? 0x9ae3bc : 0xffbd99);
            text(22,140,active_danger ? "CREATURES DRAIN BATTERY" : "REACH HOME BEFORE 00:00",0xad8f91);
        }
        if (game.inside_base() && (!game.in_spaceship() || game.charging() || distance(center,ShipInterior::charger)<3)) {
            panel(196,276,208,31,game.charging() ? 0x6ab6a0 : 0x47666f);
            text(206,284,game.charging() ? battery >= 99.99 ? "BATTERY FULL / DOCKED" : "CHARGE DOCK / CHARGING +4%" : "CHARGE DOCK / PARK INSIDE",game.charging() ? 0x9ee8c5 : 0xb6c7bc);
            text(206,297,game.in_spaceship() ? "INDUCTION / STOP ON CYAN PAD" : "GIANT WINGS / SAFE SHELTER",0x6f9a9d);
        }
    }
    if (world.planar()) minimap_.render(pixels_,game,robot_);
    draw_overlays(game);
    if (modal_open() && game.survival_enabled() && !game.finished() && (game.danger().warning() || game.danger().active())) {
        const std::string status = game.danger().warning() ? "THREAT IN " : "THREAT ACTIVE / ";
        const std::string refuge = game.inside_base() ? " / SHELTERED" : " / RETURN HOME";
        rectangle(224,33,400,10,0x482730);
        text(230,35,status+countdown(game.danger().seconds_remaining())+refuge,0xffbd9e);
    }
    if (!inspection_text_.empty()) {
        const auto label = inspection_text_.substr(0,98);
        const int w = int(label.size())*6+20, x = (width-w)/2;
        glass_panel(x,348,w,12,6);
        text(x+10,351,label,0xd6eee7);
    }
    if (animation.notice && !help_visible_ && !inventory_visible_ && !pause_visible_)
        tooltip(width/2,crafting_visible_ ? 318 : 40,animation.notice->substr(0,85));
    const double fade = game.climax().fade_alpha();
    if (fade > 0) {
        for (auto& p : pixels_) {
            const auto r = unsigned(((p>>16)&255)*(1-fade));
            const auto g = unsigned(((p>>8)&255)*(1-fade));
            const auto b = unsigned((p&255)*(1-fade));
            p = 0xff000000|(r<<16)|(g<<8)|b;
        }
    }
    if (completion_screen(game) && game.observation_result()<0) {
        panel(100,110,440,130,0x557a66);
        text(210,130,"MISSION "+std::to_string(game.mission_number())+" COMPLETE",0xb1f5c8,2);
        const std::string next=game.mission_number()==4 ? "NEXT: MISSION 6 / N TO CONTINUE" :
            game.mission_number()==3 ? "NEXT: MISSION 4 / N TO CONTINUE" :
            game.rescue_launch_complete() ? "ASTRONERD ESCAPED / BOTH ROBOTS POWER OFF" : "NEXT: MISSION 2";
        text((width-int(next.size())*6)/2,155,next,0xd3e7d6);
        panel(126,180,180,32,completion_selection_==0 ? 0x83c79a : 0x435e52);
        panel(334,180,180,32,completion_selection_==1 ? 0x83c79a : 0x435e52);
        text(177,191,"MISSION SELECT",completion_selection_==0 ? 0xe4f6e9 : 0xc2d7ca);
        const std::string continue_label=game.rescue_launch_complete() ? "RESTART" :
            "CONTINUE / MISSION "+std::to_string(game.mission_number()==4 ? 6 : game.mission_number()==3 ? 4 : 2);
        text(334+(180-int(continue_label.size())*6)/2,191,continue_label,completion_selection_==1 ? 0xe4f6e9 : 0xc2d7ca);
    }
    if (game.climax().state() == ClimaxState::Complete) {
        text(116,148,"MISSION COMPLETE.",0xd4e7e4,3);
        text(222,185,"EARTH SAVED.",0x81d5c0,2);
        text(218,226,"UNIT-01 / SIGNAL ENDED",0x637984);
        text(185,248,"A SMALL ROBOT. A BIG HOPE.",0x637984);
        text(224,278,"THANK YOU FOR PLAYING",0x8aa69f);
    } else if (game.climax().state() == ClimaxState::Failed &&
               (game.unit().fell_into_pit() || animation.collapse_age() >= 1.15)) {
        const bool empty = game.unit().battery_percent() <= 0;
        const bool pit = game.unit().fell_into_pit();
        rectangle(80,120,480,91,0x271c2b);
        text(pit ? 188 : empty ? 203 : 224,139,pit ? "FELL INTO PIT" : empty ? "BATTERY EMPTY" : "SIGNAL LOST",0xf4a08a,3);
        text(230,174,"MISSION FAILED",0xd7a2a0);
        text(212,194,"N / RESTART MISSION",0xa68d96);
    }
    if(game.mission1() && !modal_open() && game.near_observed_table() && game.observing()<0 && game.observation_result()<0 && (!game.finished() || game.observation_count()>0)) {
        panel(observe_x,observe_header_y,observe_w,18,0x73bdcb); text(observe_x+8,observe_header_y+5,game.finished() ? "OBSERVATION LOG / REVIEW" : "OBSERVED TABLE",0xc1f5df);
        for(int sample=0;sample<3;++sample) {
            int y=observe_y+sample*observe_pitch;
            const bool observed=game.sample_observed(sample);
            const bool ready=observed || (!game.finished() && game.unit().inventory().count(Game::sample_items[sample]));
            panel(observe_x,y,observe_w,observe_pitch-2,ready ? 0x7fcac7 : 0x455260);
            text(observe_x+8,y+4,std::string(observed ? "REVIEW / " : "OBSERVE / ")+Game::sample_names[sample],ready ? 0xc1f5df : 0x849496);
        }
    }
    if(game.observing()>=0) {
        panel(210,276,220,28,0x73bdcb); text(220,281,std::string("OBSERVING ")+Game::sample_names[game.observing()],0xbbe9e8);
        rectangle(220,294,200,3,0x253a47); rectangle(220,294,int(200*game.observation_progress()),3,0x87dec0);
    }
    if(game.scanning_spaceship() && !modal_open()) {
        panel(185,270,270,34,0x73bdcb);
        text(195,277,"SCANNING S-01 TO USB / "+std::to_string(int(game.spaceship_scan_progress()*100))+"%",0xbbe9e8);
        text(195,288,"STAY CLOSE / 4 SECONDS",0x8caeac);
        rectangle(195,299,250,3,0x253a47);rectangle(195,299,int(250*game.spaceship_scan_progress()),3,0x87dec0);
    }
    if(game.copying_records() && !modal_open()) {
        panel(210,270,220,34,0x73bdcb);
        text(220,277,"PREPARING USB / "+std::to_string(int(game.records_copy_progress()*100+1e-7))+"%",0xbbe9e8);
        text(220,288,"STAY NEAR PC / 4 SECONDS",0x8caeac);
        rectangle(220,299,200,3,0x253a47);
        rectangle(220,299,int(200*game.records_copy_progress()),3,0x87dec0);
    }
    if(game.rescue_power_sequence()) {
        const double drain=game.rescue_drain_effect();
        for(int y=30;y<height;++y) for(int x=0;x<width;++x) {
            const double edge=1-std::min(1.0,std::min({x,width-1-x,y-30,height-1-y})/80.0);
            const double pulse=.5+.5*std::sin(animation.clock*13);
            const double alpha=std::clamp(drain*(.10+edge*.32)+((y+int(animation.clock*23))%9==0 ? drain*.12*pulse : 0),0.0,.65);
            pixels_[y*width+x]=blend(pixels_[y*width+x],0x07121f,alpha);
        }
        if(!modal_open()) {
            panel(174,48,292,65,0x527787);
            const char* names[]{"C-02","U-01","S-01"};
            const double values[]{game.carrier_battery_percent(),game.unit().battery_percent(),game.rescue_ship_battery_percent()};
            for(int i=0;i<3;++i) {
                const int y=57+i*17;text(185,y,names[i],0xb5dbe5);
                rectangle(224,y,132,6,0x1c303d);rectangle(224,y,int(132*values[i]/100),6,i==2 ? 0x8ae6b5 : 0xe2bf74);
                text(367,y,values[i]<=0 ? "POWER OFF" : std::to_string(int(std::ceil(values[i])))+"%",values[i]<=0 ? 0xae8590 : 0xb7e9d5);
            }
        }
    }
    if(game.rescue_cinematic() && !game.rescue_launch_complete() && !modal_open()) {
        rectangle(0,30,width,16,0x08121c);
        text(17,35,game.rescue_power_sequence() ? "MISSION 8 / FINAL POWER TRANSFER" : game.rescue_mission()==8 ? "MISSION 8 / BOARDING S-01" : "MISSION 7 / ASSISTED ASTRONERD TRANSFER",0x9dcdd8);
        panel(12,263,616,42,0x527787);
        text(23,273,game.rescue_dialogue(),0xd9eee7);
        text(23,289,game.rescue_power_sequence() ? "C-02 FIRST / U-01 SECOND / ALL POWER FOR S-01" : game.rescue_mission()==8 ? "ASTRONERD BOARDS S-01 / U-01 AND C-02 STAY HERE" : "CINEMATIC / CONTROLS RESUME OUTSIDE THE BASE",0x87a9b8);
    }
    if(game.rescue_stage()==Game::RescueStage::RepairingShip && !modal_open()) {
        panel(210,270,220,34,0x73bdcb);
        text(220,277,"REPAIRING S-01 / "+std::to_string(int(game.rescue_stage_progress()*100))+"%",0xbbe9e8);
        text(220,288,"STAY CLOSE / 6 SECONDS",0x8caeac);
        rectangle(220,299,200,3,0x253a47);rectangle(220,299,int(200*game.rescue_stage_progress()),3,0x87dec0);
    }
    if(game.carrier_repairing() && !modal_open()) {
        panel(210,270,220,34,0x73bdcb);
        text(220,277,"REPAIRING CARRIER / "+std::to_string(int(game.carrier_repair_progress()*100+1e-7))+"%",0xbbe9e8);
        text(220,288,"STAY CLOSE / 6 SECONDS",0x8caeac);
        rectangle(220,299,200,3,0x253a47);
        rectangle(220,299,int(200*game.carrier_repair_progress()),3,0x87dec0);
    }
    if(game.observation_result()>=0) {
        int sample=game.observation_result(); panel(140,104,360,164,0x8dcdbd);
        text(158,121,Game::sample_names[sample],0xc1f5df,2);
        for(int row=0;row<3;++row) text(158,158+row*22,Mission1::sample_facts[sample][row],0xc2d7df);
        panel(result_close_x,result_close_y,result_close_w,result_close_h,0x7fb9aa);
        text(result_close_x+20,result_close_y+5,"CLOSE",0xc1f5df);
    }
    if (pause_visible_) draw_pause_menu();
}
bool PixelRenderer::write_ppm(const std::string& path) const {
    std::ofstream out(path,std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (auto pixel : pixels_) {
        const char bytes[] = {char((pixel>>16)&255),char((pixel>>8)&255),char(pixel&255)};
        out.write(bytes,3);
    }
    return bool(out);
}
}

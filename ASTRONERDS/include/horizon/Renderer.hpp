#pragma once
#include "horizon/Game.hpp"
#include "horizon/IsometricScene.hpp"
#include "horizon/Minimap.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace horizon {
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void render(const Game& game) = 0;
};
class TerminalRenderer final : public Renderer {
public:
    explicit TerminalRenderer(std::ostream& output) : output_(output) {}
    void render(const Game& game) override;
private:
    std::ostream& output_;
};
// Pure C++ software pixel-art renderer, shared by PPM export and SDL2. No assets
// or graphics API leak into simulation classes; each pixel is opaque ARGB8888.
class PixelRenderer final : public Renderer {
public:
    enum class MenuCommand { None, Restart, Quit, MissionSelect, ContinueMission };
    enum class StartupPage { Main, Missions, Options };
    enum class StartupCommand { None, StartMission1, StartMission2, StartMission3, StartMission4, StartMission6, Quit };
    // Turn off to hide unreleased mission slots from the start menu.
    static constexpr bool enable_upcoming_missions = true;
    static constexpr int width = 640, height = 360, tile_size = 16;
    static constexpr int scene_top = 32, scene_bottom = 360;
    PixelRenderer();
    void render(const Game& game) override;
    void open_startup();
    void open_mission_select();
    void render_startup(double time);
    bool startup_visible() const { return startup_visible_; }
    StartupPage startup_page() const { return startup_page_; }
    int startup_selection() const { return startup_selection_; }
    void startup_move(int direction);
    void startup_activate();
    void startup_back();
    bool startup_click(int x, int y);
    StartupCommand take_startup_command() { const auto command = startup_command_; startup_command_ = StartupCommand::None; return command; }
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    GridPos screen_to_tile(int x, int y) const;
    Vec2 tile_to_screen(Vec2 point, const WorldGrid& world) const;
    Vec2 item_to_screen(GridPos cell, const WorldGrid& world) const;
    Vec2 combat_target_at(int x, int y, const Game& game) const;
    bool world_input_at(int x, int y, const Game& game) const;
    bool write_ppm(const std::string& path) const;
    void toggle_tasks() { tasks_visible_ = !tasks_visible_; }
    void toggle_headlamp() { headlamp_on_ = !headlamp_on_; }
    bool headlamp_on() const { return headlamp_on_; }
    void toggle_camera() { isometric_.toggle_camera(); }
    void toggle_ship_damage_style() { isometric_.toggle_ship_damage_style(); }
    bool ship_damage_style() const { return isometric_.ship_damage_style(); }
    bool curved_camera() const { return isometric_.curved_camera(); }
    void toggle_robot_model() { robot_.model = robot_.model == RobotModel::Mark1 ? RobotModel::Mark2 : RobotModel::Mark1; }
    void set_robot_model(RobotModel model) { robot_.model = model; }
    RobotModel robot_model() const { return robot_.model; }
    const RobotAppearance& robot_appearance() const { return robot_; }
    void set_robot_appearance(const RobotAppearance& appearance) { robot_ = appearance; }
    const std::string& inspection_text() const { return inspection_text_; }
    void toggle_inventory();
    void toggle_help();
    void toggle_crafting(const Game& game);
    void toggle_pause();
    void escape_menu();
    bool paused() const { return pause_visible_ || help_visible_; }
    bool pause_visible() const { return pause_visible_; }
    bool sound_settings_visible() const { return sound_settings_visible_; }
    AudioSettings& audio_settings() { return audio_settings_; }
    const AudioSettings& audio_settings() const { return audio_settings_; }
    MenuCommand take_menu_command() { const auto command = menu_command_; menu_command_ = MenuCommand::None; return command; }
    bool completion_screen(const Game& game) const;
    void completion_move(int direction);
    void completion_activate(const Game& game);
    void close_overlays() { inventory_visible_ = crafting_visible_ = help_visible_ = pause_visible_ = sound_settings_visible_ = false; }
    bool modal_open() const { return inventory_visible_ || crafting_visible_ || help_visible_ || pause_visible_ || mission4_quiz_visible_; }
    bool help_visible() const { return help_visible_; }
    void set_pointer(int x, int y) { pointer_x_ = x; pointer_y_ = y; }
    std::optional<Action> action_at(int x, int y, const Game& game) const;
    bool click(int x, int y, Game& game);
private:
    enum class Icon { Ore, Dust, Mineral, Ice, Water, Scrap, Hydrogen, Oxygen, LH2, LOX,
                      Fuel, Plate, Polymer, Core, Slag, Drill, Treads, Shield, Bench,
                      Rocket, Valve, Laser, Jump, Climb, Health, Radiation, Tasks, Help, FoodWater, Observe, Silicate, RockSample,
                      Wheel, RobotArm, Chip, USB };
    void rectangle(int x, int y, int w, int h, std::uint32_t rgb);
    void line(int x0, int y0, int x1, int y1, std::uint32_t rgb);
    void text(int x, int y, const std::string& value, std::uint32_t rgb, int scale = 1);
    void panel(int x, int y, int w, int h, std::uint32_t accent = 0x33475b);
    void glass_panel(int x, int y, int w, int h, int radius);
    void outline(int x, int y, int w, int h, std::uint32_t color);
    void icon(int x, int y, Icon kind, int scale = 1);
    static Icon item_icon(Item item);
    void keycap(int x, int y, const std::string& key);
    void tooltip(int x, int y, const std::string& label);
    void draw_overlays(const Game& game);
    void draw_hud(const Game& game);
    void draw_ship_interior(const Game& game);
    void draw_pause_menu();
    void draw_startup_menu(double time);
    void draw_sound_settings(bool startup);
    bool sound_settings_click(int x, int y, bool startup);
    bool pause_click(int x, int y);
    IsometricScene isometric_;
    Minimap minimap_;
    RobotAppearance robot_;
    bool planar_ = false;
    std::vector<std::uint32_t> pixels_;
    int camera_x_ = 0, camera_y_ = 0;
    double smooth_x_ = 0, smooth_y_ = 0, last_clock_ = -1;
    bool tasks_visible_ = false, inventory_visible_ = false, crafting_visible_ = false, help_visible_ = false;
    int pointer_x_ = -1, pointer_y_ = -1;
    bool headlamp_on_ = true;
    bool pause_visible_ = false, sound_settings_visible_ = false;
    bool startup_visible_ = false;
    StartupPage startup_page_ = StartupPage::Main;
    int startup_selection_ = 0;
    StartupCommand startup_command_ = StartupCommand::None;
    bool mission4_quiz_visible_ = false;
    AudioSettings audio_settings_;
    MenuCommand menu_command_ = MenuCommand::None;
    int completion_selection_ = 0;
    std::string inspection_text_;
};
// Returns a process exit code; defined only in the optional SDL frontend.
int run_sdl(int frame_limit = 0, bool side_view = false);
}

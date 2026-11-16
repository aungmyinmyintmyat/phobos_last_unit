#include "horizon/Renderer.hpp"
#include "horizon/TargetTool.hpp"
#include <SDL.h>
#include <chrono>
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace horizon {
namespace {
struct SDLContext {
    SDLContext() { if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) != 0) throw std::runtime_error(SDL_GetError()); }
    ~SDLContext() { SDL_Quit(); }
};
void check(bool condition) { if (!condition) throw std::runtime_error(SDL_GetError()); }
class AudioOutput {
public:
    AudioOutput() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec wanted{};
            wanted.freq = SoundEffects::sample_rate; wanted.format = AUDIO_S16SYS;
            wanted.channels = 2; wanted.samples = 512;
            device_ = SDL_OpenAudioDevice(nullptr,0,&wanted,nullptr,0);
        }
        if (device_) SDL_PauseAudioDevice(device_,0);
        else std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';
    }
    ~AudioOutput() { if (device_) SDL_CloseAudioDevice(device_); }
    void reset() { synth_.reset(); serial_ = 0; clear(); }
    void click(const AudioSettings& settings) { synth_.play(SoundCue::Interface,settings.gain(SoundCue::Interface)); }
    void update(const Game& game, bool paused,const AudioSettings& settings) {
        for (const auto& event : game.presentation().sound_events()) {
            if (event.serial > serial_ && !settings.muted && !paused) {
                const bool urgent = game.danger().warning() || game.danger().active();
                if (!(urgent && is_robot_talk(event.cue)))
                    synth_.play(event.cue,settings.gain(event.cue)*(event.cue == SoundCue::ServoStep ? 0.65 : 1));
            }
            serial_ = std::max(serial_,event.serial);
        }
        if (!device_) return;
        if (paused || settings.muted) { clear(); synth_.reset(); return; }
        const auto velocity = game.unit().velocity();
        const double motor = game.unit().alive() && game.unit().grounded() ? std::hypot(velocity.x,velocity.y)/4 : 0;
        const auto& activity = game.presentation().active_activity;
        const double near_factory = std::max(0.0,1-distance(game.unit().center(),game.refinery().position())/7);
        const double machinery = settings.master*settings.machinery, effects = settings.master*settings.effects;
        synth_.ambience(motor*machinery,(game.presentation().mining_target ? 1 : 0)*effects,
            near_factory*(activity && activity->crafting ? 1 : 0.25)*machinery,
            (game.climax().storm().active() && !game.rocket().launched() ? 1 : 0)*machinery,
            (game.rocket().launched() && !game.finished() ? std::min(1.0,game.climax().launch_elapsed()/1.5) : 0)*effects);
        // Queue a short buffer on the main thread. No callback touches game state.
        std::array<std::int16_t,1024> buffer{};
        while (SDL_GetQueuedAudioSize(device_) < 3072*2*sizeof(std::int16_t)) {
            synth_.render(buffer.data(),buffer.size()/2);
            if (SDL_QueueAudio(device_,buffer.data(),sizeof(buffer)) != 0) break;
        }
    }
private:
    void clear() { if (device_) SDL_ClearQueuedAudio(device_); }
    SDL_AudioDeviceID device_ = 0;
    SoundEffects synth_;
    std::uint64_t serial_ = 0;
};
void repair_next(Game& game) {
    if (!game.rocket().hull_welded()) game.perform(Action::WeldHull);
    else if (!game.rocket().fuel_lines_connected()) game.perform(Action::HookupLines);
    else if (!game.rocket().data_core_socketed()) game.perform(Action::SocketCore);
    else game.perform(Action::Calibrate);
}
}
int run_sdl(int frame_limit, bool side_view) {
    SDLContext context;
    AudioOutput audio;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0"); // Nearest-neighbor pixels.
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("PHOBOS: The Last Unit",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                         1280,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_FULLSCREEN_DESKTOP),SDL_DestroyWindow);
    check(bool(window));
    
    // icon added
    auto icon = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>(
    SDL_LoadBMP("assets/horizon.bmp"), SDL_FreeSurface);
    if (icon) SDL_SetWindowIcon(window.get(), icon.get());
    
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC),SDL_DestroyRenderer);
    if (!renderer) renderer.reset(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_SOFTWARE));
    check(bool(renderer));
    check(SDL_RenderSetLogicalSize(renderer.get(),PixelRenderer::width,PixelRenderer::height) == 0);
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture(
        SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,
                          PixelRenderer::width,PixelRenderer::height),SDL_DestroyTexture);
    check(bool(texture));
    auto game = side_view ? std::make_unique<Game>(WorldGrid::generate()) : std::make_unique<Game>();
    PixelRenderer pixels;
    if (!side_view) pixels.open_startup();
    if (pixels.startup_visible()) pixels.render_startup(0); else pixels.render(*game);
    bool running = true, mining = false, focused = true, displayed_mute = false;
    TargetTool keyboard_tool;
    int mouse_x = 320, mouse_y = 150, frames = 0;
    auto previous = std::chrono::steady_clock::now();
    const auto startup_epoch = previous;
    while (running && (frame_limit <= 0 || frames < frame_limit)) {
        bool jump = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                mining = false; keyboard_tool.release(); focused = false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
            if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_j) keyboard_tool.release();
            if (event.type == SDL_MOUSEMOTION) {
                mouse_x = event.motion.x; mouse_y = event.motion.y;
                if (pixels.startup_visible() && pixels.startup_page() == PixelRenderer::StartupPage::Options &&
                    (event.motion.state & SDL_BUTTON_LMASK)) pixels.startup_click(mouse_x,mouse_y);
                else if (pixels.sound_settings_visible() && (event.motion.state & SDL_BUTTON_LMASK) &&
                    mouse_x >= 288 && mouse_x <= 416 && mouse_y >= 98 && mouse_y < 230)
                    pixels.click(mouse_x,mouse_y,*game);
            }
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                mouse_x = event.button.x; mouse_y = event.button.y;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const bool consumed = pixels.startup_visible() ? pixels.startup_click(mouse_x,mouse_y) :
                        pixels.click(mouse_x,mouse_y,*game);
                    if (consumed) audio.click(pixels.audio_settings());
                    mining = !pixels.startup_visible() && !consumed && pixels.world_input_at(mouse_x,mouse_y,*game);
                }
                if (!pixels.startup_visible() && event.button.button == SDL_BUTTON_RIGHT && pixels.world_input_at(mouse_x,mouse_y,*game))
                    game->place_tile(pixels.screen_to_tile(mouse_x,mouse_y));
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) mining = false;
            if (event.type != SDL_KEYDOWN || event.key.repeat) continue;
            if (event.key.keysym.sym == SDLK_F11) {
                const bool fullscreen = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0;
                if (SDL_SetWindowFullscreen(window.get(),fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
                    std::cerr << "Unable to change fullscreen mode: " << SDL_GetError() << '\n';
                continue;
            }
            if (event.key.keysym.sym == SDLK_F9) {
                pixels.toggle_ship_damage_style();
                SDL_SetWindowTitle(window.get(),pixels.ship_damage_style() ?
                    "PHOBOS | Damaged ship style (F9 restores original)" :
                    " | Original ship style (F9 restores damage)" );
                continue;
            }
            if (pixels.startup_visible()) {
                if (event.key.keysym.sym == SDLK_ESCAPE) pixels.startup_back();
                else if (event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_w) pixels.startup_move(-1);
                else if (event.key.keysym.sym == SDLK_DOWN || event.key.keysym.sym == SDLK_s) pixels.startup_move(1);
                else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE) pixels.startup_activate();
                else if (event.key.keysym.sym == SDLK_u) pixels.audio_settings().muted = !pixels.audio_settings().muted;
                audio.click(pixels.audio_settings());
                previous = std::chrono::steady_clock::now();
                continue;
            }
            if (pixels.completion_screen(*game)) {
                if (event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_RIGHT ||
                    event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_d)
                    pixels.completion_move(event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_a ? -1 : 1);
                else if (event.key.keysym.sym == SDLK_n) { pixels.completion_move(1);pixels.completion_activate(*game); }
                else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER || event.key.keysym.sym == SDLK_SPACE)
                    pixels.completion_activate(*game);
                else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    pixels.completion_move(-1);
                    pixels.completion_activate(*game);
                }
                audio.click(pixels.audio_settings());
                previous = std::chrono::steady_clock::now();
                continue;
            }
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_p) {
                if (event.key.keysym.sym == SDLK_ESCAPE) pixels.escape_menu(); else pixels.toggle_pause();
                mining = false; keyboard_tool.release(); jump = false;
                previous = std::chrono::steady_clock::now();
                continue;
            }
            if (event.key.keysym.sym == SDLK_u) {
                auto& settings = pixels.audio_settings(); settings.muted = !settings.muted;
                SDL_SetWindowTitle(window.get(),settings.muted ? "PHOBOS | Sound OFF (U)" : "PHOBOS | Sound ON (U)");
                continue;
            }
            if (pixels.pause_visible()) continue;
            if (pixels.help_visible() && event.key.keysym.sym != SDLK_h && event.key.keysym.sym != SDLK_ESCAPE && event.key.keysym.sym != SDLK_u && event.key.keysym.sym != SDLK_k && event.key.keysym.sym != SDLK_F9) continue;
            if(game->mission4_quiz_started() && event.key.keysym.sym!=SDLK_h) continue;
            switch (event.key.keysym.sym) {
                case SDLK_j:
                    if (pixels.world_input_at(mouse_x,mouse_y,*game)) keyboard_tool.press(*game,pixels.screen_to_tile(mouse_x,mouse_y));
                    break;
                case SDLK_SPACE: jump = !pixels.modal_open(); break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if(!pixels.modal_open()) game->enter_nearby_room();
                    break;
                case SDLK_e:
                    if(!pixels.modal_open()) {
                        if(game->campaign()) game->interact(); else game->perform(Action::Extract);
                    }
                    break;
                case SDLK_i: game->perform(Action::MeltIce); break;
                case SDLK_o: game->perform(Action::Electrolyze); break;
                case SDLK_l: game->perform(Action::Liquefy); break;
                case SDLK_f: game->perform(Action::PackageFuel); break;
                case SDLK_m: game->perform(Action::Smelt); break;
                case SDLK_1: game->perform(Action::UpgradeDrill); break;
                case SDLK_2: game->perform(Action::UpgradeTreads); break;
                case SDLK_3: game->perform(Action::UpgradeShield); break;
                case SDLK_r: repair_next(*game); break;
                case SDLK_v: game->perform(Action::ActivateValve); break;
                case SDLK_t: pixels.toggle_tasks(); break;
                case SDLK_b: pixels.toggle_headlamp(); break;
                case SDLK_g: pixels.toggle_camera(); pixels.render(*game); audio.click(pixels.audio_settings()); break;
                case SDLK_k: if (!side_view) { pixels.toggle_robot_model(); audio.click(pixels.audio_settings()); } break;
                case SDLK_c: pixels.toggle_crafting(*game); mining = false; break;
                case SDLK_TAB: pixels.toggle_inventory(); mining = false; break;
                case SDLK_n:
                    if(!game->continue_mission()) game = side_view ? std::make_unique<Game>(WorldGrid::generate()) : std::make_unique<Game>();
                    audio.reset(); mining = false; keyboard_tool.release(); pixels.close_overlays(); pixels.render(*game); break;
                case SDLK_h:
                    pixels.toggle_help(); mining = false;
                    previous = std::chrono::steady_clock::now();
                    break;
            }
        }
        const auto startup_command = pixels.take_startup_command();
        if (startup_command == PixelRenderer::StartupCommand::Quit) running = false;
        if (startup_command == PixelRenderer::StartupCommand::StartMission1) {
            game = std::make_unique<Game>();
            audio.reset(); mining = false; keyboard_tool.release(); jump = false;
            pixels.close_overlays(); pixels.render(*game);
            previous = std::chrono::steady_clock::now();
        }
        if (startup_command == PixelRenderer::StartupCommand::StartMission2 ||
            startup_command == PixelRenderer::StartupCommand::StartMission3 ||
            startup_command == PixelRenderer::StartupCommand::StartMission4 ||
            startup_command == PixelRenderer::StartupCommand::StartMission6) {
            GameOptions options;
            if (startup_command == PixelRenderer::StartupCommand::StartMission2) options.placeholder_mission = 2;
            else if (startup_command == PixelRenderer::StartupCommand::StartMission3) options.start_mission3 = true;
            else options.placeholder_mission = startup_command == PixelRenderer::StartupCommand::StartMission4 ? 4 : 6;
            game = std::make_unique<Game>(WorldGrid::generate_isometric(),options);
            audio.reset(); mining = false; keyboard_tool.release(); jump = false;
            pixels.close_overlays(); pixels.render(*game);
            previous = std::chrono::steady_clock::now();
        }
        const auto command = pixels.take_menu_command();
        if (command == PixelRenderer::MenuCommand::Quit) running = false;
        if (command == PixelRenderer::MenuCommand::MissionSelect) pixels.open_mission_select();
        if (command == PixelRenderer::MenuCommand::ContinueMission) {
            if(!game->continue_mission()) {
                GameOptions options;
                if(game->mission1_complete()) options.placeholder_mission=2;
                game=std::make_unique<Game>(WorldGrid::generate_isometric(),options);
            }
            audio.reset(); mining=false; keyboard_tool.release(); jump=false;
            pixels.close_overlays(); pixels.render(*game);
            previous=std::chrono::steady_clock::now();
        }
        if (command == PixelRenderer::MenuCommand::Restart) {
            game = side_view ? std::make_unique<Game>(WorldGrid::generate()) : std::make_unique<Game>();
            audio.reset(); mining = false; keyboard_tool.release(); jump = false;
            pixels.close_overlays(); pixels.render(*game);
            previous = std::chrono::steady_clock::now();
        }
        const auto now = std::chrono::steady_clock::now();
        if (pixels.startup_visible()) {
            game->set_paused(true);
            pixels.set_pointer(mouse_x,mouse_y);
            pixels.render_startup(std::chrono::duration<double>(now-startup_epoch).count());
            check(SDL_UpdateTexture(texture.get(),nullptr,pixels.pixels().data(),PixelRenderer::width*sizeof(std::uint32_t)) == 0);
            check(SDL_RenderClear(renderer.get()) == 0);
            check(SDL_RenderCopy(renderer.get(),texture.get(),nullptr,nullptr) == 0);
            SDL_RenderPresent(renderer.get());
            previous = now; ++frames; SDL_Delay(1); continue;
        }
        const double dt = std::chrono::duration<double>(now-previous).count();
        previous = now;
        const auto* keys = SDL_GetKeyboardState(nullptr);
        InputState input;
        input.left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
        input.right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
        input.climb = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
        input.up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
        input.down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
        input.jump = jump;
        const bool world_aim = pixels.world_input_at(mouse_x,mouse_y,*game);
        const bool firing = !side_view && world_aim && (keys[SDL_SCANCODE_X] ||
            (mining && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])));
        if (firing) input.combat_target = pixels.combat_target_at(mouse_x,mouse_y,*game);
        if (mining && world_aim && !firing) input.mining_target = pixels.screen_to_tile(mouse_x,mouse_y);
        if (world_aim && !firing) {
            if (const auto aim = keyboard_tool.target(pixels.screen_to_tile(mouse_x,mouse_y))) input.mining_target = aim;
        }
        if (pixels.modal_open() || !focused) { input = {}; keyboard_tool.release(); }
        game->set_paused(pixels.paused() || !focused);
        game->advance(input,dt);
        if (game->mission5_transition_requested()) {
            GameOptions options;options.placeholder_mission=5;
            game=std::make_unique<Game>(WorldGrid::generate_isometric(),options);
            audio.reset();mining=false;keyboard_tool.release();jump=false;
            pixels.close_overlays();pixels.render(*game);
            previous=std::chrono::steady_clock::now();
        }
        audio.update(*game,pixels.paused() || !focused,pixels.audio_settings());
        if (displayed_mute != pixels.audio_settings().muted) {
            displayed_mute = pixels.audio_settings().muted;
            SDL_SetWindowTitle(window.get(),displayed_mute ? "PHOBOS | Sound OFF (U)" : "PHOBOS | Sound ON (U)");
        }
        pixels.set_pointer(mouse_x,mouse_y);
        pixels.render(*game);
        check(SDL_UpdateTexture(texture.get(),nullptr,pixels.pixels().data(),PixelRenderer::width*sizeof(std::uint32_t)) == 0);
        check(SDL_RenderClear(renderer.get()) == 0);
        check(SDL_RenderCopy(renderer.get(),texture.get(),nullptr,nullptr) == 0);
        SDL_RenderPresent(renderer.get());
        ++frames;
        SDL_Delay(1);
    }
    return 0;
}
}

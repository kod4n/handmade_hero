// Linux platform layer, on SDL3 (window, rendering, input, audio). SDL covers
// both X11 and Wayland. The game is loaded from game.so and hot reloaded when
// it changes, like game.dll on Windows.

#include "defer.h"
#include "platform_shared.h"

#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>

using namespace hh;

static constexpr const char* k_window_title = "Handmade Hero";

// render constants
static constexpr u32 k_monitor_refresh_hz = 60;
static constexpr u32 k_game_refresh_hz = k_monitor_refresh_hz;

// audio constants
static constexpr u32 k_audio_channels = 2;
static constexpr u32 k_audio_bytes_per_frame = sizeof(AudioFrame);
static constexpr u32 k_sound_samples_per_second = 48000;
// Audio is topped up once per game frame on the main thread (no callback, so
// the game's sound code never runs on another thread mid-reload). Keep ~3
// game frames queued so scheduler jitter doesn't cause underruns.
static constexpr u32 k_audio_frames_per_game_frame = k_sound_samples_per_second / k_game_refresh_hz;
static constexpr u32 k_audio_target_queued_frames = 3 * k_audio_frames_per_game_frame;

// Same dead zone XInput uses for the left stick on Windows.
static constexpr i16 k_stick_dead_zone = 7849;
static constexpr f32 k_stick_move_threshold = 0.5f;

static bool global_running;

enum class LogSeverity { Info, Warn, Error };

static const char* log_severity(LogSeverity severity) {
    switch (severity) {
    case LogSeverity::Info:
        return "info";
    case LogSeverity::Warn:
        return "warn";
    case LogSeverity::Error:
        return "error";
    }
    return "unknown";
}

[[gnu::format(printf, 2, 3)]] static void linux_log(LogSeverity severity, const char* format, ...) {
    char buffer[1024] = {};
    int prefix_length = snprintf(buffer, sizeof(buffer), "[%s] ", log_severity(severity));
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= sizeof(buffer)) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(buffer + prefix_length, sizeof(buffer) - static_cast<size_t>(prefix_length), format, args);
    va_end(args);

    fputs(buffer, stderr);
}

static void* linux_alloc(u64 size, void* base_address = nullptr) {
    // base_address is a hint: the kernel uses it if that range is free.
    void* memory = mmap(base_address, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? nullptr : memory;
}

static void linux_free(void* memory, u64 size) {
    if (memory) {
        munmap(memory, size);
    }
}

// ---- Paths ---------------------------------------------------------------

// Directory holding the executable, ending in a slash. game.so and the
// build's lock file sit next to it in build/; data/ is one level up.
static char global_exe_dir[PATH_MAX];

static bool linux_init_exe_dir() {
    const char* base = SDL_GetBasePath();  // owned by SDL, ends in '/'
    if (!base) {
        return false;
    }
    const int length = snprintf(global_exe_dir, sizeof(global_exe_dir), "%s", base);
    return length > 0 && static_cast<size_t>(length) < sizeof(global_exe_dir);
}

// Writes exe_dir + name into out; false if it doesn't fit.
static bool linux_exe_path(char* out, size_t out_size, const char* name) {
    const int length = snprintf(out, out_size, "%s%s", global_exe_dir, name);
    return length > 0 && static_cast<size_t>(length) < out_size;
}

// ---- DEV file I/O ----------------------------------------------------------

#if DEV
// Game file names are relative to the repo's data/ folder.
static ReadFileResult dev_platform_read_file(const char* filename) {
    ReadFileResult result = {};
    char path[PATH_MAX];
    const int length = snprintf(path, sizeof(path), "%s../data/%s", global_exe_dir, filename);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) {
        linux_log(LogSeverity::Warn, "data path too long for %s\n", filename);
        return result;
    }

    size_t size = 0;
    void* content = SDL_LoadFile(path, &size);
    if (!content) {
        linux_log(LogSeverity::Warn, "failed to read file %s: %s\n", path, SDL_GetError());
        return result;
    }
    result.content = content;
    result.size = safe_truncate(size);
    return result;
}

static void dev_platform_free_file(void* memory) {
    SDL_free(memory);  // from SDL_LoadFile
}

static bool dev_platform_write_file(const char* filename, u32 size, void* memory) {
    if (!SDL_SaveFile(filename, memory, size)) {
        linux_log(LogSeverity::Warn, "failed to write file %s: %s\n", filename, SDL_GetError());
        return false;
    }
    return true;
}
#endif

// ---- Game code hot reload --------------------------------------------------

struct LinuxGameCode {
    void* library;
    struct timespec so_last_write_time;
    GameRender* render;
    GameSound* sound;
    char temp_path[PATH_MAX];  // the copy that's actually loaded
};

struct LinuxGameCodePaths {
    char source_so[PATH_MAX];
    char lock[PATH_MAX];  // exists while linux_build is writing game.so
};

static void game_render_stub(Storage&, ScreenBuffer&, Input&, AudioState&) {}

static void game_sound_stub(AudioBuffer& buffer, AudioState&) {
    // Silence rather than replaying whatever was left in the buffer.
    for (AudioFrame& frame : buffer.frames) {
        frame = {};
    }
}

static struct timespec linux_get_last_write_time(const char* path) {
    struct stat file_stat = {};
    if (stat(path, &file_stat) != 0) {
        return {};
    }
    return file_stat.st_mtim;
}

static bool linux_times_equal(const struct timespec& a, const struct timespec& b) {
    return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

static bool linux_file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

// Loads a copy of game.so rather than game.so itself, so the build can
// overwrite game.so while it's in use. Each load uses a new copy name: the
// dynamic loader hands back the already-loaded library for a path it still
// has open, so reusing one name could silently keep the old code.
static LinuxGameCode linux_load_game_code(const LinuxGameCodePaths& paths) {
    static u32 load_count = 0;

    LinuxGameCode result = {};
    const struct timespec write_time = linux_get_last_write_time(paths.source_so);

    char temp_name[64];
    snprintf(temp_name, sizeof(temp_name), "game_temp_%u.so", load_count++);
    size_t size = 0;
    void* content = SDL_LoadFile(paths.source_so, &size);
    if (content && linux_exe_path(result.temp_path, sizeof(result.temp_path), temp_name)
        && SDL_SaveFile(result.temp_path, content, size)) {
        result.library = dlopen(result.temp_path, RTLD_NOW | RTLD_LOCAL);
        if (!result.library) {
            linux_log(LogSeverity::Warn, "dlopen failed: %s\n", dlerror());
        }
    }
    SDL_free(content);

    if (result.library) {
        result.render = reinterpret_cast<GameRender*>(dlsym(result.library, "game_render"));
        result.sound = reinterpret_cast<GameSound*>(dlsym(result.library, "game_sound"));
    }

    if (result.render && result.sound) {
        // Only record the write time on success, so a failed load retries next frame.
        result.so_last_write_time = write_time;
        linux_log(LogSeverity::Info, "loaded game code\n");
    } else {
        linux_log(LogSeverity::Warn, "failed to load game code, using stubs\n");
        result.render = game_render_stub;
        result.sound = game_sound_stub;
    }
    return result;
}

static void linux_unload_game_code(LinuxGameCode& game_code) {
    if (game_code.library) {
        dlclose(game_code.library);
    }
    if (game_code.temp_path[0]) {
        unlink(game_code.temp_path);
    }
    game_code = {};
    game_code.render = game_render_stub;
    game_code.sound = game_sound_stub;
}

// ---- Input -----------------------------------------------------------------

static void linux_process_kb_button(ActionState& state, bool is_down) {
    state.ended_down = is_down;
    state.half_transition_count++;
}

static void linux_process_digital_button(bool is_down, const ActionState& old_state, ActionState& new_state) {
    new_state.ended_down = is_down;
    new_state.half_transition_count = (old_state.ended_down != new_state.ended_down) ? 1 : 0;
}

static f32 linux_normalize_stick(i16 raw_value, i16 dead_zone) {
    if (raw_value > -dead_zone && raw_value < dead_zone) {
        return 0.0f;
    }
    f32 divisor = raw_value < 0 ? 32768.0f : 32767.0f;
    return raw_value / divisor;
}

// controllers[0] is the keyboard, so gamepad slot N fills controllers[N + 1].
static SDL_Gamepad* global_gamepads[k_max_controllers - 1];

static void linux_add_gamepad(SDL_JoystickID id) {
    for (SDL_Gamepad*& slot : global_gamepads) {
        if (!slot) {
            slot = SDL_OpenGamepad(id);
            if (!slot) {
                linux_log(LogSeverity::Warn, "failed to open gamepad: %s\n", SDL_GetError());
            }
            return;
        }
    }
}

static void linux_remove_gamepad(SDL_JoystickID id) {
    for (SDL_Gamepad*& slot : global_gamepads) {
        if (slot && SDL_GetGamepadID(slot) == id) {
            SDL_CloseGamepad(slot);
            slot = nullptr;
            return;
        }
    }
}

static void linux_poll_gamepads(Input& curr_input, const Input& prev_input) {
    struct ButtonMapping {
        SDL_GamepadButton button;
        Action action;
    };
    // Face buttons by position, matching the Xbox layout used on Windows.
    constexpr ButtonMapping button_mappings[] = {
        {SDL_GAMEPAD_BUTTON_NORTH, Action::ActionUp},
        {SDL_GAMEPAD_BUTTON_SOUTH, Action::ActionDown},
        {SDL_GAMEPAD_BUTTON_WEST, Action::ActionLeft},
        {SDL_GAMEPAD_BUTTON_EAST, Action::ActionRight},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Action::LeftShoulder},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Action::RightShoulder},
        {SDL_GAMEPAD_BUTTON_START, Action::Start},
    };

    for (u32 idx = 0; idx < k_max_controllers - 1; idx++) {
        ControllerInput& new_input = curr_input.controllers[idx + 1];
        const ControllerInput& old_input = prev_input.controllers[idx + 1];
        SDL_Gamepad* pad = global_gamepads[idx];
        if (!pad) {
            new_input.is_connected = false;
            continue;
        }
        new_input.is_connected = true;
        new_input.is_analog = true;

        // SDL's y axis points down; the game's points up.
        new_input.stick_avg.x =
            linux_normalize_stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), k_stick_dead_zone);
        new_input.stick_avg.y =
            -linux_normalize_stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY), k_stick_dead_zone);

        // dpad, overrides the stick when used
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            new_input.is_analog = false;
            new_input.stick_avg.y = 1.0f;
        }
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            new_input.is_analog = false;
            new_input.stick_avg.y = -1.0f;
        }
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            new_input.is_analog = false;
            new_input.stick_avg.x = 1.0f;
        }
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            new_input.is_analog = false;
            new_input.stick_avg.x = -1.0f;
        }

        // Stick pushed past the threshold also counts as a move button.
        auto process_stick_move = [&](bool is_down, Action action) {
            linux_process_digital_button(is_down, input_action(old_input, action), input_action(new_input, action));
        };
        process_stick_move(new_input.stick_avg.x < -k_stick_move_threshold, Action::MoveLeft);
        process_stick_move(new_input.stick_avg.x > k_stick_move_threshold, Action::MoveRight);
        process_stick_move(new_input.stick_avg.y < -k_stick_move_threshold, Action::MoveDown);
        process_stick_move(new_input.stick_avg.y > k_stick_move_threshold, Action::MoveUp);

        for (const ButtonMapping& mapping : button_mappings) {
            linux_process_digital_button(
                SDL_GetGamepadButton(pad, mapping.button),
                input_action(old_input, mapping.action),
                input_action(new_input, mapping.action)
            );
        }
    }
}

static void linux_toggle_fullscreen(SDL_Window* window) {
    const bool is_fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    // With no display mode set, SDL uses borderless fullscreen at the
    // desktop resolution.
    SDL_SetWindowFullscreen(window, !is_fullscreen);
}

// Drains SDL's event queue. Keyboard events are recorded into kb.
static void linux_process_events(SDL_Window* window, ControllerInput& kb) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            global_running = false;
        } break;

        case SDL_EVENT_GAMEPAD_ADDED: {
            linux_add_gamepad(event.gdevice.which);
        } break;

        case SDL_EVENT_GAMEPAD_REMOVED: {
            linux_remove_gamepad(event.gdevice.which);
        } break;

#if DEV
        // The dev window stays on top so it can sit over the editor while
        // live coding. Fade it while another window has focus so the editor
        // shows through. Not every compositor supports opacity; SDL just
        // returns an error then.
        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            SDL_SetWindowOpacity(window, 1.0f);
        } break;

        case SDL_EVENT_WINDOW_FOCUS_LOST: {
            if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
                SDL_SetWindowOpacity(window, 64.0f / 255.0f);
            }
        } break;
#endif

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (event.key.repeat) {
                break;
            }
            const bool is_down = event.key.down;
            const bool is_alt_down = (event.key.mod & SDL_KMOD_ALT) != 0;

            switch (event.key.scancode) {
            case SDL_SCANCODE_RETURN: {
                if (is_alt_down && is_down) {
                    linux_toggle_fullscreen(window);
                }
            } break;

            case SDL_SCANCODE_F4: {
                if (is_alt_down && is_down) {
                    global_running = false;
                }
            } break;

            case SDL_SCANCODE_W: {
                linux_process_kb_button(input_action(kb, Action::MoveUp), is_down);
            } break;

            case SDL_SCANCODE_A: {
                linux_process_kb_button(input_action(kb, Action::MoveLeft), is_down);
            } break;

            case SDL_SCANCODE_S: {
                linux_process_kb_button(input_action(kb, Action::MoveDown), is_down);
            } break;

            case SDL_SCANCODE_D: {
                linux_process_kb_button(input_action(kb, Action::MoveRight), is_down);
            } break;

            case SDL_SCANCODE_Q: {
                linux_process_kb_button(input_action(kb, Action::LeftShoulder), is_down);
            } break;

            case SDL_SCANCODE_E: {
                linux_process_kb_button(input_action(kb, Action::RightShoulder), is_down);
            } break;

            case SDL_SCANCODE_C: {
                linux_process_kb_button(input_action(kb, Action::Start), is_down);
            } break;

            case SDL_SCANCODE_UP: {
                linux_process_kb_button(input_action(kb, Action::ActionUp), is_down);
            } break;

            case SDL_SCANCODE_DOWN: {
                linux_process_kb_button(input_action(kb, Action::ActionDown), is_down);
            } break;

            case SDL_SCANCODE_LEFT: {
                linux_process_kb_button(input_action(kb, Action::ActionLeft), is_down);
            } break;

            case SDL_SCANCODE_RIGHT: {
                linux_process_kb_button(input_action(kb, Action::ActionRight), is_down);
            } break;

            default: {
            } break;
            }
        } break;

        default: {
        } break;
        }
    }
}

// ---- Audio -----------------------------------------------------------------

// Tops the stream's queue back up to k_audio_target_queued_frames with
// freshly generated game audio.
static void linux_process_audio(SDL_AudioStream* stream, GameSound* game_sound, AudioState& audio_state) {
    if (!stream) {
        return;
    }

    static std::array<AudioFrame, k_audio_target_queued_frames> scratch;
    const int queued_bytes = SDL_GetAudioStreamQueued(stream);
    if (queued_bytes < 0) {
        return;
    }
    const u32 queued_frames = static_cast<u32>(queued_bytes) / k_audio_bytes_per_frame;
    if (queued_frames >= k_audio_target_queued_frames) {
        return;
    }

    AudioBuffer buffer = {
        .frames = std::span<AudioFrame>(scratch).first(k_audio_target_queued_frames - queued_frames),
        .samples_per_second = k_sound_samples_per_second,
    };
    game_sound(buffer, audio_state);
    if (!SDL_PutAudioStreamData(stream, buffer.frames.data(), static_cast<int>(buffer.frames.size_bytes()))) {
        linux_log(LogSeverity::Warn, "SDL_PutAudioStreamData failed: %s\n", SDL_GetError());
    }
}

// ---- Display ---------------------------------------------------------------

// Draws the buffer where display_rect (platform.h) puts it, black around it.
static void linux_display_buffer(
    SDL_Window* window,
    SDL_Renderer* renderer,
    SDL_Texture* texture,
    const ScreenBuffer& buffer
) {
    SDL_UpdateTexture(texture, nullptr, buffer.pixels.data(), static_cast<int>(buffer.pitch * sizeof(u32)));

    // display_rect works in window coordinates; the renderer draws in pixels,
    // which differ on a high-DPI display.
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(window, &window_width, &window_height);
    const f32 density = SDL_GetWindowPixelDensity(window);
    const DisplayRect dest =
        display_rect(static_cast<i32>(buffer.width), static_cast<i32>(buffer.height), window_width, window_height);
    const SDL_FRect dest_rect = {
        .x = dest.x * density,
        .y = dest.y * density,
        .w = dest.width * density,
        .h = dest.height * density,
    };

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, &dest_rect);
    SDL_RenderPresent(renderer);
}

#if DEV
// Where the dev window opens: flush against the right edge of the display the
// mouse is on (the one you're working on), vertically centered, so the editor
// stays visible on the left. Wayland doesn't let apps position their own
// windows, so there the compositor decides and this has no effect.
static void linux_place_dev_window(SDL_Window* window, int width, int height) {
    f32 mouse_x = 0.0f;
    f32 mouse_y = 0.0f;
    SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
    const SDL_Point mouse = {static_cast<int>(mouse_x), static_cast<int>(mouse_y)};
    SDL_DisplayID display = SDL_GetDisplayForPoint(&mouse);
    if (!display) {
        display = SDL_GetPrimaryDisplay();
    }

    SDL_Rect usable = {};
    if (!SDL_GetDisplayUsableBounds(display, &usable)) {
        return;
    }
    const int x = std::max(usable.x, usable.x + usable.w - width);
    const int y = std::max(usable.y, usable.y + (usable.h - height) / 2);
    SDL_SetWindowPosition(window, x, y);
}
#endif

// ---- Main ------------------------------------------------------------------

static u64 linux_wall_clock() {
    return SDL_GetPerformanceCounter();
}

static f32 linux_seconds_elapsed(u64 start, u64 end) {
    return static_cast<f32>(end - start) / static_cast<f32>(SDL_GetPerformanceFrequency());
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        linux_log(LogSeverity::Error, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    defer(SDL_Quit());

    if (!linux_init_exe_dir()) {
        linux_log(LogSeverity::Error, "failed to find the executable's directory\n");
        return 1;
    }

    // The window starts sized to hold the buffer 1:1 inside the gutter, like
    // the other platforms; see display_rect in platform.h.
    const int window_width = k_backbuffer_width + 2 * k_window_gutter_x;
    const int window_height = k_backbuffer_height + 2 * k_window_gutter_y;
    // Hidden until it's been placed, so it doesn't flash somewhere else first.
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    if constexpr (k_dev_mode) {
        window_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    SDL_Window* window = SDL_CreateWindow(k_window_title, window_width, window_height, window_flags);
    if (!window) {
        linux_log(LogSeverity::Error, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    defer(SDL_DestroyWindow(window));
#if DEV
    linux_place_dev_window(window, window_width, window_height);
#endif
    SDL_ShowWindow(window);
    if constexpr (!k_dev_mode) {
        SDL_HideCursor();
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        linux_log(LogSeverity::Error, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    defer(SDL_DestroyRenderer(renderer));

    // XRGB: the game leaves alpha at 0 in places (render_rect), so the
    // texture must ignore it rather than blend by it.
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        k_backbuffer_width,
        k_backbuffer_height
    );
    if (!texture) {
        linux_log(LogSeverity::Error, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }
    defer(SDL_DestroyTexture(texture));
    // Nearest-neighbor when scaling up, so pixels stay hard-edged.
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    const u64 backbuffer_pixel_count = static_cast<u64>(k_backbuffer_width) * k_backbuffer_height;
    u32* backbuffer_memory = static_cast<u32*>(linux_alloc(backbuffer_pixel_count * sizeof(u32)));
    if (!backbuffer_memory) {
        linux_log(LogSeverity::Error, "failed to allocate the backbuffer\n");
        return 1;
    }
    defer(linux_free(backbuffer_memory, backbuffer_pixel_count * sizeof(u32)));

    const SDL_AudioSpec audio_spec = {
        .format = SDL_AUDIO_S16,
        .channels = k_audio_channels,
        .freq = k_sound_samples_per_second,
    };
    SDL_AudioStream* audio_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
    if (audio_stream) {
        // Devices opened this way start paused.
        SDL_ResumeAudioStreamDevice(audio_stream);
    } else {
        linux_log(LogSeverity::Warn, "audio initialization failed (%s); continuing without audio\n", SDL_GetError());
    }
    defer(if (audio_stream) { SDL_DestroyAudioStream(audio_stream); });
    AudioState audio_state = {};

#if DEV
    void* base_address = reinterpret_cast<void*>(2_TB);
#else
    void* base_address = nullptr;
#endif

    Storage storage = {
        .main_size = 64_MB,
        .transient_size = 4_GB,
    };
    const u64 total_storage_size = storage.main_size + storage.transient_size;
    storage.main = linux_alloc(total_storage_size, base_address);
    if (!storage.main) {
        linux_log(LogSeverity::Error, "failed to allocate main game storage\n");
        return 1;
    }
    defer(linux_free(storage.main, total_storage_size));
    storage.transient = static_cast<u8*>(storage.main) + storage.main_size;
#if DEV
    storage.dev_read_file = dev_platform_read_file;
    storage.dev_free_file = dev_platform_free_file;
    storage.dev_write_file = dev_platform_write_file;
#endif

    LinuxGameCodePaths game_code_paths = {};
    if (!linux_exe_path(game_code_paths.source_so, sizeof(game_code_paths.source_so), "game.so")
        || !linux_exe_path(game_code_paths.lock, sizeof(game_code_paths.lock), "lock.tmp")) {
        linux_log(LogSeverity::Error, "game code path too long\n");
        return 1;
    }
    LinuxGameCode game_code = linux_load_game_code(game_code_paths);
    defer(linux_unload_game_code(game_code));

    const f32 target_seconds_per_frame = 1.0f / static_cast<f32>(k_game_refresh_hz);
    u64 last_counter = linux_wall_clock();

    Input input[2] = {};
    Input* curr_input = &input[0];
    Input* prev_input = &input[1];

    global_running = true;
    while (global_running) {
        curr_input->frame_dt = target_seconds_per_frame;

        // Hot reload: swap in the new game.so once the build has finished writing it.
        const struct timespec so_write_time = linux_get_last_write_time(game_code_paths.source_so);
        if (!linux_times_equal(so_write_time, game_code.so_last_write_time)
            && !linux_file_exists(game_code_paths.lock)) {
            linux_unload_game_code(game_code);
            game_code = linux_load_game_code(game_code_paths);
        }

        // Keyboard: buttons stay down across frames until a key-up arrives.
        ControllerInput& kb = curr_input->controllers[0];
        const ControllerInput& prev_kb = prev_input->controllers[0];
        kb = {};
        kb.is_connected = true;
        for (u8 i = 0; i < std::size(kb.actions); i++) {
            kb.actions[i].ended_down = prev_kb.actions[i].ended_down;
        }
        linux_process_events(window, kb);
        if (!global_running) {
            break;
        }
        linux_poll_gamepads(*curr_input, *prev_input);

        ScreenBuffer offscreen_buffer = {
            .pixels = {backbuffer_memory, backbuffer_pixel_count},
            .width = k_backbuffer_width,
            .height = k_backbuffer_height,
            .pitch = k_backbuffer_width,
        };
        game_code.render(storage, offscreen_buffer, *curr_input, audio_state);
        linux_process_audio(audio_stream, game_code.sound, audio_state);

        linux_display_buffer(window, renderer, texture, offscreen_buffer);

        // Hold the frame rate: sleep off most of the remaining time, then spin.
        f32 frame_seconds = linux_seconds_elapsed(last_counter, linux_wall_clock());
        if (frame_seconds < target_seconds_per_frame) {
            while (frame_seconds < target_seconds_per_frame) {
                const u64 remaining_ns = static_cast<u64>(1e9f * (target_seconds_per_frame - frame_seconds));
                // Leave ~1ms to spin so oversleeping doesn't miss the frame.
                if (remaining_ns > 1'000'000) {
                    SDL_DelayNS(remaining_ns - 1'000'000);
                }
                frame_seconds = linux_seconds_elapsed(last_counter, linux_wall_clock());
            }
        } else {
            linux_log(
                LogSeverity::Warn,
                "frame took %.02fms, missed target of %.02fms\n",
                1000.0f * frame_seconds,
                1000.0f * target_seconds_per_frame
            );
        }

        const u64 end_counter = linux_wall_clock();
        const f32 seconds_per_frame = linux_seconds_elapsed(last_counter, end_counter);
        last_counter = end_counter;

        char title[256];
        snprintf(
            title,
            sizeof(title),
            "%s [%.02fms/f | %.02ffps]",
            k_window_title,
            1000.0f * seconds_per_frame,
            1.0f / seconds_per_frame
        );
        SDL_SetWindowTitle(window, title);

        std::swap(curr_input, prev_input);
    }

    return 0;
}

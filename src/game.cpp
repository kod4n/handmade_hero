#include "math.h"
#include "game.h"

#include <algorithm>
#include <type_traits>

namespace HH {

static u32 pack_color(u8 r, u8 g, u8 b, u8 a = 0) {
    return (static_cast<u32>(a) << 24 | static_cast<u32>(r) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(b));
}

static void render_gradient(ScreenBuffer& buffer, GameState& state) {
    u8 r = 0;
    for (u32 y = 0; y < buffer.height; y++) {
        std::span<u32> row = buffer.pixels.subspan(y * buffer.pitch, buffer.width);
        auto g = static_cast<u8>(y + state.y_offset);
        for (u32 x = 0; x < buffer.width; x++) {
            auto b = static_cast<u8>(x + state.x_offset);
            row[x] = pack_color(r, g, b);
        }
    }
}

// Fills [x_min, x_max) x [y_min, y_max), clipped to the buffer.
static void render_rect(ScreenBuffer& buffer, int x_min, int y_min, int x_max, int y_max, u32 color) {
    const int width = static_cast<int>(buffer.width);
    const int height = static_cast<int>(buffer.height);
    x_min = std::clamp(x_min, 0, width);
    x_max = std::clamp(x_max, 0, width);
    y_min = std::clamp(y_min, 0, height);
    y_max = std::clamp(y_max, 0, height);

    for (int y = y_min; y < y_max; y++) {
        std::span<u32> row = buffer.pixels.subspan(static_cast<u32>(y) * buffer.pitch, buffer.width);
        for (int x = x_min; x < x_max; x++) {
            row[static_cast<u32>(x)] = color;
        }
    }
}

extern "C" void game_sound(AudioBuffer& audio, AudioState& state) {
    const f32 phase_step = 2.0f * k_pi * state.tone_hz / static_cast<f32>(audio.samples_per_second);

    for (AudioFrame& frame : audio.frames) {
        f32 sine = sinf(state.t_sine);
        i16 sample = static_cast<i16>(sine * state.tone_volume);
        frame = {.left = sample, .right = sample};

        // Wrap per frame: one buffer can advance the phase by several periods.
        state.t_sine += phase_step;
        if (state.t_sine >= 2.0f * k_pi) {
            state.t_sine -= 2.0f * k_pi;
        }
    }
}

extern "C" void game_render(Storage& storage, ScreenBuffer& buffer, Input& input, AudioState& audio_state) {
    if (!storage.is_initialized) {
        Arena arena = arena_make(storage.main, storage.main_size);
        GameState* state = arena_push<GameState>(arena);
        state->permanent_arena = arena;
        state->x_offset = 0;
        state->y_offset = 0;
        state->player_x = 100;
        state->player_y = 100;

        storage.is_initialized = true;
    }
    GameState* state = static_cast<GameState*>(storage.main);

    for (auto& controller : input.controllers) {
        if (!controller.is_connected) {
            continue;
        }
        constexpr f32 player_speed = 4.0f;  // pixels per frame
        if (controller.is_analog) {
            audio_state.tone_hz = 256 + static_cast<int>(128.0f * controller.stick_avg_y);
            state->x_offset += static_cast<int>(4.0f * controller.stick_avg_x);

            // Stick y is up-positive, the screen is top-down.
            state->player_x += static_cast<int>(player_speed * controller.stick_avg_x);
            state->player_y -= static_cast<int>(player_speed * controller.stick_avg_y);
        } else {  // mkb
            constexpr int step = static_cast<int>(player_speed);
            if (input_action(controller, Action::MoveLeft).ended_down) {
                state->x_offset -= 1;
                state->player_x -= step;
            }
            if (input_action(controller, Action::MoveRight).ended_down) {
                state->x_offset += 1;
                state->player_x += step;
            }
            if (input_action(controller, Action::MoveUp).ended_down) {
                state->player_y -= step;
            }
            if (input_action(controller, Action::MoveDown).ended_down) {
                state->player_y += step;
            }
        }

        if (input_action(controller, Action::ActionUp).ended_down) {
            state->y_offset += 1;
        }
    }

    render_gradient(buffer, *state);

    constexpr int player_size = 10;
    render_rect(
        buffer,
        state->player_x,
        state->player_y,
        state->player_x + player_size,
        state->player_y + player_size,
        pack_color(255, 255, 255)
    );
}

// Catch signature drift between the exported functions and the types the
// platform casts GetProcAddress results to.
static_assert(std::is_same_v<decltype(game_sound), GameSound>);
static_assert(std::is_same_v<decltype(game_render), GameRender>);

}  // namespace HH

#include "math.h"
#include "render.h"
#include "game.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace hh {

static u32 pack_color(u8 r, u8 g, u8 b, u8 a = 0) {
    return (static_cast<u32>(a) << 24 | static_cast<u32>(r) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(b));
}

// Fills [x_min, x_max) x [y_min, y_max), clipped to the buffer.
static void render_rect(ScreenBuffer& buffer, f32 x0, f32 y0, f32 x1, f32 y1, f32 r, f32 g, f32 b) {
    const f32 width = static_cast<int>(buffer.width);
    const f32 height = static_cast<int>(buffer.height);

    f32 x_min = round(x0);
    f32 x_max = round(x1);
    f32 y_min = round(y0);
    f32 y_max = round(y1);

    x_min = std::clamp(x_min, 0.0f, width);
    x_max = std::clamp(x_max, 0.0f, width);
    y_min = std::clamp(y_min, 0.0f, height);
    y_max = std::clamp(y_max, 0.0f, height);

    u32 color = pack_color(
        static_cast<u8>(round(r * 255.0f)),
        static_cast<u8>(round(g * 255.0f)),
        static_cast<u8>(round(b * 255.0f))
    );

    for (u32 y = y_min; y < y_max; y++) {
        std::span<u32> row = buffer.pixels.subspan(static_cast<u32>(y) * buffer.pitch, buffer.width);
        for (u32 x = x_min; x < x_max; x++) {
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
        state->player_x = 100;
        state->player_y = 100;

        storage.is_initialized = true;
    }
    GameState* state = static_cast<GameState*>(storage.main);

    for (auto& controller : input.controllers) {
        if (!controller.is_connected) {
            continue;
        }
        constexpr f32 player_speed = 32.0f;  // pixels per frame
        if (controller.is_analog) {
            audio_state.tone_hz = 256 + static_cast<int>(128.0f * controller.stick_avg_y);

            // Stick y is up-positive, the screen is top-down.
            state->player_x += static_cast<int>(player_speed * controller.stick_avg_x);
            state->player_y -= static_cast<int>(player_speed * controller.stick_avg_y);
        } else {  // mkb
            f32 dx = 0.0f;
            f32 dy = 0.0f;
            if (input_action(controller, Action::MoveLeft).ended_down) {
                dx = -1.0f;
            }
            if (input_action(controller, Action::MoveRight).ended_down) {
                dx = 1.0f;
            }
            if (input_action(controller, Action::MoveUp).ended_down) {
                dy = -1.0f;
            }
            if (input_action(controller, Action::MoveDown).ended_down) {
                dy = 1.0f;
            }

            dx *= player_speed;
            dy *= player_speed;

            state->player_x += static_cast<i32>(roundf(input.frame_dt * dx));
            state->player_y += static_cast<i32>(roundf(input.frame_dt * dy));
        }
    }

    u32 tile_map[tile_height][tile_width] = {
        //|          |           |           |
        {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    };

    // clear
    render_rect(buffer, 0.0f, 0.0f, buffer.width, buffer.height, 1.0f, 0.0f, 1.0f);

    f32 x_offset = -30.0f;
    f32 y_offset = -30.0f;
    for (u8 row = 0; row < tile_rows; row++) {
        for (u8 col = 0; col < tile_cols; col++) {
            u32 is_wall = tile_map[row][col];
            f32 color = 0.5f;
            if (is_wall) {
                color = 1.0f;
            }

            f32 x_min = x_offset + static_cast<f32>(col * tile_width);
            f32 x_max = x_min + static_cast<f32>(tile_width);
            f32 y_min = y_offset + static_cast<f32>(row * tile_height);
            f32 y_max = y_min + static_cast<f32>(tile_height);

            render_rect(buffer, x_min, y_min, x_max, y_max, color, color, color);
        }
    }

    // draw player
    constexpr f32 player_width = 0.75f * tile_width;
    constexpr f32 player_height = tile_height;
    const f32 player_left = state->player_x - 0.5f * player_width;
    const f32 player_top = state->player_y - player_height;
    constexpr f32 player_color_r = 1.0f;
    constexpr f32 player_color_g = 1.0f;
    constexpr f32 player_color_b = 0.0f;
    render_rect(
        buffer,
        player_left,
        player_top,
        player_left + player_width,
        player_top + player_height,
        player_color_r,
        player_color_g,
        player_color_b
    );
}

// Catch signature drift between the exported functions and the types the
// platform casts GetProcAddress results to.
static_assert(std::is_same_v<decltype(game_sound), GameSound>);
static_assert(std::is_same_v<decltype(game_render), GameRender>);

}  // namespace hh

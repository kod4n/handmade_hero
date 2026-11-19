#include "math.h"
#include "render.h"
#include "game.h"
#include "tile.cpp"

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

extern "C" void game_render(
    Storage& storage,
    ScreenBuffer& buffer,
    Input& input,
    [[maybe_unused]] AudioState& audio_state
) {
    if (!storage.is_initialized) {
        Arena arena = arena_make(storage.main, storage.main_size);
        GameState* state = arena_push<GameState>(arena);
        state->permanent_arena = arena;
        state->player_x = 100;
        state->player_y = 100;
        state->player_map_x = 0;
        state->player_map_y = 0;

        World& world = state->world;
        world.tile_count_x = k_tile_cols;
        world.tile_count_y = k_tile_rows;
        world.tile_min_x = k_render_min_x;
        world.tile_min_y = k_render_min_y;
        world.tile_width = k_tile_width;
        world.tile_height = k_tile_height;
        world.map_count_x = 2;
        world.map_count_y = 2;
        u32 map_count = world.map_count_x * world.map_count_y;
        world.maps = arena_push_array<TileMap>(state->permanent_arena, map_count).data();

        // indexed [y][x]
        u32* tiles[2][2] = {
            {reinterpret_cast<u32*>(map00), reinterpret_cast<u32*>(map01)},
            {reinterpret_cast<u32*>(map10), reinterpret_cast<u32*>(map11)},
        };
        for (u32 y = 0; y < world.map_count_y; y++) {
            for (u32 x = 0; x < world.map_count_x; x++) {
                world.maps[y * world.map_count_x + x] = {
                    .tiles = tiles[y][x],
                };
            }
        }

        storage.is_initialized = true;
    }
    GameState* state = static_cast<GameState*>(storage.main);
    World& world = state->world;

    constexpr f32 player_width = 0.75f * k_tile_width;
    constexpr f32 player_height = k_tile_height;

    for (auto& controller : input.controllers) {
        if (!controller.is_connected) {
            continue;
        }
        constexpr f32 player_speed = 128.0f;  // pixels per second
        f32 dx = 0.0f;
        f32 dy = 0.0f;
        if (controller.is_analog) {
            // Stick y is up-positive, the screen is top-down.
            dx = controller.stick_avg_x;
            dy = -controller.stick_avg_y;
        } else {  // mkb
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
        }

        dx *= player_speed;
        dy *= player_speed;

        WorldPos new_pos = {
            .map_x = state->player_map_x,
            .map_y = state->player_map_y,
            .x = state->player_x + roundf(input.frame_dt * dx),
            .y = state->player_y + roundf(input.frame_dt * dy),
        };

        // The player's position is the bottom-center of its rect, so check
        // the feet's left and right edges too, not just the center.
        WorldPos new_left = new_pos;
        new_left.x -= 0.5f * player_width;
        WorldPos new_right = new_pos;
        new_right.x += 0.5f * player_width;

        if (is_world_pos_empty(world, new_pos) && is_world_pos_empty(world, new_left)
            && is_world_pos_empty(world, new_right)) {
            // Crossing a map edge puts the player on the neighbouring map.
            WorldPos pos = canonicalize(world, new_pos);
            state->player_map_x = pos.map_x;
            state->player_map_y = pos.map_y;
            state->player_x = static_cast<i32>(pos.x);
            state->player_y = static_cast<i32>(pos.y);
        }
    }

    // clear
    render_rect(buffer, 0.0f, 0.0f, buffer.width, buffer.height, 0.0f, 0.0f, 0.0f);

    TileMap& map = get_tile_map(world, state->player_map_x, state->player_map_y);
    f32 x_offset = world.tile_min_x;
    f32 y_offset = world.tile_min_y;
    for (u8 row = 0; row < k_tile_rows; row++) {
        for (u8 col = 0; col < k_tile_cols; col++) {
            f32 color = 0.5f;
            if (is_tile_wall(state->world, map, col, row)) {
                color = 1.0f;
            }

            f32 x_min = x_offset + static_cast<f32>(col * k_tile_width);
            f32 x_max = x_min + static_cast<f32>(k_tile_width);
            f32 y_min = y_offset + static_cast<f32>(row * k_tile_height);
            f32 y_max = y_min + static_cast<f32>(k_tile_height);

            render_rect(buffer, x_min, y_min, x_max, y_max, color, color, color);
        }
    }

    // draw player
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

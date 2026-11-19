#include "math.h"
#include "render.h"
#include "game.h"
#include "random.h"
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
        state->player_pos = {
            .abs_tile_x = 2,
            .abs_tile_y = 2,
            .tile_rel_x = 0.25f,
            .tile_rel_y = 0.25f,
        };

        World& world = state->world;
        world.tile_side_in_meters = k_tile_side_in_meters;
        world.tile_side_in_pixels = k_tile_side_in_pixels;
        world.meters_to_pixels = static_cast<f32>(world.tile_side_in_pixels) / world.tile_side_in_meters;
        // world.chunk_hash starts empty: storage.main comes zeroed from the
        // platform. Chunks are created as the screens below write into them,
        // and new chunks start out as all wall.

        // Generate a path of walled screens. Each screen opens a door on its
        // right or bottom edge, chosen from the random table, and the next
        // screen opens the matching door on its left or top edge.
        //
        // The path only moves right or down: stepping left/up from screen 0
        // would wrap the u32 tile coordinates.
        constexpr u32 screen_count = 100;
        constexpr u32 door_col = k_screen_tile_cols / 2;
        constexpr u32 door_row = k_screen_tile_rows / 2;
        RandomSeries series = {};
        u32 screen_x = 0;
        u32 screen_y = 0;
        bool door_left = false;
        bool door_top = false;
        for (u32 screen = 0; screen < screen_count; screen++) {
            bool door_right = false;
            bool door_bottom = false;
            if (random_choice(series, 2) == 0) {
                door_right = true;
            } else {
                door_bottom = true;
            }

            for (u32 row = 0; row < k_screen_tile_rows; row++) {
                for (u32 col = 0; col < k_screen_tile_cols; col++) {
                    bool wall = row == 0 || row == k_screen_tile_rows - 1 || col == 0
                                || col == k_screen_tile_cols - 1;
                    if ((col == 0 && door_left) || (col == k_screen_tile_cols - 1 && door_right)) {
                        wall = wall && row != door_row;
                    }
                    if ((row == 0 && door_top) || (row == k_screen_tile_rows - 1 && door_bottom)) {
                        wall = wall && col != door_col;
                    }
                    set_tile_value(
                        state->permanent_arena,
                        world,
                        screen_x * k_screen_tile_cols + col,
                        screen_y * k_screen_tile_rows + row,
                        wall ? 1u : 0u
                    );
                }
            }

            door_left = door_right;
            door_top = door_bottom;
            if (door_right) {
                screen_x++;
            } else {
                screen_y++;
            }
        }

        storage.is_initialized = true;
    }
    GameState* state = static_cast<GameState*>(storage.main);
    World& world = state->world;

    // In meters.
    const f32 player_height = 1.4f;
    const f32 player_width = 0.75f * player_height;

    for (auto& controller : input.controllers) {
        if (!controller.is_connected) {
            continue;
        }
        constexpr f32 player_speed = 3.0f;  // meters per second
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

        // Crossing a map edge puts the player on the neighbouring map.
        WorldPosition new_pos = offset(world, state->player_pos, input.frame_dt * dx, input.frame_dt * dy);

        // The player's position is the bottom-center of its rect, so check
        // the feet's left and right edges too, not just the center.
        WorldPosition new_left = offset(world, new_pos, -0.5f * player_width, 0.0f);
        WorldPosition new_right = offset(world, new_pos, 0.5f * player_width, 0.0f);

        if (is_world_pos_empty(world, new_pos) && is_world_pos_empty(world, new_left)
            && is_world_pos_empty(world, new_right)) {
            state->player_pos = new_pos;
        }
    }

    // clear
    render_rect(buffer, 0.0f, 0.0f, buffer.width, buffer.height, 0.0f, 0.0f, 0.0f);

    // Pixel position on screen of the top-left tile of the screen shown.
    const f32 x_offset = k_render_min_x;
    const f32 y_offset = k_render_min_y;

    // Show the screen-sized block of tiles the player is in.
    const WorldPosition& player_pos = state->player_pos;
    const u32 screen_min_tile_x = (player_pos.abs_tile_x / k_screen_tile_cols) * k_screen_tile_cols;
    const u32 screen_min_tile_y = (player_pos.abs_tile_y / k_screen_tile_rows) * k_screen_tile_rows;
    const f32 tile_side = static_cast<f32>(world.tile_side_in_pixels);

    for (u32 row = 0; row < k_screen_tile_rows; row++) {
        for (u32 col = 0; col < k_screen_tile_cols; col++) {
            f32 color = 0.5f;
            if (is_tile_wall(world, screen_min_tile_x + col, screen_min_tile_y + row)) {
                color = 1.0f;
            }

            f32 x_min = x_offset + static_cast<f32>(col) * tile_side;
            f32 x_max = x_min + tile_side;
            f32 y_min = y_offset + static_cast<f32>(row) * tile_side;
            f32 y_max = y_min + tile_side;

            render_rect(buffer, x_min, y_min, x_max, y_max, color, color, color);
        }
    }

    // draw player
    const f32 m2p = world.meters_to_pixels;
    const f32 player_x =
        static_cast<f32>(player_pos.abs_tile_x - screen_min_tile_x) * tile_side + m2p * player_pos.tile_rel_x;
    const f32 player_y =
        static_cast<f32>(player_pos.abs_tile_y - screen_min_tile_y) * tile_side + m2p * player_pos.tile_rel_y;
    const f32 player_left = x_offset + player_x - 0.5f * m2p * player_width;
    const f32 player_top = y_offset + player_y - m2p * player_height;
    constexpr f32 player_color_r = 1.0f;
    constexpr f32 player_color_g = 1.0f;
    constexpr f32 player_color_b = 0.0f;
    render_rect(
        buffer,
        player_left,
        player_top,
        player_left + m2p * player_width,
        player_top + m2p * player_height,
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

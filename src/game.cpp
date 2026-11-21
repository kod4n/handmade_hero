#include "math.h"
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
        arena::Arena arena = arena::make(storage.main, storage.main_size);
        GameState* state = arena.push<GameState>();
        state->permanent_arena = arena;
        state->player_pos = {
            .abs_tile_x = 2,
            .abs_tile_y = 2,
            .abs_tile_z = 0,
            .tile_rel_x = 0.25f,
            .tile_rel_y = 0.25f,
        };
        state->camera_pos = state->player_pos;
        state->debug_zoom = 1.0f;

        World& world = state->world;
        world.tile_side_in_meters = k_tile_side_in_meters;
        world.tile_side_in_pixels = k_tile_side_in_pixels;
        world.meters_to_pixels = world.tile_side_in_pixels / world.tile_side_in_meters;
        // world.chunk_hash starts empty: storage.main comes zeroed from the
        // platform. Chunks are created as the screens below write into them,
        // and new chunks start out as all wall.

        // Generate a path of walled screens across two levels (z 0 and 1).
        // Each screen leaves through a door on its right or top edge, or
        // through stairs to the same screen on the other level, chosen from
        // the random table. The next screen opens the matching door on its
        // left or bottom edge, or the opposite stairs in the same spot. Row 0
        // is the bottom of a screen.
        //
        // The path only moves right or up: stepping left/down from screen 0
        // would wrap the u32 tile coordinates. Two level changes in a row
        // would lead back onto the screen just generated, so stairs are never
        // chosen right after arriving by stairs.
        constexpr u32 screen_count = 100;
        constexpr u32 door_col = k_screen_tile_cols / 2;
        constexpr u32 door_row = k_screen_tile_rows / 2;
        constexpr u32 stairs_col = k_screen_tile_cols / 2 + 4;
        constexpr u32 stairs_row = k_screen_tile_rows / 2 + 2;
        RandomSeries series = {};
        u32 screen_x = 0;
        u32 screen_y = 0;
        u32 screen_z = 0;
        bool door_left = false;
        bool door_bottom = false;
        u32 arrival_stairs = k_tile_empty;  // stairs this screen was entered by, if any
        for (u32 screen = 0; screen < screen_count; screen++) {
            bool door_right = false;
            bool door_top = false;
            bool take_stairs = false;
            switch (random_choice(series, arrival_stairs == k_tile_empty ? 3 : 2)) {
            case 0: door_right = true; break;
            case 1: door_top = true; break;
            default: take_stairs = true; break;
            }
            const u32 departure_stairs = screen_z == 0 ? k_tile_stairs_up : k_tile_stairs_down;

            for (u32 row = 0; row < k_screen_tile_rows; row++) {
                for (u32 col = 0; col < k_screen_tile_cols; col++) {
                    bool wall = row == 0 || row == k_screen_tile_rows - 1 || col == 0 || col == k_screen_tile_cols - 1;
                    if ((col == 0 && door_left) || (col == k_screen_tile_cols - 1 && door_right)) {
                        wall = wall && row != door_row;
                    }
                    if ((row == 0 && door_bottom) || (row == k_screen_tile_rows - 1 && door_top)) {
                        wall = wall && col != door_col;
                    }
                    u32 value = wall ? k_tile_wall : k_tile_empty;
                    if (col == stairs_col && row == stairs_row) {
                        if (take_stairs) {
                            value = departure_stairs;
                        } else if (arrival_stairs != k_tile_empty) {
                            value = arrival_stairs;
                        }
                    }
                    set_tile_value(
                        state->permanent_arena,
                        world,
                        screen_x * k_screen_tile_cols + col,
                        screen_y * k_screen_tile_rows + row,
                        screen_z,
                        value
                    );
                }
            }

            door_left = door_right;
            door_bottom = door_top;
            arrival_stairs = k_tile_empty;
            if (door_right) {
                screen_x++;
            } else if (door_top) {
                screen_y++;
            } else {
                // The other level's screen has stairs leading back here.
                arrival_stairs = departure_stairs == k_tile_stairs_up ? k_tile_stairs_down : k_tile_stairs_up;
                screen_z = 1 - screen_z;
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

        // Debug zoom: left shoulder (Q) zooms out, right shoulder (E) zooms in.
        auto pressed = [&](Action a) {
            const ActionState& s = input_action(controller, a);
            return s.ended_down && s.half_transition_count > 0;
        };
        constexpr f32 min_zoom = 1.0f / 16.0f;
        if (pressed(Action::LeftShoulder)) {
            state->debug_zoom = std::clamp(state->debug_zoom * 0.5f, min_zoom, 1.0f);
        }
        if (pressed(Action::RightShoulder)) {
            state->debug_zoom = std::clamp(state->debug_zoom * 2.0f, min_zoom, 1.0f);
        }
        f32 player_speed = 6.0f;  // meters per second
        f32 dx = 0.0f;
        f32 dy = 0.0f;
        if (controller.is_analog) {
            dx = controller.stick_avg_x;
            dy = controller.stick_avg_y;
        } else {  // mkb
            if (input_action(controller, Action::ActionUp).ended_down) {
                player_speed *= 2;
            }
            if (input_action(controller, Action::MoveLeft).ended_down) {
                dx = -1.0f;
            }
            if (input_action(controller, Action::MoveRight).ended_down) {
                dx = 1.0f;
            }
            if (input_action(controller, Action::MoveUp).ended_down) {
                dy = 1.0f;
            }
            if (input_action(controller, Action::MoveDown).ended_down) {
                dy = -1.0f;
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

        if (is_world_pos_walkable(world, new_pos) && is_world_pos_walkable(world, new_left)
            && is_world_pos_walkable(world, new_right)) {
            const WorldPosition old_pos = state->player_pos;
            state->player_pos = new_pos;

            // Stairs trigger only when stepping onto them. Arriving on the
            // other level puts the player on the matching stairs, which
            // doesn't count as a step, so it can't bounce straight back.
            bool changed_tile = new_pos.abs_tile_x != old_pos.abs_tile_x || new_pos.abs_tile_y != old_pos.abs_tile_y;
            u32 value = 0;
            if (changed_tile && get_tile_value(world, new_pos, value)) {
                if (value == k_tile_stairs_up) {
                    state->player_pos.abs_tile_z++;
                } else if (value == k_tile_stairs_down) {
                    state->player_pos.abs_tile_z--;
                }
            }
        }
    }

    // Ease the camera toward the player. 1 - exp(-k * dt) closes the same
    // fraction of the gap per second regardless of frame rate.
    {
        constexpr f32 camera_stiffness = 8.0f;  // 1/s, higher = tighter follow
        WorldDifference to_player = subtract(world, state->player_pos, state->camera_pos);
        f32 t = 1.0f - expf(-camera_stiffness * input.frame_dt);
        state->camera_pos = offset(world, state->camera_pos, t * to_player.dx, t * to_player.dy);
        // Levels don't ease; the view switches with the player.
        state->camera_pos.abs_tile_z = state->player_pos.abs_tile_z;
    }

    // clear
    render_rect(buffer, 0.0f, 0.0f, buffer.width, buffer.height, 0.0f, 0.0f, 0.0f);

    const WorldPosition& camera = state->camera_pos;
    const WorldPosition& player_pos = state->player_pos;
    const f32 tile_side = world.tile_side_in_pixels * state->debug_zoom;
    const f32 m2p = world.meters_to_pixels * state->debug_zoom;
    // The camera point lands here on screen.
    const f32 center_x = 0.5f * static_cast<f32>(buffer.width);
    const f32 center_y = 0.5f * static_cast<f32>(buffer.height);

    // Enough tiles to cover the buffer outward from the center, plus one for
    // the partial tiles at the edges.
    const i32 half_cols = static_cast<i32>(ceilf(center_x / tile_side)) + 1;
    const i32 half_rows = static_cast<i32>(ceilf(center_y / tile_side)) + 1;

    for (i32 rel_row = -half_rows; rel_row <= half_rows; rel_row++) {
        for (i32 rel_col = -half_cols; rel_col <= half_cols; rel_col++) {
            // Going below tile 0 wraps to a chunk that doesn't exist.
            const u32 tile_x = camera.abs_tile_x + static_cast<u32>(rel_col);
            const u32 tile_y = camera.abs_tile_y + static_cast<u32>(rel_row);

            // Debug colors: dark blue where no chunk exists, dark red for
            // tiles in an allocated chunk that nothing wrote.
            f32 r = 0.1f;
            f32 g = 0.1f;
            f32 b = 0.3f;
            u32 value = 0;
            if (get_tile_value(world, tile_x, tile_y, camera.abs_tile_z, value)) {
                switch (value) {
                case k_tile_empty:
                    r = g = b = 0.5f;
                    break;
                case k_tile_wall:
                    r = g = b = 1.0f;
                    break;
                case k_tile_stairs_up:
                    r = 0.3f;
                    g = 0.8f;
                    b = 0.3f;
                    break;
                case k_tile_stairs_down:
                    r = 0.2f;
                    g = 0.5f;
                    b = 0.8f;
                    break;
                default:
                    r = 0.4f;
                    g = 0.1f;
                    b = 0.1f;
                    break;
                }
            }
            if (tile_x == player_pos.abs_tile_x && tile_y == player_pos.abs_tile_y) {
                r = g = b = 0.75f;
            }

            // Bottom-left corner of the tile relative to the camera, in
            // pixels. World y points up but the pixel buffer is top-down.
            const f32 x_min = center_x + static_cast<f32>(rel_col) * tile_side - m2p * camera.tile_rel_x;
            const f32 y_max = center_y - static_cast<f32>(rel_row) * tile_side + m2p * camera.tile_rel_y;

            render_rect(buffer, x_min, y_max - tile_side, x_min + tile_side, y_max, r, g, b);
        }
    }

    // draw player
    const WorldDifference player_from_camera = subtract(world, player_pos, camera);
    const f32 player_ground_x = center_x + m2p * player_from_camera.dx;
    const f32 player_ground_y = center_y - m2p * player_from_camera.dy;
    const f32 player_left = player_ground_x - 0.5f * m2p * player_width;
    const f32 player_top = player_ground_y - m2p * player_height;
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

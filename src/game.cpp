#include "game.h"
#include <cmath>
#include "platform.h"
#include "render.h"
#include "tile.h"
#include "vec.h"
#include "random.h"
#include "tile.cpp"
#include "render.cpp"
#include "asset.cpp"

namespace hh {

static void generate_world(Arena& arena, World& world);

// Center of the room (one screen of tiles) that pos is in.
static WorldPosition room_center(World& world, const WorldPosition& pos) {
    const u32 room_x = pos.abs_tile.x / k_screen_tile_cols;
    const u32 room_y = pos.abs_tile.y / k_screen_tile_rows;
    const WorldPosition room_corner = {
        .abs_tile = {.x = room_x * k_screen_tile_cols, .y = room_y * k_screen_tile_rows, .z = pos.abs_tile.z},
        .tile_rel = {},
    };
    const Vec2 half_room_in_tiles = {
        .x = 0.5f * k_screen_tile_cols,
        .y = 0.5f * k_screen_tile_rows,
    };
    return world_pos_offset(world, room_corner, world.tile_side_in_meters * half_room_in_tiles);
}

// Hands out the next low entity slot, cleared. Returns 0 when every slot is taken.
static u32 add_entity(GameState& state) {
    assert(state.low_entity_count >= 1);
    u32 index = 0;
    if (state.low_entity_count < std::size(state.low_entities)) {
        index = state.low_entity_count++;
        state.low_entities[index] = {};
    }
    return index;
}

static Entity get_entity(GameState& state, u32 low_index) {
    Entity entity = {};
    if (low_index > 0 && low_index < state.low_entity_count) {
        entity.low_index = low_index;
        entity.low = &state.low_entities[low_index];
        if (u32 high_index = entity.low->high_index) {
            entity.high = &state.high_entities[high_index];
        }
    }
    return entity;
}

// Gives a low entity a high slot, placed relative to the current camera.
// Does nothing if it's already high or every high slot is taken. HighEntity
// has no z, so the caller checks the entity is on the camera's level.
static void make_entity_high(GameState& state, u32 low_index) {
    assert(low_index > 0 && low_index < state.low_entity_count);
    assert(state.high_entity_count >= 1);  // slot 0 is reserved at init
    LowEntity& low = state.low_entities[low_index];
    if (low.high_index || state.high_entity_count == k_max_high_entities) {
        return;
    }
    const u32 high_index = state.high_entity_count++;
    HighEntity& high = state.high_entities[high_index];
    high = {
        .pos = world_pos_diff(state.world, low.pos, state.camera_pos),
        .low_index = low_index,
    };
    low.high_index = high_index;
}

// Drops a low entity's high slot, if it has one. Nothing is written back:
// whatever moves a high entity must keep low.pos current as it goes. The last high entity is moved
// into the freed slot, so other entities' high indices can change.
static void make_entity_low(GameState& state, u32 low_index) {
    assert(low_index > 0 && low_index < state.low_entity_count);
    LowEntity& low = state.low_entities[low_index];
    const u32 high_index = low.high_index;
    if (!high_index) {
        return;
    }

    const u32 last = --state.high_entity_count;
    if (high_index != last) {
        state.high_entities[high_index] = state.high_entities[last];
        state.low_entities[state.high_entities[high_index].low_index].high_index = high_index;
    }
    low.high_index = 0;
}

// Hero sprite layout, in bitmap pixels (see make_hero in tools/make_bitmaps.py):
// a 48x60 body whose feet touch (24, 60), over a 32x8 shadow ellipse centered
// on the feet. Bitmaps are drawn 1:1, so a bitmap pixel is a screen pixel.
inline constexpr Vec2 k_hero_feet_px = {24.0f, 60.0f};
inline constexpr Vec2 k_hero_body_px = {48.0f, 60.0f};
inline constexpr Vec2 k_hero_shadow_px = {32.0f, 8.0f};
inline constexpr f32 k_meters_per_pixel = k_tile_side_in_meters / k_tile_side_in_pixels;

// Where new players appear: the middle of tile (1, 3) in the first room.
static WorldPosition player_spawn_pos(const World& world) {
    const WorldPosition tile = {.abs_tile = {.x = 8, .y = 4, .z = 0}, .tile_rel = {}};
    return world_pos_offset(world, tile, 0.5f * Vec2{world.tile_side_in_meters, world.tile_side_in_meters});
}

// Sets up the low entity add_entity handed out as a player. 0 (add_entity
// was full) is ignored. It becomes high on the next set_camera.
static void init_player(GameState& state, u32 low_index) {
    Entity entity = get_entity(state, low_index);
    if (!entity.low) {
        return;
    }
    entity.low->pos = player_spawn_pos(state.world);
    entity.low->facing = Facing::Front;
    // Sized from the sprite: the body is 1.12 x 1.4 m, and the footprint is
    // its shadow, so walls stop the hero where the shadow meets them.
    entity.low->width = k_meters_per_pixel * k_hero_body_px.x;
    entity.low->height = k_meters_per_pixel * k_hero_body_px.y;
    entity.low->collision_dim = k_meters_per_pixel * k_hero_shadow_px;

    // The first player to join gets the camera.
    if (state.camera_entity_index == 0) {
        state.camera_entity_index = low_index;
    }
}

// Whether a point rel (meters from the camera) is in the area whose entities
// are kept high: a room plus a margin, so the player isn't demoted while the
// camera lags behind it in a doorway or in Follow mode.
static bool in_camera_area(const World& world, Vec2 rel) {
    constexpr f32 tile_margin = 2.0f;
    const Vec2 tile_area = {k_screen_tile_cols + 2.0f * tile_margin, k_screen_tile_rows + 2.0f * tile_margin};
    const Vec2 half_size = 0.5f * world.tile_side_in_meters * tile_area;

    return fabsf(rel.x) <= half_size.x && fabsf(rel.y) <= half_size.y;
}

// The only place camera_pos changes. High positions are relative to the
// camera, so they're shifted by its move; then entities that left the camera
// area or aren't on its level are demoted, and ones that entered it are
// promoted. Checking the level per entity covers both the camera changing
// level and an entity taking stairs on its own; the followed player, who
// changes level along with the camera, stays high and keeps its velocity.
// world_pos_diff ignores z, so the shift is right across a level change too.
static void set_camera(GameState& state, const WorldPosition& new_pos) {
    const Vec2 camera_delta = world_pos_diff(state.world, new_pos, state.camera_pos);
    // Backwards: make_entity_low moves the last high entity into the freed
    // slot, and going down from the end means that one was already checked.
    for (u32 high_index = state.high_entity_count - 1; high_index > 0; high_index--) {
        HighEntity& high_entity = state.high_entities[high_index];
        const LowEntity& low_entity = state.low_entities[high_entity.low_index];
        high_entity.pos -= camera_delta;
        if (low_entity.pos.abs_tile.z != new_pos.abs_tile.z || !in_camera_area(state.world, high_entity.pos)) {
            make_entity_low(state, high_entity.low_index);
        }
    }

    state.camera_pos = new_pos;

    for (u32 low_index = 1; low_index < state.low_entity_count; low_index++) {
        LowEntity& low_entity = state.low_entities[low_index];
        if (low_entity.high_index != 0 || low_entity.pos.abs_tile.z != new_pos.abs_tile.z) {
            continue;
        }

        if (in_camera_area(state.world, world_pos_diff(state.world, low_entity.pos, new_pos))) {
            make_entity_high(state, low_index);
        }
    }
}

// Moves an entity for one frame. input_dir is the stick or keys (capped to length 1 here)
// speed is the top speed it leads to, in m/s.
static void move_player(GameState& state, u32 low_index, f32 dt, Vec2 input_dir, f32 speed) {
    Entity entity = get_entity(state, low_index);
    if (!entity.high) {
        return;  // only high entities should be simulated, exit early
    }

    // Face the dominant axis of the input; with no input, keep the last
    // facing. On an exact tie (a keyboard diagonal), keep the current
    // facing if it's one of the two, so it doesn't flicker.
    if (input_dir.x != 0.0f || input_dir.y != 0.0f) {
        const Facing horizontal = input_dir.x > 0.0f ? Facing::Right : Facing::Left;
        const Facing vertical = input_dir.y > 0.0f ? Facing::Back : Facing::Front;
        const f32 ax = fabsf(input_dir.x);
        const f32 ay = fabsf(input_dir.y);
        if (ax > ay) {
            entity.low->facing = horizontal;
        } else if (ay > ax) {
            entity.low->facing = vertical;
        } else if (entity.low->facing != horizontal && entity.low->facing != vertical) {
            entity.low->facing = horizontal;
        }
    }

    // A keyboard diagonal is {1, 1}, length sqrt(2), so it would move
    // ~41% faster than a single direction. Cap the input at length 1.
    // Only longer inputs are scaled: a partly tilted stick keeps its
    // smaller magnitude, so it still moves slower.
    Vec2 accel = input_dir;
    const f32 input_len_sq = length_sq(accel);
    if (input_len_sq > 1.0f) {
        accel *= 1.0f / sqrtf(input_len_sq);
    }

    // Input sets acceleration, and drag proportional to velocity opposes
    // it. Velocity levels off where they cancel, at accel / drag, so
    // accel = speed * drag makes speed the top speed. A higher drag reaches
    // top speed and stops sooner (time constant 1 / drag, ~0.125 s here).
    constexpr f32 drag = 8.0f;  // 1/s
    accel *= speed * drag;
    accel -= drag * entity.high->vel;

    // Constant-acceleration motion over the frame:
    //   p' = (0.5 * a * t^2) + (v * t) + p
    //   v' = v + (a * t)
    Vec2 delta = 0.5f * accel * square(dt) + entity.high->vel * dt;
    Vec2 new_vel = entity.high->vel + dt * accel;

    // Search in t: travel as much of the move as fits before the first
    // wall. The entity collides as its collision_dim footprint, centered on
    // its position (its feet).
    const WorldPosition old_pos = entity.low->pos;
    MoveResult move = try_move(state.world, entity.low->collision_dim, old_pos, delta);
    if (move.blocked) {
        // Glide: drop the into-wall part of the velocity and of the rest
        // of this frame's move, keeping only the part along the wall,
        // and search again from the wall. Head-on input leaves nothing
        // to glide with, so the entity just stops against the wall;
        // diagonal input slides. Wall friction slows the glide on top of
        // the usual drag: it levels off at
        // accel_along_wall / (drag + wall_friction), about half the free
        // speed along the wall here. exp keeps it frame-rate independent.
        constexpr f32 wall_friction = 8.0f;  // 1/s
        const f32 glide = expf(-wall_friction * dt);
        const Vec2 n = move.wall_normal;
        new_vel = glide * (new_vel - dot(new_vel, n) * n);
        Vec2 rest = (1.0f - move.t) * delta;
        rest = glide * (rest - dot(rest, n) * n);
        move = try_move(state.world, entity.low->collision_dim, move.new_pos, rest);
        if (move.blocked) {
            // A second wall (e.g. a corner): stop moving into it too.
            // Normals are axis-aligned, so this keeps the first removal.
            const Vec2 n2 = move.wall_normal;
            new_vel -= dot(new_vel, n2) * n2;
        }
    }
    entity.high->vel = new_vel;

    const WorldPosition& new_pos = move.new_pos;
    entity.low->pos = new_pos;
    entity.high->pos = world_pos_diff(state.world, new_pos, state.camera_pos);

    // Stairs trigger only when stepping onto them. Arriving on the other
    // level puts the entity on the matching stairs, which doesn't count as
    // a step, so it can't bounce straight back.
    bool changed_tile = new_pos.abs_tile.x != old_pos.abs_tile.x || new_pos.abs_tile.y != old_pos.abs_tile.y;
    u32 value = 0;
    if (changed_tile && get_tile_value(state.world, new_pos, value)) {
        // No demote here: set_camera demotes it if the camera doesn't follow
        // it to the new level.
        if (value == k_tile_stairs_up) {
            entity.low->pos.abs_tile.z++;
        } else if (value == k_tile_stairs_down) {
            entity.low->pos.abs_tile.z--;
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
        Arena arena = Arena::make(storage.main, storage.main_size);
        GameState* state = arena.push<GameState>();
#if DEV
        // Generated by tools/make_bitmaps.py.
        state->background = load_bmp(arena, storage.dev_read_file, storage.dev_free_file, "background.bmp");
        constexpr const char* hero_files[] = {
            "hero_front.bmp",
            "hero_back.bmp",
            "hero_left.bmp",
            "hero_right.bmp",
        };
        static_assert(std::size(hero_files) == static_cast<u32>(Facing::Count));
        for (u32 i = 0; i < std::size(hero_files); i++) {
            state->hero[i] = load_bmp(arena, storage.dev_read_file, storage.dev_free_file, hero_files[i]);
        }
#endif
        state->permanent_arena = arena;
        state->debug_zoom = 1.0f;

        // Slot 0 stays unused so index 0 can mean "no entity".
        state->low_entity_count = 1;
        state->high_entity_count = 1;

        World& world = state->world;
        world.tile_side_in_meters = k_tile_side_in_meters;
        world.tile_side_in_pixels = k_tile_side_in_pixels;
        world.meters_to_pixels = world.tile_side_in_pixels / world.tile_side_in_meters;

        // The keyboard player starts in the game; gamepads join with Start.
        // Every platform puts the keyboard in controllers[0].
        constexpr u32 keyboard_controller = 0;
        const u32 keyboard_player = add_entity(*state);
        init_player(*state, keyboard_player);
        state->player_index_for_controller[keyboard_controller] = keyboard_player;

        // Start already framing the first room instead of easing in to it.
        state->camera_mode = CameraMode::Room;
        set_camera(*state, room_center(world, player_spawn_pos(world)));

        generate_world(state->permanent_arena, world);

        storage.is_initialized = true;
    }
    GameState* state = static_cast<GameState*>(storage.main);
    World& world = state->world;

    for (u32 controller_index = 0; controller_index < std::size(input.controllers); controller_index++) {
        ControllerInput& controller = input.controllers[controller_index];
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
        // ActionDown (down arrow) switches between framing the room and
        // following the player.
        if (pressed(Action::ActionDown)) {
            state->camera_mode = state->camera_mode == CameraMode::Room ? CameraMode::Follow : CameraMode::Room;
        }

        // Each controller drives its own player. One that has none yet
        // joins by pressing Start (C), which spawns a new player.
        u32& player_index = state->player_index_for_controller[controller_index];
        if (!player_index) {
            if (pressed(Action::Start)) {
                player_index = add_entity(*state);
                init_player(*state, player_index);
            }
            continue;
        }

        f32 speed = 6.0f;  // top speed, meters per second
        Vec2 input_dir = {};
        if (controller.is_analog) {
            input_dir = controller.stick_avg;
        } else {  // mkb
            if (input_action(controller, Action::ActionUp).ended_down) {
                speed *= 2;
            }
            if (input_action(controller, Action::MoveLeft).ended_down) {
                input_dir.x = -1.0f;
            }
            if (input_action(controller, Action::MoveRight).ended_down) {
                input_dir.x = 1.0f;
            }
            if (input_action(controller, Action::MoveUp).ended_down) {
                input_dir.y = 1.0f;
            }
            if (input_action(controller, Action::MoveDown).ended_down) {
                input_dir.y = -1.0f;
            }
        }
        move_player(*state, player_index, input.frame_dt, input_dir, speed);
    }

    // Ease the camera toward its target: the followed player's room, or the
    // player. Entering a new room moves the target, so the camera scrolls
    // over to it. Until someone joins, it stays put.
    //
    // A critically damped spring: unlike moving a fixed fraction of the gap
    // each frame, which starts at full speed, it speeds up and slows down
    // smoothly, and never overshoots. This is the spring's exact solution
    // over dt, so it's stable and frame-rate independent at any dt:
    //   x' = (x + (v + w x) dt) e^(-w dt)
    //   v' = (v - w (v + w x) dt) e^(-w dt)
    // where x is the camera's offset from the target.
    const Entity camera_entity = get_entity(*state, state->camera_entity_index);
    if (camera_entity.low) {
        const LowEntity& low = *camera_entity.low;
        constexpr f32 camera_omega = 10.0f;  // 1/s, higher = tighter; settles in about 6.6 / omega s
        const WorldPosition target = state->camera_mode == CameraMode::Room ? room_center(world, low.pos) : low.pos;
        const f32 dt = input.frame_dt;
        const Vec2 x = world_pos_diff(world, state->camera_pos, target);
        const Vec2 temp = dt * (state->camera_vel + camera_omega * x);
        const f32 decay = expf(-camera_omega * dt);
        state->camera_vel = decay * (state->camera_vel - camera_omega * temp);
        const Vec2 new_x = decay * (x + temp);
        const Vec2 step = new_x - x;

        WorldPosition new_camera = world_pos_offset(world, state->camera_pos, step);
        new_camera.abs_tile.z = low.pos.abs_tile.z;
        set_camera(*state, new_camera);
    }

    // clear
    render_rect(buffer, {}, buffer.size_vec(), gray_value(0.0f));
    // Screen-space backdrop; empty tiles below are left undrawn so it shows.
    // Bitmaps are only loaded in DEV builds; elsewhere pixels is null and
    // the draw is a no-op.
    draw_bitmap(buffer, state->background, Vec2{});
    const bool has_background = state->background.pixels != nullptr;

    const WorldPosition& camera = state->camera_pos;
    const f32 tile_side = world.tile_side_in_pixels * state->debug_zoom;
    const f32 m2p = world.meters_to_pixels * state->debug_zoom;
    // Meters to pixels per axis. y is negated: world y points up, but the
    // pixel buffer is top-down.
    const Vec2 world_to_screen = {.x = m2p, .y = -m2p};
    // The camera point lands here on screen.
    const Vec2 screen_center = 0.5f * buffer.size_vec();

    // Enough tiles to cover the buffer outward from the center, plus one for
    // the partial tiles at the edges.
    const i32 half_cols = static_cast<i32>(ceilf(screen_center.x / tile_side)) + 1;
    const i32 half_rows = static_cast<i32>(ceilf(screen_center.y / tile_side)) + 1;

    for (i32 rel_row = -half_rows; rel_row <= half_rows; rel_row++) {
        for (i32 rel_col = -half_cols; rel_col <= half_cols; rel_col++) {
            // Going below tile 0 wraps to a chunk that doesn't exist.
            const u32 tile_x = camera.abs_tile.x + static_cast<u32>(rel_col);
            const u32 tile_y = camera.abs_tile.y + static_cast<u32>(rel_row);

            // Debug colors: dark blue where no chunk exists, dark red for
            // tiles in an allocated chunk that nothing wrote.
            Color tile_color = {.r = 0.1f, .g = 0.1f, .b = 0.3f};
            u32 value = 0;
            bool is_player_tile = false;
            for (u32 i = 1; i < state->low_entity_count; i++) {
                const LowEntity& e = state->low_entities[i];
                if (e.pos.abs_tile.x == tile_x && e.pos.abs_tile.y == tile_y && e.pos.abs_tile.z == camera.abs_tile.z) {
                    is_player_tile = true;
                    break;
                }
            }
            if (get_tile_value(world, tile_x, tile_y, camera.abs_tile.z, value)) {
                if (value == k_tile_empty && has_background && !is_player_tile) {
                    continue;
                }
                switch (value) {
                case k_tile_empty:
                    tile_color = gray_value(0.5f);
                    break;
                case k_tile_wall:
                    tile_color = gray_value(1.0f);
                    break;
                case k_tile_stairs_up:
                    tile_color = {.r = 0.3f, .g = 0.8f, .b = 0.3f};
                    break;
                case k_tile_stairs_down:
                    tile_color = {.r = 0.2f, .g = 0.5f, .b = 0.8f};
                    break;
                default:
                    tile_color = {.r = 0.4f, .g = 0.1f, .b = 0.1f};
                    break;
                }
            }
            if (is_player_tile) {
                tile_color = gray_value(0.75f);
            }

            // Screen position of the bottom-left corner of tile (col, row)
            // relative to the camera. World y points up but the pixel buffer
            // is top-down. Both edges of every tile go through this, so
            // neighbors share bit-identical edges: adding tile_side instead
            // differs by float error (1.4 m isn't exact), and when that
            // straddles a rounding boundary it leaves a 1px seam.
            auto corner_on_screen = [&](i32 col, i32 row) {
                const Vec2 corner_from_camera =
                    world.tile_side_in_meters * Vec2{static_cast<f32>(col), static_cast<f32>(row)} - camera.tile_rel;
                return screen_center + hadamard(world_to_screen, corner_from_camera);
            };
            const Vec2 bottom_left = corner_on_screen(rel_col, rel_row);
            const Vec2 top_right = corner_on_screen(rel_col + 1, rel_row + 1);
            const Vec2 tile_min = {bottom_left.x, top_right.y};
            const Vec2 tile_max = {top_right.x, bottom_left.y};

            render_rect(buffer, tile_min, tile_max, tile_color);
        }
    }

    // Only high entities are drawn: they're the ones near the camera, and
    // always on its level. Their pos is already relative to the camera.
    for (u32 high_index = 1; high_index < state->high_entity_count; high_index++) {
        const HighEntity& high = state->high_entities[high_index];
        const LowEntity& entity = state->low_entities[high.low_index];

        const Vec2 ground_pos = screen_center + hadamard(world_to_screen, high.pos);
        const LoadedBitmap& hero = state->hero[static_cast<u32>(entity.facing)];
        if (hero.pixels) {
            // pos is the feet, which are also the shadow's (and footprint's) center.
            draw_bitmap(buffer, hero, ground_pos - k_hero_feet_px);
        } else {
            const Vec2 dim = {.x = entity.width, .y = entity.height};
            const Vec2 origin = ground_pos - m2p * Vec2{0.5f * entity.width, entity.height};
            constexpr Color player_color = {.r = 1.0f, .g = 1.0f, .b = 0.0f};
            render_rect(buffer, origin, origin + m2p * dim, player_color);
        }

        // Debug: the footprint try_move collides with, centered on pos. Drawn
        // on top so it stays visible; it's symmetric, so the y flip doesn't
        // change which corner is min.
        if (k_dev_mode) {
            Vec2 half_collision = 0.5f * m2p * entity.collision_dim;
            constexpr Color collision_color = {.r = 1.0f, .g = 0.0f, .b = 1.0f};
            render_rect(buffer, ground_pos - half_collision, ground_pos + half_collision, collision_color);
        }
    }
}

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
//
// world.chunk_hash starts empty: storage.main comes zeroed from the
// platform. Chunks are created as the screens write into them, and new
// chunks start out as all wall, so everything outside the screens is wall.
static void generate_world(Arena& arena, World& world) {
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
        case 0:
            door_right = true;
            break;
        case 1:
            door_top = true;
            break;
        default:
            take_stairs = true;
            break;
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
                    arena,
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
}

// Catch signature drift between the exported functions and the types the
// platform casts GetProcAddress results to.
static_assert(std::is_same_v<decltype(game_sound), GameSound>);
static_assert(std::is_same_v<decltype(game_render), GameRender>);

}  // namespace hh

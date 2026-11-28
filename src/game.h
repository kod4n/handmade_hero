#pragma once

#include "platform.h"  // IWYU pragma: export
#include "types.h"
#include "arena.h"
#include "tile.h"
#include "render.h"
#include <numbers>

namespace hh {

inline constexpr f32 k_pi = std::numbers::pi_v<f32>;

// Which way the hero sprite faces; indexes GameState::hero.
enum class Facing : u32 {
    Front,  // toward the camera, moving down
    Back,   // away from the camera, moving up
    Left,
    Right,
    Count,
};

enum class CameraMode : u32 {
    Room,    // centered on the room (screen) the player is in
    Follow,  // centered on the player
};

struct LowEntity {
    WorldPosition pos;   // bottom-center
    f32 width;           // meters, of the drawn body
    f32 height;          // meters, of the drawn body
    Facing facing;       // last direction moved in; kept while idle
    Vec2 collision_dim;  // meters; ground footprint centered on pos, what collides with walls
    u32 high_index;      // ref to high entity, zero means not high freq
};

struct HighEntity {
    Vec2 pos;       // meters from camera pos
    Vec2 vel;       // m/s
    u32 low_index;  // ref to low entity
};

// A view of one entity, built on demand. Never keep one across make_entity_low
// or set_camera: they can move another entity into `high`'s slot.
struct Entity {
    u32 low_index;     // reference to low entity
    LowEntity* low;    // low entity ptr, null means not found or out of bounds index
    HighEntity* high;  // high entity ptr, null if not high (or low is null)
};

inline constexpr u32 k_max_entities = 256;
inline constexpr u32 k_max_high_entities = 64;

// Always the first allocation in Storage::main.
struct GameState {
    Arena permanent_arena;     // remainder of Storage::main after GameState
    WorldPosition camera_pos;  // world point drawn at the center of the screen
    CameraMode camera_mode;    // what camera_pos eases toward; ActionDown toggles it
    Vec2 camera_vel;           // meters per second, of the easing spring
    f32 debug_zoom;            // render scale; < 1 zooms out to inspect the map
    // Entity indices. Slot 0 is reserved and never used, so 0 means "none".
    u32 camera_entity_index;                             // who the camera tracks, low entity index
    u32 player_index_for_controller[k_max_controllers];  // set when Start joins, low entity indexes
    LowEntity low_entities[k_max_entities];
    u32 low_entity_count;   // slots handed out, counting the reserved 0
    HighEntity high_entities[k_max_high_entities];
    u32 high_entity_count;  // slots in use, counting the reserved 0
    World world;
    LoadedBitmap background;
    LoadedBitmap hero[static_cast<u32>(Facing::Count)];
};
static_assert(ArenaType<GameState>);

}  // namespace hh

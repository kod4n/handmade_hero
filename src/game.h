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

// Something that lives in the world. For now every entity is a player.
struct Entity {
    bool exists;
    WorldPosition pos;  // bottom-center
    Vec2 vel;           // meters per second
    Facing facing;      // last direction moved in; kept while idle
    f32 width;          // meters, of the drawn body
    f32 height;         // meters, of the drawn body
    // Meters. The patch of ground it stands on, centered on pos; this, not
    // width x height, is what collides with walls.
    Vec2 collision_dim;
};

inline constexpr u32 k_max_entities = 256;

// Always the first allocation in Storage::main.
struct GameState {
    Arena permanent_arena;     // remainder of Storage::main after GameState
    WorldPosition camera_pos;  // world point drawn at the center of the screen
    CameraMode camera_mode;    // what camera_pos eases toward; ActionDown toggles it
    f32 debug_zoom;            // render scale; < 1 zooms out to inspect the map
    // Entity indices. Slot 0 is reserved and never used, so 0 means "none".
    u32 camera_entity_index;                             // who the camera tracks
    u32 player_index_for_controller[k_max_controllers];  // set when Start joins
    u32 entity_count;                                    // slots handed out so far, counting the reserved 0
    Entity entities[k_max_entities];
    World world;
    LoadedBitmap background;
    LoadedBitmap hero[static_cast<u32>(Facing::Count)];
};
static_assert(ArenaType<GameState>);

}  // namespace hh

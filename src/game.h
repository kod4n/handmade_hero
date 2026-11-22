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

// Always the first allocation in Storage::main.
struct GameState {
    arena::Arena permanent_arena;  // remainder of Storage::main after GameState
    WorldPosition player_pos;      // bottom-center of the player
    WorldPosition camera_pos;      // world point drawn at the center of the screen
    CameraMode camera_mode;        // what camera_pos eases toward; Start toggles it
    f32 debug_zoom;                // render scale; < 1 zooms out to inspect the map
    World world;
    LoadedBitmap background;
    LoadedBitmap hero[static_cast<u32>(Facing::Count)];
    Facing hero_facing;  // last direction moved in; kept while idle
};
static_assert(arena::ArenaType<GameState>);

}  // namespace hh

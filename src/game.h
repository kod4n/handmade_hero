#pragma once

#include "platform.h"  // IWYU pragma: export
#include "types.h"
#include "arena.h"
#include "tile.h"
#include <numbers>

namespace hh {

inline constexpr f32 k_pi = std::numbers::pi_v<f32>;

#pragma pack(push, 1)
struct BitmapHeader {
    u16 file_type;
    u32 file_size;
    u16 reserved1;
    u16 reserved2;
    u32 offset;
    u32 size;
    i32 width;
    i32 height;
    u16 planes;
    u16 bits_per_pixel;
    u32 compression;
    u32 size_of_bitmap;
    i32 horz_resolution;
    i32 vert_resolution;
    u32 colors_used;
    u32 colors_important;
    // Only meaningful when compression is BI_BITFIELDS (3).
    u32 red_mask;
    u32 green_mask;
    u32 blue_mask;
};
#pragma pack(pop)
static_assert(sizeof(BitmapHeader) == 66);

struct LoadedBitmap {
    u32* pixels;  // 0xAARRGGBB, top-down rows, width * height, in the arena
    u32 width;
    u32 height;
};

// Always the first allocation in Storage::main.
struct GameState {
    arena::Arena permanent_arena;  // remainder of Storage::main after GameState
    WorldPosition player_pos;      // bottom-center of the player
    WorldPosition camera_pos;      // world point drawn at the center of the screen
    f32 debug_zoom;                // render scale; < 1 zooms out to inspect the map
    World world;
    LoadedBitmap background;
    LoadedBitmap hero;
};
static_assert(arena::ArenaType<GameState>);

}  // namespace hh

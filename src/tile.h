#pragma once
#include "types.h"

namespace hh {

constexpr u8 k_tile_cols = 17;
constexpr u8 k_tile_rows = 9;
constexpr u8 k_tile_width = 60;
constexpr u8 k_tile_height = 60;

struct TileMap {
    i32 count_x;
    i32 count_y;
    f32 min_x;
    f32 min_y;
    f32 tile_width;
    f32 tile_height;
    u32* tiles;
};

struct World {
    u32 map_count_x;
    u32 map_count_y;
    TileMap* maps;
};

// A position inside one tile map of the world, in that map's pixel space.
struct WorldPos {
    i32 map_x;
    i32 map_y;
    f32 x;
    f32 y;
};

}  // namespace hh

#pragma once
#include "types.h"

namespace hh {

// How many tiles one screen shows.
constexpr u8 k_screen_tile_cols = 17;
constexpr u8 k_screen_tile_rows = 9;

constexpr f32 k_tile_side_in_meters = 1.4f;
constexpr i32 k_tile_side_in_pixels = 60;

// Chunks are k_chunk_dim x k_chunk_dim tiles. An absolute tile coordinate
// keeps the chunk index in its high bits and the tile within the chunk in
// its low k_chunk_shift bits.
constexpr u32 k_chunk_shift = 8;
constexpr u32 k_chunk_dim = 1u << k_chunk_shift;
constexpr u32 k_chunk_mask = k_chunk_dim - 1;

struct TileChunk {
    u32* tiles;  // k_chunk_dim * k_chunk_dim, row-major
};

// Tiles are square. Game logic works in meters; only rendering converts to
// pixels, via meters_to_pixels.
struct World {
    u32 chunk_count_x;
    u32 chunk_count_y;
    f32 tile_side_in_meters;
    i32 tile_side_in_pixels;
    f32 meters_to_pixels;
    TileChunk* chunks;
};

// A position in the world: an absolute tile coordinate (chunk in the high
// bits, tile within the chunk in the low bits) and the offset within that
// tile in meters. Moving past tile 0 wraps the u32 around to a chunk index
// far outside the world, which reads as off-world. tile_rel is always in
// range (0 <= tile_rel < tile side); move with offset() so it stays that way.
struct WorldPosition {
    u32 abs_tile_x;
    u32 abs_tile_y;
    f32 tile_rel_x;
    f32 tile_rel_y;
};

}  // namespace hh

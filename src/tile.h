#pragma once
#include "types.h"
#include "vec.h"

namespace hh {

// How many tiles one screen shows.
inline constexpr u8 k_screen_tile_cols = 17;
inline constexpr u8 k_screen_tile_rows = 9;

inline constexpr f32 k_tile_side_in_meters = 1.4f;
inline constexpr f32 k_tile_side_in_pixels = 60.0f;

// Chunks are k_chunk_dim x k_chunk_dim tiles. An absolute tile coordinate
// keeps the chunk index in its high bits and the tile within the chunk in
// its low k_chunk_shift bits.
inline constexpr u32 k_chunk_shift = 4;
inline constexpr u32 k_chunk_dim = 1u << k_chunk_shift;
inline constexpr u32 k_chunk_mask = k_chunk_dim - 1;

// Chunks live in a hash table keyed on their chunk coordinate and are only
// allocated once something writes to them, so storage scales with the tiles
// actually in use rather than with the world's extent.
inline constexpr u32 k_chunk_hash_size = 4096;  // must be a power of two
static_assert((k_chunk_hash_size & (k_chunk_hash_size - 1)) == 0);

// Tile values.
inline constexpr u32 k_tile_empty = 0;
inline constexpr u32 k_tile_wall = 1;
inline constexpr u32 k_tile_uninitialized = 2;  // in an allocated chunk, but never written
inline constexpr u32 k_tile_stairs_up = 3;      // stepping onto it moves to z + 1
inline constexpr u32 k_tile_stairs_down = 4;    // stepping onto it moves to z - 1

// z isn't chunked: every level has its own chunks, so chunk_z is the absolute
// tile z.
struct TileChunk {
    u32 chunk_x;
    u32 chunk_y;
    u32 chunk_z;
    u32* tiles;       // k_chunk_dim * k_chunk_dim, row-major; null marks an unused slot
    TileChunk* next;  // next chunk whose coordinate hashes to the same slot
};

// Tiles are square. Game logic works in meters; only rendering converts to
// pixels, via meters_to_pixels.
struct World {
    f32 tile_side_in_meters;
    f32 tile_side_in_pixels;
    f32 meters_to_pixels;
    // The first chunk of each chain lives inline; the rest come from the arena.
    // Zeroed memory is an empty table.
    TileChunk chunk_hash[k_chunk_hash_size];
};

// World y points up: +y is up, both in tile coordinates and in tile_rel.y.
// Only rendering flips it to match the top-down pixel buffer.
//
// A position in the world: an absolute tile coordinate (chunk in the high
// bits, tile within the chunk in the low bits) and the offset within that
// tile in meters. Moving past tile 0 wraps the u32 around to a chunk index
// far from anything allocated, which reads as off-world. tile_rel is always in
// range (0 <= tile_rel < tile side); move with world_pos_offset() so it stays that way.
struct WorldPosition {
    Vec3u abs_tile;  // z is the level; it changes only through stairs
    Vec2 tile_rel;
};

}  // namespace hh

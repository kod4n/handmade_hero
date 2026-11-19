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

// Chunks live in a hash table keyed on their chunk coordinate and are only
// allocated once something writes to them, so storage scales with the tiles
// actually in use rather than with the world's extent.
constexpr u32 k_chunk_hash_size = 4096;  // must be a power of two
static_assert((k_chunk_hash_size & (k_chunk_hash_size - 1)) == 0);

struct TileChunk {
    u32 chunk_x;
    u32 chunk_y;
    u32* tiles;       // k_chunk_dim * k_chunk_dim, row-major; null marks an unused slot
    TileChunk* next;  // next chunk whose coordinate hashes to the same slot
};

// Tiles are square. Game logic works in meters; only rendering converts to
// pixels, via meters_to_pixels.
struct World {
    f32 tile_side_in_meters;
    i32 tile_side_in_pixels;
    f32 meters_to_pixels;
    // The first chunk of each chain lives inline; the rest come from the arena.
    // Zeroed memory is an empty table.
    TileChunk chunk_hash[k_chunk_hash_size];
};

// A position in the world: an absolute tile coordinate (chunk in the high
// bits, tile within the chunk in the low bits) and the offset within that
// tile in meters. Moving past tile 0 wraps the u32 around to a chunk index
// far from anything allocated, which reads as off-world. tile_rel is always in
// range (0 <= tile_rel < tile side); move with offset() so it stays that way.
struct WorldPosition {
    u32 abs_tile_x;
    u32 abs_tile_y;
    f32 tile_rel_x;
    f32 tile_rel_y;
};

}  // namespace hh

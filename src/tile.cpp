#pragma once
#include "arena.h"
#include "tile.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <span>

namespace hh {

// Where an absolute tile coordinate lands: which chunk, and which tile in it.
struct ChunkPosition {
    u32 chunk_x;
    u32 chunk_y;
    u32 tile_x;
    u32 tile_y;
};

ChunkPosition get_chunk_position(u32 abs_tile_x, u32 abs_tile_y) {
    ChunkPosition res = {
        .chunk_x = abs_tile_x >> k_chunk_shift,
        .chunk_y = abs_tile_y >> k_chunk_shift,
        .tile_x = abs_tile_x & k_chunk_mask,
        .tile_y = abs_tile_y & k_chunk_mask,
    };
    return res;
}

// Value of every tile in a newly created chunk.
constexpr u32 k_default_tile_value = 1;  // wall

// Finds the chunk at (chunk_x, chunk_y). If it doesn't exist yet, creates it
// from `arena` when one is given, and returns null otherwise.
TileChunk* get_tile_chunk(World& world, u32 chunk_x, u32 chunk_y, Arena* arena = nullptr) {
    u32 hash = chunk_x * 19 + chunk_y * 7 + (chunk_x >> 8) * 3;
    TileChunk* chunk = &world.chunk_hash[hash & (k_chunk_hash_size - 1)];

    // Walk the chain. An unused head slot can be claimed directly; otherwise
    // a new chunk gets appended after the last one.
    while (chunk->tiles) {
        if (chunk->chunk_x == chunk_x && chunk->chunk_y == chunk_y) {
            return chunk;
        }
        if (!chunk->next) {
            if (!arena) {
                return nullptr;
            }
            chunk->next = arena_push<TileChunk>(*arena);
            chunk->next->tiles = nullptr;
            chunk = chunk->next;
            break;
        }
        chunk = chunk->next;
    }

    if (!arena) {
        return nullptr;
    }

    std::span<u32> tiles = arena_push_array<u32>(*arena, k_chunk_dim * k_chunk_dim);
    std::fill(tiles.begin(), tiles.end(), k_default_tile_value);
    chunk->chunk_x = chunk_x;
    chunk->chunk_y = chunk_y;
    chunk->tiles = tiles.data();
    chunk->next = nullptr;
    return chunk;
}

// Returns false, leaving value untouched, when no chunk holds the tile.
bool get_tile_value(World& world, u32 abs_tile_x, u32 abs_tile_y, u32& value) {
    bool res = false;

    ChunkPosition pos = get_chunk_position(abs_tile_x, abs_tile_y);
    TileChunk* chunk = get_tile_chunk(world, pos.chunk_x, pos.chunk_y);
    if (chunk) {
        value = chunk->tiles[pos.tile_y * k_chunk_dim + pos.tile_x];
        res = true;
    }

    return res;
}

// Creates the tile's chunk from `arena` if it doesn't exist yet.
void set_tile_value(Arena& arena, World& world, u32 abs_tile_x, u32 abs_tile_y, u32 value) {
    ChunkPosition pos = get_chunk_position(abs_tile_x, abs_tile_y);
    TileChunk* chunk = get_tile_chunk(world, pos.chunk_x, pos.chunk_y, &arena);
    assert(chunk);
    chunk->tiles[pos.tile_y * k_chunk_dim + pos.tile_x] = value;
}

bool is_tile_empty(World& world, u32 abs_tile_x, u32 abs_tile_y) {
    u32 value = 0;
    bool res = get_tile_value(world, abs_tile_x, abs_tile_y, value) && value == 0;
    return res;
}

bool is_tile_wall(World& world, u32 abs_tile_x, u32 abs_tile_y) {
    u32 value = 0;
    bool res = get_tile_value(world, abs_tile_x, abs_tile_y, value) && value == 1;
    return res;
}

// Carries an out-of-range offset within a tile into the absolute tile
// coordinate. Carrying into the next chunk falls out of the bit layout, and
// stepping left/up from tile 0 wraps to a chunk that doesn't exist.
static void recanonicalize_coord(f32 tile_side, u32& tile, f32& tile_rel) {
    i32 carry = static_cast<i32>(floorf(tile_rel / tile_side));
    tile += static_cast<u32>(carry);
    tile_rel -= static_cast<f32>(carry) * tile_side;
    // A tiny negative tile_rel can round up to exactly tile_side above.
    if (tile_rel >= tile_side) {
        tile++;
        tile_rel -= tile_side;
    }

    assert(tile_rel >= 0.0f && tile_rel < tile_side);
}

WorldPosition offset(World& world, WorldPosition pos, f32 dx, f32 dy) {
    pos.tile_rel_x += dx;
    pos.tile_rel_y += dy;
    recanonicalize_coord(world.tile_side_in_meters, pos.abs_tile_x, pos.tile_rel_x);
    recanonicalize_coord(world.tile_side_in_meters, pos.abs_tile_y, pos.tile_rel_y);
    return pos;
}

bool is_world_pos_empty(World& world, WorldPosition pos) {
    bool res = is_tile_empty(world, pos.abs_tile_x, pos.abs_tile_y);
    return res;
}

}  // namespace hh

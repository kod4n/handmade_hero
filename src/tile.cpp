#pragma once
#include "tile.h"
#include <cassert>
#include <cmath>

namespace hh {

u32 map00[k_screen_tile_rows][k_screen_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
};
u32 map01[k_screen_tile_rows][k_screen_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
};
u32 map10[k_screen_tile_rows][k_screen_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};
u32 map11[k_screen_tile_rows][k_screen_tile_cols] = {
    //|          |           |           |
    {1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

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

// Null when the chunk is outside the world.
TileChunk* get_tile_chunk(World& world, u32 chunk_x, u32 chunk_y) {
    TileChunk* res = nullptr;
    if (chunk_x < world.chunk_count_x && chunk_y < world.chunk_count_y) {
        res = &world.chunks[chunk_y * world.chunk_count_x + chunk_x];
    }
    return res;
}

// Returns false, leaving value untouched, when the tile is outside the world.
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

// The tile must be inside the world.
void set_tile_value(World& world, u32 abs_tile_x, u32 abs_tile_y, u32 value) {
    ChunkPosition pos = get_chunk_position(abs_tile_x, abs_tile_y);
    TileChunk* chunk = get_tile_chunk(world, pos.chunk_x, pos.chunk_y);
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
// stepping left/up from tile 0 wraps to an off-world chunk.
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

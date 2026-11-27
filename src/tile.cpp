#pragma once
#include "arena.h"
#include "tile.h"
#include "vec.h"
#include "game.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <span>

namespace hh {

// Where an absolute tile coordinate lands: which chunk, and which tile in it.
struct ChunkPosition {
    u32 chunk_x;
    u32 chunk_y;
    u32 chunk_z;
    u32 tile_x;
    u32 tile_y;
};

ChunkPosition get_chunk_position(u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z) {
    ChunkPosition res = {
        .chunk_x = abs_tile_x >> k_chunk_shift,
        .chunk_y = abs_tile_y >> k_chunk_shift,
        .chunk_z = abs_tile_z,
        .tile_x = abs_tile_x & k_chunk_mask,
        .tile_y = abs_tile_y & k_chunk_mask,
    };
    return res;
}

// Value of every tile in a newly created chunk.
constexpr u32 k_default_tile_value = k_tile_uninitialized;

// Finds the chunk at (chunk_x, chunk_y, chunk_z). If it doesn't exist yet,
// creates it from `arena` when one is given, and returns null otherwise.
TileChunk* get_tile_chunk(World& world, u32 chunk_x, u32 chunk_y, u32 chunk_z, Arena* arena = nullptr) {
    u32 hash = chunk_x * 19 + chunk_y * 7 + chunk_z * 3;
    TileChunk* chunk = &world.chunk_hash[hash & (k_chunk_hash_size - 1)];

    // Walk the chain. An unused head slot can be claimed directly; otherwise
    // a new chunk gets appended after the last one.
    while (chunk->tiles) {
        if (chunk->chunk_x == chunk_x && chunk->chunk_y == chunk_y && chunk->chunk_z == chunk_z) {
            return chunk;
        }
        if (!chunk->next) {
            if (!arena) {
                return nullptr;
            }
            chunk->next = arena->push<TileChunk>();
            chunk->next->tiles = nullptr;
            chunk = chunk->next;
            break;
        }
        chunk = chunk->next;
    }

    if (!arena) {
        return nullptr;
    }

    std::span<u32> tiles = arena->push_array<u32>(k_chunk_dim * k_chunk_dim);
    std::fill(tiles.begin(), tiles.end(), k_default_tile_value);
    chunk->chunk_x = chunk_x;
    chunk->chunk_y = chunk_y;
    chunk->chunk_z = chunk_z;
    chunk->tiles = tiles.data();
    chunk->next = nullptr;
    return chunk;
}

// Returns false, leaving value untouched, when no chunk holds the tile.
bool get_tile_value(World& world, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z, u32& value) {
    bool res = false;

    ChunkPosition pos = get_chunk_position(abs_tile_x, abs_tile_y, abs_tile_z);
    TileChunk* chunk = get_tile_chunk(world, pos.chunk_x, pos.chunk_y, pos.chunk_z);
    if (chunk) {
        value = chunk->tiles[pos.tile_y * k_chunk_dim + pos.tile_x];
        res = true;
    }

    return res;
}

// Creates the tile's chunk from `arena` if it doesn't exist yet.
void set_tile_value(Arena& arena, World& world, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z, u32 value) {
    ChunkPosition pos = get_chunk_position(abs_tile_x, abs_tile_y, abs_tile_z);
    TileChunk* chunk = get_tile_chunk(world, pos.chunk_x, pos.chunk_y, pos.chunk_z, &arena);
    assert(chunk);
    chunk->tiles[pos.tile_y * k_chunk_dim + pos.tile_x] = value;
}

bool get_tile_value(World& world, const WorldPosition& pos, u32& value) {
    return get_tile_value(world, pos.abs_tile.x, pos.abs_tile.y, pos.abs_tile.z, value);
}

// Floor and stairs can be walked on; walls, unwritten tiles and missing
// chunks can't.
bool is_tile_walkable(World& world, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z) {
    u32 value = 0;
    bool res = get_tile_value(world, abs_tile_x, abs_tile_y, abs_tile_z, value)
               && (value == k_tile_empty || value == k_tile_stairs_up || value == k_tile_stairs_down);
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

WorldPosition world_pos_offset(const World& world, WorldPosition pos, Vec2 delta) {
    pos.tile_rel += delta;
    recanonicalize_coord(world.tile_side_in_meters, pos.abs_tile.x, pos.tile_rel.x);
    recanonicalize_coord(world.tile_side_in_meters, pos.abs_tile.y, pos.tile_rel.y);
    return pos;
}

bool is_world_pos_walkable(World& world, const WorldPosition& pos) {
    bool res = is_tile_walkable(world, pos.abs_tile.x, pos.abs_tile.y, pos.abs_tile.z);
    return res;
}

// a - b, in meters, within a level: z is ignored.
Vec2 world_pos_diff(const World& world, const WorldPosition& a, const WorldPosition& b) {
    // Subtracting as u32 and reinterpreting as i32 gives the signed distance
    // even across a wrap.
    const Vec2 d_tiles = {
        .x = static_cast<f32>(static_cast<i32>(a.abs_tile.x - b.abs_tile.x)),
        .y = static_cast<f32>(static_cast<i32>(a.abs_tile.y - b.abs_tile.y)),
    };
    return world.tile_side_in_meters * d_tiles + (a.tile_rel - b.tile_rel);
}

// The wall is the line x = wall_x, running from min_y to max_y;
// the names say x/y, but the caller swaps the axes to test horizontal walls
// with the same code. rel is the start point relative to the tile the wall belongs to.
// normal is the wall face's outward normal: only movement into the face (against the normal)
// can hit it, so an entity resting on a face can still step away from it,
// and one stuck inside a wall can walk out.
//
// If the path crosses the wall earlier than t_min, lowers t_min to just
// short of the crossing and reports the hit.
static bool test_wall(
    f32 wall_x,
    f32 rel_x,
    f32 rel_y,
    f32 delta_x,
    f32 delta_y,
    f32 min_y,
    f32 max_y,
    f32 normal_x,
    f32& t_min
) {
    // Backs off a little so the entity stops just in front of the wall
    // rather than on it, where rounding could put it on either side.
    constexpr f32 t_epsilon = 0.0001f;
    bool hit = false;
    if (delta_x * normal_x < 0.0f) {
        const f32 t_result = (wall_x - rel_x) / delta_x;
        const f32 y = rel_y + t_result * delta_y;
        if (t_result >= 0.0f && t_min > t_result && y >= min_y && y <= max_y) {
            t_min = std::max(0.0f, t_result - t_epsilon);
            hit = true;
        }
    }
    return hit;
}

// Moves the entity by delta from `from`, stopping at the first unwalkable tile
// along the way. The entity is its collision_dim box centered on `from`. Each wall
// is grown by that box (their Minkowski sum), so the box hits a wall exactly when its
// center point hits the grown wall, and only that point's path has to be traced.
// Every tile in the rectangle spanned by the start and end tiles, widened by the
// box's half size, is checked, and the four faces of each unwalkable one's grown box
// are intersected with the path, so a fast move can't skip over a wall.
MoveResult try_move(World& world, const Entity& entity, const WorldPosition& from, Vec2 delta) {
    const WorldPosition to = world_pos_offset(world, from, delta);
    const f32 side = world.tile_side_in_meters;
    const Vec2 half_size = 0.5f * entity.collision_dim;
    const u32 pad_x = static_cast<u32>(ceilf(half_size.x / side));
    const u32 pad_y = static_cast<u32>(ceilf(half_size.y / side));
    const u32 min_tile_x = std::min(from.abs_tile.x, to.abs_tile.x) - pad_x;
    const u32 min_tile_y = std::min(from.abs_tile.y, to.abs_tile.y) - pad_y;
    const u32 one_past_max_tile_x = std::max(from.abs_tile.x, to.abs_tile.x) + 1 + pad_x;
    const u32 one_past_max_tile_y = std::max(from.abs_tile.y, to.abs_tile.y) + 1 + pad_y;
    const u32 abs_tile_z = from.abs_tile.z;
    // Corners of the grown wall, relative to the wall tile's bottom-left
    // corner (tile_rel 0): the tile pushed out by half the box on every side.
    const Vec2 min_corner = -half_size;
    const Vec2 max_corner = Vec2{side, side} + half_size;

    MoveResult res = {.t = 1.0f};
    // != rather than <, so the loops still end if the range wraps past u32 0.
    for (u32 abs_tile_y = min_tile_y; abs_tile_y != one_past_max_tile_y; abs_tile_y++) {
        for (u32 abs_tile_x = min_tile_x; abs_tile_x != one_past_max_tile_x; abs_tile_x++) {
            if (is_tile_walkable(world, abs_tile_x, abs_tile_y, abs_tile_z)) {
                continue;
            }
            const WorldPosition tile_corner = {
                .abs_tile = {.x = abs_tile_x, .y = abs_tile_y, .z = abs_tile_z},
                .tile_rel = {},
            };
            const Vec2 rel = world_pos_diff(world, from, tile_corner);
            if (test_wall(min_corner.x, rel.x, rel.y, delta.x, delta.y, min_corner.y, max_corner.y, -1.0f, res.t)) {
                res.wall_normal = {.x = -1.0f, .y = 0.0f};
            }
            if (test_wall(max_corner.x, rel.x, rel.y, delta.x, delta.y, min_corner.y, max_corner.y, 1.0f, res.t)) {
                res.wall_normal = {.x = 1.0f, .y = 0.0f};
            }
            if (test_wall(min_corner.y, rel.y, rel.x, delta.y, delta.x, min_corner.x, max_corner.x, -1.0f, res.t)) {
                res.wall_normal = {.x = 0.0f, .y = -1.0f};
            }
            if (test_wall(max_corner.y, rel.y, rel.x, delta.y, delta.x, min_corner.x, max_corner.x, 1.0f, res.t)) {
                res.wall_normal = {.x = 0.0f, .y = 1.0f};
            }
        }
    }
    res.blocked = res.t < 1.0f;
    res.new_pos = res.blocked ? world_pos_offset(world, from, res.t * delta) : to;
    return res;
}

}  // namespace hh

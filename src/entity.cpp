#pragma once
// Entity storage (low/high slots) and movement against walls and other
// entities. Included by game.cpp and tests/test_entity.cpp.
#include "game.h"
#include "tile.cpp"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace hh {

// Hands out the next low entity slot, cleared. Returns 0 when every slot is taken.
static u32 add_entity(GameState& state) {
    assert(state.low_entity_count >= 1);
    u32 index = 0;
    if (state.low_entity_count < std::size(state.low_entities)) {
        index = state.low_entity_count++;
        state.low_entities[index] = {};
    }
    return index;
}

static Entity get_entity(GameState& state, u32 low_index) {
    Entity entity = {};
    if (low_index > 0 && low_index < state.low_entity_count) {
        entity.low_index = low_index;
        entity.low = &state.low_entities[low_index];
        if (u32 high_index = entity.low->high_index) {
            entity.high = &state.high_entities[high_index];
        }
    }
    return entity;
}

// Gives a low entity a high slot, placed relative to the current camera.
// Does nothing if it's already high. HighEntity has no z, so the caller
// checks the entity is on the camera's level.
//
// Running out of high slots asserts: an entity that can't be promoted isn't
// simulated, drawn or collided with, so a wall would silently become a gap.
// If it fires, raise k_max_high_entities. Without asserts it does nothing.
static void make_entity_high(GameState& state, u32 low_index) {
    assert(low_index > 0 && low_index < state.low_entity_count);
    assert(state.high_entity_count >= 1);  // slot 0 is reserved at init
    LowEntity& low = state.low_entities[low_index];
    if (low.high_index) {
        return;
    }
    assert(state.high_entity_count < k_max_high_entities);
    if (state.high_entity_count == k_max_high_entities) {
        return;
    }
    const u32 high_index = state.high_entity_count++;
    HighEntity& high = state.high_entities[high_index];
    high = {
        .pos = world_pos_diff(state.world, low.pos, state.camera_pos),
        .low_index = low_index,
    };
    low.high_index = high_index;
}

// Drops a low entity's high slot, if it has one. Nothing is written back:
// whatever moves a high entity must keep low.pos current as it goes. The last high entity is moved
// into the freed slot, so other entities' high indices can change.
static void make_entity_low(GameState& state, u32 low_index) {
    assert(low_index > 0 && low_index < state.low_entity_count);
    LowEntity& low = state.low_entities[low_index];
    const u32 high_index = low.high_index;
    if (!high_index) {
        return;
    }

    const u32 last = --state.high_entity_count;
    if (high_index != last) {
        state.high_entities[high_index] = state.high_entities[last];
        state.low_entities[state.high_entities[high_index].low_index].high_index = high_index;
    }
    low.high_index = 0;
}

// An entity of `type` filling tile (abs_tile_x, abs_tile_y, abs_tile_z):
// centered on it and exactly its size. Returns it, or an empty handle if
// out of low slots, which asserts (raise k_max_entities).
static Entity add_tile_entity(GameState& state, EntityType type, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z) {
    const Entity entity = get_entity(state, add_entity(state));
    assert(entity.low);
    if (!entity.low) {
        return entity;
    }
    const f32 side = state.world.tile_side_in_meters;
    LowEntity& low = *entity.low;
    low.type = type;
    low.pos = {
        .abs_tile = {.x = abs_tile_x, .y = abs_tile_y, .z = abs_tile_z},
        .tile_rel = tile_center(state.world),
    };
    low.width = side;
    low.height = side;
    low.collision_dim = Vec2::all(side);
    return entity;
}

// A wall filling its tile, colliding, so it blocks the same as a wall tile.
static void add_wall(GameState& state, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z) {
    if (const Entity wall = add_tile_entity(state, EntityType::Wall, abs_tile_x, abs_tile_y, abs_tile_z); wall.low) {
        wall.low->collides = true;
    }
}

// Stairs filling their tile. They don't collide: they're walked onto, and
// stairs_level_change notices.
static void add_stairs(GameState& state, bool up, u32 abs_tile_x, u32 abs_tile_y, u32 abs_tile_z) {
    add_tile_entity(state, up ? EntityType::StairsUp : EntityType::StairsDown, abs_tile_x, abs_tile_y, abs_tile_z);
}

// Whether p is in the dim-sized box centered on `center`. Half-open, like a
// tile, so a point on the edge between two boxes is in exactly one.
static bool in_box(const World& world, const WorldPosition& p, const WorldPosition& center, Vec2 dim) {
    const Vec2 rel = world_pos_diff(world, p, center);
    const Vec2 half = 0.5f * dim;
    return -half.x <= rel.x && rel.x < half.x && -half.y <= rel.y && rel.y < half.y;
}

// How many levels a mover going from old_pos to new_pos (same level) climbs
// by stairs: +1 or -1 if its center stepped onto high stairs this move, else
// 0. Only stepping on counts, not being on them: arriving on the other level
// puts the mover on the matching stairs, and that mustn't send it straight
// back.
static i32 stairs_level_change(GameState& state, const WorldPosition& old_pos, const WorldPosition& new_pos) {
    for (u32 high_index = 1; high_index < state.high_entity_count; high_index++) {
        const LowEntity& stairs = state.low_entities[state.high_entities[high_index].low_index];
        if ((stairs.type != EntityType::StairsUp && stairs.type != EntityType::StairsDown)
            || stairs.pos.abs_tile.z != new_pos.abs_tile.z) {
            continue;
        }
        if (in_box(state.world, new_pos, stairs.pos, stairs.collision_dim)
            && !in_box(state.world, old_pos, stairs.pos, stairs.collision_dim)) {
            return stairs.type == EntityType::StairsUp ? 1 : -1;
        }
    }
    return 0;
}

// The wall is the line x = wall_x, running from min_y to max_y;
// the names say x/y, but the caller swaps the axes to test horizontal walls
// with the same code. rel is the start point relative to the box the wall belongs to.
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

// Tests the path from rel along delta against the four faces of the box from
// min_corner to max_corner (in the same frame as rel), keeping the earliest
// hit in res.
static void test_box(Vec2 rel, Vec2 delta, Vec2 min_corner, Vec2 max_corner, MoveResult& res) {
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

// Moves `mover`'s collision_dim box, centered on `from`, by delta, stopping
// at the first colliding entity along the way, walls included (see add_wall).
// Each entity is grown by the mover (their Minkowski sum), so the mover hits
// it exactly when its center point hits the grown box, and only that point's
// path has to be traced: the four faces of each grown box are intersected
// with the path, so a fast move can't skip over anything.
//
// Every high entity other than the mover is checked, as long as both have
// `collides` set. Only high entities count: they're the ones being
// simulated, and all on the camera's level. Only moving into a face blocks,
// so an entity resting on a face can step away from it, and two overlapping
// entities (e.g. players spawned on the same spot) can still walk apart.
//
// `from` is separate from mover.low->pos so a caller can search again from
// partway along a move (see the glide in move_player).
MoveResult try_move(GameState& state, const Entity& mover, const WorldPosition& from, Vec2 delta) {
    const World& world = state.world;
    const Vec2 collision_dim = mover.low->collision_dim;
    const u32 abs_tile_z = from.abs_tile.z;

    MoveResult res = {.t = 1.0f};

    // Entities, relative to their centers: grown by the mover, so the faces
    // sit half the sum of both sizes out from the center.
    if (mover.low->collides) {
        for (u32 high_index = 1; high_index < state.high_entity_count; high_index++) {
            const u32 other_index = state.high_entities[high_index].low_index;
            const LowEntity& other = state.low_entities[other_index];
            if (other_index == mover.low_index || !other.collides || other.pos.abs_tile.z != abs_tile_z) {
                continue;
            }
            const Vec2 grown_half = 0.5f * (other.collision_dim + collision_dim);
            test_box(world_pos_diff(world, from, other.pos), delta, -grown_half, grown_half, res);
        }
    }
    res.blocked = res.t < 1.0f;
    res.new_pos = world_pos_offset(world, from, res.t * delta);
    return res;
}

}  // namespace hh

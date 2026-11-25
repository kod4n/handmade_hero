// Tests for src/tile.cpp. Built as part of tests/main.cpp; the includes are here
// so clangd can make sense of this file on its own.

#include "test.h"
#include "test_support.h"
#include "defer.h"
#include "tile.cpp"

using namespace hh;

// World is too big for the stack, so it lives on the heap. Zeroed is empty.
static World* make_world() {
    World* world = static_cast<World*>(std::calloc(1, sizeof(World)));
    world->tile_side_in_meters = k_tile_side_in_meters;
    world->tile_side_in_pixels = k_tile_side_in_pixels;
    world->meters_to_pixels = k_tile_side_in_pixels / k_tile_side_in_meters;
    return world;
}

static bool is_canonical(const World& world, const WorldPosition& pos) {
    const f32 side = world.tile_side_in_meters;
    return pos.tile_rel.x >= 0.0f && pos.tile_rel.x < side && pos.tile_rel.y >= 0.0f && pos.tile_rel.y < side;
}

TEST(chunk_position_splits_bits) {
    ChunkPosition last_in_chunk = get_chunk_position(15, 15, 3);
    CHECK_EQ(last_in_chunk.chunk_x, 0);
    CHECK_EQ(last_in_chunk.tile_x, 15);
    CHECK_EQ(last_in_chunk.chunk_y, 0);
    CHECK_EQ(last_in_chunk.tile_y, 15);
    CHECK_EQ(last_in_chunk.chunk_z, 3);  // z isn't chunked

    ChunkPosition first_in_next = get_chunk_position(16, 33, 0);
    CHECK_EQ(first_in_next.chunk_x, 1);
    CHECK_EQ(first_in_next.tile_x, 0);
    CHECK_EQ(first_in_next.chunk_y, 2);
    CHECK_EQ(first_in_next.tile_y, 1);

    ChunkPosition top = get_chunk_position(0xFFFFFFFF, 0, 0);
    CHECK_EQ(top.chunk_x, 0x0FFFFFFF);
    CHECK_EQ(top.tile_x, 15);
}

TEST(world_pos_offset_carries_into_neighbor_tiles) {
    World* world = make_world();
    defer(std::free(world));
    const f32 side = world->tile_side_in_meters;
    const WorldPosition start = {.abs_tile_x = 5, .abs_tile_y = 5, .abs_tile_z = 0, .tile_rel = {0.7f, 0.7f}};

    WorldPosition right = world_pos_offset(*world, start, {1.0f, 0.0f});
    CHECK_EQ(right.abs_tile_x, 6);
    CHECK_NEAR(right.tile_rel.x, 0.3f, 1e-5f);
    CHECK_EQ(right.abs_tile_y, 5);  // the other axis is untouched
    CHECK_EQ(right.tile_rel.y, start.tile_rel.y);

    WorldPosition left = world_pos_offset(*world, start, {-1.0f, 0.0f});
    CHECK_EQ(left.abs_tile_x, 4);
    CHECK_NEAR(left.tile_rel.x, 1.1f, 1e-5f);

    WorldPosition far_up = world_pos_offset(*world, start, {0.0f, 5.0f * side + 0.2f});
    CHECK_EQ(far_up.abs_tile_y, 10);
    CHECK_NEAR(far_up.tile_rel.y, 0.9f, 1e-5f);

    WorldPosition far_down = world_pos_offset(*world, start, {0.0f, -3.0f * side});
    CHECK_EQ(far_down.abs_tile_y, 2);
    CHECK_NEAR(far_down.tile_rel.y, 0.7f, 1e-5f);

    // Landing exactly on the boundary belongs to the next tile.
    WorldPosition on_edge = world_pos_offset(*world, {.abs_tile_x = 5}, {side, 0.0f});
    CHECK_EQ(on_edge.abs_tile_x, 6);
    CHECK_EQ(on_edge.tile_rel.x, 0.0f);
}

TEST(world_pos_offset_tiny_negative_stays_canonical) {
    World* world = make_world();
    defer(std::free(world));

    // -1e-8 + 1.4 rounds to exactly 1.4f: the case recanonicalize_coord
    // corrects after the floor.
    WorldPosition pos = world_pos_offset(*world, {.abs_tile_x = 5}, {-1e-8f, 0.0f});
    CHECK(is_canonical(*world, pos));
    CHECK_EQ(pos.abs_tile_x, 5);
    CHECK_EQ(pos.tile_rel.x, 0.0f);
}

TEST(world_pos_offset_left_of_zero_wraps_off_world) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    set_tile_value(arena, *world, 0, 0, 0, k_tile_empty);

    WorldPosition pos = world_pos_offset(*world, {.tile_rel = {0.5f, 0.5f}}, {-1.0f, 0.0f});
    CHECK_EQ(pos.abs_tile_x, 0xFFFFFFFF);
    CHECK_NEAR(pos.tile_rel.x, 0.9f, 1e-5f);
    CHECK(!is_world_pos_walkable(*world, pos));
}

TEST(random_walk_stays_canonical_and_tracks_distance) {
    World* world = make_world();
    defer(std::free(world));

    // Starts at tile 0, so the walk crosses the u32 wrap in both directions.
    const WorldPosition start = {.tile_rel = {0.1f, 1.3f}};
    WorldPosition pos = start;
    double total_dx = 0.0;
    double total_dy = 0.0;
    bool canonical = true;
    RandomSeries series = {.index = 3};
    for (int i = 0; i < 10000; i++) {
        // Steps up to 10 m either way, several tiles at once.
        const f32 dx = static_cast<f32>(static_cast<i32>(random_choice(series, 2001)) - 1000) / 100.0f;
        const f32 dy = static_cast<f32>(static_cast<i32>(random_choice(series, 2001)) - 1000) / 100.0f;
        pos = world_pos_offset(*world, pos, {dx, dy});
        total_dx += static_cast<double>(dx);
        total_dy += static_cast<double>(dy);
        canonical = canonical && is_canonical(*world, pos);
    }
    CHECK(canonical);

    Vec2 diff = world_pos_diff(*world, pos, start);
    CHECK_NEAR(diff.x, total_dx, 0.01);
    CHECK_NEAR(diff.y, total_dy, 0.01);
}

TEST(world_pos_diff_is_signed) {
    World* world = make_world();
    defer(std::free(world));
    const f32 side = world->tile_side_in_meters;

    const WorldPosition a = {.abs_tile_x = 7, .abs_tile_y = 3, .tile_rel = {0.2f, 0.5f}};
    const WorldPosition b = {.abs_tile_x = 5, .abs_tile_y = 4, .tile_rel = {1.0f, 0.5f}};

    Vec2 ab = world_pos_diff(*world, a, b);
    CHECK_NEAR(ab.x, 2.0f * side - 0.8f, 1e-5f);
    CHECK_NEAR(ab.y, -side, 1e-5f);

    Vec2 ba = world_pos_diff(*world, b, a);
    CHECK_NEAR(ba.x, -ab.x, 1e-5f);
    CHECK_NEAR(ba.y, -ab.y, 1e-5f);

    // Across the wrap: tile 0xFFFFFFFF is one tile left of tile 0.
    const WorldPosition wrapped = {.abs_tile_x = 0xFFFFFFFF, .tile_rel = {0.5f, 0.0f}};
    const WorldPosition one = {.abs_tile_x = 1, .tile_rel = {0.5f, 0.0f}};
    CHECK_NEAR(world_pos_diff(*world, wrapped, one).x, -2.0f * side, 1e-5f);
    CHECK_NEAR(world_pos_diff(*world, one, wrapped).x, 2.0f * side, 1e-5f);

    // z is ignored.
    WorldPosition other_level = a;
    other_level.abs_tile_z = 9;
    CHECK_NEAR(world_pos_diff(*world, other_level, a).x, 0.0f, 0.0f);
}

TEST(tile_chunk_lookup_does_not_allocate) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();

    CHECK(get_tile_chunk(*world, 1, 2, 3) == nullptr);
    CHECK_EQ(arena.used, 0);

    TileChunk* created = get_tile_chunk(*world, 1, 2, 3, &arena);
    CHECK(created != nullptr);
    const u64 used = arena.used;

    // Found again with or without an arena, without allocating more.
    CHECK(get_tile_chunk(*world, 1, 2, 3) == created);
    CHECK(get_tile_chunk(*world, 1, 2, 3, &arena) == created);
    CHECK_EQ(arena.used, used);

    bool all_uninitialized = true;
    for (u32 i = 0; i < k_chunk_dim * k_chunk_dim; i++) {
        all_uninitialized = all_uninitialized && created->tiles[i] == k_tile_uninitialized;
    }
    CHECK(all_uninitialized);
}

TEST(tile_chunk_hash_collisions_chain) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();

    // hash = x * 19 + y * 7 + z * 3: all three of these hash to 133.
    TileChunk* a = get_tile_chunk(*world, 7, 0, 0, &arena);
    TileChunk* b = get_tile_chunk(*world, 0, 19, 0, &arena);
    CHECK(a != nullptr && b != nullptr && a != b);
    CHECK(&world->chunk_hash[133] == a);
    CHECK(world->chunk_hash[133].next == b);

    CHECK(get_tile_chunk(*world, 7, 0, 0) == a);
    CHECK(get_tile_chunk(*world, 0, 19, 0) == b);

    // Same slot, not in the chain.
    const u64 used = arena.used;
    CHECK(get_tile_chunk(*world, 0, 1, 42) == nullptr);
    CHECK_EQ(arena.used, used);

    // Collide only after masking, and differ in just one coordinate, so
    // matching has to compare each of x, y and z.
    TileChunk* origin = get_tile_chunk(*world, 0, 0, 0, &arena);
    TileChunk* far_x = get_tile_chunk(*world, 4096, 0, 0, &arena);
    TileChunk* far_y = get_tile_chunk(*world, 0, 4096, 0, &arena);
    TileChunk* far_z = get_tile_chunk(*world, 0, 0, 4096, &arena);
    CHECK(origin != far_x && origin != far_y && origin != far_z);
    CHECK(far_x != far_y && far_y != far_z && far_x != far_z);
    CHECK(get_tile_chunk(*world, 0, 0, 0) == origin);
    CHECK(get_tile_chunk(*world, 4096, 0, 0) == far_x);
    CHECK(get_tile_chunk(*world, 0, 4096, 0) == far_y);
    CHECK(get_tile_chunk(*world, 0, 0, 4096) == far_z);
}

// A 3x3 floor at tiles (10..12, 10..12) on level 0, ringed by walls.
static void fill_walled_room(Arena& arena, World& world) {
    for (u32 y = 9; y <= 13; y++) {
        for (u32 x = 9; x <= 13; x++) {
            const bool edge = x == 9 || x == 13 || y == 9 || y == 13;
            set_tile_value(arena, world, x, y, 0, edge ? k_tile_wall : k_tile_empty);
        }
    }
}

TEST(try_move_stops_at_first_wall) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    fill_walled_room(arena, *world);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    // Open floor: not blocked, the whole move is taken.
    const WorldPosition center = {.abs_tile_x = 11, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {mid, mid}};
    MoveResult clear = try_move(*world, center, {0.3f, -0.2f});
    CHECK(!clear.blocked);
    CHECK_EQ(clear.t, 1.0f);
    CHECK_EQ(clear.new_pos.abs_tile_x, 11);
    CHECK_NEAR(clear.new_pos.tile_rel.x, mid + 0.3f, 1e-5f);
    CHECK_NEAR(clear.new_pos.tile_rel.y, mid - 0.2f, 1e-5f);

    // Into the right wall, 0.05 away with a 0.1 move: stops halfway, just
    // short of the wall, with the normal pointing back out of it.
    const WorldPosition by_right =
        {.abs_tile_x = 12, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {side - 0.05f, mid}};
    MoveResult right = try_move(*world, by_right, {0.1f, 0.0f});
    CHECK(right.blocked);
    CHECK_NEAR(right.t, 0.5f, 1e-3f);
    CHECK_EQ(right.new_pos.abs_tile_x, 12);
    CHECK(right.new_pos.tile_rel.x < side);
    CHECK_NEAR(right.new_pos.tile_rel.x, side, 1e-4f);
    CHECK_EQ(right.wall_normal, (Vec2{-1.0f, 0.0f}));

    // Into the left wall: normal points right.
    const WorldPosition by_left = {.abs_tile_x = 10, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {0.05f, mid}};
    MoveResult left = try_move(*world, by_left, {-0.1f, 0.0f});
    CHECK(left.blocked);
    CHECK_EQ(left.new_pos.abs_tile_x, 10);
    CHECK_EQ(left.wall_normal, (Vec2{1.0f, 0.0f}));

    // Down into the bottom wall: normal points up (world y is up).
    const WorldPosition by_bottom = {.abs_tile_x = 11, .abs_tile_y = 10, .abs_tile_z = 0, .tile_rel = {mid, 0.05f}};
    MoveResult bottom = try_move(*world, by_bottom, {0.0f, -0.1f});
    CHECK(bottom.blocked);
    CHECK_EQ(bottom.new_pos.abs_tile_y, 10);
    CHECK_EQ(bottom.wall_normal, (Vec2{0.0f, 1.0f}));

    // Diagonally toward the top-right corner, nearer the right wall than the
    // top one but moving so the top face is crossed first (t 0.4 vs 0.5).
    const WorldPosition by_corner =
        {.abs_tile_x = 12, .abs_tile_y = 12, .abs_tile_z = 0, .tile_rel = {side - 0.05f, side - 0.04f}};
    MoveResult corner = try_move(*world, by_corner, {0.1f, 0.1f});
    CHECK(corner.blocked);
    CHECK_NEAR(corner.t, 0.4f, 1e-3f);
    CHECK_EQ(corner.wall_normal, (Vec2{0.0f, -1.0f}));
}

TEST(try_move_does_not_tunnel_through_walls) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    fill_walled_room(arena, *world);
    // Floor on the far side of the right wall, so the end point is walkable
    // and only the path in between is blocked.
    set_tile_value(arena, *world, 14, 11, 0, k_tile_empty);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    const WorldPosition start = {.abs_tile_x = 12, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {mid, mid}};
    MoveResult move = try_move(*world, start, {2.0f * side, 0.0f});
    CHECK(move.blocked);
    CHECK_NEAR(move.t, 0.25f, 1e-3f);
    CHECK_EQ(move.new_pos.abs_tile_x, 12);
    CHECK_EQ(move.wall_normal, (Vec2{-1.0f, 0.0f}));
}

TEST(try_move_only_blocks_moving_into_a_face) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    fill_walled_room(arena, *world);
    const f32 side = world->tile_side_in_meters;
    const f32 mid = 0.5f * side;

    // Resting exactly on the left wall's face: stepping away is free...
    const WorldPosition on_face = {.abs_tile_x = 10, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {0.0f, mid}};
    MoveResult away = try_move(*world, on_face, {0.1f, 0.0f});
    CHECK(!away.blocked);
    CHECK_NEAR(away.new_pos.tile_rel.x, 0.1f, 1e-5f);

    // ...and stepping into it goes nowhere.
    MoveResult into = try_move(*world, on_face, {-0.1f, 0.0f});
    CHECK(into.blocked);
    CHECK_EQ(into.t, 0.0f);
    CHECK_EQ(into.new_pos.abs_tile_x, 10);
    CHECK_EQ(into.new_pos.tile_rel.x, 0.0f);

    // Moving along a wall, parallel to its face, isn't blocked.
    const WorldPosition by_wall = {.abs_tile_x = 10, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {0.01f, mid}};
    MoveResult along = try_move(*world, by_wall, {0.0f, side});
    CHECK(!along.blocked);
    CHECK_EQ(along.new_pos.abs_tile_y, 12);

    // Stuck inside a wall: the faces only block from outside, so it can
    // walk back out onto the floor.
    const WorldPosition in_wall = {.abs_tile_x = 13, .abs_tile_y = 11, .abs_tile_z = 0, .tile_rel = {mid, mid}};
    MoveResult out = try_move(*world, in_wall, {-side, 0.0f});
    CHECK(!out.blocked);
    CHECK_EQ(out.new_pos.abs_tile_x, 12);
}

TEST(tile_values_and_walkability) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();

    // No chunk: false, and value is left alone.
    u32 value = 12345;
    CHECK(!get_tile_value(*world, 20, 3, 0, value));
    CHECK_EQ(value, 12345);
    CHECK(!is_tile_walkable(*world, 20, 3, 0));

    set_tile_value(arena, *world, 20, 3, 0, k_tile_wall);
    CHECK(get_tile_value(*world, 20, 3, 0, value));
    CHECK_EQ(value, k_tile_wall);
    CHECK(!is_tile_walkable(*world, 20, 3, 0));

    // Same chunk, never written.
    CHECK(get_tile_value(*world, 21, 3, 0, value));
    CHECK_EQ(value, k_tile_uninitialized);
    CHECK(!is_tile_walkable(*world, 21, 3, 0));

    // Same x and y on another level is a different chunk.
    CHECK(!get_tile_value(*world, 20, 3, 1, value));

    set_tile_value(arena, *world, 22, 3, 0, k_tile_empty);
    set_tile_value(arena, *world, 23, 3, 0, k_tile_stairs_up);
    set_tile_value(arena, *world, 24, 3, 0, k_tile_stairs_down);
    CHECK(is_tile_walkable(*world, 22, 3, 0));
    CHECK(is_tile_walkable(*world, 23, 3, 0));
    CHECK(is_tile_walkable(*world, 24, 3, 0));

    // Overwriting.
    set_tile_value(arena, *world, 20, 3, 0, k_tile_empty);
    CHECK(is_tile_walkable(*world, 20, 3, 0));

    // The WorldPosition overload reads the same tile.
    const WorldPosition pos = {.abs_tile_x = 23, .abs_tile_y = 3, .abs_tile_z = 0, .tile_rel = {0.5f, 0.0f}};
    CHECK(get_tile_value(*world, pos, value));
    CHECK_EQ(value, k_tile_stairs_up);
    CHECK(is_world_pos_walkable(*world, pos));
}

// Tests for src/tile.cpp. Built as part of tests/main.cpp; the includes are here
// so clangd can make sense of this file on its own.

#include "test.h"
#include "test_support.h"
#include "defer.h"
#include "random.h"
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

    // Negative tiles floor into the chunk below 0.
    ChunkPosition minus_one = get_chunk_position(-1, -16, -2);
    CHECK_EQ(minus_one.chunk_x, -1);
    CHECK_EQ(minus_one.tile_x, 15);
    CHECK_EQ(minus_one.chunk_y, -1);
    CHECK_EQ(minus_one.tile_y, 0);
    CHECK_EQ(minus_one.chunk_z, -2);

    ChunkPosition minus_seventeen = get_chunk_position(-17, 0, 0);
    CHECK_EQ(minus_seventeen.chunk_x, -2);
    CHECK_EQ(minus_seventeen.tile_x, 15);
}

TEST(world_pos_offset_carries_into_neighbor_tiles) {
    World* world = make_world();
    defer(std::free(world));
    const f32 side = world->tile_side_in_meters;
    const WorldPosition start = {.abs_tile = {.x = 5, .y = 5, .z = 0}, .tile_rel = {0.7f, 0.7f}};

    WorldPosition right = world_pos_offset(*world, start, {1.0f, 0.0f});
    CHECK_EQ(right.abs_tile.x, 6);
    CHECK_NEAR(right.tile_rel.x, 0.3f, 1e-5f);
    CHECK_EQ(right.abs_tile.y, 5);  // the other axis is untouched
    CHECK_EQ(right.tile_rel.y, start.tile_rel.y);

    WorldPosition left = world_pos_offset(*world, start, {-1.0f, 0.0f});
    CHECK_EQ(left.abs_tile.x, 4);
    CHECK_NEAR(left.tile_rel.x, 1.1f, 1e-5f);

    WorldPosition far_up = world_pos_offset(*world, start, {0.0f, 5.0f * side + 0.2f});
    CHECK_EQ(far_up.abs_tile.y, 10);
    CHECK_NEAR(far_up.tile_rel.y, 0.9f, 1e-5f);

    WorldPosition far_down = world_pos_offset(*world, start, {0.0f, -3.0f * side});
    CHECK_EQ(far_down.abs_tile.y, 2);
    CHECK_NEAR(far_down.tile_rel.y, 0.7f, 1e-5f);

    // Landing exactly on the boundary belongs to the next tile.
    WorldPosition on_edge = world_pos_offset(*world, {.abs_tile = {.x = 5}}, {side, 0.0f});
    CHECK_EQ(on_edge.abs_tile.x, 6);
    CHECK_EQ(on_edge.tile_rel.x, 0.0f);
}

TEST(world_pos_offset_tiny_negative_stays_canonical) {
    World* world = make_world();
    defer(std::free(world));

    // -1e-8 + 1.4 rounds to exactly 1.4f: the case recanonicalize_coord
    // corrects after the floor.
    WorldPosition pos = world_pos_offset(*world, {.abs_tile = {.x = 5}}, {-1e-8f, 0.0f});
    CHECK(is_canonical(*world, pos));
    CHECK_EQ(pos.abs_tile.x, 5);
    CHECK_EQ(pos.tile_rel.x, 0.0f);
}

TEST(world_pos_offset_left_of_zero_goes_negative) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    set_tile_value(arena, *world, 0, 0, 0, k_tile_empty);

    WorldPosition pos = world_pos_offset(*world, {.tile_rel = {0.5f, 0.5f}}, {-1.0f, 0.0f});
    CHECK_EQ(pos.abs_tile.x, -1);
    CHECK_NEAR(pos.tile_rel.x, 0.9f, 1e-5f);
    // Nothing written there yet, so no chunk.
    CHECK(!is_world_pos_walkable(*world, pos));

    // Once written, tile -1 is walkable like any other.
    set_tile_value(arena, *world, -1, 0, 0, k_tile_empty);
    CHECK(is_world_pos_walkable(*world, pos));
}

TEST(negative_tiles_get_their_own_chunks) {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    set_tile_value(arena, *world, -1, -1, 0, k_tile_wall);
    set_tile_value(arena, *world, 15, 15, 0, k_tile_empty);  // same low bits, chunk 0

    u32 value = 0;
    CHECK(get_tile_value(*world, -1, -1, 0, value));
    CHECK_EQ(value, k_tile_wall);
    CHECK(get_tile_value(*world, 15, 15, 0, value));
    CHECK_EQ(value, k_tile_empty);
}

TEST(random_walk_stays_canonical_and_tracks_distance) {
    World* world = make_world();
    defer(std::free(world));

    // Starts at tile 0, so the walk crosses into negative tiles and back.
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

    const WorldPosition a = {.abs_tile = {.x = 7, .y = 3}, .tile_rel = {0.2f, 0.5f}};
    const WorldPosition b = {.abs_tile = {.x = 5, .y = 4}, .tile_rel = {1.0f, 0.5f}};

    Vec2 ab = world_pos_diff(*world, a, b);
    CHECK_NEAR(ab.x, 2.0f * side - 0.8f, 1e-5f);
    CHECK_NEAR(ab.y, -side, 1e-5f);

    Vec2 ba = world_pos_diff(*world, b, a);
    CHECK_NEAR(ba.x, -ab.x, 1e-5f);
    CHECK_NEAR(ba.y, -ab.y, 1e-5f);

    // Across the origin: tile -1 is one tile left of tile 0.
    const WorldPosition minus_one = {.abs_tile = {.x = -1}, .tile_rel = {0.5f, 0.0f}};
    const WorldPosition one = {.abs_tile = {.x = 1}, .tile_rel = {0.5f, 0.0f}};
    CHECK_NEAR(world_pos_diff(*world, minus_one, one).x, -2.0f * side, 1e-5f);
    CHECK_NEAR(world_pos_diff(*world, one, minus_one).x, 2.0f * side, 1e-5f);

    // z is ignored.
    WorldPosition other_level = a;
    other_level.abs_tile.z = 9;
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
    const WorldPosition pos = {.abs_tile = {.x = 23, .y = 3, .z = 0}, .tile_rel = {0.5f, 0.0f}};
    CHECK(get_tile_value(*world, pos, value));
    CHECK_EQ(value, k_tile_stairs_up);
    CHECK(is_world_pos_walkable(*world, pos));
}
